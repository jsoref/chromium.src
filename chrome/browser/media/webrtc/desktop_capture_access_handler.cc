// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/webrtc/desktop_capture_access_handler.h"

#include <utility>

#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/media/webrtc/desktop_streams_registry.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/screen_capture_notification_ui.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/desktop_media_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/media_stream_request.h"
#include "content/public/common/origin_util.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/browser/app_window/app_window_registry.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/switches.h"
#include "media/audio/audio_device_description.h"
#include "net/base/url_util.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capture_types.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_CHROMEOS)
#include "ash/shell.h"
#endif  // defined(OS_CHROMEOS)

using content::BrowserThread;

namespace {

// Helper to get title of the calling application shown in the screen capture
// notification.
base::string16 GetApplicationTitle(content::WebContents* web_contents,
                                   const extensions::Extension* extension) {
  // Use extension name as title for extensions and host/origin for drive-by
  // web.
  std::string title;
  if (extension) {
    title = extension->name();
    return base::UTF8ToUTF16(title);
  }
  GURL url = web_contents->GetURL();
  title = content::IsOriginSecure(url) ? net::GetHostAndOptionalPort(url)
                                       : url.GetOrigin().spec();
  return base::UTF8ToUTF16(title);
}

// Returns whether an on-screen notification should appear after desktop capture
// is approved for |extension|.  Component extensions do not display a
// notification.
bool ShouldDisplayNotification(const extensions::Extension* extension) {
  return !(extension &&
           (extension->location() == extensions::Manifest::COMPONENT ||
            extension->is_nwjs_app() ||
            extension->location() == extensions::Manifest::EXTERNAL_COMPONENT));
}

#if 0
base::string16 GetStopSharingUIString(
    const base::string16& application_title,
    const base::string16& registered_extension_name,
    bool capture_audio,
    content::DesktopMediaID::Type capture_type) {
  if (!capture_audio) {
    if (application_title == registered_extension_name) {
      switch (capture_type) {
        case content::DesktopMediaID::TYPE_SCREEN:
          return l10n_util::GetStringFUTF16(
              IDS_MEDIA_SCREEN_CAPTURE_NOTIFICATION_TEXT, application_title);
        case content::DesktopMediaID::TYPE_WINDOW:
          return l10n_util::GetStringFUTF16(
              IDS_MEDIA_WINDOW_CAPTURE_NOTIFICATION_TEXT, application_title);
        case content::DesktopMediaID::TYPE_WEB_CONTENTS:
          return l10n_util::GetStringFUTF16(
              IDS_MEDIA_TAB_CAPTURE_NOTIFICATION_TEXT, application_title);
        case content::DesktopMediaID::TYPE_NONE:
          NOTREACHED();
      }
    } else {
      switch (capture_type) {
        case content::DesktopMediaID::TYPE_SCREEN:
          return l10n_util::GetStringFUTF16(
              IDS_MEDIA_SCREEN_CAPTURE_NOTIFICATION_TEXT_DELEGATED,
              registered_extension_name, application_title);
        case content::DesktopMediaID::TYPE_WINDOW:
          return l10n_util::GetStringFUTF16(
              IDS_MEDIA_WINDOW_CAPTURE_NOTIFICATION_TEXT_DELEGATED,
              registered_extension_name, application_title);
        case content::DesktopMediaID::TYPE_WEB_CONTENTS:
          return l10n_util::GetStringFUTF16(
              IDS_MEDIA_TAB_CAPTURE_NOTIFICATION_TEXT_DELEGATED,
              registered_extension_name, application_title);
        case content::DesktopMediaID::TYPE_NONE:
          NOTREACHED();
      }
    }
  } else {  // The case with audio
    if (application_title == registered_extension_name) {
      switch (capture_type) {
        case content::DesktopMediaID::TYPE_SCREEN:
          return l10n_util::GetStringFUTF16(
              IDS_MEDIA_SCREEN_CAPTURE_WITH_AUDIO_NOTIFICATION_TEXT,
              application_title);
        case content::DesktopMediaID::TYPE_WEB_CONTENTS:
          return l10n_util::GetStringFUTF16(
              IDS_MEDIA_TAB_CAPTURE_WITH_AUDIO_NOTIFICATION_TEXT,
              application_title);
        case content::DesktopMediaID::TYPE_NONE:
        case content::DesktopMediaID::TYPE_WINDOW:
          NOTREACHED();
      }
    } else {
      switch (capture_type) {
        case content::DesktopMediaID::TYPE_SCREEN:
          return l10n_util::GetStringFUTF16(
              IDS_MEDIA_SCREEN_CAPTURE_WITH_AUDIO_NOTIFICATION_TEXT_DELEGATED,
              registered_extension_name, application_title);
        case content::DesktopMediaID::TYPE_WEB_CONTENTS:
          return l10n_util::GetStringFUTF16(
              IDS_MEDIA_TAB_CAPTURE_WITH_AUDIO_NOTIFICATION_TEXT_DELEGATED,
              registered_extension_name, application_title);
        case content::DesktopMediaID::TYPE_NONE:
        case content::DesktopMediaID::TYPE_WINDOW:
          NOTREACHED();
      }
    }
  }
  return base::string16();
}
#endif

// Helper to get list of media stream devices for desktop capture in |devices|.
// Registers to display notification if |display_notification| is true.
// Returns an instance of MediaStreamUI to be passed to content layer.
std::unique_ptr<content::MediaStreamUI> GetDevicesForDesktopCapture(
    content::MediaStreamDevices* devices,
    content::DesktopMediaID media_id,
    bool capture_audio,
    bool disable_local_echo,
    bool display_notification,
    const base::string16& application_title,
    const base::string16& registered_extension_name) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  std::unique_ptr<content::MediaStreamUI> ui;

  DVLOG(2) << __func__ << ": media_id " << media_id.ToString()
           << ", capture_audio " << capture_audio << ", disable_local_echo "
           << disable_local_echo << ", display_notification "
           << display_notification << ", application_title "
           << application_title << ", extension_name "
           << registered_extension_name;

  // Add selected desktop source to the list.
  devices->push_back(
      content::MediaStreamDevice(content::MEDIA_DESKTOP_VIDEO_CAPTURE,
                                 media_id.ToString(), media_id.ToString()));
  if (capture_audio) {
    if (media_id.type == content::DesktopMediaID::TYPE_WEB_CONTENTS) {
      content::WebContentsMediaCaptureId web_id = media_id.web_contents_id;
      web_id.disable_local_echo = disable_local_echo;
      devices->push_back(
          content::MediaStreamDevice(content::MEDIA_DESKTOP_AUDIO_CAPTURE,
                                     web_id.ToString(), "Tab audio"));
    } else if (disable_local_echo) {
      // Use the special loopback device ID for system audio capture.
      devices->push_back(content::MediaStreamDevice(
          content::MEDIA_DESKTOP_AUDIO_CAPTURE,
          media::AudioDeviceDescription::kLoopbackWithMuteDeviceId,
          "System Audio"));
    } else {
      // Use the special loopback device ID for system audio capture.
      devices->push_back(content::MediaStreamDevice(
          content::MEDIA_DESKTOP_AUDIO_CAPTURE,
          media::AudioDeviceDescription::kLoopbackInputDeviceId,
          "System Audio"));
    }
  }

#if 0
  // If required, register to display the notification for stream capture.
  if (!display_notification) {
    return ui;
  }

  ui = ScreenCaptureNotificationUI::Create(GetStopSharingUIString(
      application_title, registered_extension_name, capture_audio,
      media_id.type));
#endif

  return ui;
}

#if !defined(OS_ANDROID)
// Find browser or app window from a given |web_contents|.
gfx::NativeWindow FindParentWindowForWebContents(
    content::WebContents* web_contents) {
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents);
  if (browser && browser->window())
    return browser->window()->GetNativeWindow();

  const extensions::AppWindowRegistry::AppWindowList& window_list =
      extensions::AppWindowRegistry::Get(web_contents->GetBrowserContext())
          ->app_windows();
  for (extensions::AppWindowRegistry::AppWindowList::const_iterator iter =
           window_list.begin();
       iter != window_list.end(); ++iter) {
    if ((*iter)->web_contents() == web_contents)
      return (*iter)->GetNativeWindow();
  }

  return NULL;
}
#endif

}  // namespace

DesktopCaptureAccessHandler::DesktopCaptureAccessHandler() {
}

DesktopCaptureAccessHandler::~DesktopCaptureAccessHandler() {
}

void DesktopCaptureAccessHandler::ProcessScreenCaptureAccessRequest(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    const content::MediaResponseCallback& callback,
    const extensions::Extension* extension) {
  content::MediaStreamDevices devices;
  std::unique_ptr<content::MediaStreamUI> ui;

  DCHECK_EQ(request.video_type, content::MEDIA_DESKTOP_VIDEO_CAPTURE);

  UpdateExtensionTrusted(request, extension);

  bool loopback_audio_supported = false;
#if defined(USE_CRAS) || defined(OS_WIN)
  // Currently loopback audio capture is supported only on Windows and ChromeOS.
  loopback_audio_supported = true;
#endif

  bool screen_capture_enabled =
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableUserMediaScreenCapturing) ||
      MediaCaptureDevicesDispatcher::IsOriginForCasting(
          request.security_origin) ||
      IsExtensionWhitelistedForScreenCapture(extension) ||
      IsBuiltInExtension(request.security_origin);

  const bool origin_is_secure =
      content::IsOriginSecure(request.security_origin) ||
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAllowHttpScreenCapture);

  // If basic conditions (screen capturing is enabled and origin is secure)
  // aren't fulfilled, we'll use "invalid state" as result. Otherwise, we set
  // it after checking permission.
  // TODO(grunell): It would be good to change this result for something else,
  // probably a new one.
  content::MediaStreamRequestResult result =
      content::MEDIA_DEVICE_INVALID_STATE;

  // Approve request only when the following conditions are met:
  //  1. Screen capturing is enabled via command line switch or white-listed for
  //     the given origin.
  //  2. Request comes from a page with a secure origin or from an extension.
  if (screen_capture_enabled && origin_is_secure) {
    // Get title of the calling application prior to showing the message box.
    // chrome::ShowQuestionMessageBox() starts a nested run loop which may
    // allow |web_contents| to be destroyed on the UI thread before the messag
    // box is closed. See http://crbug.com/326690.
    base::string16 application_title =
        GetApplicationTitle(web_contents, extension);
#if !defined(OS_ANDROID)
    gfx::NativeWindow parent_window =
        FindParentWindowForWebContents(web_contents);
#else
    gfx::NativeWindow parent_window = NULL;
#endif
    web_contents = NULL;

    // Some extensions do not require user approval, because they provide their
    // own user approval UI.
    bool is_approved = IsDefaultApproved(extension);
    if (!is_approved) {
      base::string16 application_name =
          base::UTF8ToUTF16(request.security_origin.spec());
      if (extension)
        application_name = base::UTF8ToUTF16(extension->name());
      base::string16 confirmation_text = l10n_util::GetStringFUTF16(
          request.audio_type == content::MEDIA_NO_SERVICE
              ? IDS_MEDIA_SCREEN_CAPTURE_CONFIRMATION_TEXT
              : IDS_MEDIA_SCREEN_AND_AUDIO_CAPTURE_CONFIRMATION_TEXT,
          application_name);
      chrome::MessageBoxResult result = chrome::ShowQuestionMessageBox(
          parent_window,
          l10n_util::GetStringFUTF16(
              IDS_MEDIA_SCREEN_CAPTURE_CONFIRMATION_TITLE, application_name),
          confirmation_text);
      is_approved = (result == chrome::MESSAGE_BOX_RESULT_YES);
    }

    if (is_approved) {
      content::DesktopMediaID screen_id;
#if defined(OS_CHROMEOS)
      screen_id = content::DesktopMediaID::RegisterAuraWindow(
          content::DesktopMediaID::TYPE_SCREEN,
          ash::Shell::Get()->GetPrimaryRootWindow());
#else   // defined(OS_CHROMEOS)
      screen_id = content::DesktopMediaID(content::DesktopMediaID::TYPE_SCREEN,
                                          webrtc::kFullDesktopScreenId);
#endif  // !defined(OS_CHROMEOS)

      bool capture_audio =
          (request.audio_type == content::MEDIA_DESKTOP_AUDIO_CAPTURE &&
           loopback_audio_supported);

      // Determine if the extension is required to display a notification.
      const bool display_notification = ShouldDisplayNotification(extension);

      ui = GetDevicesForDesktopCapture(
          &devices, screen_id, capture_audio, request.disable_local_echo,
          display_notification, application_title, application_title);
      DCHECK(!devices.empty());
    }

    // The only case when devices can be empty is if the user has denied
    // permission.
    result = devices.empty() ? content::MEDIA_DEVICE_PERMISSION_DENIED
                             : content::MEDIA_DEVICE_OK;
  }

  callback.Run(devices, result, std::move(ui));
}

bool DesktopCaptureAccessHandler::IsDefaultApproved(
    const extensions::Extension* extension) {
  return extension &&
         (extension->location() == extensions::Manifest::COMPONENT ||
          extension->is_nwjs_app() ||
          extension->location() == extensions::Manifest::EXTERNAL_COMPONENT ||
          IsExtensionWhitelistedForScreenCapture(extension));
}

bool DesktopCaptureAccessHandler::SupportsStreamType(
    content::WebContents* web_contents,
    const content::MediaStreamType type,
    const extensions::Extension* extension) {
  return type == content::MEDIA_DESKTOP_VIDEO_CAPTURE ||
         type == content::MEDIA_DESKTOP_AUDIO_CAPTURE;
}

bool DesktopCaptureAccessHandler::CheckMediaAccessPermission(
    content::WebContents* web_contents,
    const GURL& security_origin,
    content::MediaStreamType type,
    const extensions::Extension* extension) {
  return false;
}

void DesktopCaptureAccessHandler::HandleRequest(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    const content::MediaResponseCallback& callback,
    const extensions::Extension* extension) {
  content::MediaStreamDevices devices;
  std::unique_ptr<content::MediaStreamUI> ui;

  if (request.video_type != content::MEDIA_DESKTOP_VIDEO_CAPTURE) {
    callback.Run(devices, content::MEDIA_DEVICE_INVALID_STATE, std::move(ui));
    return;
  }

  // If the device id wasn't specified then this is a screen capture request
  // (i.e. chooseDesktopMedia() API wasn't used to generate device id).
  if (request.requested_video_device_id.empty()) {
    ProcessScreenCaptureAccessRequest(web_contents, request, callback,
                                      extension);
    return;
  }

  // The extension name that the stream is registered with.
  std::string original_extension_name;
  // Resolve DesktopMediaID for the specified device id.
  content::DesktopMediaID media_id;
  // TODO(miu): Replace "main RenderFrame" IDs with the request's actual
  // RenderFrame IDs once the desktop capture extension API implementation is
  // fixed.  http://crbug.com/304341
  content::WebContents* const web_contents_for_stream =
      content::WebContents::FromRenderFrameHost(
          content::RenderFrameHost::FromID(request.render_process_id,
                                           request.render_frame_id));
  content::RenderFrameHost* const main_frame =
      web_contents_for_stream ? web_contents_for_stream->GetMainFrame() : NULL;
  if (main_frame) {
    media_id = MediaCaptureDevicesDispatcher::GetInstance()
                   ->GetDesktopStreamsRegistry()
                   ->RequestMediaForStreamId(request.requested_video_device_id,
                                             main_frame->GetProcess()->GetID(),
                                             main_frame->GetRoutingID(),
                                             request.security_origin,
                                             &original_extension_name);
  }

  // Received invalid device id.
  if (media_id.type == content::DesktopMediaID::TYPE_NONE) {
    callback.Run(devices, content::MEDIA_DEVICE_INVALID_STATE, std::move(ui));
    return;
  }

  bool loopback_audio_supported = false;
#if defined(USE_CRAS) || defined(OS_WIN)
  // Currently loopback audio capture is supported only on Windows and ChromeOS.
  loopback_audio_supported = true;
#endif

  // This value essentially from the checkbox on picker window, so it
  // corresponds to user permission.
  const bool audio_permitted = media_id.audio_share;

  // This value essentially from whether getUserMedia requests audio stream.
  const bool audio_requested =
      request.audio_type == content::MEDIA_DESKTOP_AUDIO_CAPTURE;

  // This value shows for a given capture type, whether the system or our code
  // can support audio sharing. Currently audio is only supported for screen and
  // tab/webcontents capture streams.
  const bool audio_supported =
      (media_id.type == content::DesktopMediaID::TYPE_SCREEN &&
       loopback_audio_supported) ||
      media_id.type == content::DesktopMediaID::TYPE_WEB_CONTENTS;

  const bool check_audio_permission =
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          extensions::switches::kDisableDesktopCaptureAudio);
  const bool capture_audio =
      (check_audio_permission ? audio_permitted : true) && audio_requested &&
      audio_supported;

  // Determine if the extension is required to display a notification.
  const bool display_notification = ShouldDisplayNotification(extension);

  ui = GetDevicesForDesktopCapture(&devices, media_id, capture_audio,
                                   request.disable_local_echo,
                                   display_notification,
                                   GetApplicationTitle(web_contents, extension),
                                   base::UTF8ToUTF16(original_extension_name));
  UpdateExtensionTrusted(request, extension);
  callback.Run(devices, content::MEDIA_DEVICE_OK, std::move(ui));
}
