// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/site_per_process_browsertest.h"

#include "base/command_line.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "content/browser/frame_host/cross_process_frame_connector.h"
#include "content/browser/frame_host/frame_tree.h"
#include "content/browser/frame_host/navigator.h"
#include "content/browser/frame_host/render_frame_proxy_host.h"
#include "content/browser/frame_host/render_widget_host_view_child_frame.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "content/shell/browser/shell.h"
#include "content/test/content_browser_test_utils_internal.h"
#include "content/test/test_frame_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace content {

class SitePerProcessWebContentsObserver: public WebContentsObserver {
 public:
  explicit SitePerProcessWebContentsObserver(WebContents* web_contents)
      : WebContentsObserver(web_contents),
        navigation_succeeded_(false) {}
  ~SitePerProcessWebContentsObserver() override {}

  void DidStartProvisionalLoadForFrame(RenderFrameHost* render_frame_host,
                                       const GURL& validated_url,
                                       bool is_error_page,
                                       bool is_iframe_srcdoc) override {
    navigation_succeeded_ = false;
  }

  void DidFailProvisionalLoad(
      RenderFrameHost* render_frame_host,
      const GURL& validated_url,
      int error_code,
      const base::string16& error_description) override {
    navigation_url_ = validated_url;
    navigation_succeeded_ = false;
  }

  void DidCommitProvisionalLoadForFrame(
      RenderFrameHost* render_frame_host,
      const GURL& url,
      ui::PageTransition transition_type) override {
    navigation_url_ = url;
    navigation_succeeded_ = true;
  }

  const GURL& navigation_url() const {
    return navigation_url_;
  }

  int navigation_succeeded() const { return navigation_succeeded_; }

 private:
  GURL navigation_url_;
  bool navigation_succeeded_;

  DISALLOW_COPY_AND_ASSIGN(SitePerProcessWebContentsObserver);
};

class RedirectNotificationObserver : public NotificationObserver {
 public:
  // Register to listen for notifications of the given type from either a
  // specific source, or from all sources if |source| is
  // NotificationService::AllSources().
  RedirectNotificationObserver(int notification_type,
                               const NotificationSource& source);
  ~RedirectNotificationObserver() override;

  // Wait until the specified notification occurs.  If the notification was
  // emitted between the construction of this object and this call then it
  // returns immediately.
  void Wait();

  // Returns NotificationService::AllSources() if we haven't observed a
  // notification yet.
  const NotificationSource& source() const {
    return source_;
  }

  const NotificationDetails& details() const {
    return details_;
  }

  // NotificationObserver:
  void Observe(int type,
               const NotificationSource& source,
               const NotificationDetails& details) override;

 private:
  bool seen_;
  bool seen_twice_;
  bool running_;
  NotificationRegistrar registrar_;

  NotificationSource source_;
  NotificationDetails details_;
  scoped_refptr<MessageLoopRunner> message_loop_runner_;

  DISALLOW_COPY_AND_ASSIGN(RedirectNotificationObserver);
};

RedirectNotificationObserver::RedirectNotificationObserver(
    int notification_type,
    const NotificationSource& source)
    : seen_(false),
      running_(false),
      source_(NotificationService::AllSources()) {
  registrar_.Add(this, notification_type, source);
}

RedirectNotificationObserver::~RedirectNotificationObserver() {}

void RedirectNotificationObserver::Wait() {
  if (seen_ && seen_twice_)
    return;

  running_ = true;
  message_loop_runner_ = new MessageLoopRunner;
  message_loop_runner_->Run();
  EXPECT_TRUE(seen_);
}

void RedirectNotificationObserver::Observe(
    int type,
    const NotificationSource& source,
    const NotificationDetails& details) {
  source_ = source;
  details_ = details;
  seen_twice_ = seen_;
  seen_ = true;
  if (!running_)
    return;

  message_loop_runner_->Quit();
  running_ = false;
}

// This observer keeps track of the number of created RenderFrameHosts.  Tests
// can use this to ensure that a certain number of child frames has been
// created after navigating.
class RenderFrameHostCreatedObserver : public WebContentsObserver {
 public:
  RenderFrameHostCreatedObserver(WebContents* web_contents,
                                 int expected_frame_count)
      : WebContentsObserver(web_contents),
        expected_frame_count_(expected_frame_count),
        frames_created_(0),
        message_loop_runner_(new MessageLoopRunner) {}

  ~RenderFrameHostCreatedObserver() override;

  // Runs a nested message loop and blocks until the expected number of
  // RenderFrameHosts is created.
  void Wait();

 private:
  // WebContentsObserver
  void RenderFrameCreated(RenderFrameHost* render_frame_host) override;

  // The number of RenderFrameHosts to wait for.
  int expected_frame_count_;

  // The number of RenderFrameHosts that have been created.
  int frames_created_;

  // The MessageLoopRunner used to spin the message loop.
  scoped_refptr<MessageLoopRunner> message_loop_runner_;

  DISALLOW_COPY_AND_ASSIGN(RenderFrameHostCreatedObserver);
};

RenderFrameHostCreatedObserver::~RenderFrameHostCreatedObserver() {
}

void RenderFrameHostCreatedObserver::Wait() {
  message_loop_runner_->Run();
}

void RenderFrameHostCreatedObserver::RenderFrameCreated(
    RenderFrameHost* render_frame_host) {
  frames_created_++;
  if (frames_created_ == expected_frame_count_) {
    message_loop_runner_->Quit();
  }
}

//
// SitePerProcessBrowserTest
//

SitePerProcessBrowserTest::SitePerProcessBrowserTest() {
};

void SitePerProcessBrowserTest::StartFrameAtDataURL() {
  std::string data_url_script =
      "var iframes = document.getElementById('test');iframes.src="
      "'data:text/html,dataurl';";
  ASSERT_TRUE(ExecuteScript(shell()->web_contents(), data_url_script));
}

void SitePerProcessBrowserTest::SetUpCommandLine(
    base::CommandLine* command_line) {
  command_line->AppendSwitch(switches::kSitePerProcess);
};

void SitePerProcessBrowserTest::SetUpOnMainThread() {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  SetupCrossSiteRedirector(embedded_test_server());
}

// Ensure that navigating subframes in --site-per-process mode works and the
// correct documents are committed.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, CrossSiteIframe) {
  GURL main_url(embedded_test_server()->GetURL("/site_per_process_main.html"));
  NavigateToURL(shell(), main_url);

  // It is safe to obtain the root frame tree node here, as it doesn't change.
  FrameTreeNode* root =
      static_cast<WebContentsImpl*>(shell()->web_contents())->
          GetFrameTree()->root();

  SitePerProcessWebContentsObserver observer(shell()->web_contents());

  // Load same-site page into iframe.
  FrameTreeNode* child = root->child_at(0);
  GURL http_url(embedded_test_server()->GetURL("/title1.html"));
  NavigateFrameToURL(child, http_url);
  EXPECT_EQ(http_url, observer.navigation_url());
  EXPECT_TRUE(observer.navigation_succeeded());
  {
    // There should be only one RenderWidgetHost when there are no
    // cross-process iframes.
    std::set<RenderWidgetHostView*> views_set =
        static_cast<WebContentsImpl*>(shell()->web_contents())
            ->GetRenderWidgetHostViewsInTree();
    EXPECT_EQ(1U, views_set.size());
  }
  RenderFrameProxyHost* proxy_to_parent =
      child->render_manager()->GetRenderFrameProxyHost(
          shell()->web_contents()->GetSiteInstance());
  EXPECT_FALSE(proxy_to_parent);

  // Load cross-site page into iframe.
  GURL url = embedded_test_server()->GetURL("foo.com", "/title2.html");
  NavigateFrameToURL(root->child_at(0), url);
  // Verify that the navigation succeeded and the expected URL was loaded.
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(url, observer.navigation_url());

  // Ensure that we have created a new process for the subframe.
  ASSERT_EQ(2U, root->child_count());
  SiteInstance* site_instance = child->current_frame_host()->GetSiteInstance();
  RenderViewHost* rvh = child->current_frame_host()->render_view_host();
  RenderProcessHost* rph = child->current_frame_host()->GetProcess();
  EXPECT_NE(shell()->web_contents()->GetRenderViewHost(), rvh);
  EXPECT_NE(shell()->web_contents()->GetSiteInstance(), site_instance);
  EXPECT_NE(shell()->web_contents()->GetRenderProcessHost(), rph);
  {
    // There should be now two RenderWidgetHosts, one for each process
    // rendering a frame.
    std::set<RenderWidgetHostView*> views_set =
        static_cast<WebContentsImpl*>(shell()->web_contents())
            ->GetRenderWidgetHostViewsInTree();
    EXPECT_EQ(2U, views_set.size());
  }
  proxy_to_parent = child->render_manager()->GetProxyToParent();
  EXPECT_TRUE(proxy_to_parent);
  EXPECT_TRUE(proxy_to_parent->cross_process_frame_connector());
  // The out-of-process iframe should have its own RenderWidgetHost,
  // independent of any RenderViewHost.
  EXPECT_NE(
      rvh->GetView(),
      proxy_to_parent->cross_process_frame_connector()->get_view_for_testing());
  EXPECT_TRUE(child->current_frame_host()->GetRenderWidgetHost());

  // Load another cross-site page into the same iframe.
  url = embedded_test_server()->GetURL("bar.com", "/title3.html");
  NavigateFrameToURL(root->child_at(0), url);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(url, observer.navigation_url());

  // Check again that a new process is created and is different from the
  // top level one and the previous one.
  ASSERT_EQ(2U, root->child_count());
  child = root->child_at(0);
  EXPECT_NE(shell()->web_contents()->GetRenderViewHost(),
            child->current_frame_host()->render_view_host());
  EXPECT_NE(rvh, child->current_frame_host()->render_view_host());
  EXPECT_NE(shell()->web_contents()->GetSiteInstance(),
            child->current_frame_host()->GetSiteInstance());
  EXPECT_NE(site_instance,
            child->current_frame_host()->GetSiteInstance());
  EXPECT_NE(shell()->web_contents()->GetRenderProcessHost(),
            child->current_frame_host()->GetProcess());
  EXPECT_NE(rph, child->current_frame_host()->GetProcess());
  {
    std::set<RenderWidgetHostView*> views_set =
        static_cast<WebContentsImpl*>(shell()->web_contents())
            ->GetRenderWidgetHostViewsInTree();
    EXPECT_EQ(2U, views_set.size());
  }
  EXPECT_EQ(proxy_to_parent, child->render_manager()->GetProxyToParent());
  EXPECT_TRUE(proxy_to_parent->cross_process_frame_connector());
  EXPECT_NE(
      child->current_frame_host()->render_view_host()->GetView(),
      proxy_to_parent->cross_process_frame_connector()->get_view_for_testing());
  EXPECT_TRUE(child->current_frame_host()->GetRenderWidgetHost());
}

// Disabled for flaky crashing: crbug.com/446575
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       DISABLED_NavigateRemoteFrame) {
  GURL main_url(embedded_test_server()->GetURL("/site_per_process_main.html"));
  NavigateToURL(shell(), main_url);

  // It is safe to obtain the root frame tree node here, as it doesn't change.
  FrameTreeNode* root =
      static_cast<WebContentsImpl*>(shell()->web_contents())->
          GetFrameTree()->root();

  SitePerProcessWebContentsObserver observer(shell()->web_contents());

  // Load same-site page into iframe.
  FrameTreeNode* child = root->child_at(0);
  GURL http_url(embedded_test_server()->GetURL("/title1.html"));
  NavigateFrameToURL(child, http_url);
  EXPECT_EQ(http_url, observer.navigation_url());
  EXPECT_TRUE(observer.navigation_succeeded());

  // Load cross-site page into iframe.
  GURL url = embedded_test_server()->GetURL("foo.com", "/title2.html");
  NavigateFrameToURL(root->child_at(0), url);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(url, observer.navigation_url());

  // Ensure that we have created a new process for the subframe.
  ASSERT_EQ(2U, root->child_count());
  SiteInstance* site_instance = child->current_frame_host()->GetSiteInstance();
  EXPECT_NE(shell()->web_contents()->GetSiteInstance(), site_instance);

  // Emulate the main frame changing the src of the iframe such that it
  // navigates cross-site.
  url = embedded_test_server()->GetURL("bar.com", "/title3.html");
  NavigateIframeToURL(shell()->web_contents(), "test", url);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(url, observer.navigation_url());

  // Check again that a new process is created and is different from the
  // top level one and the previous one.
  ASSERT_EQ(2U, root->child_count());
  child = root->child_at(0);
  EXPECT_NE(shell()->web_contents()->GetSiteInstance(),
            child->current_frame_host()->GetSiteInstance());
  EXPECT_NE(site_instance,
            child->current_frame_host()->GetSiteInstance());

  // Navigate back to the parent's origin and ensure we return to the
  // parent's process.
  NavigateFrameToURL(child, http_url);
  EXPECT_EQ(http_url, observer.navigation_url());
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(shell()->web_contents()->GetSiteInstance(),
            child->current_frame_host()->GetSiteInstance());
}

// In A-embed-B-embed-C scenario, verify that killing process B clears proxies
// of C from the tree.
//
//     1          A                  A
//    / \        / \                / \    .
//   2   3 ->   B   A -> Kill B -> B*  A
//  /          /
// 4          C
//
// node1 is the root.
// Initially, both node1.proxy_hosts_ and node3.proxy_hosts_ contain C.
// After we kill B, make sure proxies for C are cleared.
//
// TODO(lazyboy): Once http://crbug.com/432107 is fixed, we should also make
// sure that proxies for B are not cleared when we kill B.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       KillingRendererClearsDescendantProxies) {
  GURL main_url(
      embedded_test_server()->GetURL("/frame_tree/page_with_two_frames.html"));
  NavigateToURL(shell(), main_url);

  // It is safe to obtain the root frame tree node here, as it doesn't change.
  FrameTreeNode* root =
      static_cast<WebContentsImpl*>(shell()->web_contents())->
          GetFrameTree()->root();
  SitePerProcessWebContentsObserver observer(shell()->web_contents());

  ASSERT_EQ(2U, root->child_count());

  // Navigate the second subframe (node3) to a local frame.
  GURL site_a_url(embedded_test_server()->GetURL("/title1.html"));
  NavigateFrameToURL(root->child_at(1), site_a_url);

  // Navigate the first subframe (node2) to a cross-site page with two
  // subframes.
  // NavigateFrameToURL can't be used here because it doesn't guarantee that
  // FrameTreeNodes will have been created for child frames when it returns.
  RenderFrameHostCreatedObserver frame_observer(shell()->web_contents(), 3);
  GURL site_b_url(
      embedded_test_server()->GetURL(
          "bar.com", "/frame_tree/page_with_one_frame.html"));
  NavigationController::LoadURLParams params_b(site_b_url);
  params_b.transition_type = ui::PAGE_TRANSITION_LINK;
  params_b.frame_tree_node_id = root->child_at(0)->frame_tree_node_id();
  root->child_at(0)->navigator()->GetController()->LoadURLWithParams(params_b);
  frame_observer.Wait();

  // We can't use a SitePerProcessWebContentsObserver to verify the URL here,
  // since the frame has children that may have clobbered it in the observer.
  EXPECT_EQ(site_b_url, root->child_at(0)->current_url());

  // Ensure that a new process is created for node2.
  EXPECT_NE(shell()->web_contents()->GetSiteInstance(),
            root->child_at(0)->current_frame_host()->GetSiteInstance());
  // Ensure that a new process is *not* created for node3.
  EXPECT_EQ(shell()->web_contents()->GetSiteInstance(),
            root->child_at(1)->current_frame_host()->GetSiteInstance());

  ASSERT_EQ(1U, root->child_at(0)->child_count());

  // Navigate node4 to cross-site-page.
  FrameTreeNode* node4 = root->child_at(0)->child_at(0);
  GURL site_c_url(embedded_test_server()->GetURL("baz.com", "/title2.html"));
  NavigateFrameToURL(node4, site_c_url);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(site_c_url, observer.navigation_url());

  // |site_instance_c| is expected to go away once we kill |child_process_b|
  // below, so create a local scope so we can extend the lifetime of
  // |site_instance_c| with a refptr.
  {
    SiteInstance* site_instance_b =
        root->child_at(0)->current_frame_host()->GetSiteInstance();
    scoped_refptr<SiteInstanceImpl> site_instance_c =
        node4->current_frame_host()->GetSiteInstance();

    // Initially proxies for both B and C will be present in the root.
    EXPECT_TRUE(root->render_manager()->GetRenderFrameProxyHost(
                    site_instance_b));
    EXPECT_TRUE(root->render_manager()->GetRenderFrameProxyHost(
                    site_instance_c.get()));

    // Kill process B.
    RenderProcessHost* child_process_b =
        root->child_at(0)->current_frame_host()->GetProcess();
    RenderProcessHostWatcher crash_observer(
        child_process_b, RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
    child_process_b->Shutdown(0, false);
    crash_observer.Wait();

    // Make sure proxy C has gone from root.
    EXPECT_FALSE(root->render_manager()->GetRenderFrameProxyHost(
                     site_instance_c.get()));
    // Make sure proxy C has gone from node3 as well.
    EXPECT_FALSE(root->child_at(1)->render_manager()->GetRenderFrameProxyHost(
                     site_instance_c.get()));
    // TODO(lazyboy): Once http://crbug.com/432107 is fixed, we should also
    // check that proxy B exists in both root and node3.
  }
}

// Crash a subframe and ensures its children are cleared from the FrameTree.
// See http://crbug.com/338508.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, CrashSubframe) {
  GURL main_url(embedded_test_server()->GetURL("/site_per_process_main.html"));
  NavigateToURL(shell(), main_url);

  StartFrameAtDataURL();

  // These must stay in scope with replace_host.
  GURL::Replacements replace_host;
  std::string foo_com("foo.com");

  // Load cross-site page into iframe.
  EXPECT_TRUE(NavigateIframeToURL(
      shell()->web_contents(), "test",
      embedded_test_server()->GetURL("/cross-site/foo.com/title2.html")));

  // Check the subframe process.
  FrameTreeNode* root =
      static_cast<WebContentsImpl*>(shell()->web_contents())->
          GetFrameTree()->root();
  ASSERT_EQ(2U, root->child_count());
  FrameTreeNode* child = root->child_at(0);
  EXPECT_EQ(main_url, root->current_url());
  EXPECT_EQ("foo.com", child->current_url().host());
  EXPECT_EQ("/title2.html", child->current_url().path());

  EXPECT_TRUE(
      child->current_frame_host()->render_view_host()->IsRenderViewLive());
  EXPECT_TRUE(child->current_frame_host()->IsRenderFrameLive());

  // Crash the subframe process.
  RenderProcessHost* root_process = root->current_frame_host()->GetProcess();
  RenderProcessHost* child_process = child->current_frame_host()->GetProcess();
  {
    RenderProcessHostWatcher crash_observer(
        child_process,
        RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
    child_process->Shutdown(0, false);
    crash_observer.Wait();
  }

  // Ensure that the child frame still exists but has been cleared.
  EXPECT_EQ(2U, root->child_count());
  EXPECT_EQ(main_url, root->current_url());
  EXPECT_EQ(GURL(), child->current_url());

  EXPECT_FALSE(
      child->current_frame_host()->render_view_host()->IsRenderViewLive());
  EXPECT_FALSE(child->current_frame_host()->IsRenderFrameLive());
  EXPECT_FALSE(child->current_frame_host()->render_frame_created_);

  // Now crash the top-level page to clear the child frame.
  {
    RenderProcessHostWatcher crash_observer(
        root_process,
        RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
    root_process->Shutdown(0, false);
    crash_observer.Wait();
  }
  EXPECT_EQ(0U, root->child_count());
  EXPECT_EQ(GURL(), root->current_url());
}

// TODO(nasko): Disable this test until out-of-process iframes is ready and the
// security checks are back in place.
// TODO(creis): Replace SpawnedTestServer with host_resolver to get test to run
// on Android (http://crbug.com/187570).
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       DISABLED_CrossSiteIframeRedirectOnce) {
  ASSERT_TRUE(test_server()->Start());
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS,
      net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL("content/test/data")));
  ASSERT_TRUE(https_server.Start());

  GURL main_url(test_server()->GetURL("files/site_per_process_main.html"));
  GURL http_url(test_server()->GetURL("files/title1.html"));
  GURL https_url(https_server.GetURL("files/title1.html"));

  NavigateToURL(shell(), main_url);

  SitePerProcessWebContentsObserver observer(shell()->web_contents());
  {
    // Load cross-site client-redirect page into Iframe.
    // Should be blocked.
    GURL client_redirect_https_url(https_server.GetURL(
        "client-redirect?files/title1.html"));
    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    client_redirect_https_url));
    // DidFailProvisionalLoad when navigating to client_redirect_https_url.
    EXPECT_EQ(observer.navigation_url(), client_redirect_https_url);
    EXPECT_FALSE(observer.navigation_succeeded());
  }

  {
    // Load cross-site server-redirect page into Iframe,
    // which redirects to same-site page.
    GURL server_redirect_http_url(https_server.GetURL(
        "server-redirect?" + http_url.spec()));
    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    server_redirect_http_url));
    EXPECT_EQ(observer.navigation_url(), http_url);
    EXPECT_TRUE(observer.navigation_succeeded());
  }

  {
    // Load cross-site server-redirect page into Iframe,
    // which redirects to cross-site page.
    GURL server_redirect_http_url(https_server.GetURL(
        "server-redirect?files/title1.html"));
    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    server_redirect_http_url));
    // DidFailProvisionalLoad when navigating to https_url.
    EXPECT_EQ(observer.navigation_url(), https_url);
    EXPECT_FALSE(observer.navigation_succeeded());
  }

  {
    // Load same-site server-redirect page into Iframe,
    // which redirects to cross-site page.
    GURL server_redirect_http_url(test_server()->GetURL(
        "server-redirect?" + https_url.spec()));
    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    server_redirect_http_url));

    EXPECT_EQ(observer.navigation_url(), https_url);
    EXPECT_FALSE(observer.navigation_succeeded());
   }

  {
    // Load same-site client-redirect page into Iframe,
    // which redirects to cross-site page.
    GURL client_redirect_http_url(test_server()->GetURL(
        "client-redirect?" + https_url.spec()));

    RedirectNotificationObserver load_observer2(
        NOTIFICATION_LOAD_STOP,
        Source<NavigationController>(
            &shell()->web_contents()->GetController()));

    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    client_redirect_http_url));

    // Same-site Client-Redirect Page should be loaded successfully.
    EXPECT_EQ(observer.navigation_url(), client_redirect_http_url);
    EXPECT_TRUE(observer.navigation_succeeded());

    // Redirecting to Cross-site Page should be blocked.
    load_observer2.Wait();
    EXPECT_EQ(observer.navigation_url(), https_url);
    EXPECT_FALSE(observer.navigation_succeeded());
  }

  {
    // Load same-site server-redirect page into Iframe,
    // which redirects to same-site page.
    GURL server_redirect_http_url(test_server()->GetURL(
        "server-redirect?files/title1.html"));
    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    server_redirect_http_url));
    EXPECT_EQ(observer.navigation_url(), http_url);
    EXPECT_TRUE(observer.navigation_succeeded());
   }

  {
    // Load same-site client-redirect page into Iframe,
    // which redirects to same-site page.
    GURL client_redirect_http_url(test_server()->GetURL(
        "client-redirect?" + http_url.spec()));
    RedirectNotificationObserver load_observer2(
        NOTIFICATION_LOAD_STOP,
        Source<NavigationController>(
            &shell()->web_contents()->GetController()));

    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    client_redirect_http_url));

    // Same-site Client-Redirect Page should be loaded successfully.
    EXPECT_EQ(observer.navigation_url(), client_redirect_http_url);
    EXPECT_TRUE(observer.navigation_succeeded());

    // Redirecting to Same-site Page should be loaded successfully.
    load_observer2.Wait();
    EXPECT_EQ(observer.navigation_url(), http_url);
    EXPECT_TRUE(observer.navigation_succeeded());
  }
}

// TODO(nasko): Disable this test until out-of-process iframes is ready and the
// security checks are back in place.
// TODO(creis): Replace SpawnedTestServer with host_resolver to get test to run
// on Android (http://crbug.com/187570).
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       DISABLED_CrossSiteIframeRedirectTwice) {
  ASSERT_TRUE(test_server()->Start());
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS,
      net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL("content/test/data")));
  ASSERT_TRUE(https_server.Start());

  GURL main_url(test_server()->GetURL("files/site_per_process_main.html"));
  GURL http_url(test_server()->GetURL("files/title1.html"));
  GURL https_url(https_server.GetURL("files/title1.html"));

  NavigateToURL(shell(), main_url);

  SitePerProcessWebContentsObserver observer(shell()->web_contents());
  {
    // Load client-redirect page pointing to a cross-site client-redirect page,
    // which eventually redirects back to same-site page.
    GURL client_redirect_https_url(https_server.GetURL(
        "client-redirect?" + http_url.spec()));
    GURL client_redirect_http_url(test_server()->GetURL(
        "client-redirect?" + client_redirect_https_url.spec()));

    // We should wait until second client redirect get cancelled.
    RedirectNotificationObserver load_observer2(
        NOTIFICATION_LOAD_STOP,
        Source<NavigationController>(
            &shell()->web_contents()->GetController()));

    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    client_redirect_http_url));

    // DidFailProvisionalLoad when navigating to client_redirect_https_url.
    load_observer2.Wait();
    EXPECT_EQ(observer.navigation_url(), client_redirect_https_url);
    EXPECT_FALSE(observer.navigation_succeeded());
  }

  {
    // Load server-redirect page pointing to a cross-site server-redirect page,
    // which eventually redirect back to same-site page.
    GURL server_redirect_https_url(https_server.GetURL(
        "server-redirect?" + http_url.spec()));
    GURL server_redirect_http_url(test_server()->GetURL(
        "server-redirect?" + server_redirect_https_url.spec()));
    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    server_redirect_http_url));
    EXPECT_EQ(observer.navigation_url(), http_url);
    EXPECT_TRUE(observer.navigation_succeeded());
  }

  {
    // Load server-redirect page pointing to a cross-site server-redirect page,
    // which eventually redirects back to cross-site page.
    GURL server_redirect_https_url(https_server.GetURL(
        "server-redirect?" + https_url.spec()));
    GURL server_redirect_http_url(test_server()->GetURL(
        "server-redirect?" + server_redirect_https_url.spec()));
    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    server_redirect_http_url));

    // DidFailProvisionalLoad when navigating to https_url.
    EXPECT_EQ(observer.navigation_url(), https_url);
    EXPECT_FALSE(observer.navigation_succeeded());
  }

  {
    // Load server-redirect page pointing to a cross-site client-redirect page,
    // which eventually redirects back to same-site page.
    GURL client_redirect_http_url(https_server.GetURL(
        "client-redirect?" + http_url.spec()));
    GURL server_redirect_http_url(test_server()->GetURL(
        "server-redirect?" + client_redirect_http_url.spec()));
    EXPECT_TRUE(NavigateIframeToURL(shell()->web_contents(), "test",
                                    server_redirect_http_url));

    // DidFailProvisionalLoad when navigating to client_redirect_http_url.
    EXPECT_EQ(observer.navigation_url(), client_redirect_http_url);
    EXPECT_FALSE(observer.navigation_succeeded());
  }
}

// Ensure that when navigating a frame cross-process RenderFrameProxyHosts are
// created in the FrameTree skipping the subtree of the navigating frame.
//
// Disabled on Mac due to flakiness on ASAN. http://crbug.com/425248
// Disabled on Windows due to flakiness on Win 7 bot. http://crbug.com/444563
#if defined(OS_MACOSX) || defined(OS_WIN)
#define MAYBE_ProxyCreationSkipsSubtree DISABLED_ProxyCreationSkipsSubtree
#else
#define MAYBE_ProxyCreationSkipsSubtree ProxyCreationSkipsSubtree
#endif
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       MAYBE_ProxyCreationSkipsSubtree) {
  GURL main_url(embedded_test_server()->GetURL("/site_per_process_main.html"));
  NavigateToURL(shell(), main_url);

  // It is safe to obtain the root frame tree node here, as it doesn't change.
  FrameTreeNode* root =
      static_cast<WebContentsImpl*>(shell()->web_contents())->
          GetFrameTree()->root();

  EXPECT_TRUE(root->child_at(1) != NULL);
  EXPECT_EQ(2U, root->child_at(1)->child_count());

  {
    // Load same-site page into iframe.
    SitePerProcessWebContentsObserver observer(shell()->web_contents());
    GURL http_url(embedded_test_server()->GetURL("/title1.html"));
    NavigateFrameToURL(root->child_at(0), http_url);
    EXPECT_EQ(http_url, observer.navigation_url());
    EXPECT_TRUE(observer.navigation_succeeded());
    RenderFrameProxyHost* proxy_to_parent =
        root->child_at(0)->render_manager()->GetRenderFrameProxyHost(
            shell()->web_contents()->GetSiteInstance());
    EXPECT_FALSE(proxy_to_parent);
  }

  // Create the cross-site URL to navigate to.
  GURL cross_site_url =
      embedded_test_server()->GetURL("foo.com", "/frame_tree/1-1.html");

  // Load cross-site page into the second iframe without waiting for the
  // navigation to complete. Once LoadURLWithParams returns, we would expect
  // proxies to have been created in the frame tree, but children of the
  // navigating frame to still be present. The reason is that we don't run the
  // message loop, so no IPCs that alter the frame tree can be processed.
  FrameTreeNode* child = root->child_at(1);
  SiteInstance* site = NULL;
  {
    SitePerProcessWebContentsObserver observer(shell()->web_contents());
    TestFrameNavigationObserver navigation_observer(child);
    NavigationController::LoadURLParams params(cross_site_url);
    params.transition_type = PageTransitionFromInt(ui::PAGE_TRANSITION_LINK);
    params.frame_tree_node_id = child->frame_tree_node_id();
    child->navigator()->GetController()->LoadURLWithParams(params);
    EXPECT_TRUE(child->render_manager()->pending_frame_host());

    site = child->render_manager()->pending_frame_host()->GetSiteInstance();
    EXPECT_NE(shell()->web_contents()->GetSiteInstance(), site);

    EXPECT_TRUE(root->render_manager()->GetRenderFrameProxyHost(site));
    EXPECT_TRUE(
        root->child_at(0)->render_manager()->GetRenderFrameProxyHost(site));
    EXPECT_FALSE(child->render_manager()->GetRenderFrameProxyHost(site));
    for (size_t i = 0; i < child->child_count(); ++i) {
      EXPECT_FALSE(
          child->child_at(i)->render_manager()->GetRenderFrameProxyHost(site));
    }
    // Now that the verification is done, run the message loop and wait for the
    // navigation to complete.
    navigation_observer.Wait();
    EXPECT_FALSE(child->render_manager()->pending_frame_host());
    EXPECT_TRUE(observer.navigation_succeeded());
    EXPECT_EQ(cross_site_url, observer.navigation_url());
  }

  // Load another cross-site page into the same iframe.
  cross_site_url = embedded_test_server()->GetURL("bar.com", "/title2.html");
  {
    // Perform the same checks as the first cross-site navigation, since
    // there have been issues in subsequent cross-site navigations. Also ensure
    // that the SiteInstance has properly changed.
    // TODO(nasko): Once we have proper cleanup of resources, add code to
    // verify that the intermediate SiteInstance/RenderFrameHost have been
    // properly cleaned up.
    SitePerProcessWebContentsObserver observer(shell()->web_contents());
    TestFrameNavigationObserver navigation_observer(child);
    NavigationController::LoadURLParams params(cross_site_url);
    params.transition_type = PageTransitionFromInt(ui::PAGE_TRANSITION_LINK);
    params.frame_tree_node_id = child->frame_tree_node_id();
    child->navigator()->GetController()->LoadURLWithParams(params);
    EXPECT_TRUE(child->render_manager()->pending_frame_host() != NULL);

    SiteInstance* site2 =
        child->render_manager()->pending_frame_host()->GetSiteInstance();
    EXPECT_NE(shell()->web_contents()->GetSiteInstance(), site2);
    EXPECT_NE(site, site2);

    EXPECT_TRUE(root->render_manager()->GetRenderFrameProxyHost(site2));
    EXPECT_TRUE(
        root->child_at(0)->render_manager()->GetRenderFrameProxyHost(site2));
    EXPECT_FALSE(child->render_manager()->GetRenderFrameProxyHost(site2));
    for (size_t i = 0; i < child->child_count(); ++i) {
      EXPECT_FALSE(
          child->child_at(i)->render_manager()->GetRenderFrameProxyHost(site2));
    }

    navigation_observer.Wait();
    EXPECT_TRUE(observer.navigation_succeeded());
    EXPECT_EQ(cross_site_url, observer.navigation_url());
    EXPECT_EQ(0U, child->child_count());
  }
}

// Verify that origin replication works for an A-embed-B-embed-C hierarchy.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, OriginReplication) {
  GURL main_url(embedded_test_server()->GetURL("/site_per_process_main.html"));
  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  // It is safe to obtain the root frame tree node here, as it doesn't change.
  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  SitePerProcessWebContentsObserver observer(shell()->web_contents());

  // Navigate the first subframe to a cross-site page with two subframes.
  // NavigateFrameToURL can't be used here because it doesn't guarantee that
  // FrameTreeNodes will have been created for child frames when it returns.
  RenderFrameHostCreatedObserver frame_observer(shell()->web_contents(), 4);
  GURL foo_url(
      embedded_test_server()->GetURL("foo.com", "/frame_tree/1-1.html"));
  NavigationController::LoadURLParams params(foo_url);
  params.transition_type = ui::PAGE_TRANSITION_LINK;
  params.frame_tree_node_id = root->child_at(0)->frame_tree_node_id();
  root->child_at(0)->navigator()->GetController()->LoadURLWithParams(params);
  frame_observer.Wait();

  // We can't use a SitePerProcessWebContentsObserver to verify the URL here,
  // since the frame has children that may have clobbered it in the observer.
  EXPECT_EQ(foo_url, root->child_at(0)->current_url());

  // Ensure that a new process is created for the subframe.
  EXPECT_NE(shell()->web_contents()->GetSiteInstance(),
            root->child_at(0)->current_frame_host()->GetSiteInstance());

  // Load cross-site page into subframe's subframe.
  ASSERT_EQ(2U, root->child_at(0)->child_count());
  GURL bar_url(embedded_test_server()->GetURL("bar.com", "/title1.html"));
  NavigateFrameToURL(root->child_at(0)->child_at(0), bar_url);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(bar_url, observer.navigation_url());

  // Check that a new process is created and is different from the top one and
  // the middle one.
  FrameTreeNode* bottom_child = root->child_at(0)->child_at(0);
  EXPECT_NE(shell()->web_contents()->GetSiteInstance(),
            bottom_child->current_frame_host()->GetSiteInstance());
  EXPECT_NE(root->child_at(0)->current_frame_host()->GetSiteInstance(),
            bottom_child->current_frame_host()->GetSiteInstance());

  // Check that foo.com frame's location.ancestorOrigins contains the correct
  // origin for the parent.  The origin should have been replicated as part of
  // the ViewMsg_New message that created the parent's RenderFrameProxy in
  // foo.com's process.
  int ancestor_origins_length = 0;
  EXPECT_TRUE(ExecuteScriptAndExtractInt(
      root->child_at(0)->current_frame_host(),
      "window.domAutomationController.send(location.ancestorOrigins.length);",
      &ancestor_origins_length));
  EXPECT_EQ(1, ancestor_origins_length);
  std::string result;
  EXPECT_TRUE(ExecuteScriptAndExtractString(
      root->child_at(0)->current_frame_host(),
      "window.domAutomationController.send(location.ancestorOrigins[0]);",
      &result));
  EXPECT_EQ(result + "/", main_url.GetOrigin().spec());

  // Check that bar.com frame's location.ancestorOrigins contains the correct
  // origin for its two ancestors. The topmost parent origin should be
  // replicated as part of ViewMsg_New, and the middle frame (foo.com's) origin
  // should be replicated as part of FrameMsg_NewFrameProxy sent for foo.com's
  // frame in bar.com's process.
  EXPECT_TRUE(ExecuteScriptAndExtractInt(
      bottom_child->current_frame_host(),
      "window.domAutomationController.send(location.ancestorOrigins.length);",
      &ancestor_origins_length));
  EXPECT_EQ(2, ancestor_origins_length);
  EXPECT_TRUE(ExecuteScriptAndExtractString(
      bottom_child->current_frame_host(),
      "window.domAutomationController.send(location.ancestorOrigins[0]);",
      &result));
  EXPECT_EQ(result + "/", foo_url.GetOrigin().spec());
  EXPECT_TRUE(ExecuteScriptAndExtractString(
      bottom_child->current_frame_host(),
      "window.domAutomationController.send(location.ancestorOrigins[1]);",
      &result));
  EXPECT_EQ(result + "/", main_url.GetOrigin().spec());
}

// Check that iframe sandbox flags are replicated correctly.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, SandboxFlagsReplication) {
  GURL main_url(embedded_test_server()->GetURL("/sandboxed_frames.html"));
  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  // It is safe to obtain the root frame tree node here, as it doesn't change.
  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  SitePerProcessWebContentsObserver observer(shell()->web_contents());

  // Navigate the second (sandboxed) subframe to a cross-site page with a
  // subframe. Use RenderFrameHostCreatedObserver to guarantee that all
  // FrameTreeNodes are created for child frames.
  RenderFrameHostCreatedObserver frame_observer(shell()->web_contents(), 4);
  GURL foo_url(
      embedded_test_server()->GetURL("foo.com", "/frame_tree/1-1.html"));
  NavigateFrameToURL(root->child_at(1), foo_url);
  frame_observer.Wait();

  // We can't use a SitePerProcessWebContentsObserver to verify the URL here,
  // since the frame has children that may have clobbered it in the observer.
  EXPECT_EQ(foo_url, root->child_at(1)->current_url());

  // Load cross-site page into subframe's subframe.
  ASSERT_EQ(2U, root->child_at(1)->child_count());
  GURL bar_url(embedded_test_server()->GetURL("bar.com", "/title1.html"));
  NavigateFrameToURL(root->child_at(1)->child_at(0), bar_url);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(bar_url, observer.navigation_url());

  // Opening a popup in the sandboxed foo.com iframe should fail.
  bool success = false;
  EXPECT_TRUE(
      ExecuteScriptAndExtractBool(root->child_at(1)->current_frame_host(),
                                  "window.domAutomationController.send("
                                  "!window.open('data:text/html,dataurl'));",
                                  &success));
  EXPECT_TRUE(success);
  EXPECT_EQ(Shell::windows().size(), 1u);

  // Opening a popup in a frame whose parent is sandboxed should also fail.
  // Here, bar.com frame's sandboxed parent frame is a remote frame in
  // bar.com's process.
  success = false;
  EXPECT_TRUE(ExecuteScriptAndExtractBool(
      root->child_at(1)->child_at(0)->current_frame_host(),
      "window.domAutomationController.send("
      "!window.open('data:text/html,dataurl'));",
      &success));
  EXPECT_TRUE(success);
  EXPECT_EQ(Shell::windows().size(), 1u);

  // Same, but now try the case where bar.com frame's sandboxed parent is a
  // local frame in bar.com's process.
  success = false;
  EXPECT_TRUE(ExecuteScriptAndExtractBool(
      root->child_at(2)->child_at(0)->current_frame_host(),
      "window.domAutomationController.send("
      "!window.open('data:text/html,dataurl'));",
      &success));
  EXPECT_TRUE(success);
  EXPECT_EQ(Shell::windows().size(), 1u);

  // Check that foo.com frame's location.ancestorOrigins contains the correct
  // origin for the parent, which should be unaffected by sandboxing.
  int ancestor_origins_length = 0;
  EXPECT_TRUE(ExecuteScriptAndExtractInt(
      root->child_at(1)->current_frame_host(),
      "window.domAutomationController.send(location.ancestorOrigins.length);",
      &ancestor_origins_length));
  EXPECT_EQ(1, ancestor_origins_length);
  std::string result;
  EXPECT_TRUE(ExecuteScriptAndExtractString(
      root->child_at(1)->current_frame_host(),
      "window.domAutomationController.send(location.ancestorOrigins[0]);",
      &result));
  EXPECT_EQ(result + "/", main_url.GetOrigin().spec());

  // Now check location.ancestorOrigins for the bar.com frame. The middle frame
  // (foo.com's) origin should be unique, since that frame is sandboxed, and
  // the top frame should match |main_url|.
  FrameTreeNode* bottom_child = root->child_at(1)->child_at(0);
  EXPECT_TRUE(ExecuteScriptAndExtractInt(
      bottom_child->current_frame_host(),
      "window.domAutomationController.send(location.ancestorOrigins.length);",
      &ancestor_origins_length));
  EXPECT_EQ(2, ancestor_origins_length);
  EXPECT_TRUE(ExecuteScriptAndExtractString(
      bottom_child->current_frame_host(),
      "window.domAutomationController.send(location.ancestorOrigins[0]);",
      &result));
  EXPECT_EQ(result, "null");
  EXPECT_TRUE(ExecuteScriptAndExtractString(
      bottom_child->current_frame_host(),
      "window.domAutomationController.send(location.ancestorOrigins[1]);",
      &result));
  EXPECT_EQ(result + "/", main_url.GetOrigin().spec());
}

// Verify that a child frame can retrieve the name property set by its parent.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, WindowNameReplication) {
  GURL main_url(embedded_test_server()->GetURL("/frame_tree/2-4.html"));
  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  // It is safe to obtain the root frame tree node here, as it doesn't change.
  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  SitePerProcessWebContentsObserver observer(shell()->web_contents());

  // Load cross-site page into iframe.
  GURL frame_url =
      embedded_test_server()->GetURL("foo.com", "/frame_tree/3-1.html");
  NavigateFrameToURL(root->child_at(0), frame_url);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(frame_url, observer.navigation_url());

  // Ensure that a new process is created for the subframe.
  EXPECT_NE(shell()->web_contents()->GetSiteInstance(),
            root->child_at(0)->current_frame_host()->GetSiteInstance());

  // Check that the window.name seen by the frame matches the name attribute
  // specified by its parent in the iframe tag.
  std::string result;
  EXPECT_TRUE(ExecuteScriptAndExtractString(
      root->child_at(0)->current_frame_host(),
      "window.domAutomationController.send(window.name);", &result));
  EXPECT_EQ(result, "3-1-name");
}

// TODO(lfg): Merge the test below with NavigateRemoteFrame test.
// TODO(lfg): Disabled because this triggers http://crbug.com/433012, and since
// the renderer process crashes, it causes the title watcher to never return.
// Alternatively, this could also be fixed if we could use NavigateIframeToURL
// and classified the navigation as MANUAL_SUBFRAME (http://crbug.com/441863) or
// if we waited for DidStopLoading (currently broken -- see comment in
// NavigateIframeToURL).
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       DISABLED_NavigateRemoteToDataURL) {
  GURL main_url(embedded_test_server()->GetURL("/site_per_process_main.html"));
  NavigateToURL(shell(), main_url);

  // It is safe to obtain the root frame tree node here, as it doesn't change.
  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  SitePerProcessWebContentsObserver observer(shell()->web_contents());

  // Load cross-site page into iframe.
  GURL url = embedded_test_server()->GetURL("foo.com", "/title1.html");
  NavigateFrameToURL(root->child_at(0), url);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(url, observer.navigation_url());

  // Ensure that we have created a new process for the subframe.
  EXPECT_NE(shell()->web_contents()->GetSiteInstance(),
            root->child_at(0)->current_frame_host()->GetSiteInstance());

  // Navigate iframe to a data URL. The navigation happens from a script in the
  // parent frame, so the data URL should be committed in the same SiteInstance
  // as the parent frame.
  GURL data_url("data:text/html,dataurl");
  std::string script = base::StringPrintf(
      "setTimeout(function() {"
      "var iframe = document.getElementById('test');"
      "iframe.onload = function() { document.title = 'LOADED'; };"
      "iframe.src=\"%s\";"
      "},0);",
      data_url.spec().c_str());
  base::string16 passed_string(base::UTF8ToUTF16("LOADED"));
  TitleWatcher title_watcher(shell()->web_contents(), passed_string);
  EXPECT_TRUE(ExecuteScript(shell()->web_contents(), script));
  EXPECT_EQ(title_watcher.WaitAndGetTitle(), passed_string);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(data_url, observer.navigation_url());

  // Ensure that we have navigated using the top level process.
  EXPECT_EQ(shell()->web_contents()->GetSiteInstance(),
            root->child_at(0)->current_frame_host()->GetSiteInstance());
}

// TODO(lfg): Merge the test below with NavigateRemoteFrame test.
// Disabled due to the same reason as NavigateRemoteToDataURL.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       DISABLED_NavigateRemoteToBlankURL) {
  GURL main_url(embedded_test_server()->GetURL("/site_per_process_main.html"));
  NavigateToURL(shell(), main_url);

  // It is safe to obtain the root frame tree node here, as it doesn't change.
  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  SitePerProcessWebContentsObserver observer(shell()->web_contents());

  // Load cross-site page into iframe.
  GURL url = embedded_test_server()->GetURL("foo.com", "/title1.html");
  NavigateFrameToURL(root->child_at(0), url);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(url, observer.navigation_url());

  // Ensure that we have created a new process for the subframe.
  EXPECT_NE(shell()->web_contents()->GetSiteInstance(),
            root->child_at(0)->current_frame_host()->GetSiteInstance());

  // Navigate iframe to about:blank. The navigation happens from a script in the
  // parent frame, so it should be committed in the same SiteInstance as the
  // parent frame.
  GURL about_blank_url("about:blank");
  std::string script = base::StringPrintf(
      "setTimeout(function() {"
      "var iframe = document.getElementById('test');"
      "iframe.onload = function() { document.title = 'LOADED'; };"
      "iframe.src=\"%s\";"
      "},0);",
      about_blank_url.spec().c_str());
  base::string16 passed_string(base::UTF8ToUTF16("LOADED"));
  TitleWatcher title_watcher(shell()->web_contents(), passed_string);
  EXPECT_TRUE(ExecuteScript(shell()->web_contents(), script));
  EXPECT_EQ(title_watcher.WaitAndGetTitle(), passed_string);
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(about_blank_url, observer.navigation_url());

  // Ensure that we have navigated using the top level process.
  EXPECT_EQ(shell()->web_contents()->GetSiteInstance(),
            root->child_at(0)->current_frame_host()->GetSiteInstance());
}

// Ensure that navigating subframes in --site-per-process mode properly fires
// the DidStopLoading event on WebContentsObserver.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, CrossSiteDidStopLoading) {
  GURL main_url(embedded_test_server()->GetURL("/site_per_process_main.html"));
  NavigateToURL(shell(), main_url);

  // It is safe to obtain the root frame tree node here, as it doesn't change.
  FrameTreeNode* root =
      static_cast<WebContentsImpl*>(shell()->web_contents())->
          GetFrameTree()->root();

  SitePerProcessWebContentsObserver observer(shell()->web_contents());

  // Load same-site page into iframe.
  FrameTreeNode* child = root->child_at(0);
  GURL http_url(embedded_test_server()->GetURL("/title1.html"));
  NavigateFrameToURL(child, http_url);
  EXPECT_EQ(http_url, observer.navigation_url());
  EXPECT_TRUE(observer.navigation_succeeded());

  // Load cross-site page into iframe.
  TestNavigationObserver nav_observer(shell()->web_contents(), 1);
  GURL url = embedded_test_server()->GetURL("foo.com", "/title2.html");
  NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_LINK;
  params.frame_tree_node_id = child->frame_tree_node_id();
  child->navigator()->GetController()->LoadURLWithParams(params);
  nav_observer.Wait();

  // Verify that the navigation succeeded and the expected URL was loaded.
  EXPECT_TRUE(observer.navigation_succeeded());
  EXPECT_EQ(url, observer.navigation_url());
}

}  // namespace content
