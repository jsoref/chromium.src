// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_danger_prompt.h"

#include "base/macros.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_service.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/common/safe_browsing/file_type_policies.h"
#include "content/public/browser/download_danger_type.h"
#include "content/public/browser/download_item.h"

using safe_browsing::ClientDownloadResponse;
using safe_browsing::ClientSafeBrowsingReportRequest;

namespace {

//const char kDownloadDangerPromptPrefix[] = "Download.DownloadDangerPrompt";

#if 0
// Converts DownloadDangerType into their corresponding string.
const char* GetDangerTypeString(
    const content::DownloadDangerType& danger_type) {
  switch (danger_type) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE:
      return "DangerousFile";
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
      return "DangerousURL";
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
      return "DangerousContent";
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST:
      return "DangerousHost";
    case content::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT:
      return "UncommonContent";
    case content::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED:
      return "PotentiallyUnwanted";
    case content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS:
    case content::DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_USER_VALIDATED:
    case content::DOWNLOAD_DANGER_TYPE_MAX:
      break;
  }
  NOTREACHED();
  return nullptr;
}
#endif

}  // namespace

void DownloadDangerPrompt::SendSafeBrowsingDownloadReport(
    ClientSafeBrowsingReportRequest::ReportType report_type,
    bool did_proceed,
    const content::DownloadItem& download) {
#if 0
  safe_browsing::SafeBrowsingService* sb_service =
      g_browser_process->safe_browsing_service();
  ClientSafeBrowsingReportRequest report;
  report.set_type(report_type);
  switch (download.GetDangerType()) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
      report.set_download_verdict(ClientDownloadResponse::DANGEROUS);
      break;
    case content::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT:
      report.set_download_verdict(ClientDownloadResponse::UNCOMMON);
      break;
    case content::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED:
      report.set_download_verdict(ClientDownloadResponse::POTENTIALLY_UNWANTED);
      break;
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST:
      report.set_download_verdict(ClientDownloadResponse::DANGEROUS_HOST);
      break;
    default:  // Don't send report for any other danger types.
      return;
  }
  report.set_url(download.GetURL().spec());
  report.set_did_proceed(did_proceed);
  std::string token =
    safe_browsing::DownloadProtectionService::GetDownloadPingToken(
        &download);
  if (!token.empty())
    report.set_token(token);
  std::string serialized_report;
  if (report.SerializeToString(&serialized_report))
    sb_service->SendSerializedDownloadReport(serialized_report);
  else
    DLOG(ERROR) << "Unable to serialize the threat report.";
#endif
}

void DownloadDangerPrompt::RecordDownloadDangerPrompt(
    bool did_proceed,
    const content::DownloadItem& download) {
#if 0
  int64_t file_type_uma_value =
      safe_browsing::FileTypePolicies::GetInstance()->UmaValueForFile(
          download.GetTargetFilePath());
  content::DownloadDangerType danger_type = download.GetDangerType();

  UMA_HISTOGRAM_SPARSE_SLOWLY(
      base::StringPrintf("%s.%s.Shown", kDownloadDangerPromptPrefix,
                         GetDangerTypeString(danger_type)),
      file_type_uma_value);
  if (did_proceed) {
    UMA_HISTOGRAM_SPARSE_SLOWLY(
        base::StringPrintf("%s.%s.Proceed", kDownloadDangerPromptPrefix,
                           GetDangerTypeString(danger_type)),
        file_type_uma_value);
  }
#endif
}
