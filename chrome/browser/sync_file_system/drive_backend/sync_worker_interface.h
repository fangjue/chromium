// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_SYNC_WORKER_INTERFACE_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_SYNC_WORKER_INTERFACE_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/sync_file_system/remote_file_sync_service.h"
#include "chrome/browser/sync_file_system/sync_callbacks.h"
#include "net/base/network_change_notifier.h"

class GURL;

namespace base {
class FilePath;
class ListValue;
}

namespace drive {
class DriveServiceInterface;
class DriveUploaderInterface;
}

namespace fileapi {
class FileSystemURL;
}

namespace sync_file_system {

class FileChange;
class SyncFileMetadata;

namespace drive_backend {

class MetadataDatabase;
class RemoteChangeProcessorOnWorker;
class SyncTaskManager;

class SyncWorkerInterface {
 public:
  virtual ~SyncWorkerInterface() {}

  // Initializes SyncWorkerInterface after constructions of some member classes.
  virtual void Initialize() = 0;

  // See RemoteFileSyncService for the details.
  virtual void RegisterOrigin(const GURL& origin,
                              const SyncStatusCallback& callback) = 0;
  virtual void EnableOrigin(const GURL& origin,
                            const SyncStatusCallback& callback) = 0;
  virtual void DisableOrigin(const GURL& origin,
                             const SyncStatusCallback& callback) = 0;
  virtual void UninstallOrigin(
      const GURL& origin,
      RemoteFileSyncService::UninstallFlag flag,
      const SyncStatusCallback& callback) = 0;
  virtual void ProcessRemoteChange(const SyncFileCallback& callback) = 0;
  virtual void SetRemoteChangeProcessor(
      RemoteChangeProcessorOnWorker* remote_change_processor_on_worker) = 0;
  virtual RemoteServiceState GetCurrentState() const = 0;
  virtual void GetOriginStatusMap(
      const RemoteFileSyncService::StatusMapCallback& callback) = 0;
  virtual scoped_ptr<base::ListValue> DumpFiles(const GURL& origin) = 0;
  virtual scoped_ptr<base::ListValue> DumpDatabase() = 0;
  virtual void SetSyncEnabled(bool enabled) = 0;
  virtual void PromoteDemotedChanges() = 0;

  // See LocalChangeProcessor for the details.
  virtual void ApplyLocalChange(
      const FileChange& local_change,
      const base::FilePath& local_path,
      const SyncFileMetadata& local_metadata,
      const fileapi::FileSystemURL& url,
      const SyncStatusCallback& callback) = 0;

  // See drive::DriveNotificationObserver for the details.
  virtual void OnNotificationReceived() = 0;

  // See drive::DriveServiceObserver for the details.
  virtual void OnReadyToSendRequests(const std::string& account_id) = 0;
  virtual void OnRefreshTokenInvalid() = 0;

  // See net::NetworkChangeNotifier::NetworkChangeObserver for the details.
  virtual void OnNetworkChanged(
      net::NetworkChangeNotifier::ConnectionType type) = 0;

  virtual drive::DriveServiceInterface* GetDriveService() = 0;
  virtual drive::DriveUploaderInterface* GetDriveUploader() = 0;
  virtual MetadataDatabase* GetMetadataDatabase() = 0;
  virtual SyncTaskManager* GetSyncTaskManager() = 0;

  virtual void DetachFromSequence() = 0;

 private:
  friend class SyncEngineTest;

  // TODO(peria): Remove this interface after making FakeSyncWorker class.
  virtual void SetHasRefreshToken(bool has_refresh_token) = 0;
};

}  // namespace drive_backend
}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_SYNC_WORKER_INTERFACE_H_
