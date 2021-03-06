// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FRAME_HOST_NAVIGATOR_H_
#define CONTENT_BROWSER_FRAME_HOST_NAVIGATOR_H_

#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "content/common/content_export.h"
#include "content/public/browser/navigation_controller.h"
#include "ui/base/window_open_disposition.h"

class GURL;
struct FrameHostMsg_BeginNavigation_Params;
struct FrameHostMsg_DidCommitProvisionalLoad_Params;
struct FrameHostMsg_DidFailProvisionalLoadWithError_Params;

namespace base {
class TimeTicks;
}

namespace content {

class FrameTreeNode;
class NavigationControllerImpl;
class NavigationEntryImpl;
class NavigationRequest;
class NavigatorDelegate;
class RenderFrameHostImpl;
class StreamHandle;
struct CommonNavigationParams;
struct ResourceResponse;

// Implementations of this interface are responsible for performing navigations
// in a node of the FrameTree. Its lifetime is bound to all FrameTreeNode
// objects that are using it and will be released once all nodes that use it are
// freed. The Navigator is bound to a single frame tree and cannot be used by
// multiple instances of FrameTree.
// TODO(nasko): Move all navigation methods, such as didStartProvisionalLoad
// from WebContentsImpl to this interface.
class CONTENT_EXPORT Navigator : public base::RefCounted<Navigator> {
 public:
  // Returns the NavigationController associated with this Navigator.
  virtual NavigationController* GetController();

  // Notifications coming from the RenderFrameHosts ----------------------------

  // The RenderFrameHostImpl started a provisional load.
  virtual void DidStartProvisionalLoad(RenderFrameHostImpl* render_frame_host,
                                       const GURL& url,
                                       bool is_transition_navigation) {};

  // The RenderFrameHostImpl has failed a provisional load.
  virtual void DidFailProvisionalLoadWithError(
      RenderFrameHostImpl* render_frame_host,
      const FrameHostMsg_DidFailProvisionalLoadWithError_Params& params) {};

  // The RenderFrameHostImpl has failed to load the document.
  virtual void DidFailLoadWithError(
      RenderFrameHostImpl* render_frame_host,
      const GURL& url,
      int error_code,
      const base::string16& error_description) {}

  // The RenderFrameHostImpl has committed a navigation.
  virtual void DidNavigate(
      RenderFrameHostImpl* render_frame_host,
      const FrameHostMsg_DidCommitProvisionalLoad_Params& params) {}

  // Called by the NavigationController to cause the Navigator to navigate
  // to the current pending entry. The NavigationController should be called
  // back with RendererDidNavigate on success or DiscardPendingEntry on failure.
  // The callbacks can be inside of this function, or at some future time.
  //
  // The entry has a PageID of -1 if newly created (corresponding to navigation
  // to a new URL).
  //
  // If this method returns false, then the navigation is discarded (equivalent
  // to calling DiscardPendingEntry on the NavigationController).
  //
  // TODO(nasko): Remove this method from the interface, since Navigator and
  // NavigationController know about each other. This will be possible once
  // initialization of Navigator and NavigationController is properly done.
  virtual bool NavigateToPendingEntry(
      RenderFrameHostImpl* render_frame_host,
      NavigationController::ReloadType reload_type);


  // Navigation requests -------------------------------------------------------

  virtual base::TimeTicks GetCurrentLoadStart();

  // The RenderFrameHostImpl has received a request to open a URL with the
  // specified |disposition|.
  virtual void RequestOpenURL(RenderFrameHostImpl* render_frame_host,
                              const GURL& url,
                              SiteInstance* source_site_instance,
                              const Referrer& referrer,
                              WindowOpenDisposition disposition,
                              bool should_replace_current_entry,
                              bool user_gesture) {}

  // The RenderFrameHostImpl wants to transfer the request to a new renderer.
  // |redirect_chain| contains any redirect URLs (excluding |url|) that happened
  // before the transfer.
  virtual void RequestTransferURL(
      RenderFrameHostImpl* render_frame_host,
      const GURL& url,
      SiteInstance* source_site_instance,
      const std::vector<GURL>& redirect_chain,
      const Referrer& referrer,
      ui::PageTransition page_transition,
      WindowOpenDisposition disposition,
      const GlobalRequestID& transferred_global_request_id,
      bool should_replace_current_entry,
      bool user_gesture) {}

  // PlzNavigate: Used to start a navigation. OnBeginNavigation is called
  // directly by RequestNavigation when there is no live renderer. Otherwise, it
  // is called following a BeginNavigation IPC from the renderer (which in
  // browser-initiated navigation also happens after RequestNavigation has been
  // called).
  virtual void OnBeginNavigation(
      FrameTreeNode* frame_tree_node,
      const FrameHostMsg_BeginNavigation_Params& params,
      const CommonNavigationParams& common_params) {}

  // PlzNavigate
  // Signal |render_frame_host| that a navigation is ready to commit (the
  // response to the navigation request has been received).
  virtual void CommitNavigation(FrameTreeNode* frame_tree_node,
                                ResourceResponse* response,
                                scoped_ptr<StreamHandle> body);

  // PlzNavigate
  // Cancel a NavigationRequest for |frame_tree_node|. Called when
  // |frame_tree_node| is destroyed.
  virtual void CancelNavigation(FrameTreeNode* frame_tree_node) {}

  // Called when the first resource request for a given navigation is executed
  // so that it can be tracked into an histogram.
  virtual void LogResourceRequestTime(
    base::TimeTicks timestamp, const GURL& url) {};

  // Called to record the time it took to execute the before unload hook for the
  // current navigation.
  virtual void LogBeforeUnloadTime(
      const base::TimeTicks& renderer_before_unload_start_time,
      const base::TimeTicks& renderer_before_unload_end_time) {}

  // PlzNavigate
  // Returns whether there is an ongoing navigation waiting for the BeforeUnload
  // event to execute in the renderer process.
  virtual bool IsWaitingForBeforeUnloadACK(FrameTreeNode* frame_tree_node);

 protected:
  friend class base::RefCounted<Navigator>;
  virtual ~Navigator() {}
};

}  // namespace content

#endif  // CONTENT_BROWSER_FRAME_HOST_NAVIGATOR_H_
