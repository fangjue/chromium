// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_BLUETOOTH_AUDIO_SINK_CHROMEOS_H_
#define DEVICE_BLUETOOTH_BLUETOOTH_AUDIO_SINK_CHROMEOS_H_

#include <stdint.h>
#include <string>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chromeos/dbus/bluetooth_media_client.h"
#include "chromeos/dbus/bluetooth_media_endpoint_service_provider.h"
#include "chromeos/dbus/bluetooth_media_transport_client.h"
#include "dbus/file_descriptor.h"
#include "dbus/object_path.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_adapter_chromeos.h"
#include "device/bluetooth/bluetooth_audio_sink.h"
#include "device/bluetooth/bluetooth_export.h"

namespace chromeos {

class DEVICE_BLUETOOTH_EXPORT BluetoothAudioSinkChromeOS
    : public device::BluetoothAudioSink,
      public device::BluetoothAdapter::Observer,
      public BluetoothMediaClient::Observer,
      public BluetoothMediaTransportClient::Observer,
      public BluetoothMediaEndpointServiceProvider::Delegate {
 public:
  explicit BluetoothAudioSinkChromeOS(BluetoothAdapterChromeOS* adapter);

  // device::BluetoothAudioSink overrides.
  void AddObserver(BluetoothAudioSink::Observer* observer) override;
  void RemoveObserver(BluetoothAudioSink::Observer* observer) override;
  device::BluetoothAudioSink::State GetState() const override;
  uint16_t GetVolume() const override;

  // device::BluetoothAdapter::Observer overrides.
  void AdapterPresentChanged(device::BluetoothAdapter* adapter,
                             bool present) override;
  void AdapterPoweredChanged(device::BluetoothAdapter* adapter,
                             bool powered) override;

  // BluetoothMediaClient::Observer overrides.
  void MediaRemoved(const dbus::ObjectPath& object_path) override;

  // BluetoothMediaTransportClient::Observer overrides.
  void MediaTransportRemoved(const dbus::ObjectPath& object_path) override;
  void MediaTransportPropertyChanged(const dbus::ObjectPath& object_path,
                                     const std::string& property_name) override;

  // BluetoothMediaEndpointServiceProvider::Delegate overrides.
  void SetConfiguration(const dbus::ObjectPath& transport_path,
                        const dbus::MessageReader& properties) override;
  void SelectConfiguration(
      const std::vector<uint8_t>& capabilities,
      const SelectConfigurationCallback& callback) override;
  void ClearConfiguration(const dbus::ObjectPath& transport_path) override;
  void Release() override;

  // Registers a BluetoothAudioSink. User applications can use |options| to
  // configure the audio sink. |callback| will be executed if the audio sink is
  // successfully registered, otherwise |error_callback| will be called. Called
  // from BluetoothAdapterChromeOS.
  void Register(
      const device::BluetoothAudioSink::Options& options,
      const base::Closure& callback,
      const device::BluetoothAudioSink::ErrorCallback& error_callback);

  // Unregisters a BluetoothAudioSink. |callback| should handle
  // the clean-up after the audio sink is deleted successfully, otherwise
  // |error_callback| will be called.
  void Unregister(
      const base::Closure& callback,
      const device::BluetoothAudioSink::ErrorCallback& error_callback) override;

 private:
  ~BluetoothAudioSinkChromeOS() override;

  // Called when the state property of BluetoothMediaTransport has been updated.
  void StateChanged(device::BluetoothAudioSink::State state);

  // Called when the volume property of BluetoothMediaTransport has been
  // updated.
  void VolumeChanged(uint16_t volume);

  // Reads from the file descriptor acquired via Media Transport object and
  // notify |observer_| while the audio data is available.
  void ReadFromFD();

  // The connection state between the BluetoothAudioSinkChromeOS and the remote
  // device.
  device::BluetoothAudioSink::State state_;

  // Indicates whether the adapter is present.
  bool present_;

  // Indicates whether the adapter is powered.
  bool powered_;

  // The volume control by the remote device during the streaming.
  uint16_t volume_;

  // Read MTU of the file descriptor acquired via Media Transport object.
  uint16_t read_mtu_;

  // Write MTU of the file descriptor acquired via Media Transport object.
  uint16_t write_mtu_;

  // File descriptor acquired via Media Transport object.
  dbus::FileDescriptor fd_;

  // Object path of the media object being used.
  dbus::ObjectPath media_path_;

  // Object path of the transport object being used.
  dbus::ObjectPath transport_path_;

  // Object path of the media endpoint object being used.
  dbus::ObjectPath endpoint_path_;

  // BT adapter which the audio sink binds to. |adapter_| should outlive
  // a BluetoothAudioSinkChromeOS object.
  BluetoothAdapterChromeOS* adapter_;

  // Options used to initiate Media Endpoint and select configuration for the
  // transport.
  device::BluetoothAudioSink::Options options_;

  // Media Endpoint object owned by the audio sink object.
  scoped_ptr<BluetoothMediaEndpointServiceProvider> media_endpoint_;

  // List of observers interested in event notifications from us. Objects in
  // |observers_| are expected to outlive a BluetoothAudioSinkChromeOS object.
  ObserverList<BluetoothAudioSink::Observer> observers_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<BluetoothAudioSinkChromeOS> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothAudioSinkChromeOS);
};

}  // namespace chromeos

#endif  // DEVICE_BLUETOOTH_BLUETOOTH_AUDIO_SINK_CHROMEOS_H_
