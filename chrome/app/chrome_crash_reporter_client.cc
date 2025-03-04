// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/app/chrome_crash_reporter_client.h"

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/chrome_result_codes.h"
#include "chrome/common/crash_keys.h"
#include "chrome/common/env_vars.h"
#include "chrome/installer/util/google_update_settings.h"
#include "content/public/common/content_switches.h"

#if defined(OS_POSIX) && !defined(OS_MACOSX)
#include "components/upload_list/crash_upload_list.h"
#include "components/version_info/version_info_values.h"
#endif

#if defined(OS_POSIX)
#include "base/debug/dump_without_crashing.h"
#endif

#if defined(OS_ANDROID)
#include "chrome/common/descriptors_android.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/common/channel_info.h"
#include "chromeos/chromeos_switches.h"
#include "components/version_info/version_info.h"
#endif

ChromeCrashReporterClient::ChromeCrashReporterClient() {}

ChromeCrashReporterClient::~ChromeCrashReporterClient() {}

#if !defined(OS_MACOSX)
void ChromeCrashReporterClient::SetCrashReporterClientIdFromGUID(
    const std::string& client_guid) {
  crash_keys::SetMetricsClientIdFromGUID(client_guid);
}
#endif

#if defined(OS_POSIX)
void ChromeCrashReporterClient::GetProductNameAndVersion(
    const char** product_name,
    const char** version) {
  DCHECK(product_name);
  DCHECK(version);
#if defined(OS_ANDROID)
  *product_name = "Chrome_Android";
#elif defined(OS_CHROMEOS)
  *product_name = "Chrome_ChromeOS";
#else  // OS_LINUX
#if !defined(ADDRESS_SANITIZER)
  *product_name = product_name_.c_str();
#else
  *product_name = "Chrome_Linux_ASan";
#endif
#endif

  *version = product_version_.c_str();
}

#if !defined(OS_MACOSX)
base::FilePath ChromeCrashReporterClient::GetReporterLogFilename() {
  return base::FilePath(CrashUploadList::kReporterLogFilename);
}
#endif
#endif

bool ChromeCrashReporterClient::GetCrashDumpLocation(
    base::FilePath* crash_dir) {
  // By setting the BREAKPAD_DUMP_LOCATION environment variable, an alternate
  // location to write breakpad crash dumps can be set.
  std::unique_ptr<base::Environment> env(base::Environment::Create());
  std::string alternate_crash_dump_location;
  if (env->GetVar("BREAKPAD_DUMP_LOCATION", &alternate_crash_dump_location)) {
    base::FilePath crash_dumps_dir_path =
        base::FilePath::FromUTF8Unsafe(alternate_crash_dump_location);
    PathService::Override(chrome::DIR_CRASH_DUMPS, crash_dumps_dir_path);
  }
  return PathService::Get(chrome::DIR_CRASH_DUMPS, crash_dir);
}

size_t ChromeCrashReporterClient::RegisterCrashKeys() {
  return crash_keys::RegisterChromeCrashKeys();
}

bool ChromeCrashReporterClient::IsRunningUnattended() {
  // std::unique_ptr<base::Environment> env(base::Environment::Create());
  // return env->HasVar(env_vars::kHeadless);
  return !enable_upload_;
}

bool ChromeCrashReporterClient::GetCollectStatsConsent() {
  return true;
#if 0
#if defined(GOOGLE_CHROME_BUILD)
  bool is_official_chrome_build = true;
#else
  bool is_official_chrome_build = false;
#endif

#if defined(OS_CHROMEOS)
  bool is_guest_session = base::CommandLine::ForCurrentProcess()->HasSwitch(
      chromeos::switches::kGuestSession);
  bool is_stable_channel =
      chrome::GetChannel() == version_info::Channel::STABLE;

  if (is_guest_session && is_stable_channel)
    return false;
#endif  // defined(OS_CHROMEOS)

#if defined(OS_ANDROID)
  // TODO(jcivelli): we should not initialize the crash-reporter when it was not
  // enabled. Right now if it is disabled we still generate the minidumps but we
  // do not upload them.
  return is_official_chrome_build;
#else  // !defined(OS_ANDROID)
  return is_official_chrome_build &&
      GoogleUpdateSettings::GetCollectStatsConsent();
#endif  // defined(OS_ANDROID)
#endif // 0
}

#if defined(OS_ANDROID)
int ChromeCrashReporterClient::GetAndroidMinidumpDescriptor() {
  return kAndroidMinidumpDescriptor;
}
#endif

bool ChromeCrashReporterClient::EnableBreakpadForProcess(
    const std::string& process_type) {
  return process_type == switches::kRendererProcess ||
         process_type == switches::kPpapiPluginProcess ||
         process_type == switches::kZygoteProcess ||
         process_type == switches::kGpuProcess;
}
