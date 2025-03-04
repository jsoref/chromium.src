// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/notification_test_util.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_context.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "content/public/test/test_utils.h"

// -----------------------------------------------------------------------------

StubNotificationUIManager::StubNotificationUIManager() {}

StubNotificationUIManager::~StubNotificationUIManager() {}

unsigned int StubNotificationUIManager::GetNotificationCount() const {
  return notifications_.size();
}

const Notification& StubNotificationUIManager::GetNotificationAt(
    unsigned int index) const {
  DCHECK_GT(GetNotificationCount(), index);
  return notifications_[index].first;
}

void StubNotificationUIManager::SetNotificationAddedCallback(
    const base::Closure& callback) {
  notification_added_callback_ = callback;
}

bool StubNotificationUIManager::SilentDismissById(
    const std::string& delegate_id,
    ProfileID profile_id) {
  auto iter = notifications_.begin();
  for (; iter != notifications_.end(); ++iter) {
    if (iter->first.id() != delegate_id || iter->second != profile_id)
      continue;
    notifications_.erase(iter);
    return true;
  }
  return false;
}

void StubNotificationUIManager::Add(const Notification& notification,
                                    Profile* profile) {
  if (is_shutdown_started_)
    return;

  notifications_.push_back(std::make_pair(
      notification, NotificationUIManager::GetProfileID(profile)));

  if (!notification_added_callback_.is_null()) {
    notification_added_callback_.Run();
    notification_added_callback_.Reset();
  }

  // Fire the Display() event on the delegate.
  notification.delegate()->Display();
}

bool StubNotificationUIManager::Update(const Notification& notification,
                                       Profile* profile) {
  const ProfileID profile_id = NotificationUIManager::GetProfileID(profile);
  if (notification.tag().empty())
    return false;

  auto iter = notifications_.begin();
  for (; iter != notifications_.end(); ++iter) {
    const Notification& old_notification = iter->first;
    if (old_notification.tag() == notification.tag() &&
        old_notification.origin_url() == notification.origin_url() &&
        iter->second == profile_id) {
      notifications_.erase(iter);
      notifications_.push_back(std::make_pair(notification, profile_id));
      return true;
    }
  }

  return false;
}

const Notification* StubNotificationUIManager::FindById(
    const std::string& delegate_id,
    ProfileID profile_id) const {
  auto iter = notifications_.begin();
  for (; iter != notifications_.end(); ++iter) {
    if (iter->first.id() != delegate_id || iter->second != profile_id)
      continue;

    return &iter->first;
  }

  return nullptr;
}

bool StubNotificationUIManager::CancelById(const std::string& delegate_id,
                                           ProfileID profile_id) {
  auto iter = notifications_.begin();
  for (; iter != notifications_.end(); ++iter) {
    if (iter->first.id() != delegate_id || iter->second != profile_id)
      continue;

    iter->first.delegate()->Close(false /* by_user */);
    notifications_.erase(iter);
    return true;
  }

  return false;
}

std::set<std::string> StubNotificationUIManager::GetAllIdsByProfile(
    ProfileID profile_id) {
  std::set<std::string> delegate_ids;
  for (const auto& pair : notifications_) {
    if (pair.second == profile_id)
      delegate_ids.insert(pair.first.id());
  }
  return delegate_ids;
}

bool StubNotificationUIManager::CancelAllBySourceOrigin(
    const GURL& source_origin) {
  last_canceled_source_ = source_origin;
  return true;
}

bool StubNotificationUIManager::CancelAllByProfile(ProfileID profile_id) {
  NOTIMPLEMENTED();
  return false;
}

void StubNotificationUIManager::CancelAll() {
  for (const auto& pair : notifications_)
    pair.first.delegate()->Close(false /* by_user */);
  notifications_.clear();
}

void StubNotificationUIManager::StartShutdown() {
  is_shutdown_started_ = true;
  CancelAll();
}

FullscreenStateWaiter::FullscreenStateWaiter(
    Browser* browser, bool desired_state)
    : browser_(browser),
      desired_state_(desired_state) {}

void FullscreenStateWaiter::Wait() {
  while (desired_state_ !=
      browser_->exclusive_access_manager()->context()->IsFullscreen()) {
    content::RunAllPendingInMessageLoop();
  }
}
