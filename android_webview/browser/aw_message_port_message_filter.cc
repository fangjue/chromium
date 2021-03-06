// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/aw_message_port_message_filter.h"

#include "android_webview/browser/aw_browser_context.h"
#include "android_webview/common/aw_message_port_messages.h"
#include "content/public/browser/message_port_provider.h"

using content::BrowserThread;
using content::MessagePortProvider;

namespace android_webview {

AwMessagePortMessageFilter::AwMessagePortMessageFilter(int route_id)
    : BrowserMessageFilter(AwMessagePortMsgStart), route_id_(route_id) {
}

AwMessagePortMessageFilter::~AwMessagePortMessageFilter() {
}

void AwMessagePortMessageFilter::OnChannelClosing() {
  MessagePortProvider::OnMessagePortDelegateClosing(this);
  AwBrowserContext::GetDefault()->GetMessagePortService()->
      OnMessagePortMessageFilterClosing(this);
}

bool AwMessagePortMessageFilter::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(AwMessagePortMessageFilter, message)
    IPC_MESSAGE_FORWARD(AwMessagePortHostMsg_ConvertedMessage,
                        AwBrowserContext::GetDefault()->GetMessagePortService(),
                        AwMessagePortService::OnConvertedMessage)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void AwMessagePortMessageFilter::OnDestruct() const {
  BrowserThread::DeleteOnIOThread::Destruct(this);
}

void AwMessagePortMessageFilter::SendMessage(
    int msg_port_route_id,
    const base::string16& message,
    const std::vector<int>& sent_message_port_ids) {
  Send(new AwMessagePortMsg_Message(route_id_,
                                    msg_port_route_id, // same as the port id
                                    message,
                                    sent_message_port_ids));
}

void AwMessagePortMessageFilter::SendMessagesAreQueued(int route_id) {
  // TODO(sgurun) implement
  NOTREACHED();
}

}   // namespace android_webview
