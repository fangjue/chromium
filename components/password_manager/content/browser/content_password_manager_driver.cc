// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/content/browser/content_password_manager_driver.h"

#include "components/autofill/content/common/autofill_messages.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/password_form.h"
#include "components/password_manager/content/browser/content_password_manager_driver_factory.h"
#include "components/password_manager/core/browser/password_manager_client.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/ssl_status.h"
#include "ipc/ipc_message_macros.h"
#include "net/cert/cert_status_flags.h"

namespace password_manager {

ContentPasswordManagerDriver::ContentPasswordManagerDriver(
    content::RenderFrameHost* render_frame_host,
    PasswordManagerClient* client,
    autofill::AutofillClient* autofill_client)
    : render_frame_host_(render_frame_host),
      client_(client),
      password_generation_manager_(client, this),
      password_autofill_manager_(client, this, autofill_client),
      next_free_key_(0) {
}

ContentPasswordManagerDriver::~ContentPasswordManagerDriver() {
}

// static
ContentPasswordManagerDriver*
ContentPasswordManagerDriver::GetForRenderFrameHost(
    content::RenderFrameHost* render_frame_host) {
  ContentPasswordManagerDriverFactory* factory =
      ContentPasswordManagerDriverFactory::FromWebContents(
          content::WebContents::FromRenderFrameHost(render_frame_host));
  return factory ? factory->GetDriverForFrame(render_frame_host) : nullptr;
}

void ContentPasswordManagerDriver::FillPasswordForm(
    const autofill::PasswordFormFillData& form_data) {
  const int key = next_free_key_++;
  password_autofill_manager_.OnAddPasswordFormMapping(key, form_data);
  render_frame_host_->Send(new AutofillMsg_FillPasswordForm(
      render_frame_host_->GetRoutingID(), key, form_data));
}

void ContentPasswordManagerDriver::AllowPasswordGenerationForForm(
    const autofill::PasswordForm& form) {
  content::RenderFrameHost* host = render_frame_host_;
  host->Send(new AutofillMsg_FormNotBlacklisted(host->GetRoutingID(), form));
}

void ContentPasswordManagerDriver::AccountCreationFormsFound(
    const std::vector<autofill::FormData>& forms) {
  content::RenderFrameHost* host = render_frame_host_;
  host->Send(new AutofillMsg_AccountCreationFormsDetected(host->GetRoutingID(),
                                                          forms));
}

void ContentPasswordManagerDriver::GeneratedPasswordAccepted(
    const base::string16& password) {
  content::RenderFrameHost* host = render_frame_host_;
  host->Send(new AutofillMsg_GeneratedPasswordAccepted(host->GetRoutingID(),
                                                       password));
}

void ContentPasswordManagerDriver::FillSuggestion(
    const base::string16& username,
    const base::string16& password) {
  content::RenderFrameHost* host = render_frame_host_;
  host->Send(new AutofillMsg_FillPasswordSuggestion(host->GetRoutingID(),
                                                    username, password));
}

void ContentPasswordManagerDriver::PreviewSuggestion(
    const base::string16& username,
    const base::string16& password) {
  content::RenderFrameHost* host = render_frame_host_;
  host->Send(new AutofillMsg_PreviewPasswordSuggestion(host->GetRoutingID(),
                                                       username, password));
}

void ContentPasswordManagerDriver::ClearPreviewedForm() {
  content::RenderFrameHost* host = render_frame_host_;
  host->Send(new AutofillMsg_ClearPreviewedForm(host->GetRoutingID()));
}

PasswordGenerationManager*
ContentPasswordManagerDriver::GetPasswordGenerationManager() {
  return &password_generation_manager_;
}

PasswordManager* ContentPasswordManagerDriver::GetPasswordManager() {
  return client_->GetPasswordManager();
}

PasswordAutofillManager*
ContentPasswordManagerDriver::GetPasswordAutofillManager() {
  return &password_autofill_manager_;
}

bool ContentPasswordManagerDriver::HandleMessage(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ContentPasswordManagerDriver, message)
  IPC_MESSAGE_HANDLER(AutofillHostMsg_PasswordFormsParsed,
                      OnPasswordFormsParsed)
  IPC_MESSAGE_HANDLER(AutofillHostMsg_PasswordFormsRendered,
                      OnPasswordFormsRendered)
  IPC_MESSAGE_HANDLER(AutofillHostMsg_PasswordFormSubmitted,
                      OnPasswordFormSubmitted)
  IPC_MESSAGE_HANDLER(AutofillHostMsg_InPageNavigation, OnInPageNavigation)
  IPC_MESSAGE_FORWARD(AutofillHostMsg_ShowPasswordSuggestions,
                      &password_autofill_manager_,
                      PasswordAutofillManager::OnShowPasswordSuggestions)
  IPC_MESSAGE_FORWARD(AutofillHostMsg_RecordSavePasswordProgress, client_,
                      PasswordManagerClient::LogSavePasswordProgress)
  IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void ContentPasswordManagerDriver::OnPasswordFormsParsed(
    const std::vector<autofill::PasswordForm>& forms) {
  GetPasswordManager()->OnPasswordFormsParsed(this, forms);
}

void ContentPasswordManagerDriver::OnPasswordFormsRendered(
    const std::vector<autofill::PasswordForm>& visible_forms,
    bool did_stop_loading) {
  GetPasswordManager()->OnPasswordFormsRendered(this, visible_forms,
                                                did_stop_loading);
}

void ContentPasswordManagerDriver::OnPasswordFormSubmitted(
    const autofill::PasswordForm& password_form) {
  GetPasswordManager()->OnPasswordFormSubmitted(this, password_form);
}

void ContentPasswordManagerDriver::DidNavigateFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  // Clear page specific data after main frame navigation.
  if (!render_frame_host_->GetParent() && !details.is_in_page) {
    GetPasswordManager()->DidNavigateMainFrame();
    GetPasswordAutofillManager()->DidNavigateMainFrame();
  }
}

void ContentPasswordManagerDriver::OnInPageNavigation(
    const autofill::PasswordForm& password_form) {
  GetPasswordManager()->OnInPageNavigation(this, password_form);
}

}  // namespace password_manager
