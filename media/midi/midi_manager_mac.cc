// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/midi/midi_manager_mac.h"

#include <algorithm>
#include <string>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"

#include <CoreAudio/HostTime.h>

using base::IntToString;
using base::SysCFStringRefToUTF8;
using std::string;

// NB: System MIDI types are pointer types in 32-bit and integer types in
// 64-bit. Therefore, the initialization is the simplest one that satisfies both
// (if possible).

namespace media {

namespace {

MidiPortInfo GetPortInfoFromEndpoint(MIDIEndpointRef endpoint) {
  SInt32 id_number = 0;
  MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &id_number);
  string id = IntToString(id_number);

  string manufacturer;
  CFStringRef manufacturer_ref = NULL;
  OSStatus result = MIDIObjectGetStringProperty(
      endpoint, kMIDIPropertyManufacturer, &manufacturer_ref);
  if (result == noErr) {
    manufacturer = SysCFStringRefToUTF8(manufacturer_ref);
  } else {
    // kMIDIPropertyManufacturer is not supported in IAC driver providing
    // endpoints, and the result will be kMIDIUnknownProperty (-10835).
    DLOG(WARNING) << "Failed to get kMIDIPropertyManufacturer with status "
                  << result;
  }

  string name;
  CFStringRef name_ref = NULL;
  result = MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &name_ref);
  if (result == noErr)
    name = SysCFStringRefToUTF8(name_ref);
  else
    DLOG(WARNING) << "Failed to get kMIDIPropertyName with status " << result;

  string version;
  SInt32 version_number = 0;
  result = MIDIObjectGetIntegerProperty(
      endpoint, kMIDIPropertyDriverVersion, &version_number);
  if (result == noErr) {
    version = IntToString(version_number);
  } else {
    // kMIDIPropertyDriverVersion is not supported in IAC driver providing
    // endpoints, and the result will be kMIDIUnknownProperty (-10835).
    DLOG(WARNING) << "Failed to get kMIDIPropertyDriverVersion with status "
                  << result;
  }

  const MidiPortState state = MIDI_PORT_OPENED;
  return MidiPortInfo(id, manufacturer, name, version, state);
}

double MIDITimeStampToSeconds(MIDITimeStamp timestamp) {
  UInt64 nanoseconds = AudioConvertHostTimeToNanos(timestamp);
  return static_cast<double>(nanoseconds) / 1.0e9;
}

MIDITimeStamp SecondsToMIDITimeStamp(double seconds) {
  UInt64 nanos = UInt64(seconds * 1.0e9);
  return AudioConvertNanosToHostTime(nanos);
}

}  // namespace

MidiManager* MidiManager::Create() {
  return new MidiManagerMac();
}

MidiManagerMac::MidiManagerMac()
    : midi_client_(0),
      coremidi_input_(0),
      coremidi_output_(0),
      packet_list_(NULL),
      midi_packet_(NULL),
      client_thread_("MidiClientThread"),
      shutdown_(false) {
}

MidiManagerMac::~MidiManagerMac() {
  // Wait for the termination of |client_thread_| before disposing MIDI ports.
  shutdown_ = true;
  client_thread_.Stop();

  if (coremidi_input_)
    MIDIPortDispose(coremidi_input_);
  if (coremidi_output_)
    MIDIPortDispose(coremidi_output_);
  if (midi_client_)
    MIDIClientDispose(midi_client_);
}

void MidiManagerMac::StartInitialization() {
  // MIDIClient should be created on |client_thread_| to receive CoreMIDI event
  // notifications.
  RunOnClientThread(
      base::Bind(&MidiManagerMac::InitializeCoreMIDI, base::Unretained(this)));
}

void MidiManagerMac::DispatchSendMidiData(MidiManagerClient* client,
                                          uint32 port_index,
                                          const std::vector<uint8>& data,
                                          double timestamp) {
  RunOnClientThread(
      base::Bind(&MidiManagerMac::SendMidiData,
                 base::Unretained(this), client, port_index, data, timestamp));
}

void MidiManagerMac::RunOnClientThread(const base::Closure& closure) {
  if (shutdown_)
    return;

  if (!client_thread_.IsRunning())
    client_thread_.Start();

  client_thread_.message_loop()->PostTask(FROM_HERE, closure);
}

void MidiManagerMac::InitializeCoreMIDI() {
  DCHECK(client_thread_.message_loop_proxy()->BelongsToCurrentThread());

  // CoreMIDI registration.
  // TODO(toyoshim): Set MIDINotifyProc to receive CoreMIDI event notifications.
  midi_client_ = 0;
  OSStatus result =
      MIDIClientCreate(CFSTR("Chrome"), ReceiveMidiNotifyDispatch, this,
                       &midi_client_);

  if (result != noErr)
    return CompleteInitialization(MIDI_INITIALIZATION_ERROR);

  coremidi_input_ = 0;

  // Create input and output port.
  result = MIDIInputPortCreate(
      midi_client_,
      CFSTR("MIDI Input"),
      ReadMidiDispatch,
      this,
      &coremidi_input_);
  if (result != noErr)
    return CompleteInitialization(MIDI_INITIALIZATION_ERROR);

  result = MIDIOutputPortCreate(
      midi_client_,
      CFSTR("MIDI Output"),
      &coremidi_output_);
  if (result != noErr)
    return CompleteInitialization(MIDI_INITIALIZATION_ERROR);

  uint32 destination_count = MIDIGetNumberOfDestinations();
  destinations_.resize(destination_count);

  for (uint32 i = 0; i < destination_count ; i++) {
    MIDIEndpointRef destination = MIDIGetDestination(i);

    // Keep track of all destinations (known as outputs by the Web MIDI API).
    // Cache to avoid any possible overhead in calling MIDIGetDestination().
    destinations_[i] = destination;

    MidiPortInfo info = GetPortInfoFromEndpoint(destination);
    AddOutputPort(info);
  }

  // Open connections from all sources.
  uint32 source_count = MIDIGetNumberOfSources();

  for (uint32 i = 0; i < source_count; ++i)  {
    // Receive from all sources.
    MIDIEndpointRef src = MIDIGetSource(i);
    MIDIPortConnectSource(coremidi_input_, src, reinterpret_cast<void*>(src));

    // Keep track of all sources (known as inputs in Web MIDI API terminology).
    source_map_[src] = i;

    MidiPortInfo info = GetPortInfoFromEndpoint(src);
    AddInputPort(info);
  }

  packet_list_ = reinterpret_cast<MIDIPacketList*>(midi_buffer_);
  midi_packet_ = MIDIPacketListInit(packet_list_);

  CompleteInitialization(MIDI_OK);
}

// static
void MidiManagerMac::ReceiveMidiNotifyDispatch(const MIDINotification* message,
                                               void* refcon) {
  MidiManagerMac* manager = static_cast<MidiManagerMac*>(refcon);
  manager->ReceiveMidiNotify(message);
}

void MidiManagerMac::ReceiveMidiNotify(const MIDINotification* message) {
  DCHECK(client_thread_.message_loop_proxy()->BelongsToCurrentThread());

  if (kMIDIMsgObjectAdded == message->messageID) {
    const MIDIObjectAddRemoveNotification* notification =
        reinterpret_cast<const MIDIObjectAddRemoveNotification*>(message);
    MIDIEndpointRef endpoint =
        static_cast<MIDIEndpointRef>(notification->child);
    if (notification->childType == kMIDIObjectType_Source) {
      SourceMap::iterator it = source_map_.find(endpoint);
      if (it == source_map_.end()) {
        uint32 index = source_map_.size();
        source_map_[endpoint] = index;
        MidiPortInfo info = GetPortInfoFromEndpoint(endpoint);
        AddInputPort(info);
      } else {
        uint32 index = it->second;
        SetInputPortState(index, MIDI_PORT_OPENED);
      }
    } else if (notification->childType == kMIDIObjectType_Destination) {
      auto i = std::find(destinations_.begin(), destinations_.end(), endpoint);
      if (i != destinations_.end()) {
        SetOutputPortState(i - destinations_.begin(), MIDI_PORT_OPENED);
      } else {
        destinations_.push_back(endpoint);
        MidiPortInfo info = GetPortInfoFromEndpoint(endpoint);
        AddOutputPort(info);
      }
    }
  } else if (kMIDIMsgObjectRemoved == message->messageID) {
    const MIDIObjectAddRemoveNotification* notification =
        reinterpret_cast<const MIDIObjectAddRemoveNotification*>(message);
    MIDIEndpointRef endpoint =
        static_cast<MIDIEndpointRef>(notification->child);
    if (notification->childType == kMIDIObjectType_Source) {
      SourceMap::iterator it = source_map_.find(endpoint);
      if (it != source_map_.end()) {
        uint32 index = it->second;
        SetInputPortState(index, MIDI_PORT_DISCONNECTED);
      }
    } else if (notification->childType == kMIDIObjectType_Destination) {
      auto i = std::find(destinations_.begin(), destinations_.end(), endpoint);
      if (i != destinations_.end())
        SetOutputPortState(i - destinations_.begin(), MIDI_PORT_DISCONNECTED);
    }
  }
}

// static
void MidiManagerMac::ReadMidiDispatch(const MIDIPacketList* packet_list,
                                      void* read_proc_refcon,
                                      void* src_conn_refcon) {
  // This method is called on a separate high-priority thread owned by CoreMIDI.

  MidiManagerMac* manager = static_cast<MidiManagerMac*>(read_proc_refcon);
#if __LP64__
  MIDIEndpointRef source = reinterpret_cast<uintptr_t>(src_conn_refcon);
#else
  MIDIEndpointRef source = static_cast<MIDIEndpointRef>(src_conn_refcon);
#endif

  // Dispatch to class method.
  manager->ReadMidi(source, packet_list);
}

void MidiManagerMac::ReadMidi(MIDIEndpointRef source,
                              const MIDIPacketList* packet_list) {
  // This method is called from ReadMidiDispatch() and runs on a separate
  // high-priority thread owned by CoreMIDI.

  // Lookup the port index based on the source.
  SourceMap::iterator j = source_map_.find(source);
  if (j == source_map_.end())
    return;
  // This is safe since MidiManagerMac does not remove any existing
  // MIDIEndpointRef, and the order is saved.
  uint32 port_index = source_map_[source];

  // Go through each packet and process separately.
  for (size_t i = 0; i < packet_list->numPackets; i++) {
    // Each packet contains MIDI data for one or more messages (like note-on).
    const MIDIPacket &packet = packet_list->packet[i];
    double timestamp_seconds = MIDITimeStampToSeconds(packet.timeStamp);

    ReceiveMidiData(
        port_index,
        packet.data,
        packet.length,
        timestamp_seconds);
  }
}

void MidiManagerMac::SendMidiData(MidiManagerClient* client,
                                  uint32 port_index,
                                  const std::vector<uint8>& data,
                                  double timestamp) {
  DCHECK(client_thread_.message_loop_proxy()->BelongsToCurrentThread());

  // System Exclusive has already been filtered.
  MIDITimeStamp coremidi_timestamp = SecondsToMIDITimeStamp(timestamp);

  midi_packet_ = MIDIPacketListAdd(
      packet_list_,
      kMaxPacketListSize,
      midi_packet_,
      coremidi_timestamp,
      data.size(),
      &data[0]);

  // Lookup the destination based on the port index.
  if (static_cast<size_t>(port_index) >= destinations_.size())
    return;

  MIDIEndpointRef destination = destinations_[port_index];

  MIDISend(coremidi_output_, destination, packet_list_);

  // Re-initialize for next time.
  midi_packet_ = MIDIPacketListInit(packet_list_);

  client->AccumulateMidiBytesSent(data.size());
}

}  // namespace media
