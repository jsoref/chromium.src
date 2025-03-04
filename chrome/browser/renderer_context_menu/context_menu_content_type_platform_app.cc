// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_context_menu/context_menu_content_type_platform_app.h"

#include "base/command_line.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/process_manager.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest.h"

#include "content/nw/src/common/shell_switches.h"

using extensions::Extension;
using extensions::ProcessManager;

ContextMenuContentTypePlatformApp::ContextMenuContentTypePlatformApp(
    content::WebContents* web_contents,
    const content::ContextMenuParams& params)
    : ContextMenuContentType(web_contents, params, false) {
}

ContextMenuContentTypePlatformApp::~ContextMenuContentTypePlatformApp() {
}

const Extension* ContextMenuContentTypePlatformApp::GetExtension() const {
  ProcessManager* process_manager =
      ProcessManager::Get(source_web_contents()->GetBrowserContext());
  return process_manager->GetExtensionForWebContents(
      source_web_contents());
}

bool ContextMenuContentTypePlatformApp::SupportsGroup(int group) {
  const extensions::Extension* platform_app = GetExtension();

  // The RVH might be for a process sandboxed from the extension.
  if (!platform_app)
    return false;

  DCHECK(platform_app->is_platform_app());

#if defined(NWJS_SDK)
  bool enable_devtools = true;
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kDisableDevTools))
    enable_devtools = false;
#endif

  switch (group) {
    // Add undo/redo, cut/copy/paste etc for text fields.
    case ITEM_GROUP_EDITABLE:
    case ITEM_GROUP_COPY:
      return ContextMenuContentType::SupportsGroup(group);
    case ITEM_GROUP_CURRENT_EXTENSION:
      return true;
#if defined(NWJS_SDK)
    case ITEM_GROUP_DEVTOOLS_UNPACKED_EXT:
      return enable_devtools;
#endif
    default:
      return false;
  }
}
