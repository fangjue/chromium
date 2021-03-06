// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_PRINTER_PROVIDER_INTERNAL_PRINTER_PROVIDER_INTERNAL_API_H_
#define EXTENSIONS_BROWSER_API_PRINTER_PROVIDER_INTERNAL_PRINTER_PROVIDER_INTERNAL_API_H_

#include "base/macros.h"
#include "base/observer_list.h"
#include "extensions/browser/api/printer_provider_internal/printer_provider_internal_api_observer.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "extensions/browser/extension_function.h"
#include "extensions/common/api/printer_provider_internal.h"

namespace base {
class DictionaryValue;
class ListValue;
}

namespace content {
class BrowserContext;
}

namespace extensions {
class Extension;
}

namespace extensions {

// Internal API instance. Primarily used to enable observers to watch for when
// printerProviderInternal API functions are called.
class PrinterProviderInternalAPI : public BrowserContextKeyedAPI {
 public:
  static BrowserContextKeyedAPIFactory<PrinterProviderInternalAPI>*
  GetFactoryInstance();

  explicit PrinterProviderInternalAPI(content::BrowserContext* browser_context);
  ~PrinterProviderInternalAPI() override;

  void AddObserver(PrinterProviderInternalAPIObserver* observer);
  void RemoveObserver(PrinterProviderInternalAPIObserver* observer);

 private:
  friend class BrowserContextKeyedAPIFactory<PrinterProviderInternalAPI>;
  friend class PrinterProviderInternalReportPrintersFunction;
  friend class PrinterProviderInternalReportPrinterCapabilityFunction;
  friend class PrinterProviderInternalReportPrintResultFunction;

  // Notifies observers that a printerProvider.onGetPrintersRequested callback
  // has been called. Called from
  // |PrinterProviderInternalReportPrintersFunction|.
  void NotifyGetPrintersResult(const Extension* extension,
                               int request_id,
                               const base::ListValue& printers);

  // Notifies observers that a printerProvider.onGetCapabilityRequested callback
  // has been called. Called from
  // |PrinterProviderInternalReportPrinterCapabilityFunction|.
  void NotifyGetCapabilityResult(const Extension* extension,
                                 int request_id,
                                 const base::DictionaryValue& capability);

  // Notifies observers that a printerProvider.onPrintRequested callback has
  // been called. Called from
  // |PrinterProviderInternalReportPrintResultFunction|.
  void NotifyPrintResult(const Extension* extension,
                         int request_id,
                         core_api::printer_provider_internal::PrintError error);

  // BrowserContextKeyedAPI implementation.
  static const char* service_name() { return "PrinterProviderInternal"; }

  ObserverList<PrinterProviderInternalAPIObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(PrinterProviderInternalAPI);
};

class PrinterProviderInternalReportPrintResultFunction
    : public UIThreadExtensionFunction {
 public:
  PrinterProviderInternalReportPrintResultFunction();

 protected:
  ~PrinterProviderInternalReportPrintResultFunction() override;

  ExtensionFunction::ResponseAction Run() override;

 private:
  DECLARE_EXTENSION_FUNCTION("printerProviderInternal.reportPrintResult",
                             PRINTERPROVIDERINTERNAL_REPORTPRINTRESULT)

  DISALLOW_COPY_AND_ASSIGN(PrinterProviderInternalReportPrintResultFunction);
};

class PrinterProviderInternalReportPrinterCapabilityFunction
    : public UIThreadExtensionFunction {
 public:
  PrinterProviderInternalReportPrinterCapabilityFunction();

 protected:
  ~PrinterProviderInternalReportPrinterCapabilityFunction() override;

  ExtensionFunction::ResponseAction Run() override;

 private:
  DECLARE_EXTENSION_FUNCTION("printerProviderInternal.reportPrinterCapability",
                             PRINTERPROVIDERINTERNAL_REPORTPRINTERCAPABILITY)

  DISALLOW_COPY_AND_ASSIGN(
      PrinterProviderInternalReportPrinterCapabilityFunction);
};

class PrinterProviderInternalReportPrintersFunction
    : public UIThreadExtensionFunction {
 public:
  PrinterProviderInternalReportPrintersFunction();

 protected:
  ~PrinterProviderInternalReportPrintersFunction() override;
  ExtensionFunction::ResponseAction Run() override;

 private:
  DECLARE_EXTENSION_FUNCTION("printerProviderInternal.reportPrinters",
                             PRINTERPROVIDERINTERNAL_REPORTPRINTERS)

  DISALLOW_COPY_AND_ASSIGN(PrinterProviderInternalReportPrintersFunction);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_API_PRINTER_PROVIDER_INTERNAL_PRINTER_PROVIDER_INTERNAL_API_H_
