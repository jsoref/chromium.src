// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/app_lifetime_monitor.h"

#include "content/public/browser/browser_context.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/notification_types.h"
#include "extensions/common/extension.h"

namespace apps {

using extensions::AppWindow;
using extensions::AppWindowRegistry;
using extensions::Extension;
using extensions::ExtensionHost;

AppLifetimeMonitor::AppLifetimeMonitor(content::BrowserContext* context)
    : context_(context) {
  registrar_.Add(this,
                 extensions::NOTIFICATION_EXTENSION_HOST_DID_STOP_FIRST_LOAD,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 extensions::NOTIFICATION_EXTENSION_HOST_DESTROYED,
                 content::NotificationService::AllSources());

  AppWindowRegistry* app_window_registry =
      AppWindowRegistry::Factory::GetForBrowserContext(context_,
                                                       false /* create */);
  DCHECK(app_window_registry);
  app_window_registry->AddObserver(this);
}

AppLifetimeMonitor::~AppLifetimeMonitor() {}

void AppLifetimeMonitor::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void AppLifetimeMonitor::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void AppLifetimeMonitor::Observe(int type,
                                const content::NotificationSource& source,
                                const content::NotificationDetails& details) {
  switch (type) {
    case extensions::NOTIFICATION_EXTENSION_HOST_DID_STOP_FIRST_LOAD: {
      ExtensionHost* host = content::Details<ExtensionHost>(details).ptr();
      const Extension* extension = host->extension();
      if (!extension || !extension->is_platform_app())
        return;

      NotifyAppStart(extension->id());
      break;
    }

    case extensions::NOTIFICATION_EXTENSION_HOST_DESTROYED: {
      ExtensionHost* host = content::Details<ExtensionHost>(details).ptr();
      const Extension* extension = host->extension();
      if (!extension || !extension->is_platform_app())
        return;

      NotifyAppStop(extension->id());
      break;
    }
  }
}

void AppLifetimeMonitor::OnAppWindowRemoved(AppWindow* app_window) {
  if (!HasOtherVisibleAppWindows(app_window))
    NotifyAppDeactivated(app_window->extension_id());
}

void AppLifetimeMonitor::OnAppWindowHidden(AppWindow* app_window) {
#if 0
  if (!HasOtherVisibleAppWindows(app_window))
    NotifyAppDeactivated(app_window->extension_id());
#endif
}

void AppLifetimeMonitor::OnAppWindowShown(AppWindow* app_window,
                                          bool was_hidden) {
  if (app_window->window_type() != AppWindow::WINDOW_TYPE_DEFAULT)
    return;

  // The app is being activated if this is the first window to become visible.
  if (was_hidden && !HasOtherVisibleAppWindows(app_window)) {
    NotifyAppActivated(app_window->extension_id());
  }
}

void AppLifetimeMonitor::Shutdown() {
  AppWindowRegistry* app_window_registry =
      AppWindowRegistry::Factory::GetForBrowserContext(context_,
                                                       false /* create */);
  if (app_window_registry)
    app_window_registry->RemoveObserver(this);
}

bool AppLifetimeMonitor::HasOtherVisibleAppWindows(
    AppWindow* app_window) const {
  AppWindowRegistry::AppWindowList windows =
      AppWindowRegistry::Get(app_window->browser_context())
          ->GetAppWindowsForApp(app_window->extension_id());

  for (AppWindowRegistry::AppWindowList::const_iterator i = windows.begin();
       i != windows.end();
       ++i) {
    if (*i != app_window && !(*i)->is_hidden())
      return true;
  }
  return false;
}

void AppLifetimeMonitor::NotifyAppStart(const std::string& app_id) {
  for (auto& observer : observers_)
    observer.OnAppStart(context_, app_id);
}

void AppLifetimeMonitor::NotifyAppActivated(const std::string& app_id) {
  for (auto& observer : observers_)
    observer.OnAppActivated(context_, app_id);
}

void AppLifetimeMonitor::NotifyAppDeactivated(const std::string& app_id) {
  for (auto& observer : observers_)
    observer.OnAppDeactivated(context_, app_id);
}

void AppLifetimeMonitor::NotifyAppStop(const std::string& app_id) {
  for (auto& observer : observers_)
    observer.OnAppStop(context_, app_id);
}

}  // namespace apps
