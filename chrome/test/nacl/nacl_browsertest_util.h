// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_NACL_NACL_BROWSERTEST_UTIL_H_
#define CHROME_TEST_NACL_NACL_BROWSERTEST_UTIL_H_

#include "base/files/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/javascript_test_observer.h"

// A helper base class that decodes structured automation messages of the form:
// {"type": type_name, ...}
class StructuredMessageHandler : public content::TestMessageHandler {
 public:
  MessageResponse HandleMessage(const std::string& json) override;

  // This method provides a higher-level interface for handling JSON messages
  // from the DOM automation controler.  Instead of handling a string
  // containing a JSON-encoded object, this specialization of TestMessageHandler
  // decodes the string into a dictionary. This makes it easier to send and
  // receive structured messages.  It is assumed the dictionary will always have
  // a "type" field that indicates the nature of message.
  virtual MessageResponse HandleStructuredMessage(
      const std::string& type,
      base::DictionaryValue* msg) = 0;

 protected:
  // The structured message is missing an expected field.
  MessageResponse MissingField(
      const std::string& type,
      const std::string& field) WARN_UNUSED_RESULT;

  // Something went wrong while decoding the message.
  MessageResponse InternalError(const std::string& reason) WARN_UNUSED_RESULT;
};

// A simple structured message handler for tests that load nexes.
class LoadTestMessageHandler : public StructuredMessageHandler {
 public:
  LoadTestMessageHandler();

  void Log(const std::string& type, const std::string& message);

  MessageResponse HandleStructuredMessage(const std::string& type,
                                          base::DictionaryValue* msg) override;

  bool test_passed() const {
    return test_passed_;
  }

 private:
  bool test_passed_;

  DISALLOW_COPY_AND_ASSIGN(LoadTestMessageHandler);
};

class NaClBrowserTestBase : public InProcessBrowserTest {
 public:
  NaClBrowserTestBase();
  ~NaClBrowserTestBase() override;

  void SetUpCommandLine(base::CommandLine* command_line) override;

  void SetUpOnMainThread() override;

  // What variant are we running - newlib, glibc, pnacl, etc?
  // This is used to compute what directory we're pulling data from, but it can
  // also be used to affect the behavior of the test.
  virtual base::FilePath::StringType Variant() = 0;

  // Where are the files for this class of test located on disk?
  virtual bool GetDocumentRoot(base::FilePath* document_root);

  virtual bool IsAPnaclTest();

  // Map a file relative to the variant directory to a URL served by the test
  // web server.
  GURL TestURL(const base::FilePath::StringType& url_fragment);

  // Load a URL and listen to automation events with a given handler.
  // Returns true if the test glue function correctly.  (The handler should
  // seperately indicate if the test failed.)
  bool RunJavascriptTest(const GURL& url, content::TestMessageHandler* handler);

  // Run a simple test that checks that a nexe loads correctly.  Useful for
  // setting up other tests, such as checking that UMA data was logged.
  void RunLoadTest(const base::FilePath::StringType& test_file);

  // Run a test that was originally written to use NaCl's integration testing
  // jig. These tests were originally driven by NaCl's SCons build in the
  // nacl_integration test stage on the Chrome waterfall. Changes in the
  // boundaries between the Chrome and NaCl repos have resulted in many of
  // these tests having a stronger affinity with the Chrome repo. This method
  // provides a compatibility layer to simplify turning nacl_integration tests
  // into browser tests.
  // |full_url| is true if the full URL is given, otherwise it is a
  // relative URL.
  void RunNaClIntegrationTest(const base::FilePath::StringType& url,
                              bool full_url = false);

 private:
  bool StartTestServer();

  scoped_ptr<net::SpawnedTestServer> test_server_;
};

class NaClBrowserTestNewlib : public NaClBrowserTestBase {
 public:
  base::FilePath::StringType Variant() override;
};

class NaClBrowserTestGLibc : public NaClBrowserTestBase {
 public:
  base::FilePath::StringType Variant() override;
};

class NaClBrowserTestPnacl : public NaClBrowserTestBase {
 public:
  base::FilePath::StringType Variant() override;

  bool IsAPnaclTest() override;
};

class NaClBrowserTestPnaclNonSfi : public NaClBrowserTestBase {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override;
  base::FilePath::StringType Variant() override;
};

// "Transitional" here means that this uses nacl_helper_nonsfi which has
// nacl_helper feature for Non-SFI mode, but statically linked to newlib
// instead of using host glibc. It is still under development.
// TODO(hidehiko): Switch NonSfi tests to use nacl_helper_nonsfi, when
// it is launched officially.
class NaClBrowserTestPnaclTransitionalNonSfi
    : public NaClBrowserTestPnaclNonSfi {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override;
};

class NaClBrowserTestNonSfiMode : public NaClBrowserTestBase {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override;
  base::FilePath::StringType Variant() override;
};

// TODO(hidehiko): Switch NonSfi tests to use nacl_helper_nonsfi, when
// it is launched officially. See NaClBrowserTestPnaclTransitionalNonSfi
// for more details.
class NaClBrowserTestTransitionalNonSfi : public NaClBrowserTestNonSfiMode {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override;
};

// A NaCl browser test only using static files.
class NaClBrowserTestStatic : public NaClBrowserTestBase {
 public:
  base::FilePath::StringType Variant() override;
  bool GetDocumentRoot(base::FilePath* document_root) override;
};

// A NaCl browser test that loads from an unpacked chrome extension.
// The directory of the unpacked extension files is determined by
// the tester's document root.
class NaClBrowserTestNewlibExtension : public NaClBrowserTestNewlib {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override;
};

class NaClBrowserTestGLibcExtension : public NaClBrowserTestGLibc {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override;
};

// PNaCl tests take a long time on windows debug builds
// and sometimes time out.  Disable until it is made faster:
// https://code.google.com/p/chromium/issues/detail?id=177555
#if (defined(OS_WIN) && !defined(NDEBUG))
#  define MAYBE_PNACL(test_name) DISABLED_##test_name
#else
#  define MAYBE_PNACL(test_name) test_name
#endif

// NaCl glibc tests are included for x86 only, as there is no glibc support
// for other architectures (ARM/MIPS).
#if defined(ARCH_CPU_X86_FAMILY)
#  define MAYBE_GLIBC(test_name) test_name
#else
#  define MAYBE_GLIBC(test_name) DISABLED_##test_name
#endif

// Sanitizers internally use some syscalls which non-SFI NaCl disallows.
#if defined(OS_LINUX) && !defined(ADDRESS_SANITIZER) && \
    !defined(THREAD_SANITIZER) && !defined(MEMORY_SANITIZER) && \
    !defined(LEAK_SANITIZER)
#  define MAYBE_NONSFI(test_case) test_case
#else
#  define MAYBE_NONSFI(test_case) DISABLED_##test_case
#endif

// Currently, we only support it on x86-32 or ARM architecture.
// TODO(hidehiko,mazda): Enable this on x86-64, too, when it is supported.
#if defined(OS_LINUX) && !defined(ADDRESS_SANITIZER) && \
    !defined(THREAD_SANITIZER) && !defined(MEMORY_SANITIZER) && \
    !defined(LEAK_SANITIZER) && \
    (defined(ARCH_CPU_X86) || defined(ARCH_CPU_ARMEL))
#  define MAYBE_TRANSITIONAL_NONSFI(test_case) test_case
#else
#  define MAYBE_TRANSITIONAL_NONSFI(test_case) DISABLED_##test_case
#endif

// Currently, translation from pexe to non-sfi nexe is supported only for
// x86-32 or ARM binary.
#if defined(OS_LINUX) && (defined(ARCH_CPU_X86) || defined(ARCH_CPU_ARMEL))
#  define MAYBE_PNACL_NONSFI(test_case) test_case
#else
#  define MAYBE_PNACL_NONSFI(test_case) DISABLED_##test_case
#endif

// Similar to MAYBE_TRANSITIONAL_NONSFI, this is available only on x86-32 or
// ARM.
// TODO(hidehiko,mazda): Merge this to the MAYBE_PNACL_NONSFI when it is
// supported on x86-64.
#if defined(OS_LINUX) && (defined(ARCH_CPU_X86) || defined(ARCH_CPU_ARMEL))
#  define MAYBE_PNACL_TRANSITIONAL_NONSFI(test_case) test_case
#else
#  define MAYBE_PNACL_TRANSITIONAL_NONSFI(test_case) DISABLED_##test_case
#endif


#define NACL_BROWSER_TEST_F(suite, name, body) \
IN_PROC_BROWSER_TEST_F(suite##Newlib, name) \
body \
IN_PROC_BROWSER_TEST_F(suite##GLibc, MAYBE_GLIBC(name)) \
body \
IN_PROC_BROWSER_TEST_F(suite##Pnacl, MAYBE_PNACL(name)) \
body

#endif  // CHROME_TEST_NACL_NACL_BROWSERTEST_UTIL_H_
