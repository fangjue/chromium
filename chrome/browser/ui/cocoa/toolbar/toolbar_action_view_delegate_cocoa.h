// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_TOOLBAR_TOOLBAR_ACTION_VIEW_DELEGATE_COCOA_H_
#define CHROME_BROWSER_UI_COCOA_TOOLBAR_TOOLBAR_ACTION_VIEW_DELEGATE_COCOA_H_

#include "chrome/browser/ui/toolbar/toolbar_action_view_delegate.h"

class ToolbarActionViewDelegateCocoa : public ToolbarActionViewDelegate {
 public:
  // A Cocoa-specific implementation to return the point used when showing a
  // popup.
  virtual NSPoint GetPopupPoint() = 0;
};

#endif  // CHROME_BROWSER_UI_COCOA_TOOLBAR_TOOLBAR_ACTION_VIEW_DELEGATE_COCOA_H_
