// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/plugins/plugin_observer.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/debug/crash_logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/plugins/flash_download_interception.h"
#include "chrome/browser/plugins/plugin_finder.h"
#include "chrome/browser/plugins/plugin_infobar_delegates.h"
#include "chrome/browser/plugins/plugin_installer.h"
#include "chrome/browser/plugins/plugin_installer_observer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tab_modal_confirm_dialog.h"
#include "chrome/browser/ui/tab_modal_confirm_dialog_delegate.h"
#include "chrome/common/features.h"
#include "chrome/common/render_messages.h"
#include "chrome/grit/generated_resources.h"
#include "components/component_updater/component_updater_service.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/infobars/core/infobar.h"
#include "components/infobars/core/infobar_delegate.h"
#include "components/infobars/core/simple_alert_infobar_delegate.h"
#include "components/metrics_services_manager/metrics_services_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/common/webplugininfo.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "ui/base/l10n/l10n_util.h"

using content::PluginService;

DEFINE_WEB_CONTENTS_USER_DATA_KEY(PluginObserver);

namespace {

// ReloadPluginInfoBarDelegate -------------------------------------------------

class ReloadPluginInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  static void Create(InfoBarService* infobar_service,
                     content::NavigationController* controller,
                     const base::string16& message);

 private:
  ReloadPluginInfoBarDelegate(content::NavigationController* controller,
                              const base::string16& message);
  ~ReloadPluginInfoBarDelegate() override;

  // ConfirmInfobarDelegate:
  infobars::InfoBarDelegate::InfoBarIdentifier GetIdentifier() const override;
  const gfx::VectorIcon& GetVectorIcon() const override;
  base::string16 GetMessageText() const override;
  int GetButtons() const override;
  base::string16 GetButtonLabel(InfoBarButton button) const override;
  bool Accept() override;

  content::NavigationController* controller_;
  base::string16 message_;
};

// static
void ReloadPluginInfoBarDelegate::Create(
    InfoBarService* infobar_service,
    content::NavigationController* controller,
    const base::string16& message) {
  infobar_service->AddInfoBar(infobar_service->CreateConfirmInfoBar(
      std::unique_ptr<ConfirmInfoBarDelegate>(
          new ReloadPluginInfoBarDelegate(controller, message))));
}

ReloadPluginInfoBarDelegate::ReloadPluginInfoBarDelegate(
    content::NavigationController* controller,
    const base::string16& message)
    : controller_(controller),
      message_(message) {}

ReloadPluginInfoBarDelegate::~ReloadPluginInfoBarDelegate() {}

infobars::InfoBarDelegate::InfoBarIdentifier
ReloadPluginInfoBarDelegate::GetIdentifier() const {
  return RELOAD_PLUGIN_INFOBAR_DELEGATE;
}

const gfx::VectorIcon& ReloadPluginInfoBarDelegate::GetVectorIcon() const {
  return kExtensionCrashedIcon;
}

base::string16 ReloadPluginInfoBarDelegate::GetMessageText() const {
  return message_;
}

int ReloadPluginInfoBarDelegate::GetButtons() const {
  return BUTTON_OK;
}

base::string16 ReloadPluginInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  DCHECK_EQ(BUTTON_OK, button);
  return l10n_util::GetStringUTF16(IDS_RELOAD_PAGE_WITH_PLUGIN);
}

bool ReloadPluginInfoBarDelegate::Accept() {
  controller_->Reload(content::ReloadType::NORMAL, true);
  return true;
}

}  // namespace

// PluginObserver -------------------------------------------------------------

class PluginObserver::PluginPlaceholderHost : public PluginInstallerObserver {
 public:
  PluginPlaceholderHost(
      PluginObserver* observer,
      base::string16 plugin_name,
      PluginInstaller* installer,
      chrome::mojom::PluginRendererPtr plugin_renderer_interface)
      : PluginInstallerObserver(installer),
        observer_(observer),
        plugin_renderer_interface_(std::move(plugin_renderer_interface)) {
    plugin_renderer_interface_.set_connection_error_handler(
        base::Bind(&PluginObserver::RemovePluginPlaceholderHost,
                   base::Unretained(observer_), this));
    DCHECK(installer);
  }

  void DownloadFinished() override {
    plugin_renderer_interface_->FinishedDownloading();
  }

 private:
  PluginObserver* observer_;
  chrome::mojom::PluginRendererPtr plugin_renderer_interface_;
};

class PluginObserver::ComponentObserver
    : public update_client::UpdateClient::Observer {
 public:
  using Events = update_client::UpdateClient::Observer::Events;
  ComponentObserver(PluginObserver* observer,
                    const std::string& component_id,
                    chrome::mojom::PluginRendererPtr plugin_renderer_interface)
      : observer_(observer),
        component_id_(component_id),
        plugin_renderer_interface_(std::move(plugin_renderer_interface)) {
    plugin_renderer_interface_.set_connection_error_handler(
        base::Bind(&PluginObserver::RemoveComponentObserver,
                   base::Unretained(observer_), this));
    //g_browser_process->component_updater()->AddObserver(this);
  }

  ~ComponentObserver() override {
    //g_browser_process->component_updater()->RemoveObserver(this);
  }

  void OnEvent(Events event, const std::string& id) override {
    // TODO(lukasza): https://crbug.com/760637: |routing_id_| might live in a
    // different process than the RenderViewHost - need to track and use
    // placeholder's process when calling Send below.

    if (id != component_id_)
      return;
    switch (event) {
      case Events::COMPONENT_UPDATED:
        plugin_renderer_interface_->UpdateSuccess();
        observer_->RemoveComponentObserver(this);
        break;
      case Events::COMPONENT_UPDATE_FOUND:
        plugin_renderer_interface_->UpdateDownloading();
        break;
      case Events::COMPONENT_NOT_UPDATED:
        plugin_renderer_interface_->UpdateFailure();
        observer_->RemoveComponentObserver(this);
        break;
      default:
        // No message to send.
        break;
    }
  }

 private:
  PluginObserver* observer_;
  std::string component_id_;
  chrome::mojom::PluginRendererPtr plugin_renderer_interface_;
  DISALLOW_COPY_AND_ASSIGN(ComponentObserver);
};

PluginObserver::PluginObserver(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      plugin_host_bindings_(web_contents, this),
      weak_ptr_factory_(this) {}

PluginObserver::~PluginObserver() {
}

void PluginObserver::PluginCrashed(const base::FilePath& plugin_path,
                                   base::ProcessId plugin_pid) {
  DCHECK(!plugin_path.value().empty());

  base::string16 plugin_name =
      PluginService::GetInstance()->GetPluginDisplayNameByPath(plugin_path);
  base::string16 infobar_text;
#if defined(OS_WIN)
  // Find out whether the plugin process is still alive.
  // Note: Although the chances are slim, it is possible that after the plugin
  // process died, |plugin_pid| has been reused by a new process. The
  // consequence is that we will display |IDS_PLUGIN_DISCONNECTED_PROMPT| rather
  // than |IDS_PLUGIN_CRASHED_PROMPT| to the user, which seems acceptable.
  base::Process plugin_process =
      base::Process::OpenWithAccess(plugin_pid,
                                    PROCESS_QUERY_INFORMATION | SYNCHRONIZE);
  bool is_running = false;
  if (plugin_process.IsValid()) {
    int unused_exit_code = 0;
    is_running = base::GetTerminationStatus(plugin_process.Handle(),
                                            &unused_exit_code) ==
                 base::TERMINATION_STATUS_STILL_RUNNING;
    plugin_process.Close();
  }

  if (is_running) {
    infobar_text = l10n_util::GetStringFUTF16(IDS_PLUGIN_DISCONNECTED_PROMPT,
                                              plugin_name);
    UMA_HISTOGRAM_COUNTS("Plugin.ShowDisconnectedInfobar", 1);
  } else {
    infobar_text = l10n_util::GetStringFUTF16(IDS_PLUGIN_CRASHED_PROMPT,
                                              plugin_name);
    UMA_HISTOGRAM_COUNTS("Plugin.ShowCrashedInfobar", 1);
  }
#else
  // Calling the POSIX version of base::GetTerminationStatus() may affect other
  // code which is interested in the process termination status. (Please see the
  // comment of the function.) Therefore, a better way is needed to distinguish
  // disconnections from crashes.
  infobar_text = l10n_util::GetStringFUTF16(IDS_PLUGIN_CRASHED_PROMPT,
                                            plugin_name);
  UMA_HISTOGRAM_COUNTS("Plugin.ShowCrashedInfobar", 1);
#endif

  ReloadPluginInfoBarDelegate::Create(
      InfoBarService::FromWebContents(web_contents()),
      &web_contents()->GetController(),
      infobar_text);
}

void PluginObserver::BlockedOutdatedPlugin(
    chrome::mojom::PluginRendererPtr plugin_renderer,
    const std::string& identifier) {
  PluginFinder* finder = PluginFinder::GetInstance();
  // Find plugin to update.
  PluginInstaller* installer = NULL;
  std::unique_ptr<PluginMetadata> plugin;
  if (finder->FindPluginWithIdentifier(identifier, &installer, &plugin)) {
    auto plugin_placeholder = base::MakeUnique<PluginPlaceholderHost>(
        this, plugin->name(), installer, std::move(plugin_renderer));
    plugin_placeholders_[plugin_placeholder.get()] =
        std::move(plugin_placeholder);

    OutdatedPluginInfoBarDelegate::Create(
        InfoBarService::FromWebContents(web_contents()), installer,
        std::move(plugin));
  } else {
    NOTREACHED();
  }
}

void PluginObserver::BlockedComponentUpdatedPlugin(
    chrome::mojom::PluginRendererPtr plugin_renderer,
    const std::string& identifier) {
#if 0
  auto component_observer = base::MakeUnique<ComponentObserver>(
      this, identifier, std::move(plugin_renderer));
  component_observers_[component_observer.get()] =
      std::move(component_observer);
  g_browser_process->component_updater()->GetOnDemandUpdater().OnDemandUpdate(
      identifier, component_updater::Callback());
#endif
}

void PluginObserver::RemoveComponentObserver(
    ComponentObserver* component_observer) {
  component_observers_.erase(component_observer);
}

void PluginObserver::RemovePluginPlaceholderHost(
    PluginPlaceholderHost* placeholder) {
  plugin_placeholders_.erase(placeholder);
}

void PluginObserver::ShowFlashPermissionBubble() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  FlashDownloadInterception::InterceptFlashDownloadNavigation(
      web_contents(), web_contents()->GetLastCommittedURL());
}

void PluginObserver::CouldNotLoadPlugin(const base::FilePath& plugin_path) {
  g_browser_process->GetMetricsServicesManager()->OnPluginLoadingError(
      plugin_path);
  base::string16 plugin_name =
      PluginService::GetInstance()->GetPluginDisplayNameByPath(plugin_path);
  SimpleAlertInfoBarDelegate::Create(
      InfoBarService::FromWebContents(web_contents()),
      infobars::InfoBarDelegate::PLUGIN_OBSERVER, &kExtensionCrashedIcon,
      l10n_util::GetStringFUTF16(IDS_PLUGIN_INITIALIZATION_ERROR_PROMPT,
                                 plugin_name),
      true);
}
