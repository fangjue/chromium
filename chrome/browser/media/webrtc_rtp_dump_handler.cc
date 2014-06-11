// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/webrtc_rtp_dump_handler.h"

#include "base/file_util.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/media/webrtc_rtp_dump_writer.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace {

static const size_t kMaxOngoingRtpDumpsAllowed = 5;

// The browser process wide total number of ongoing (i.e. started and not
// released) RTP dumps. Incoming and outgoing in one WebRtcDumpHandler are
// counted as one dump.
// Must be accessed on the browser IO thread.
static size_t g_ongoing_rtp_dumps = 0;

void FireGenericDoneCallback(
    const WebRtcRtpDumpHandler::GenericDoneCallback& callback,
    bool success,
    const std::string& error_message) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  content::BrowserThread::PostTask(
      content::BrowserThread::UI,
      FROM_HERE,
      base::Bind(callback, success, error_message));
}

bool DumpTypeContainsIncoming(RtpDumpType type) {
  return type == RTP_DUMP_INCOMING || type == RTP_DUMP_BOTH;
}

bool DumpTypeContainsOutgoing(RtpDumpType type) {
  return type == RTP_DUMP_OUTGOING || type == RTP_DUMP_BOTH;
}

}  // namespace

WebRtcRtpDumpHandler::WebRtcRtpDumpHandler(const base::FilePath& dump_dir)
    : dump_dir_(dump_dir),
      incoming_state_(STATE_NONE),
      outgoing_state_(STATE_NONE),
      weak_ptr_factory_(this) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
}

WebRtcRtpDumpHandler::~WebRtcRtpDumpHandler() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Reset dump writer first to stop writing.
  if (dump_writer_) {
    --g_ongoing_rtp_dumps;
    dump_writer_.reset();
  }

  if (incoming_state_ != STATE_NONE && !incoming_dump_path_.empty()) {
    BrowserThread::PostTask(
        BrowserThread::FILE,
        FROM_HERE,
        base::Bind(
            base::IgnoreResult(&base::DeleteFile), incoming_dump_path_, false));
  }

  if (outgoing_state_ != STATE_NONE && !outgoing_dump_path_.empty()) {
    BrowserThread::PostTask(
        BrowserThread::FILE,
        FROM_HERE,
        base::Bind(
            base::IgnoreResult(&base::DeleteFile), outgoing_dump_path_, false));
  }
}

bool WebRtcRtpDumpHandler::StartDump(RtpDumpType type,
                                     std::string* error_message) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  if (!dump_writer_ && g_ongoing_rtp_dumps >= kMaxOngoingRtpDumpsAllowed) {
    *error_message = "Max RTP dump limit reached.";
    DVLOG(2) << *error_message;
    return false;
  }

  // Returns an error if any type of dump specified by the caller cannot be
  // started.
  if ((DumpTypeContainsIncoming(type) && incoming_state_ != STATE_NONE) ||
      (DumpTypeContainsOutgoing(type) && outgoing_state_ != STATE_NONE)) {
    *error_message =
        "RTP dump already started for type " + base::IntToString(type);
    return false;
  }

  if (DumpTypeContainsIncoming(type))
    incoming_state_ = STATE_STARTED;

  if (DumpTypeContainsOutgoing(type))
    outgoing_state_ = STATE_STARTED;

  DVLOG(2) << "Start RTP dumping: type = " << type;

  if (!dump_writer_) {
    ++g_ongoing_rtp_dumps;

    static const char kRecvDumpFilePrefix[] = "rtpdump_recv_";
    static const char kSendDumpFilePrefix[] = "rtpdump_send_";
    static const size_t kMaxDumpSize = 5 * 1024 * 1024;  // 5MB

    std::string dump_id = base::DoubleToString(base::Time::Now().ToDoubleT());
    incoming_dump_path_ =
        dump_dir_.AppendASCII(std::string(kRecvDumpFilePrefix) + dump_id)
            .AddExtension(FILE_PATH_LITERAL(".gz"));

    outgoing_dump_path_ =
        dump_dir_.AppendASCII(std::string(kSendDumpFilePrefix) + dump_id)
            .AddExtension(FILE_PATH_LITERAL(".gz"));

    // WebRtcRtpDumpWriter does not support changing the dump path after it's
    // created. So we assign both incoming and outgoing dump path even if only
    // one type of dumping has been started.
    // For "Unretained(this)", see comments StopDump.
    dump_writer_.reset(new WebRtcRtpDumpWriter(
        incoming_dump_path_,
        outgoing_dump_path_,
        kMaxDumpSize,
        base::Bind(&WebRtcRtpDumpHandler::OnMaxDumpSizeReached,
                   base::Unretained(this))));
  }

  return true;
}

void WebRtcRtpDumpHandler::StopDump(RtpDumpType type,
                                    const GenericDoneCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Returns an error if any type of dump specified by the caller cannot be
  // stopped.
  if ((DumpTypeContainsIncoming(type) && incoming_state_ != STATE_STARTED) ||
      (DumpTypeContainsOutgoing(type) && outgoing_state_ != STATE_STARTED)) {
    if (!callback.is_null()) {
      FireGenericDoneCallback(
          callback,
          false,
          "RTP dump not started or already stopped for type " +
              base::IntToString(type));
    }
    return;
  }

  DVLOG(2) << "Stopping RTP dumping: type = " << type;

  if (DumpTypeContainsIncoming(type))
    incoming_state_ = STATE_STOPPING;

  if (DumpTypeContainsOutgoing(type))
    outgoing_state_ = STATE_STOPPING;

  // Using "Unretained(this)" because the this object owns the writer and the
  // writer is guaranteed to cancel the callback before it goes away. Same for
  // the other posted tasks bound to the writer.
  dump_writer_->EndDump(
      type,
      base::Bind(&WebRtcRtpDumpHandler::OnDumpEnded,
                 base::Unretained(this),
                 callback.is_null()
                     ? base::Closure()
                     : base::Bind(&FireGenericDoneCallback, callback, true, ""),
                 type));
}

bool WebRtcRtpDumpHandler::ReadyToRelease() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  return incoming_state_ != STATE_STARTED &&
         incoming_state_ != STATE_STOPPING &&
         outgoing_state_ != STATE_STARTED && outgoing_state_ != STATE_STOPPING;
}

WebRtcRtpDumpHandler::ReleasedDumps WebRtcRtpDumpHandler::ReleaseDumps() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(ReadyToRelease());

  base::FilePath incoming_dump, outgoing_dump;

  if (incoming_state_ == STATE_STOPPED) {
    DVLOG(2) << "Incoming RTP dumps released: " << incoming_dump_path_.value();

    incoming_state_ = STATE_NONE;
    incoming_dump = incoming_dump_path_;
  }

  if (outgoing_state_ == STATE_STOPPED) {
    DVLOG(2) << "Outgoing RTP dumps released: " << outgoing_dump_path_.value();

    outgoing_state_ = STATE_NONE;
    outgoing_dump = outgoing_dump_path_;
  }
  return ReleasedDumps(incoming_dump, outgoing_dump);
}

void WebRtcRtpDumpHandler::OnRtpPacket(const uint8* packet_header,
                                       size_t header_length,
                                       size_t packet_length,
                                       bool incoming) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  if ((incoming && incoming_state_ != STATE_STARTED) ||
      (!incoming && outgoing_state_ != STATE_STARTED)) {
    return;
  }

  dump_writer_->WriteRtpPacket(
      packet_header, header_length, packet_length, incoming);
}

void WebRtcRtpDumpHandler::StopOngoingDumps(const base::Closure& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!callback.is_null());

  // No ongoing dumps, return directly.
  if ((incoming_state_ == STATE_NONE || incoming_state_ == STATE_STOPPED) &&
      (outgoing_state_ == STATE_NONE || outgoing_state_ == STATE_STOPPED)) {
    callback.Run();
    return;
  }

  // If the FILE thread is working on stopping the dumps, wait for the FILE
  // thread to return and check the states again.
  if (incoming_state_ == STATE_STOPPING || outgoing_state_ == STATE_STOPPING) {
    BrowserThread::PostTaskAndReply(
        BrowserThread::FILE,
        FROM_HERE,
        base::Bind(&base::DoNothing),
        base::Bind(&WebRtcRtpDumpHandler::StopOngoingDumps,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
    return;
  }

  // Either incoming or outgoing dump must be ongoing.
  RtpDumpType type =
      (incoming_state_ == STATE_STARTED)
          ? (outgoing_state_ == STATE_STARTED ? RTP_DUMP_BOTH
                                              : RTP_DUMP_INCOMING)
          : RTP_DUMP_OUTGOING;

  if (incoming_state_ == STATE_STARTED)
    incoming_state_ = STATE_STOPPING;

  if (outgoing_state_ == STATE_STARTED)
    outgoing_state_ = STATE_STOPPING;

  DVLOG(2) << "Stopping ongoing dumps: type = " << type;

  dump_writer_->EndDump(type,
                        base::Bind(&WebRtcRtpDumpHandler::OnDumpEnded,
                                   base::Unretained(this),
                                   callback,
                                   type));
}

void WebRtcRtpDumpHandler::SetDumpWriterForTesting(
    scoped_ptr<WebRtcRtpDumpWriter> writer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  dump_writer_ = writer.Pass();
  ++g_ongoing_rtp_dumps;

  incoming_dump_path_ = dump_dir_.AppendASCII("recv");
  outgoing_dump_path_ = dump_dir_.AppendASCII("send");
}

void WebRtcRtpDumpHandler::OnMaxDumpSizeReached() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  RtpDumpType type =
      (incoming_state_ == STATE_STARTED)
          ? (outgoing_state_ == STATE_STARTED ? RTP_DUMP_BOTH
                                              : RTP_DUMP_INCOMING)
          : RTP_DUMP_OUTGOING;
  StopDump(type, GenericDoneCallback());
}

void WebRtcRtpDumpHandler::OnDumpEnded(const base::Closure& callback,
                                       RtpDumpType ended_type,
                                       bool incoming_success,
                                       bool outgoing_success) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  if (DumpTypeContainsIncoming(ended_type)) {
    DCHECK_EQ(STATE_STOPPING, incoming_state_);
    incoming_state_ = STATE_STOPPED;

    if (!incoming_success) {
      BrowserThread::PostTask(BrowserThread::FILE,
                              FROM_HERE,
                              base::Bind(base::IgnoreResult(&base::DeleteFile),
                                         incoming_dump_path_,
                                         false));

      DVLOG(2) << "Deleted invalid incoming dump "
               << incoming_dump_path_.value();
      incoming_dump_path_.clear();
    }
  }

  if (DumpTypeContainsOutgoing(ended_type)) {
    DCHECK_EQ(STATE_STOPPING, outgoing_state_);
    outgoing_state_ = STATE_STOPPED;

    if (!outgoing_success) {
      BrowserThread::PostTask(BrowserThread::FILE,
                              FROM_HERE,
                              base::Bind(base::IgnoreResult(&base::DeleteFile),
                                         outgoing_dump_path_,
                                         false));

      DVLOG(2) << "Deleted invalid outgoing dump "
               << outgoing_dump_path_.value();
      outgoing_dump_path_.clear();
    }
  }

  // Release the writer when it's no longer needed.
  if (incoming_state_ != STATE_STOPPING && outgoing_state_ != STATE_STOPPING &&
      incoming_state_ != STATE_STARTED && outgoing_state_ != STATE_STARTED) {
    dump_writer_.reset();
    --g_ongoing_rtp_dumps;
  }

  // This object might be deleted after running the callback.
  if (!callback.is_null())
    callback.Run();
}
