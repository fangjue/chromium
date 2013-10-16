// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_FETCHER_H_
#define NET_URL_REQUEST_URL_FETCHER_H_

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/supports_user_data.h"
#include "base/task_runner.h"
#include "net/base/net_export.h"

class GURL;

namespace base {
class FilePath;
class MessageLoopProxy;
class TimeDelta;
}

namespace net {
class HostPortPair;
class HttpRequestHeaders;
class HttpResponseHeaders;
class URLFetcherDelegate;
class URLFetcherResponseWriter;
class URLRequestContextGetter;
class URLRequestStatus;
typedef std::vector<std::string> ResponseCookies;

// To use this class, create an instance with the desired URL and a pointer to
// the object to be notified when the URL has been loaded:
//   URLFetcher* fetcher = URLFetcher::Create("http://www.google.com",
//                                            URLFetcher::GET, this);
//
// You must also set a request context getter:
//
//   fetcher->SetRequestContext(&my_request_context_getter);
//
// Then, optionally set properties on this object, like the request context or
// extra headers:
//   fetcher->set_extra_request_headers("X-Foo: bar");
//
// Finally, start the request:
//   fetcher->Start();
//
//
// The object you supply as a delegate must inherit from
// URLFetcherDelegate; when the fetch is completed,
// OnURLFetchComplete() will be called with a pointer to the URLFetcher.  From
// that point until the original URLFetcher instance is destroyed, you may use
// accessor methods to see the result of the fetch. You should copy these
// objects if you need them to live longer than the URLFetcher instance. If the
// URLFetcher instance is destroyed before the callback happens, the fetch will
// be canceled and no callback will occur.
//
// You may create the URLFetcher instance on any thread; OnURLFetchComplete()
// will be called back on the same thread you use to create the instance.
//
//
// NOTE: By default URLFetcher requests are NOT intercepted, except when
// interception is explicitly enabled in tests.
class NET_EXPORT URLFetcher {
 public:
  // Imposible http response code. Used to signal that no http response code
  // was received.
  enum ResponseCode {
    RESPONSE_CODE_INVALID = -1
  };

  enum RequestType {
    GET,
    POST,
    HEAD,
    DELETE_REQUEST,   // DELETE is already taken on Windows.
                      // <winnt.h> defines a DELETE macro.
    PUT,
    PATCH,
  };

  // Used by SetURLRequestUserData.  The callback should make a fresh
  // base::SupportsUserData::Data object every time it's called.
  typedef base::Callback<base::SupportsUserData::Data*()> CreateDataCallback;

  virtual ~URLFetcher();

  // |url| is the URL to send the request to.
  // |request_type| is the type of request to make.
  // |d| the object that will receive the callback on fetch completion.
  static URLFetcher* Create(const GURL& url,
                            URLFetcher::RequestType request_type,
                            URLFetcherDelegate* d);

  // Like above, but if there's a URLFetcherFactory registered with the
  // implementation it will be used. |id| may be used during testing to identify
  // who is creating the URLFetcher.
  static URLFetcher* Create(int id,
                            const GURL& url,
                            URLFetcher::RequestType request_type,
                            URLFetcherDelegate* d);

  // Cancels all existing URLFetchers.  Will notify the URLFetcherDelegates.
  // Note that any new URLFetchers created while this is running will not be
  // cancelled.  Typically, one would call this in the CleanUp() method of an IO
  // thread, so that no new URLRequests would be able to start on the IO thread
  // anyway.  This doesn't prevent new URLFetchers from trying to post to the IO
  // thread though, even though the task won't ever run.
  static void CancelAll();

  // Normally interception is disabled for URLFetcher, but you can use this
  // to enable it for tests. Also see ScopedURLFetcherFactory for another way
  // of testing code that uses an URLFetcher.
  static void SetEnableInterceptionForTests(bool enabled);

  // Normally, URLFetcher will abort loads that request SSL client certificate
  // authentication, but this method may be used to cause URLFetchers to ignore
  // requests for client certificates and continue anonymously. Because such
  // behaviour affects the URLRequestContext's shared network state and socket
  // pools, it should only be used for testing.
  static void SetIgnoreCertificateRequests(bool ignored);

  // Sets data only needed by POSTs.  All callers making POST requests should
  // call one of the SetUpload* methods before the request is started.
  // |upload_content_type| is the MIME type of the content, while
  // |upload_content| is the data to be sent (the Content-Length header value
  // will be set to the length of this data).
  virtual void SetUploadData(const std::string& upload_content_type,
                             const std::string& upload_content) = 0;

  // Sets data only needed by POSTs.  All callers making POST requests should
  // call one of the SetUpload* methods before the request is started.
  // |upload_content_type| is the MIME type of the content, while
  // |file_path| is the path to the file containing the data to be sent (the
  // Content-Length header value will be set to the length of this file).
  // |range_offset| and |range_length| specify the range of the part
  // to be uploaded. To upload the whole file, (0, kuint64max) can be used.
  // |file_task_runner| will be used for all file operations.
  virtual void SetUploadFilePath(
      const std::string& upload_content_type,
      const base::FilePath& file_path,
      uint64 range_offset,
      uint64 range_length,
      scoped_refptr<base::TaskRunner> file_task_runner) = 0;

  // Indicates that the POST data is sent via chunked transfer encoding.
  // This may only be called before calling Start().
  // Use AppendChunkToUpload() to give the data chunks after calling Start().
  virtual void SetChunkedUpload(const std::string& upload_content_type) = 0;

  // Adds the given bytes to a request's POST data transmitted using chunked
  // transfer encoding.
  // This method should be called ONLY after calling Start().
  virtual void AppendChunkToUpload(const std::string& data,
                                   bool is_last_chunk) = 0;

  // Set one or more load flags as defined in net/base/load_flags.h.  Must be
  // called before the request is started.
  virtual void SetLoadFlags(int load_flags) = 0;

  // Returns the current load flags.
  virtual int GetLoadFlags() const = 0;

  // The referrer URL for the request. Must be called before the request is
  // started.
  virtual void SetReferrer(const std::string& referrer) = 0;

  // Set extra headers on the request.  Must be called before the request
  // is started.
  // This replaces the entire extra request headers.
  virtual void SetExtraRequestHeaders(
      const std::string& extra_request_headers) = 0;

  // Add header (with format field-name ":" [ field-value ]) to the request
  // headers.  Must be called before the request is started.
  // This appends the header to the current extra request headers.
  virtual void AddExtraRequestHeader(const std::string& header_line) = 0;

  virtual void GetExtraRequestHeaders(
      HttpRequestHeaders* headers) const = 0;

  // Set the URLRequestContext on the request.  Must be called before the
  // request is started.
  virtual void SetRequestContext(
      URLRequestContextGetter* request_context_getter) = 0;

  // Set the URL that should be consulted for the third-party cookie
  // blocking policy.
  virtual void SetFirstPartyForCookies(
      const GURL& first_party_for_cookies) = 0;

  // Set the key and data callback that is used when setting the user
  // data on any URLRequest objects this object creates.
  virtual void SetURLRequestUserData(
      const void* key,
      const CreateDataCallback& create_data_callback) = 0;

  // If |stop_on_redirect| is true, 3xx responses will cause the fetch to halt
  // immediately rather than continue through the redirect.  OnURLFetchComplete
  // will be called, with the URLFetcher's URL set to the redirect destination,
  // its status set to CANCELED, and its response code set to the relevant 3xx
  // server response code.
  virtual void SetStopOnRedirect(bool stop_on_redirect) = 0;

  // If |retry| is false, 5xx responses will be propagated to the observer,
  // if it is true URLFetcher will automatically re-execute the request,
  // after backoff_delay() elapses. URLFetcher has it set to true by default.
  virtual void SetAutomaticallyRetryOn5xx(bool retry) = 0;

  virtual void SetMaxRetriesOn5xx(int max_retries) = 0;
  virtual int GetMaxRetriesOn5xx() const = 0;

  // Returns the back-off delay before the request will be retried,
  // when a 5xx response was received.
  virtual base::TimeDelta GetBackoffDelay() const = 0;

  // Retries up to |max_retries| times when requests fail with
  // ERR_NETWORK_CHANGED. If ERR_NETWORK_CHANGED is received after having
  // retried |max_retries| times then it is propagated to the observer.
  virtual void SetAutomaticallyRetryOnNetworkChanges(int max_retries) = 0;

  // By default, the response is saved in a string. Call this method to save the
  // response to a file instead. Must be called before Start().
  // |file_task_runner| will be used for all file operations.
  // To save to a temporary file, use SaveResponseToTemporaryFile().
  // The created file is removed when the URLFetcher is deleted unless you
  // take ownership by calling GetResponseAsFilePath().
  virtual void SaveResponseToFileAtPath(
      const base::FilePath& file_path,
      scoped_refptr<base::TaskRunner> file_task_runner) = 0;

  // By default, the response is saved in a string. Call this method to save the
  // response to a temporary file instead. Must be called before Start().
  // |file_task_runner| will be used for all file operations.
  // The created file is removed when the URLFetcher is deleted unless you
  // take ownership by calling GetResponseAsFilePath().
  virtual void SaveResponseToTemporaryFile(
      scoped_refptr<base::TaskRunner> file_task_runner) = 0;

  // By default, the response is saved in a string. Call this method to use the
  // specified writer to save the response. Must be called before Start().
  virtual void SaveResponseWithWriter(
      scoped_ptr<URLFetcherResponseWriter> response_writer) = 0;

  // Retrieve the response headers from the request.  Must only be called after
  // the OnURLFetchComplete callback has run.
  virtual HttpResponseHeaders* GetResponseHeaders() const = 0;

  // Retrieve the remote socket address from the request.  Must only
  // be called after the OnURLFetchComplete callback has run and if
  // the request has not failed.
  virtual HostPortPair GetSocketAddress() const = 0;

  // Returns true if the request was delivered through a proxy.  Must only
  // be called after the OnURLFetchComplete callback has run and the request
  // has not failed.
  virtual bool WasFetchedViaProxy() const = 0;

  // Start the request.  After this is called, you may not change any other
  // settings.
  virtual void Start() = 0;

  // Return the URL that we were asked to fetch.
  virtual const GURL& GetOriginalURL() const = 0;

  // Return the URL that this fetcher is processing.
  virtual const GURL& GetURL() const = 0;

  // The status of the URL fetch.
  virtual const URLRequestStatus& GetStatus() const = 0;

  // The http response code received. Will return RESPONSE_CODE_INVALID
  // if an error prevented any response from being received.
  virtual int GetResponseCode() const = 0;

  // Cookies recieved.
  virtual const ResponseCookies& GetCookies() const = 0;

  // Reports that the received content was malformed.
  virtual void ReceivedContentWasMalformed() = 0;

  // Get the response as a string. Return false if the fetcher was not
  // set to store the response as a string.
  virtual bool GetResponseAsString(std::string* out_response_string) const = 0;

  // Get the path to the file containing the response body. Returns false
  // if the response body was not saved to a file. If take_ownership is
  // true, caller takes responsibility for the file, and it will not
  // be removed once the URLFetcher is destroyed.  User should not take
  // ownership more than once, or call this method after taking ownership.
  virtual bool GetResponseAsFilePath(
      bool take_ownership,
      base::FilePath* out_response_path) const = 0;
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_FETCHER_H_
