// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/disks/mock_disk_mount_manager.h"

#include <stdint.h>

#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::ReturnRef;

namespace chromeos {
namespace disks {

namespace {

const char kTestSystemPath[] = "/this/system/path";
const char kTestSystemPathPrefix[] = "/this/system";
const char kTestDevicePath[] = "/this/device/path";
const char kTestMountPath[] = "/media/foofoo";
const char kTestFilePath[] = "/this/file/path";
const char kTestDeviceLabel[] = "A label";
const char kTestDriveLabel[] = "Another label";
const char kTestVendorId[] = "0123";
const char kTestVendorName[] = "A vendor";
const char kTestProductId[] = "abcd";
const char kTestProductName[] = "A product";
const char kTestUuid[] = "FFFF-FFFF";
const char kTestFileSystemType[] = "vfat";

}  // namespace

void MockDiskMountManager::AddObserverInternal(
    DiskMountManager::Observer* observer) {
  observers_.AddObserver(observer);
}

void MockDiskMountManager::RemoveObserverInternal(
    DiskMountManager::Observer* observer) {
  observers_.RemoveObserver(observer);
}

MockDiskMountManager::MockDiskMountManager() {
  ON_CALL(*this, AddObserver(_))
      .WillByDefault(Invoke(this, &MockDiskMountManager::AddObserverInternal));
  ON_CALL(*this, RemoveObserver(_))
      .WillByDefault(Invoke(this,
                            &MockDiskMountManager::RemoveObserverInternal));
  ON_CALL(*this, disks())
      .WillByDefault(Invoke(this, &MockDiskMountManager::disksInternal));
  ON_CALL(*this, mount_points())
      .WillByDefault(Invoke(this, &MockDiskMountManager::mountPointsInternal));
  ON_CALL(*this, FindDiskBySourcePath(_))
      .WillByDefault(Invoke(
          this, &MockDiskMountManager::FindDiskBySourcePathInternal));
  ON_CALL(*this, EnsureMountInfoRefreshed(_, _))
      .WillByDefault(Invoke(
          this, &MockDiskMountManager::EnsureMountInfoRefreshedInternal));
}

MockDiskMountManager::~MockDiskMountManager() {
}

void MockDiskMountManager::NotifyDeviceInsertEvents() {
  std::unique_ptr<DiskMountManager::Disk> disk1_ptr =
      base::MakeUnique<DiskMountManager::Disk>(
          std::string(kTestDevicePath), std::string(),
          false,  // write_disabled_by_policy
          std::string(kTestSystemPath), std::string(kTestFilePath),
          std::string(), std::string(kTestDriveLabel),
          std::string(kTestVendorId), std::string(kTestVendorName),
          std::string(kTestProductId), std::string(kTestProductName),
          std::string(kTestUuid), std::string(kTestSystemPathPrefix),
          DEVICE_TYPE_USB, 4294967295U,
          false,  // is_parent
          false,  // is_read_only
          true,   // has_media
          false,  // on_boot_device
          true,   // on_removable_device
          false,  // is_hidden
          std::string(kTestFileSystemType), std::string());
  DiskMountManager::Disk* disk1 = disk1_ptr.get();

  disks_.clear();
  disks_[std::string(kTestDevicePath)] = std::move(disk1_ptr);

  // Device Added
  NotifyDeviceChanged(DEVICE_ADDED, kTestSystemPath);

  // Disk Added
  NotifyDiskChanged(DISK_ADDED, disk1);

  // Disk Changed
  std::unique_ptr<DiskMountManager::Disk> disk2_ptr =
      base::MakeUnique<DiskMountManager::Disk>(
          std::string(kTestDevicePath), std::string(kTestMountPath),
          false,  // write_disabled_by_policy
          std::string(kTestSystemPath), std::string(kTestFilePath),
          std::string(kTestDeviceLabel), std::string(kTestDriveLabel),
          std::string(kTestVendorId), std::string(kTestVendorName),
          std::string(kTestProductId), std::string(kTestProductName),
          std::string(kTestUuid), std::string(kTestSystemPathPrefix),
          DEVICE_TYPE_MOBILE, 1073741824,
          false,  // is_parent
          false,  // is_read_only
          true,   // has_media
          false,  // on_boot_device
          true,   // on_removable_device
          false,  // is_hidden
          std::string(kTestFileSystemType), std::string());
  DiskMountManager::Disk* disk2 = disk2_ptr.get();
  disks_.clear();
  disks_[std::string(kTestDevicePath)] = std::move(disk2_ptr);
  NotifyDiskChanged(DISK_CHANGED, disk2);
}

void MockDiskMountManager::NotifyDeviceRemoveEvents() {
  std::unique_ptr<DiskMountManager::Disk> disk_ptr =
      base::MakeUnique<DiskMountManager::Disk>(
          std::string(kTestDevicePath), std::string(kTestMountPath),
          false,  // write_disabled_by_policy
          std::string(kTestSystemPath), std::string(kTestFilePath),
          std::string(kTestDeviceLabel), std::string(kTestDriveLabel),
          std::string(kTestVendorId), std::string(kTestVendorName),
          std::string(kTestProductId), std::string(kTestProductName),
          std::string(kTestUuid), std::string(kTestSystemPathPrefix),
          DEVICE_TYPE_SD, 1073741824,
          false,  // is_parent
          false,  // is_read_only
          true,   // has_media
          false,  // on_boot_device
          true,   // on_removable_device
          false,  // is_hidden
          std::string(kTestFileSystemType), std::string());
  DiskMountManager::Disk* disk = disk_ptr.get();
  disks_.clear();
  disks_[std::string(kTestDevicePath)] = std::move(disk_ptr);
  NotifyDiskChanged(DISK_REMOVED, disk);
}

void MockDiskMountManager::SetupDefaultReplies() {
  EXPECT_CALL(*this, AddObserver(_))
      .Times(AnyNumber());
  EXPECT_CALL(*this, RemoveObserver(_))
      .Times(AnyNumber());
  EXPECT_CALL(*this, disks())
      .WillRepeatedly(ReturnRef(disks_));
  EXPECT_CALL(*this, mount_points())
      .WillRepeatedly(ReturnRef(mount_points_));
  EXPECT_CALL(*this, FindDiskBySourcePath(_))
      .Times(AnyNumber());
  EXPECT_CALL(*this, EnsureMountInfoRefreshed(_, _)).Times(AnyNumber());
  EXPECT_CALL(*this, MountPath(_, _, _, _, _)).Times(AnyNumber());
  EXPECT_CALL(*this, UnmountPath(_, _, _))
      .Times(AnyNumber());
  EXPECT_CALL(*this, RemountAllRemovableDrives(_)).Times(AnyNumber());
  EXPECT_CALL(*this, FormatMountedDevice(_))
      .Times(AnyNumber());
  EXPECT_CALL(*this, UnmountDeviceRecursively(_, _))
      .Times(AnyNumber());
}

void MockDiskMountManager::CreateDiskEntryForMountDevice(
    const DiskMountManager::MountPointInfo& mount_info,
    const std::string& device_id,
    const std::string& device_label,
    const std::string& vendor_name,
    const std::string& product_name,
    DeviceType device_type,
    uint64_t total_size_in_bytes,
    bool is_parent,
    bool has_media,
    bool on_boot_device,
    bool on_removable_device,
    const std::string& file_system_type) {
  std::unique_ptr<DiskMountManager::Disk> disk_ptr =
      base::MakeUnique<DiskMountManager::Disk>(
          mount_info.source_path, mount_info.mount_path,
          false,          // write_disabled_by_policy
          std::string(),  // system_path
          mount_info.source_path, device_label,
          std::string(),  // drive_label
          std::string(),  // vendor_id
          vendor_name,
          std::string(),  // product_id
          product_name,
          device_id,      // fs_uuid
          std::string(),  // system_path_prefix
          device_type, total_size_in_bytes, is_parent,
          false,  // is_read_only
          has_media, on_boot_device, on_removable_device,
          false,  // is_hidden
          file_system_type, std::string());
  disks_[std::string(mount_info.source_path)] = std::move(disk_ptr);
}

void MockDiskMountManager::RemoveDiskEntryForMountDevice(
    const DiskMountManager::MountPointInfo& mount_info) {
  disks_.erase(mount_info.source_path);
}

const DiskMountManager::MountPointMap&
MockDiskMountManager::mountPointsInternal() const {
  return mount_points_;
}

const DiskMountManager::Disk*
MockDiskMountManager::FindDiskBySourcePathInternal(
    const std::string& source_path) const {
  DiskMap::const_iterator disk_it = disks_.find(source_path);
  return disk_it == disks_.end() ? nullptr : disk_it->second.get();
}

void MockDiskMountManager::EnsureMountInfoRefreshedInternal(
    const EnsureMountInfoRefreshedCallback& callback,
    bool force) {
  callback.Run(true);
}

void MockDiskMountManager::NotifyDiskChanged(
    DiskEvent event,
    const DiskMountManager::Disk* disk) {
  for (auto& observer : observers_)
    observer.OnDiskEvent(event, disk);
}

void MockDiskMountManager::NotifyDeviceChanged(DeviceEvent event,
                                               const std::string& path) {
  for (auto& observer : observers_)
    observer.OnDeviceEvent(event, path);
}

}  // namespace disks
}  // namespace chromeos
