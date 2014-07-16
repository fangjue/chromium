// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/service_worker_host_impl.h"

#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/browser/service_worker/service_worker_registration.h"
#include "content/browser/service_worker/service_worker_version.h"
#include "content/public/browser/browser_thread.h"
#include "ipc/ipc_message.h"

namespace {

using content::BrowserThread;
using content::SERVICE_WORKER_OK;
using content::ServiceWorkerContextWrapper;
using content::ServiceWorkerRegistration;
using content::ServiceWorkerStatusCode;
using content::ServiceWorkerVersion;

void OnRegistrationFoundSendMessage(
    IPC::Message* message,
    ServiceWorkerStatusCode status,
    const scoped_refptr<ServiceWorkerRegistration>& registration) {
  fprintf(stderr, "%s:%s:%d \n", __FILE__, __FUNCTION__, __LINE__);
  if (status != SERVICE_WORKER_OK) {  // || !registration->active_version()) {
    //
    //
    // TODO(scheib) Inform the Host client that we failed to send?
    //
    //
    fprintf(stderr,
            "%s:%s:%d returning early %d, ok:%d, active %d\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            status,
            status == SERVICE_WORKER_OK,
            !!registration->active_version());
    return;
  }
  CHECK(message);  // Check pointer before taking a reference to it.
  //
  //
  // TODO(scheib) Inform the Host client that we failed to send (via callback)?
  //
  //
  fprintf(stderr,
          "%s:%s:%d ok, calling registration->active_version()->SendMessage\n",
          __FILE__,
          __FUNCTION__,
          __LINE__);
  registration->active_version()->SendMessage(
      *message, ServiceWorkerVersion::StatusCallback());
}

void SendOnIO(scoped_refptr<ServiceWorkerContextWrapper> context_wrapper,
              const GURL scope,
              IPC::Message* message) {
  fprintf(stderr, "%s:%s:%d \n", __FILE__, __FUNCTION__, __LINE__);
  //
  //
  // TODO: Optimize by keeping a reference to Registration.
  //
  //
  context_wrapper->context()->storage()->FindRegistrationForPattern(
      scope, base::Bind(&OnRegistrationFoundSendMessage, message));
}

}  // namespace

namespace content {

ServiceWorkerHostImpl::ServiceWorkerHostImpl(
    const GURL& scope,
    scoped_refptr<ServiceWorkerContextWrapper> context_wrapper,
    const scoped_refptr<ServiceWorkerRegistration>& registration,
    ServiceWorkerHostClient* client)
    : scope_(scope),
      context_wrapper_(context_wrapper),
      ui_thread_(client),
      io_thread_(registration) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  //
  //
  //  TODO CONTINUE TO INSTALL
  //
  //
}

const GURL& ServiceWorkerHostImpl::scope() {
  return scope_;
}

const GURL& ServiceWorkerHostImpl::script() {
  return script_;
}

bool ServiceWorkerHostImpl::HasActiveVersion() {
  //
  //
  // TODO
  //
  //
  return false;
}

bool ServiceWorkerHostImpl::Send(IPC::Message* message) {
  fprintf(stderr, "%s:%s:%d \n", __FILE__, __FUNCTION__, __LINE__);
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(SendOnIO, context_wrapper_, scope_, message));
  return true;
}

void ServiceWorkerHostImpl::DisconnectServiceWorkerHostClient() {
  ui_thread_.client = NULL;
}

ServiceWorkerHostImpl::~ServiceWorkerHostImpl() {
  //
  //
  // TODO: Disconnect client from wherever we've registered it
  //
  //
}

// ServiceWorkerHostImpl::UIThreadMembers

ServiceWorkerHostImpl::UIThreadMembers::UIThreadMembers(
    ServiceWorkerHostClient* client)
    : client(client) {
}

ServiceWorkerHostImpl::UIThreadMembers::~UIThreadMembers() {
}

// ServiceWorkerHostImpl::IOThreadMembers

ServiceWorkerHostImpl::IOThreadMembers::IOThreadMembers(
    const scoped_refptr<ServiceWorkerRegistration>& registration)
    : registration(registration) {
}

ServiceWorkerHostImpl::IOThreadMembers::~IOThreadMembers() {
}

}  // namespace content
