// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>
#include <dbt.h>

#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/storage_monitor/mock_removable_storage_observer.h"
#include "chrome/browser/storage_monitor/portable_device_watcher_win.h"
#include "chrome/browser/storage_monitor/removable_device_constants.h"
#include "chrome/browser/storage_monitor/storage_info.h"
#include "chrome/browser/storage_monitor/storage_monitor_win.h"
#include "chrome/browser/storage_monitor/test_portable_device_watcher_win.h"
#include "chrome/browser/storage_monitor/test_storage_monitor_win.h"
#include "chrome/browser/storage_monitor/test_volume_mount_watcher_win.h"
#include "chrome/browser/storage_monitor/volume_mount_watcher_win.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chrome {
namespace test {

using content::BrowserThread;

typedef std::vector<int> DeviceIndices;

// StorageMonitorWinTest -------------------------------------------------------

class StorageMonitorWinTest : public testing::Test {
 public:
  StorageMonitorWinTest();
  virtual ~StorageMonitorWinTest();

 protected:
  // testing::Test:
  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  void PreAttachDevices();

  // Runs all the pending tasks on UI thread, FILE thread and blocking thread.
  void RunUntilIdle();

  void DoMassStorageDeviceAttachedTest(const DeviceIndices& device_indices);
  void DoMassStorageDevicesDetachedTest(const DeviceIndices& device_indices);

  // Injects a device attach or detach change (depending on the value of
  // |test_attach|) and tests that the appropriate handler is called.
  void DoMTPDeviceTest(const string16& pnp_device_id, bool test_attach);

  // Gets the MTP details of the storage specified by the |storage_device_id|.
  // On success, returns true and fills in |pnp_device_id| and
  // |storage_object_id|.
  bool GetMTPStorageInfo(const std::string& storage_device_id,
                         string16* pnp_device_id,
                         string16* storage_object_id);

  scoped_ptr<TestStorageMonitorWin> monitor_;

  // Weak pointer; owned by the device notifications class.
  TestVolumeMountWatcherWin* volume_mount_watcher_;

  MockRemovableStorageObserver observer_;

 private:
  MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;
};

StorageMonitorWinTest::StorageMonitorWinTest()
    : ui_thread_(BrowserThread::UI, &message_loop_),
      file_thread_(BrowserThread::FILE, &message_loop_) {
}

StorageMonitorWinTest::~StorageMonitorWinTest() {
}

void StorageMonitorWinTest::SetUp() {
  ASSERT_TRUE(BrowserThread::CurrentlyOn(BrowserThread::UI));
  volume_mount_watcher_ = new TestVolumeMountWatcherWin;
  monitor_.reset(new TestStorageMonitorWin(volume_mount_watcher_,
                                           new TestPortableDeviceWatcherWin));
  monitor_->Init();
  RunUntilIdle();
  monitor_->AddObserver(&observer_);
}

void StorageMonitorWinTest::TearDown() {
  RunUntilIdle();
  monitor_->RemoveObserver(&observer_);
  volume_mount_watcher_->ShutdownWorkerPool();
  monitor_.reset(NULL);
}

void StorageMonitorWinTest::PreAttachDevices() {
  monitor_.reset();
  volume_mount_watcher_ = new TestVolumeMountWatcherWin;
  volume_mount_watcher_->SetAttachedDevicesFake();

  int expect_attach_calls = 0;
  std::vector<base::FilePath> initial_devices =
      volume_mount_watcher_->GetAttachedDevicesCallback().Run();
  for (std::vector<base::FilePath>::const_iterator it = initial_devices.begin();
       it != initial_devices.end(); ++it) {
    bool removable;
    ASSERT_TRUE(volume_mount_watcher_->GetDeviceRemovable(*it, &removable));
    if (removable)
      expect_attach_calls++;
  }

  monitor_.reset(new TestStorageMonitorWin(volume_mount_watcher_,
                                           new TestPortableDeviceWatcherWin));
  monitor_->AddObserver(&observer_);
  monitor_->Init();

  EXPECT_EQ(0u, volume_mount_watcher_->devices_checked().size());

  // This dance is because attachment bounces through a couple of
  // closures, which need to be executed to finish the process.
  RunUntilIdle();
  volume_mount_watcher_->FlushWorkerPoolForTesting();
  RunUntilIdle();

  std::vector<base::FilePath> checked_devices =
      volume_mount_watcher_->devices_checked();
  sort(checked_devices.begin(), checked_devices.end());
  EXPECT_EQ(initial_devices, checked_devices);
  EXPECT_EQ(expect_attach_calls, observer_.attach_calls());
  EXPECT_EQ(0, observer_.detach_calls());
}

void StorageMonitorWinTest::RunUntilIdle() {
  volume_mount_watcher_->FlushWorkerPoolForTesting();
  message_loop_.RunUntilIdle();
}

void StorageMonitorWinTest::DoMassStorageDeviceAttachedTest(
    const DeviceIndices& device_indices) {
  DEV_BROADCAST_VOLUME volume_broadcast;
  volume_broadcast.dbcv_size = sizeof(volume_broadcast);
  volume_broadcast.dbcv_devicetype = DBT_DEVTYP_VOLUME;
  volume_broadcast.dbcv_unitmask = 0x0;
  volume_broadcast.dbcv_flags = 0x0;

  int expect_attach_calls = 0;
  for (DeviceIndices::const_iterator it = device_indices.begin();
       it != device_indices.end(); ++it) {
    volume_broadcast.dbcv_unitmask |= 0x1 << *it;
    bool removable;
    ASSERT_TRUE(volume_mount_watcher_->GetDeviceRemovable(
        VolumeMountWatcherWin::DriveNumberToFilePath(*it), &removable));
    if (removable)
      expect_attach_calls++;
  }
  monitor_->InjectDeviceChange(DBT_DEVICEARRIVAL,
                               reinterpret_cast<DWORD>(&volume_broadcast));

  RunUntilIdle();
  volume_mount_watcher_->FlushWorkerPoolForTesting();
  RunUntilIdle();

  EXPECT_EQ(expect_attach_calls, observer_.attach_calls());
  EXPECT_EQ(0, observer_.detach_calls());
}

void StorageMonitorWinTest::DoMassStorageDevicesDetachedTest(
    const DeviceIndices& device_indices) {
  DEV_BROADCAST_VOLUME volume_broadcast;
  volume_broadcast.dbcv_size = sizeof(volume_broadcast);
  volume_broadcast.dbcv_devicetype = DBT_DEVTYP_VOLUME;
  volume_broadcast.dbcv_unitmask = 0x0;
  volume_broadcast.dbcv_flags = 0x0;

  int pre_attach_calls = observer_.attach_calls();
  int expect_detach_calls = 0;
  for (DeviceIndices::const_iterator it = device_indices.begin();
       it != device_indices.end(); ++it) {
    volume_broadcast.dbcv_unitmask |= 0x1 << *it;
    StorageInfo info;
    ASSERT_TRUE(volume_mount_watcher_->GetDeviceInfo(
        VolumeMountWatcherWin::DriveNumberToFilePath(*it), &info));
    if (StorageInfo::IsRemovableDevice(info.device_id))
      expect_detach_calls++;
  }
  monitor_->InjectDeviceChange(DBT_DEVICEREMOVECOMPLETE,
                               reinterpret_cast<DWORD>(&volume_broadcast));
  RunUntilIdle();
  EXPECT_EQ(pre_attach_calls, observer_.attach_calls());
  EXPECT_EQ(expect_detach_calls, observer_.detach_calls());
}

void StorageMonitorWinTest::DoMTPDeviceTest(const string16& pnp_device_id,
                                            bool test_attach) {
  GUID guidDevInterface = GUID_NULL;
  HRESULT hr = CLSIDFromString(kWPDDevInterfaceGUID, &guidDevInterface);
  if (FAILED(hr))
    return;

  size_t device_id_size = pnp_device_id.size() * sizeof(char16);
  size_t size = sizeof(DEV_BROADCAST_DEVICEINTERFACE) + device_id_size;
  scoped_ptr_malloc<DEV_BROADCAST_DEVICEINTERFACE> dev_interface_broadcast(
      static_cast<DEV_BROADCAST_DEVICEINTERFACE*>(malloc(size)));
  DCHECK(dev_interface_broadcast.get());
  ZeroMemory(dev_interface_broadcast.get(), size);
  dev_interface_broadcast->dbcc_size = size;
  dev_interface_broadcast->dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
  dev_interface_broadcast->dbcc_classguid = guidDevInterface;
  memcpy(dev_interface_broadcast->dbcc_name, pnp_device_id.data(),
         device_id_size);

  int expect_attach_calls = observer_.attach_calls();
  int expect_detach_calls = observer_.detach_calls();
  PortableDeviceWatcherWin::StorageObjectIDs storage_object_ids =
      TestPortableDeviceWatcherWin::GetMTPStorageObjectIds(pnp_device_id);
  for (PortableDeviceWatcherWin::StorageObjectIDs::const_iterator it =
       storage_object_ids.begin(); it != storage_object_ids.end(); ++it) {
    std::string unique_id;
    string16 name;
    string16 location;
    TestPortableDeviceWatcherWin::GetMTPStorageDetails(pnp_device_id, *it,
                                                       &location, &unique_id,
                                                       &name);
    if (test_attach && !name.empty() && !unique_id.empty())
      expect_attach_calls++;
    else if (!name.empty() && !unique_id.empty())
      expect_detach_calls++;
  }

  monitor_->InjectDeviceChange(
      test_attach ? DBT_DEVICEARRIVAL : DBT_DEVICEREMOVECOMPLETE,
      reinterpret_cast<DWORD>(dev_interface_broadcast.get()));

  RunUntilIdle();
  EXPECT_EQ(expect_attach_calls, observer_.attach_calls());
  EXPECT_EQ(expect_detach_calls, observer_.detach_calls());
}

bool StorageMonitorWinTest::GetMTPStorageInfo(
    const std::string& storage_device_id,
    string16* pnp_device_id,
    string16* storage_object_id) {
  return monitor_->GetMTPStorageInfoFromDeviceId(storage_device_id,
                                                 pnp_device_id,
                                                 storage_object_id);
}

TEST_F(StorageMonitorWinTest, RandomMessage) {
  monitor_->InjectDeviceChange(DBT_DEVICEQUERYREMOVE, NULL);
  RunUntilIdle();
}

TEST_F(StorageMonitorWinTest, DevicesAttached) {
  DeviceIndices device_indices;
  device_indices.push_back(1);  // B
  device_indices.push_back(5);  // F
  device_indices.push_back(7);  // H
  device_indices.push_back(13);  // N
  DoMassStorageDeviceAttachedTest(device_indices);

  StorageInfo info;
  EXPECT_TRUE(monitor_->volume_mount_watcher()->GetDeviceInfo(
      base::FilePath(ASCIIToUTF16("F:\\")), &info));
  EXPECT_EQ(ASCIIToUTF16("F:\\"), info.location);
  EXPECT_EQ("dcim:\\\\?\\Volume{F0000000-0000-0000-0000-000000000000}\\",
            info.device_id);
  EXPECT_EQ(ASCIIToUTF16("F:\\ Drive"), info.name);

  EXPECT_FALSE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("G:\\")), &info));
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("F:\\")), &info));
  StorageInfo info1;
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("F:\\subdir")), &info1));
  StorageInfo info2;
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("F:\\subdir\\sub")), &info2));
  EXPECT_EQ(ASCIIToUTF16("F:\\ Drive"), info.name);
  EXPECT_EQ(ASCIIToUTF16("F:\\ Drive"), info1.name);
  EXPECT_EQ(ASCIIToUTF16("F:\\ Drive"), info2.name);
}

TEST_F(StorageMonitorWinTest, PathMountDevices) {
  PreAttachDevices();

  volume_mount_watcher_->AddDeviceForTesting(
      base::FilePath(FILE_PATH_LITERAL("F:\\mount1")),
      "dcim:mount1", L"mount1", 100);
  volume_mount_watcher_->AddDeviceForTesting(
      base::FilePath(FILE_PATH_LITERAL("F:\\mount1\\subdir")),
      "dcim:mount1subdir", L"mount1subdir", 100);
  volume_mount_watcher_->AddDeviceForTesting(
      base::FilePath(FILE_PATH_LITERAL("F:\\mount2")),
      "dcim:mount2", L"mount2", 100);
  RunUntilIdle();
  EXPECT_EQ(9, monitor_->GetAttachedStorage().size());

  StorageInfo info;
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("F:\\dir")), &info));
  EXPECT_EQ(L"F:\\ Drive", info.name);
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("F:\\mount1")), &info));
  EXPECT_EQ(L"mount1", info.name);
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("F:\\mount1\\dir")), &info));
  EXPECT_EQ(L"mount1", info.name);
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("F:\\mount2\\dir")), &info));
  EXPECT_EQ(L"mount2", info.name);
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("F:\\mount1\\subdir")), &info));
  EXPECT_EQ(L"mount1subdir", info.name);
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("F:\\mount1\\subdir\\dir")), &info));
  EXPECT_EQ(L"mount1subdir", info.name);
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(
      base::FilePath(ASCIIToUTF16("F:\\mount1\\subdir\\dir\\dir")), &info));
  EXPECT_EQ(L"mount1subdir", info.name);
}

TEST_F(StorageMonitorWinTest, DevicesAttachedHighBoundary) {
  DeviceIndices device_indices;
  device_indices.push_back(25);

  DoMassStorageDeviceAttachedTest(device_indices);
}

TEST_F(StorageMonitorWinTest, DevicesAttachedLowBoundary) {
  DeviceIndices device_indices;
  device_indices.push_back(0);

  DoMassStorageDeviceAttachedTest(device_indices);
}

TEST_F(StorageMonitorWinTest, DevicesAttachedAdjacentBits) {
  DeviceIndices device_indices;
  device_indices.push_back(0);
  device_indices.push_back(1);
  device_indices.push_back(2);
  device_indices.push_back(3);

  DoMassStorageDeviceAttachedTest(device_indices);
}

TEST_F(StorageMonitorWinTest, DevicesDetached) {
  PreAttachDevices();

  DeviceIndices device_indices;
  device_indices.push_back(1);
  device_indices.push_back(5);
  device_indices.push_back(7);
  device_indices.push_back(13);

  DoMassStorageDevicesDetachedTest(device_indices);
}

TEST_F(StorageMonitorWinTest, DevicesDetachedHighBoundary) {
  PreAttachDevices();

  DeviceIndices device_indices;
  device_indices.push_back(25);

  DoMassStorageDevicesDetachedTest(device_indices);
}

TEST_F(StorageMonitorWinTest, DevicesDetachedLowBoundary) {
  PreAttachDevices();

  DeviceIndices device_indices;
  device_indices.push_back(0);

  DoMassStorageDevicesDetachedTest(device_indices);
}

TEST_F(StorageMonitorWinTest, DevicesDetachedAdjacentBits) {
  PreAttachDevices();

  DeviceIndices device_indices;
  device_indices.push_back(0);
  device_indices.push_back(1);
  device_indices.push_back(2);
  device_indices.push_back(3);

  DoMassStorageDevicesDetachedTest(device_indices);
}

TEST_F(StorageMonitorWinTest, DuplicateAttachCheckSuppressed) {
  // Make sure the original C: mount notification makes it all the
  // way through.
  RunUntilIdle();
  volume_mount_watcher_->FlushWorkerPoolForTesting();
  RunUntilIdle();

  volume_mount_watcher_->BlockDeviceCheckForTesting();
  base::FilePath kAttachedDevicePath =
      VolumeMountWatcherWin::DriveNumberToFilePath(8);  // I:

  DEV_BROADCAST_VOLUME volume_broadcast;
  volume_broadcast.dbcv_size = sizeof(volume_broadcast);
  volume_broadcast.dbcv_devicetype = DBT_DEVTYP_VOLUME;
  volume_broadcast.dbcv_flags = 0x0;
  volume_broadcast.dbcv_unitmask = 0x100;  // I: drive
  monitor_->InjectDeviceChange(DBT_DEVICEARRIVAL,
                               reinterpret_cast<DWORD>(&volume_broadcast));

  EXPECT_EQ(0u, volume_mount_watcher_->devices_checked().size());

  // Re-attach the same volume. We haven't released the mock device check
  // event, so there'll be pending calls in the UI thread to finish the
  // device check notification, blocking the duplicate device injection.
  monitor_->InjectDeviceChange(DBT_DEVICEARRIVAL,
                               reinterpret_cast<DWORD>(&volume_broadcast));

  EXPECT_EQ(0u, volume_mount_watcher_->devices_checked().size());
  volume_mount_watcher_->ReleaseDeviceCheck();
  RunUntilIdle();
  volume_mount_watcher_->ReleaseDeviceCheck();

  // Now let all attach notifications finish running. We'll only get one
  // finish-attach call.
  volume_mount_watcher_->FlushWorkerPoolForTesting();
  RunUntilIdle();

  std::vector<base::FilePath> checked_devices =
      volume_mount_watcher_->devices_checked();
  ASSERT_EQ(1u, checked_devices.size());
  EXPECT_EQ(kAttachedDevicePath, checked_devices[0]);

  // We'll receive a duplicate check now that the first check has fully cleared.
  monitor_->InjectDeviceChange(DBT_DEVICEARRIVAL,
                               reinterpret_cast<DWORD>(&volume_broadcast));
  volume_mount_watcher_->FlushWorkerPoolForTesting();
  volume_mount_watcher_->ReleaseDeviceCheck();
  RunUntilIdle();

  checked_devices = volume_mount_watcher_->devices_checked();
  ASSERT_EQ(2u, checked_devices.size());
  EXPECT_EQ(kAttachedDevicePath, checked_devices[0]);
  EXPECT_EQ(kAttachedDevicePath, checked_devices[1]);
}

TEST_F(StorageMonitorWinTest, DeviceInfoForPath) {
  PreAttachDevices();

  // An invalid path.
  EXPECT_FALSE(monitor_->GetStorageInfoForPath(base::FilePath(L"COM1:\\"),
                                               NULL));

  // An unconnected removable device.
  EXPECT_FALSE(monitor_->GetStorageInfoForPath(base::FilePath(L"E:\\"), NULL));

  // A connected removable device.
  base::FilePath removable_device(L"F:\\");
  StorageInfo device_info;
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(removable_device, &device_info));

  StorageInfo info;
  ASSERT_TRUE(volume_mount_watcher_->GetDeviceInfo(removable_device, &info));
  EXPECT_TRUE(StorageInfo::IsRemovableDevice(info.device_id));
  EXPECT_EQ(info.device_id, device_info.device_id);
  EXPECT_EQ(info.name, device_info.name);
  EXPECT_EQ(info.location, device_info.location);
  EXPECT_EQ(1000000, info.total_size_in_bytes);

  // A fixed device.
  base::FilePath fixed_device(L"N:\\");
  EXPECT_TRUE(monitor_->GetStorageInfoForPath(fixed_device, &device_info));

  ASSERT_TRUE(volume_mount_watcher_->GetDeviceInfo(
      fixed_device, &info));
  EXPECT_FALSE(StorageInfo::IsRemovableDevice(info.device_id));
  EXPECT_EQ(info.device_id, device_info.device_id);
  EXPECT_EQ(info.name, device_info.name);
  EXPECT_EQ(info.location, device_info.location);
}

// Test to verify basic MTP storage attach and detach notifications.
TEST_F(StorageMonitorWinTest, MTPDeviceBasicAttachDetach) {
  DoMTPDeviceTest(TestPortableDeviceWatcherWin::kMTPDeviceWithValidInfo, true);
  DoMTPDeviceTest(TestPortableDeviceWatcherWin::kMTPDeviceWithValidInfo, false);
}

// When a MTP storage device with invalid storage label and id is
// attached/detached, there should not be any device attach/detach
// notifications.
TEST_F(StorageMonitorWinTest, MTPDeviceWithInvalidInfo) {
  DoMTPDeviceTest(TestPortableDeviceWatcherWin::kMTPDeviceWithInvalidInfo,
                  true);
  DoMTPDeviceTest(TestPortableDeviceWatcherWin::kMTPDeviceWithInvalidInfo,
                  false);
}

// Attach a device with two data partitions. Verify that attach/detach
// notifications are sent out for each removable storage.
TEST_F(StorageMonitorWinTest, MTPDeviceWithMultipleStorageObjects) {
  DoMTPDeviceTest(TestPortableDeviceWatcherWin::kMTPDeviceWithMultipleStorages,
                  true);
  DoMTPDeviceTest(TestPortableDeviceWatcherWin::kMTPDeviceWithMultipleStorages,
                  false);
}

TEST_F(StorageMonitorWinTest, DriveNumberToFilePath) {
  EXPECT_EQ(L"A:\\", VolumeMountWatcherWin::DriveNumberToFilePath(0).value());
  EXPECT_EQ(L"Y:\\", VolumeMountWatcherWin::DriveNumberToFilePath(24).value());
  EXPECT_EQ(L"", VolumeMountWatcherWin::DriveNumberToFilePath(-1).value());
  EXPECT_EQ(L"", VolumeMountWatcherWin::DriveNumberToFilePath(199).value());
}

// Given a MTP storage persistent id, GetMTPStorageInfo() should fetch the
// device interface path and local storage object identifier.
TEST_F(StorageMonitorWinTest, GetMTPStorageInfoFromDeviceId) {
  DoMTPDeviceTest(TestPortableDeviceWatcherWin::kMTPDeviceWithValidInfo, true);
  PortableDeviceWatcherWin::StorageObjects storage_objects =
      TestPortableDeviceWatcherWin::GetDeviceStorageObjects(
          TestPortableDeviceWatcherWin::kMTPDeviceWithValidInfo);
  for (PortableDeviceWatcherWin::StorageObjects::const_iterator it =
           storage_objects.begin();
       it != storage_objects.end(); ++it) {
    string16 pnp_device_id;
    string16 storage_object_id;
    ASSERT_TRUE(GetMTPStorageInfo(it->object_persistent_id, &pnp_device_id,
                                  &storage_object_id));
    EXPECT_EQ(string16(TestPortableDeviceWatcherWin::kMTPDeviceWithValidInfo),
              pnp_device_id);
    EXPECT_EQ(it->object_persistent_id,
              TestPortableDeviceWatcherWin::GetMTPStorageUniqueId(
                  pnp_device_id, storage_object_id));
  }
  DoMTPDeviceTest(TestPortableDeviceWatcherWin::kMTPDeviceWithValidInfo, false);
}

}  // namespace test
}  // namespace chrome
