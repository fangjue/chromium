// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/spellchecker/spellcheck_message_filter_mac.h"

#include <algorithm>
#include <functional>

#include "base/barrier_closure.h"
#include "base/bind.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/browser/spellchecker/spellcheck_platform_mac.h"
#include "chrome/browser/spellchecker/spellcheck_service.h"
#include "chrome/browser/spellchecker/spelling_service_client.h"
#include "chrome/common/spellcheck_messages.h"
#include "chrome/common/spellcheck_result.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"

using content::BrowserThread;
using content::BrowserContext;

namespace {

bool CompareLocation(const SpellCheckResult& r1,
                     const SpellCheckResult& r2) {
  return r1.location < r2.location;
}

}  // namespace

class SpellingRequest {
 public:
  SpellingRequest(SpellingServiceClient* client,
                  content::BrowserMessageFilter* destination,
                  int render_process_id);

  void RequestCheck(const base::string16& text,
                    int route_id,
                    int identifier,
                    int document_tag,
                    const std::vector<SpellCheckMarker>& markers);
 private:
  // Request server-side checking.
  void RequestRemoteCheck(const base::string16& text);

  // Request a check from local spell checker.
  void RequestLocalCheck(const base::string16& text, int document_tag);

  // Check if all pending requests are done, send reply to render process if so.
  void OnCheckCompleted();

  // Called when server-side checking is complete.
  void OnRemoteCheckCompleted(bool success,
                              const base::string16& text,
                              const std::vector<SpellCheckResult>& results);

  // Called when local checking is complete.
  void OnLocalCheckCompleted(const std::vector<SpellCheckResult>& results);

  std::vector<SpellCheckResult> local_results_;
  std::vector<SpellCheckResult> remote_results_;

  // Barrier closure for completion of both remote and local check.
  base::Closure completion_barrier_;
  bool remote_success_;

  SpellingServiceClient* client_;  // Owned by |destination|.
  content::BrowserMessageFilter* destination_;  // ref-counted.
  int render_process_id_;

  int route_id_;
  int identifier_;
  int document_tag_;
  std::vector<SpellCheckMarker> markers_;
};

SpellingRequest::SpellingRequest(SpellingServiceClient* client,
                                 content::BrowserMessageFilter* destination,
                                 int render_process_id)
    : remote_success_(false),
      client_(client),
      destination_(destination),
      render_process_id_(render_process_id),
      route_id_(-1),
      identifier_(-1),
      document_tag_(-1) {
  destination_->AddRef();
}

void SpellingRequest::RequestCheck(
    const base::string16& text,
    int route_id,
    int identifier,
    int document_tag,
    const std::vector<SpellCheckMarker>& markers) {
  DCHECK(!text.empty());
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  route_id_ = route_id;
  identifier_ = identifier;
  document_tag_ = document_tag;
  markers_ = markers;

  // Send the remote query out. The barrier owns |this|, ensuring it is deleted
  // after completion.
  completion_barrier_ =
      BarrierClosure(2,
                     base::Bind(&SpellingRequest::OnCheckCompleted,
                     base::Owned(this)));
  RequestRemoteCheck(text);
  RequestLocalCheck(text, document_tag_);
}

void SpellingRequest::RequestRemoteCheck(const base::string16& text) {
  BrowserContext* context = NULL;
  content::RenderProcessHost* host =
      content::RenderProcessHost::FromID(render_process_id_);
  if (host)
    context = host->GetBrowserContext();

  client_->RequestTextCheck(
    context,
    SpellingServiceClient::SPELLCHECK,
    text,
    base::Bind(&SpellingRequest::OnRemoteCheckCompleted,
               base::Unretained(this)));
}

void SpellingRequest::RequestLocalCheck(const base::string16& text,
                                        int document_tag) {
  spellcheck_mac::RequestTextCheck(
      document_tag,
      text,
      base::Bind(&SpellingRequest::OnLocalCheckCompleted,
                 base::Unretained(this)));
}

void SpellingRequest::OnCheckCompleted() {
  // Final completion can happen on any thread - don't DCHECK thread.
  const std::vector<SpellCheckResult>* check_results = &local_results_;
  if (remote_success_) {
    std::sort(remote_results_.begin(), remote_results_.end(), CompareLocation);
    std::sort(local_results_.begin(), local_results_.end(), CompareLocation);
    SpellCheckMessageFilterMac::CombineResults(&remote_results_,
                                               local_results_);
    check_results = &remote_results_;
  }

  destination_->Send(
      new SpellCheckMsg_RespondTextCheck(
          route_id_,
          identifier_,
          *check_results));
  destination_->Release();

  // Object is self-managed - at this point, its life span is over.
  // No need to delete, since the OnCheckCompleted callback owns |this|.
}

void SpellingRequest::OnRemoteCheckCompleted(
    bool success,
    const base::string16& text,
    const std::vector<SpellCheckResult>& results) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  remote_success_ = success;
  remote_results_ = results;
  completion_barrier_.Run();
}

void SpellingRequest::OnLocalCheckCompleted(
    const std::vector<SpellCheckResult>& results) {
  // Local checking can happen on any thread - don't DCHECK thread.
  local_results_ = results;
  completion_barrier_.Run();
}


SpellCheckMessageFilterMac::SpellCheckMessageFilterMac(int render_process_id)
    : BrowserMessageFilter(SpellCheckMsgStart),
      render_process_id_(render_process_id),
      client_(new SpellingServiceClient) {
}

void SpellCheckMessageFilterMac::OverrideThreadForMessage(
    const IPC::Message& message, BrowserThread::ID* thread) {
  if (message.type() == SpellCheckHostMsg_RequestTextCheck::ID)
    *thread = BrowserThread::UI;
}

bool SpellCheckMessageFilterMac::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(SpellCheckMessageFilterMac, message)
    IPC_MESSAGE_HANDLER(SpellCheckHostMsg_CheckSpelling,
                        OnCheckSpelling)
    IPC_MESSAGE_HANDLER(SpellCheckHostMsg_FillSuggestionList,
                        OnFillSuggestionList)
    IPC_MESSAGE_HANDLER(SpellCheckHostMsg_ShowSpellingPanel,
                        OnShowSpellingPanel)
    IPC_MESSAGE_HANDLER(SpellCheckHostMsg_UpdateSpellingPanelWithMisspelledWord,
                        OnUpdateSpellingPanelWithMisspelledWord)
    IPC_MESSAGE_HANDLER(SpellCheckHostMsg_RequestTextCheck,
                        OnRequestTextCheck)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

// static
void SpellCheckMessageFilterMac::CombineResults(
    std::vector<SpellCheckResult>* remote_results,
    const std::vector<SpellCheckResult>& local_results) {
  std::vector<SpellCheckResult>::const_iterator local_iter(
      local_results.begin());
  std::vector<SpellCheckResult>::iterator remote_iter;

  for (remote_iter = remote_results->begin();
       remote_iter != remote_results->end();
       ++remote_iter) {
    // Discard all local results occurring before remote result.
    while (local_iter != local_results.end() &&
           local_iter->location < remote_iter->location) {
      local_iter++;
    }

    // Unless local and remote result coincide, result is GRAMMAR.
    remote_iter->decoration = SpellCheckResult::GRAMMAR;
    if (local_iter != local_results.end() &&
        local_iter->location == remote_iter->location &&
        local_iter->length == remote_iter->length) {
      remote_iter->decoration = SpellCheckResult::SPELLING;
    }
  }
}

SpellCheckMessageFilterMac::~SpellCheckMessageFilterMac() {}

void SpellCheckMessageFilterMac::OnCheckSpelling(const base::string16& word,
                                                 int route_id,
                                                 bool* correct) {
  *correct = spellcheck_mac::CheckSpelling(word, ToDocumentTag(route_id));
}

void SpellCheckMessageFilterMac::OnFillSuggestionList(
    const base::string16& word,
    std::vector<base::string16>* suggestions) {
  spellcheck_mac::FillSuggestionList(word, suggestions);
}

void SpellCheckMessageFilterMac::OnShowSpellingPanel(bool show) {
  spellcheck_mac::ShowSpellingPanel(show);
}

void SpellCheckMessageFilterMac::OnUpdateSpellingPanelWithMisspelledWord(
    const base::string16& word) {
  spellcheck_mac::UpdateSpellingPanelWithMisspelledWord(word);
}

void SpellCheckMessageFilterMac::OnRequestTextCheck(
    int route_id,
    int identifier,
    const base::string16& text,
    std::vector<SpellCheckMarker> markers) {
  DCHECK(!text.empty());
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  // Erase invalid markers (with offsets out of boundaries of text length).
  markers.erase(
      std::remove_if(
          markers.begin(),
          markers.end(),
          std::not1(SpellCheckMarker::IsValidPredicate(text.length()))),
      markers.end());
  // SpellingRequest self-destructs.
  SpellingRequest* request =
    new SpellingRequest(client_.get(), this, render_process_id_);
  request->RequestCheck(
      text, route_id, identifier, ToDocumentTag(route_id), markers);
}

int SpellCheckMessageFilterMac::ToDocumentTag(int route_id) {
  if (!tag_map_.count(route_id))
    tag_map_[route_id] = spellcheck_mac::GetDocumentTag();
  return tag_map_[route_id];
}

// TODO(groby): We are currently not notified of retired tags. We need
// to track destruction of RenderViewHosts on the browser process side
// to update our mappings when a document goes away.
void SpellCheckMessageFilterMac::RetireDocumentTag(int route_id) {
  spellcheck_mac::CloseDocumentWithTag(ToDocumentTag(route_id));
  tag_map_.erase(route_id);
}

