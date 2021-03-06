// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPC_IPC_MESSAGE_ATTACHMENT_H_
#define IPC_IPC_MESSAGE_ATTACHMENT_H_

#include "base/memory/ref_counted.h"
#include "ipc/ipc_export.h"

namespace IPC {

// Auxiliary data sent with |Message|. This can be a platform file descriptor
// or a mojo |MessagePipe|. |GetType()| returns the type of the subclass.
class IPC_EXPORT MessageAttachment
    : public base::RefCounted<MessageAttachment> {
 public:
  enum Type {
    TYPE_PLATFORM_FILE,     // The instance is |PlatformFileAttachment|.
    TYPE_MOJO_MESSAGE_PIPE, // The instance is a mojo-based class.
  };

  virtual Type GetType() const = 0;

 protected:
  friend class base::RefCounted<MessageAttachment>;
  virtual ~MessageAttachment();
};

}  // namespace IPC

#endif  // IPC_IPC_MESSAGE_ATTACHMENT_H_
