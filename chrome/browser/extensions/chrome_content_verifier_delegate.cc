// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/chrome_content_verifier_delegate.h"

#include <algorithm>
#include <memory>
#include <set>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/syslog_logging.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/version.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/install_verifier.h"
#include "chrome/browser/extensions/policy_extension_reinstaller.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_constants.h"
#include "extensions/browser/content_verifier.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/common/disable_reason.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/extensions_client.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_url_handlers.h"
#include "net/base/backoff_entry.h"
#include "net/base/escape.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/extensions/extension_assets_manager_chromeos.h"
#endif

namespace {

const char kContentVerificationExperimentName[] =
    "ExtensionContentVerification";


}  // namespace

namespace extensions {

// static
ContentVerifierDelegate::Mode ChromeContentVerifierDelegate::GetDefaultMode() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  Mode experiment_value;
#if defined(GOOGLE_CHROME_BUILD)
  experiment_value = ContentVerifierDelegate::ENFORCE_STRICT;
#else
  experiment_value = ContentVerifierDelegate::NONE;
#endif
  const std::string group =
      base::FieldTrialList::FindFullName(kContentVerificationExperimentName);
  if (group == "EnforceStrict")
    experiment_value = ContentVerifierDelegate::ENFORCE_STRICT;
  else if (group == "Enforce")
    experiment_value = ContentVerifierDelegate::ENFORCE;
  else if (group == "Bootstrap")
    experiment_value = ContentVerifierDelegate::BOOTSTRAP;
  else if (group == "None")
    experiment_value = ContentVerifierDelegate::NONE;

  // The field trial value that normally comes from the server can be
  // overridden on the command line, which we don't want to allow since
  // malware can set chrome command line flags. There isn't currently a way
  // to find out what the server-provided value is in this case, so we
  // conservatively default to the strictest mode if we detect our experiment
  // name being overridden.
  if (command_line->HasSwitch(switches::kForceFieldTrials)) {
    std::string forced_trials =
        command_line->GetSwitchValueASCII(switches::kForceFieldTrials);
    if (forced_trials.find(kContentVerificationExperimentName) !=
        std::string::npos)
      experiment_value = ContentVerifierDelegate::ENFORCE_STRICT;
  }

  Mode cmdline_value = NONE;
  if (command_line->HasSwitch(switches::kExtensionContentVerification)) {
    std::string switch_value = command_line->GetSwitchValueASCII(
        switches::kExtensionContentVerification);
    if (switch_value == switches::kExtensionContentVerificationBootstrap)
      cmdline_value = ContentVerifierDelegate::BOOTSTRAP;
    else if (switch_value == switches::kExtensionContentVerificationEnforce)
      cmdline_value = ContentVerifierDelegate::ENFORCE;
    else if (switch_value ==
             switches::kExtensionContentVerificationEnforceStrict)
      cmdline_value = ContentVerifierDelegate::ENFORCE_STRICT;
    else
      // If no value was provided (or the wrong one), just default to enforce.
      cmdline_value = ContentVerifierDelegate::ENFORCE;
  }

  // We don't want to allow the command-line flags to eg disable enforcement
  // if the experiment group says it should be on, or malware may just modify
  // the command line flags. So return the more restrictive of the 2 values.
  return std::max(experiment_value, cmdline_value);
}

ChromeContentVerifierDelegate::ChromeContentVerifierDelegate(
    content::BrowserContext* context)
    : context_(context),
      default_mode_(GetDefaultMode()),
      policy_extension_reinstaller_(
          base::MakeUnique<PolicyExtensionReinstaller>(context_)) {}

ChromeContentVerifierDelegate::~ChromeContentVerifierDelegate() {
}

ContentVerifierDelegate::Mode ChromeContentVerifierDelegate::ShouldBeVerified(
    const Extension& extension) {
#if defined(OS_CHROMEOS)
  if (ExtensionAssetsManagerChromeOS::IsSharedInstall(&extension))
    return ContentVerifierDelegate::ENFORCE_STRICT;
#endif

  if (extension.is_nwjs_app() && !Manifest::IsComponentLocation(extension.location()))
    return default_mode_;
  if (!extension.is_extension() && !extension.is_legacy_packaged_app())
    return ContentVerifierDelegate::NONE;
  if (!Manifest::IsAutoUpdateableLocation(extension.location()))
    return ContentVerifierDelegate::NONE;

  // Use the InstallVerifier's |IsFromStore| method to avoid discrepancies
  // between which extensions are considered in-store.
  // See https://crbug.com/766806 for details.
  if (!InstallVerifier::IsFromStore(extension)) {
    // It's possible that the webstore update url was overridden for testing
    // so also consider extensions with the default (production) update url
    // to be from the store as well.
    if (ManifestURL::GetUpdateURL(&extension) !=
        extension_urls::GetDefaultWebstoreUpdateUrl()) {
      return ContentVerifierDelegate::NONE;
    }
  }

  return default_mode_;
}

ContentVerifierKey ChromeContentVerifierDelegate::GetPublicKey() {
  return ContentVerifierKey(kWebstoreSignaturesPublicKey,
                            kWebstoreSignaturesPublicKeySize);
}

GURL ChromeContentVerifierDelegate::GetSignatureFetchUrl(
    const std::string& extension_id,
    const base::Version& version) {
  // TODO(asargent) Factor out common code from the extension updater's
  // ManifestFetchData class that can be shared for use here.
  std::string id_part = "id=" + extension_id;
  std::string version_part = "v=" + version.GetString();
  std::string x_value = net::EscapeQueryParamValue(
      base::JoinString({"uc", "installsource=signature", id_part, version_part},
                       "&"),
      true);
  std::string query = "response=redirect&x=" + x_value;

  GURL base_url = extension_urls::GetWebstoreUpdateUrl();
  GURL::Replacements replacements;
  replacements.SetQuery(query.c_str(), url::Component(0, query.length()));
  return base_url.ReplaceComponents(replacements);
}

std::set<base::FilePath> ChromeContentVerifierDelegate::GetBrowserImagePaths(
    const extensions::Extension* extension) {
  return ExtensionsClient::Get()->GetBrowserImagePaths(extension);
}

void ChromeContentVerifierDelegate::VerifyFailed(
    const std::string& extension_id,
    const base::FilePath& relative_path,
    ContentVerifyJob::FailureReason reason) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(context_);
  const Extension* extension =
      registry->enabled_extensions().GetByID(extension_id);
  if (!extension)
    return;
  ExtensionSystem* system = ExtensionSystem::Get(context_);
  ExtensionService* service = system->extension_service();
  Mode mode = ShouldBeVerified(*extension);
  if (mode >= ContentVerifierDelegate::ENFORCE) {
    if (ContentVerifier::ShouldRepairIfCorrupted(system->management_policy(),
                                                 extension)) {
      PendingExtensionManager* pending_manager =
          service->pending_extension_manager();
      if (pending_manager->IsPolicyReinstallForCorruptionExpected(extension_id))
        return;
      SYSLOG(WARNING) << "Corruption detected in policy extension "
                      << extension_id << " installed at: "
                      << extension->path().value();
      pending_manager->ExpectPolicyReinstallForCorruption(extension_id);
      service->DisableExtension(extension_id,
                                disable_reason::DISABLE_CORRUPTED);
      // Attempt to reinstall.
      policy_extension_reinstaller_->NotifyExtensionDisabledDueToCorruption();
      return;
    }
    DLOG(WARNING) << "Disabling extension " << extension_id << " ('"
                  << extension->name()
                  << "') due to content verification failure. In tests you "
                  << "might want to use a ScopedIgnoreContentVerifierForTest "
                  << "instance to prevent this.";
    service->DisableExtension(extension_id, disable_reason::DISABLE_CORRUPTED);
    ExtensionPrefs::Get(context_)->IncrementCorruptedDisableCount();
    UMA_HISTOGRAM_BOOLEAN("Extensions.CorruptExtensionBecameDisabled", true);
    UMA_HISTOGRAM_ENUMERATION("Extensions.CorruptExtensionDisabledReason",
                              reason, ContentVerifyJob::FAILURE_REASON_MAX);
  } else if (!base::ContainsKey(would_be_disabled_ids_, extension_id)) {
    UMA_HISTOGRAM_BOOLEAN("Extensions.CorruptExtensionWouldBeDisabled", true);
    would_be_disabled_ids_.insert(extension_id);
  }
}

void ChromeContentVerifierDelegate::Shutdown() {
  // Shut down |policy_extension_reinstaller_| on its creation thread. |this|
  // can be destroyed through InfoMap on IO thread, we do not want to destroy
  // |policy_extension_reinstaller_| there.
  policy_extension_reinstaller_.reset();
}

}  // namespace extensions
