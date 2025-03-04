// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_browsertest.h"

#include <stdint.h>

#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/sys_info.h"
#include "base/test/test_file_util.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/download/chrome_download_manager_delegate.h"
#include "chrome/browser/download/download_commands.h"
#include "chrome/browser/download/download_core_service.h"
#include "chrome/browser/download/download_core_service_factory.h"
#include "chrome/browser/download/download_crx_util.h"
#include "chrome/browser/download/download_history.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/download/download_request_limiter.h"
#include "chrome/browser/download/download_shelf.h"
#include "chrome/browser/download/download_target_determiner.h"
#include "chrome/browser/download/download_test_file_activity_observer.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/net/url_request_mock_util.h"
#include "chrome/browser/notifications/notification_ui_manager.h"
#include "chrome/browser/permissions/permission_request_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu_browsertest_util.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu_test_util.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/safe_browsing/download_file_types.pb.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/history/content/browser/download_conversions.h"
#include "components/history/core/browser/download_constants.h"
#include "components/history/core/browser/download_row.h"
#include "components/history/core/browser/history_service.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/infobars/core/infobar.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/common/safe_browsing_prefs.h"
#include "components/safe_browsing/proto/csd.pb.h"
#include "content/public/browser/download_danger_type.h"
#include "content/public/browser/download_interrupt_reasons.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/download_save_info.h"
#include "content/public/browser/download_url_parameters.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/context_menu_params.h"
#include "content/public/common/quarantine.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/download_test_observer.h"
#include "content/public/test/test_download_request_handler.h"
#include "content/public/test/test_file_error_injector.h"
#include "content/public/test/test_navigation_observer.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/scoped_ignore_content_verifier_for_test.h"
#include "net/base/filename_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/url_request/url_request_mock_http_job.h"
#include "net/test/url_request/url_request_slow_download_job.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/page_transition_types.h"

#if defined(FULL_SAFE_BROWSING)
#include "chrome/browser/safe_browsing/download_protection/download_feedback_service.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_service.h"
#include "chrome/browser/safe_browsing/test_safe_browsing_service.h"
#endif

using content::BrowserContext;
using content::BrowserThread;
using content::DownloadItem;
using content::DownloadManager;
using content::DownloadUrlParameters;
using content::WebContents;
using extensions::Extension;
using net::URLRequestMockHTTPJob;

namespace {

const char kDownloadTest1Path[] = "download-test1.lib";

class CreatedObserver : public content::DownloadManager::Observer {
 public:
  explicit CreatedObserver(content::DownloadManager* manager)
      : manager_(manager),
        waiting_(false) {
    manager->AddObserver(this);
  }
  ~CreatedObserver() override {
    if (manager_)
      manager_->RemoveObserver(this);
  }

  void Wait() {
    std::vector<DownloadItem*> downloads;
    manager_->GetAllDownloads(&downloads);
    if (!downloads.empty())
      return;
    waiting_ = true;
    content::RunMessageLoop();
    waiting_ = false;
  }

 private:
  void OnDownloadCreated(content::DownloadManager* manager,
                         content::DownloadItem* item) override {
    DCHECK_EQ(manager_, manager);
    if (waiting_)
      base::RunLoop::QuitCurrentWhenIdleDeprecated();
  }

  content::DownloadManager* manager_;
  bool waiting_;

  DISALLOW_COPY_AND_ASSIGN(CreatedObserver);
};

class PercentWaiter : public content::DownloadItem::Observer {
 public:
  explicit PercentWaiter(DownloadItem* item)
    : item_(item),
      waiting_(false),
      error_(false),
      prev_percent_(0) {
    item_->AddObserver(this);
  }
  ~PercentWaiter() override {
    if (item_)
      item_->RemoveObserver(this);
  }

  bool WaitForFinished() {
    if (item_->GetState() == DownloadItem::COMPLETE) {
      return item_->PercentComplete() == 100;
    }
    waiting_ = true;
    content::RunMessageLoop();
    waiting_ = false;
    return !error_;
  }

 private:
  void OnDownloadUpdated(content::DownloadItem* item) override {
    DCHECK_EQ(item_, item);
    if (!error_ &&
        ((prev_percent_ > item_->PercentComplete()) ||
         (item_->GetState() == DownloadItem::COMPLETE &&
          (item_->PercentComplete() != 100)))) {
      error_ = true;
      if (waiting_)
        base::RunLoop::QuitCurrentWhenIdleDeprecated();
    }
    if (item_->GetState() == DownloadItem::COMPLETE && waiting_)
      base::RunLoop::QuitCurrentWhenIdleDeprecated();
  }

  void OnDownloadDestroyed(content::DownloadItem* item) override {
    DCHECK_EQ(item_, item);
    item_->RemoveObserver(this);
    item_ = NULL;
  }

  content::DownloadItem* item_;
  bool waiting_;
  bool error_;
  int prev_percent_;

  DISALLOW_COPY_AND_ASSIGN(PercentWaiter);
};

// DownloadTestObserver subclass that observes one download until it transitions
// from a non-resumable state to a resumable state a specified number of
// times. Note that this observer can only observe a single download.
class DownloadTestObserverResumable : public content::DownloadTestObserver {
 public:
  // Construct a new observer. |transition_count| is the number of times the
  // download should transition from a non-resumable state to a resumable state.
  DownloadTestObserverResumable(DownloadManager* download_manager,
                                size_t transition_count)
      : DownloadTestObserver(download_manager, 1,
                             ON_DANGEROUS_DOWNLOAD_FAIL),
        was_previously_resumable_(false),
        transitions_left_(transition_count) {
    Init();
  }
  ~DownloadTestObserverResumable() override {}

 private:
  bool IsDownloadInFinalState(DownloadItem* download) override {
    bool is_resumable_now = download->CanResume();
    if (!was_previously_resumable_ && is_resumable_now)
      --transitions_left_;
    was_previously_resumable_ = is_resumable_now;
    return transitions_left_ == 0;
  }

  bool was_previously_resumable_;
  size_t transitions_left_;

  DISALLOW_COPY_AND_ASSIGN(DownloadTestObserverResumable);
};

// IDs and paths of CRX files used in tests.
const char kGoodCrxId[] = "ldnnhddmnhbkjipkidpdiheffobcpfmf";
const char kGoodCrxPath[] = "extensions/good.crx";

const char kLargeThemeCrxId[] = "pjpgmfcmabopnnfonnhmdjglfpjjfkbf";
const char kLargeThemePath[] = "extensions/theme2.crx";

// Get History Information.
class DownloadsHistoryDataCollector {
 public:
  explicit DownloadsHistoryDataCollector(Profile* profile)
      : profile_(profile), result_valid_(false) {}

  bool WaitForDownloadInfo(
      std::unique_ptr<std::vector<history::DownloadRow>>* results) {
    history::HistoryService* hs = HistoryServiceFactory::GetForProfile(
        profile_, ServiceAccessType::EXPLICIT_ACCESS);
    DCHECK(hs);
    hs->QueryDownloads(
        base::Bind(&DownloadsHistoryDataCollector::OnQueryDownloadsComplete,
                   base::Unretained(this)));

    content::RunMessageLoop();
    if (result_valid_) {
      *results = std::move(results_);
    }
    return result_valid_;
  }

 private:
  void OnQueryDownloadsComplete(
      std::unique_ptr<std::vector<history::DownloadRow>> entries) {
    result_valid_ = true;
    results_ = std::move(entries);
    base::RunLoop::QuitCurrentWhenIdleDeprecated();
  }

  Profile* profile_;
  std::unique_ptr<std::vector<history::DownloadRow>> results_;
  bool result_valid_;

  DISALLOW_COPY_AND_ASSIGN(DownloadsHistoryDataCollector);
};

static DownloadManager* DownloadManagerForBrowser(Browser* browser) {
  return BrowserContext::GetDownloadManager(browser->profile());
}

bool WasAutoOpened(DownloadItem* item) {
  return item->GetAutoOpened();
}

#if !defined(OS_CHROMEOS)
// Called when a download starts. Marks the download as hidden.
void SetHiddenDownloadCallback(DownloadItem* item,
                               content::DownloadInterruptReason reason) {
  DownloadItemModel(item).SetShouldShowInShelf(false);
}
#endif

// Callback for HistoryObserver; used in DownloadHistoryCheck
bool HasDataAndName(const history::DownloadRow& row) {
  return row.received_bytes > 0 && !row.target_path.empty();
}

}  // namespace

DownloadTestObserverNotInProgress::DownloadTestObserverNotInProgress(
    DownloadManager* download_manager,
    size_t count)
    : DownloadTestObserver(download_manager, count, ON_DANGEROUS_DOWNLOAD_FAIL),
      started_observing_(false) {
  Init();
}

DownloadTestObserverNotInProgress::~DownloadTestObserverNotInProgress() {}

void DownloadTestObserverNotInProgress::StartObserving() {
  started_observing_ = true;
}

bool DownloadTestObserverNotInProgress::IsDownloadInFinalState(
    DownloadItem* download) {
  return started_observing_ &&
         download->GetState() != DownloadItem::IN_PROGRESS;
}

class HistoryObserver : public DownloadHistory::Observer {
 public:
  typedef base::Callback<bool(const history::DownloadRow&)> FilterCallback;

  explicit HistoryObserver(Profile* profile)
      : profile_(profile),
        waiting_(false),
        seen_stored_(false) {
    DownloadCoreServiceFactory::GetForBrowserContext(profile_)
        ->GetDownloadHistory()
        ->AddObserver(this);
  }

  ~HistoryObserver() override {
    DownloadCoreService* service =
        DownloadCoreServiceFactory::GetForBrowserContext(profile_);
    if (service && service->GetDownloadHistory())
      service->GetDownloadHistory()->RemoveObserver(this);
  }

  void SetFilterCallback(const FilterCallback& callback) {
    callback_ = callback;
  }

  void OnDownloadStored(content::DownloadItem* item,
                        const history::DownloadRow& info) override {
    if (!callback_.is_null() && (!callback_.Run(info)))
        return;

    seen_stored_ = true;
    if (waiting_)
      base::RunLoop::QuitCurrentWhenIdleDeprecated();
  }

  void OnDownloadHistoryDestroyed() override {
    DownloadCoreServiceFactory::GetForBrowserContext(profile_)
        ->GetDownloadHistory()
        ->RemoveObserver(this);
  }

  void WaitForStored() {
    if (seen_stored_)
      return;
    waiting_ = true;
    content::RunMessageLoop();
    waiting_ = false;
  }

 private:
  Profile* profile_;
  bool waiting_;
  bool seen_stored_;
  FilterCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(HistoryObserver);
};

class DownloadTest : public InProcessBrowserTest {
 public:
  // Choice of navigation or direct fetch.  Used by |DownloadFileCheckErrors()|.
  enum DownloadMethod {
    DOWNLOAD_NAVIGATE,
    DOWNLOAD_DIRECT
  };

  // Information passed in to |DownloadFileCheckErrors()|.
  struct DownloadInfo {
    const char* starting_url;           // URL for initiating the download.
    const char* expected_download_url;  // Expected value of DI::GetURL(). Can
                                        // be different if |starting_url|
                                        // initiates a download from another
                                        // URL.
    DownloadMethod download_method;     // Navigation or Direct.
    // Download interrupt reason (NONE is OK).
    content::DownloadInterruptReason reason;
    bool show_download_item;  // True if the download item appears on the shelf.
    bool should_redirect_to_documents;  // True if we save it in "My Documents".
  };

  struct FileErrorInjectInfo {
    DownloadInfo download_info;
    content::TestFileErrorInjector::FileErrorInfo error_info;
  };

  DownloadTest() {}

  void SetUpOnMainThread() override {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::BindOnce(&chrome_browser_net::SetUrlRequestMocksEnabled, true));
    ASSERT_TRUE(InitialSetup());
    host_resolver()->AddRule("www.a.com", "127.0.0.1");
  }

  void TearDownOnMainThread() override {
    // Needs to be torn down on the main thread. file_activity_observer_ holds a
    // reference to the ChromeDownloadManagerDelegate which should be destroyed
    // on the UI thread.
    file_activity_observer_.reset();
  }

  // Returning false indicates a failure of the setup, and should be asserted
  // in the caller.
  bool InitialSetup() {
    bool have_test_dir = PathService::Get(chrome::DIR_TEST_DATA, &test_dir_);
    EXPECT_TRUE(have_test_dir);
    if (!have_test_dir)
      return false;

    // Sanity check default values for window and tab count.
    int window_count = chrome::GetTotalBrowserCount();
    EXPECT_EQ(1, window_count);
    EXPECT_EQ(1, browser()->tab_strip_model()->count());

    // Set up the temporary download folder.
    bool created_downloads_dir = CreateAndSetDownloadsDirectory(browser());
    EXPECT_TRUE(created_downloads_dir);
    if (!created_downloads_dir)
      return false;
    browser()->profile()->GetPrefs()->SetBoolean(
        prefs::kPromptForDownload, false);

    DownloadManager* manager = DownloadManagerForBrowser(browser());
    DownloadPrefs::FromDownloadManager(manager)->ResetAutoOpen();

    file_activity_observer_.reset(
        new DownloadTestFileActivityObserver(browser()->profile()));

    return true;
  }

 protected:
  enum SizeTestType {
    SIZE_TEST_TYPE_KNOWN,
    SIZE_TEST_TYPE_UNKNOWN,
  };

  base::FilePath GetTestDataDirectory() {
    base::FilePath test_file_directory;
    PathService::Get(chrome::DIR_TEST_DATA, &test_file_directory);
    return test_file_directory;
  }

  base::FilePath GetDownloadsDirectory() {
    return downloads_directory_.GetPath();
  }

  // Location of the file source (the place from which it is downloaded).
  base::FilePath OriginFile(const base::FilePath& file) {
    return test_dir_.Append(file);
  }

  // Location of the file destination (place to which it is downloaded).
  base::FilePath DestinationFile(Browser* browser, const base::FilePath& file) {
    return GetDownloadDirectory(browser).Append(file.BaseName());
  }

  // Must be called after browser creation.  Creates a temporary
  // directory for downloads that is auto-deleted on destruction.
  // Returning false indicates a failure of the function, and should be asserted
  // in the caller.
  bool CreateAndSetDownloadsDirectory(Browser* browser) {
    if (!browser)
      return false;

    base::ThreadRestrictions::ScopedAllowIO allow_io;
    if (!downloads_directory_.CreateUniqueTempDir())
      return false;

    browser->profile()->GetPrefs()->SetFilePath(
        prefs::kDownloadDefaultDirectory, downloads_directory_.GetPath());
    browser->profile()->GetPrefs()->SetFilePath(
        prefs::kSaveFileDefaultDirectory, downloads_directory_.GetPath());

    return true;
  }

  DownloadPrefs* GetDownloadPrefs(Browser* browser) {
    return DownloadPrefs::FromDownloadManager(
        DownloadManagerForBrowser(browser));
  }

  base::FilePath GetDownloadDirectory(Browser* browser) {
    return GetDownloadPrefs(browser)->DownloadPath();
  }

  // Create a DownloadTestObserverTerminal that will wait for the
  // specified number of downloads to finish.
  content::DownloadTestObserver* CreateWaiter(
      Browser* browser, int num_downloads) {
    DownloadManager* download_manager = DownloadManagerForBrowser(browser);
    return new content::DownloadTestObserverTerminal(
        download_manager, num_downloads,
        content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);
  }

  // Create a DownloadTestObserverInProgress that will wait for the
  // specified number of downloads to start.
  content::DownloadTestObserver* CreateInProgressWaiter(
      Browser* browser, int num_downloads) {
    DownloadManager* download_manager = DownloadManagerForBrowser(browser);
    return new content::DownloadTestObserverInProgress(
        download_manager, num_downloads);
  }

  // Create a DownloadTestObserverTerminal that will wait for the
  // specified number of downloads to finish, or for
  // a dangerous download warning to be shown.
  content::DownloadTestObserver* DangerousDownloadWaiter(
      Browser* browser,
      int num_downloads,
      content::DownloadTestObserver::DangerousDownloadAction
          dangerous_download_action) {
    DownloadManager* download_manager = DownloadManagerForBrowser(browser);
    return new content::DownloadTestObserverTerminal(
        download_manager, num_downloads, dangerous_download_action);
  }

  void CheckDownloadStatesForBrowser(Browser* browser,
                                     size_t num,
                                     DownloadItem::DownloadState state) {
    std::vector<DownloadItem*> download_items;
    GetDownloads(browser, &download_items);

    EXPECT_EQ(num, download_items.size());

    for (size_t i = 0; i < download_items.size(); ++i) {
      EXPECT_EQ(state, download_items[i]->GetState()) << " Item " << i;
    }
  }

  void CheckDownloadStates(size_t num, DownloadItem::DownloadState state) {
    CheckDownloadStatesForBrowser(browser(), num, state);
  }

  bool VerifyNoDownloads() const {
    DownloadManager::DownloadVector items;
    GetDownloads(browser(), &items);
    return items.empty();
  }

  // Download |url|, then wait for the download to finish.
  // |disposition| indicates where the navigation occurs (current tab, new
  // foreground tab, etc).
  // |browser_test_flags| indicate what to wait for, and is an OR of 0 or more
  // values in the ui_test_utils::BrowserTestWaitFlags enum.
  void DownloadAndWaitWithDisposition(Browser* browser,
                                      const GURL& url,
                                      WindowOpenDisposition disposition,
                                      int browser_test_flags) {
    // Setup notification, navigate, and block.
    std::unique_ptr<content::DownloadTestObserver> observer(
        CreateWaiter(browser, 1));
    // This call will block until the condition specified by
    // |browser_test_flags|, but will not wait for the download to finish.
    ui_test_utils::NavigateToURLWithDisposition(browser,
                                                url,
                                                disposition,
                                                browser_test_flags);
    // Waits for the download to complete.
    observer->WaitForFinished();
    EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
    // We don't expect a file chooser to be shown.
    EXPECT_FALSE(DidShowFileChooser());
  }

  // Download a file in the current tab, then wait for the download to finish.
  void DownloadAndWait(Browser* browser,
                       const GURL& url) {
    DownloadAndWaitWithDisposition(
        browser, url, WindowOpenDisposition::CURRENT_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  }

  // Should only be called when the download is known to have finished
  // (in error or not).
  // Returning false indicates a failure of the function, and should be asserted
  // in the caller.
  bool CheckDownload(Browser* browser,
                     const base::FilePath& downloaded_filename,
                     const base::FilePath& origin_filename) {
    // Find the path to which the data will be downloaded.
    base::FilePath downloaded_file(
        DestinationFile(browser, downloaded_filename));

    // Find the origin path (from which the data comes).
    base::FilePath origin_file(OriginFile(origin_filename));
    return CheckDownloadFullPaths(browser, downloaded_file, origin_file);
  }

  // A version of CheckDownload that allows complete path specification.
  bool CheckDownloadFullPaths(Browser* browser,
                              const base::FilePath& downloaded_file,
                              const base::FilePath& origin_file) {
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    bool origin_file_exists = base::PathExists(origin_file);
    EXPECT_TRUE(origin_file_exists) << origin_file.value();
    if (!origin_file_exists)
      return false;

    // Confirm the downloaded data file exists.
    bool downloaded_file_exists = base::PathExists(downloaded_file);
    EXPECT_TRUE(downloaded_file_exists) << downloaded_file.value();
    if (!downloaded_file_exists)
      return false;

    int64_t origin_file_size = 0;
    EXPECT_TRUE(base::GetFileSize(origin_file, &origin_file_size));
    std::string original_file_contents;
    EXPECT_TRUE(base::ReadFileToString(origin_file, &original_file_contents));
    EXPECT_TRUE(
        VerifyFile(downloaded_file, original_file_contents, origin_file_size));

    // Delete the downloaded copy of the file.
    bool downloaded_file_deleted = base::DieFileDie(downloaded_file, false);
    EXPECT_TRUE(downloaded_file_deleted);
    return downloaded_file_deleted;
  }

  content::DownloadTestObserver* CreateInProgressDownloadObserver(
      size_t download_count) {
    DownloadManager* manager = DownloadManagerForBrowser(browser());
    return new content::DownloadTestObserverInProgress(
        manager, download_count);
  }

  DownloadItem* CreateSlowTestDownload() {
    std::unique_ptr<content::DownloadTestObserver> observer(
        CreateInProgressDownloadObserver(1));
    GURL slow_download_url(net::URLRequestSlowDownloadJob::kUnknownSizeUrl);
    DownloadManager* manager = DownloadManagerForBrowser(browser());

    EXPECT_EQ(0, manager->NonMaliciousInProgressCount());
    EXPECT_EQ(0, manager->InProgressCount());
    if (manager->InProgressCount() != 0)
      return NULL;

    ui_test_utils::NavigateToURL(browser(), slow_download_url);

    observer->WaitForFinished();
    EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::IN_PROGRESS));

    DownloadManager::DownloadVector items;
    manager->GetAllDownloads(&items);

    DownloadItem* new_item = NULL;
    for (DownloadManager::DownloadVector::iterator iter = items.begin();
         iter != items.end(); ++iter) {
      if ((*iter)->GetState() == DownloadItem::IN_PROGRESS) {
        // There should be only one IN_PROGRESS item.
        EXPECT_EQ(NULL, new_item);
        new_item = *iter;
      }
    }
    return new_item;
  }

  bool RunSizeTest(Browser* browser,
                   SizeTestType type,
                   const std::string& partial_indication,
                   const std::string& total_indication) {
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    EXPECT_TRUE(type == SIZE_TEST_TYPE_UNKNOWN || type == SIZE_TEST_TYPE_KNOWN);
    if (type != SIZE_TEST_TYPE_KNOWN && type != SIZE_TEST_TYPE_UNKNOWN)
      return false;
    GURL url(type == SIZE_TEST_TYPE_KNOWN ?
             net::URLRequestSlowDownloadJob::kKnownSizeUrl :
             net::URLRequestSlowDownloadJob::kUnknownSizeUrl);

    // TODO(ahendrickson) -- |expected_title_in_progress| and
    // |expected_title_finished| need to be checked.
    base::FilePath filename;
    net::FileURLToFilePath(url, &filename);
    base::string16 expected_title_in_progress(
        base::ASCIIToUTF16(partial_indication) + filename.LossyDisplayName());
    base::string16 expected_title_finished(
        base::ASCIIToUTF16(total_indication) + filename.LossyDisplayName());

    // Download a partial web page in a background tab and wait.
    // The mock system will not complete until it gets a special URL.
    std::unique_ptr<content::DownloadTestObserver> observer(
        CreateWaiter(browser, 1));
    ui_test_utils::NavigateToURL(browser, url);

    // TODO(ahendrickson): check download status text before downloading.
    // Need to:
    //  - Add a member function to the |DownloadShelf| interface class, that
    //    indicates how many members it has.
    //  - Add a member function to |DownloadShelf| to get the status text
    //    of a given member (for example, via the name in |DownloadItemView|'s
    //    GetAccessibleNodeData() member function), by index.
    //  - Iterate over browser->window()->GetDownloadShelf()'s members
    //    to see if any match the status text we want.  Start with the last one.

    // Allow the request to finish.  We do this by loading a second URL in a
    // separate tab.
    GURL finish_url(net::URLRequestSlowDownloadJob::kFinishDownloadUrl);
    ui_test_utils::NavigateToURLWithDisposition(
        browser, finish_url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
    observer->WaitForFinished();
    EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
    CheckDownloadStatesForBrowser(browser, 1, DownloadItem::COMPLETE);

    EXPECT_EQ(2, browser->tab_strip_model()->count());

    // TODO(ahendrickson): check download status text after downloading.

    base::FilePath basefilename(filename.BaseName());
    net::FileURLToFilePath(url, &filename);
    base::FilePath download_path =
        downloads_directory_.GetPath().Append(basefilename);

    bool downloaded_path_exists = base::PathExists(download_path);
    EXPECT_TRUE(downloaded_path_exists);
    if (!downloaded_path_exists)
      return false;

    // Check the file contents.
    size_t file_size = net::URLRequestSlowDownloadJob::kFirstDownloadSize +
                       net::URLRequestSlowDownloadJob::kSecondDownloadSize;
    std::string expected_contents(file_size, '*');
    EXPECT_TRUE(VerifyFile(download_path, expected_contents, file_size));

    // Delete the file we just downloaded.
    EXPECT_TRUE(base::DieFileDie(download_path, false));
    EXPECT_FALSE(base::PathExists(download_path));

    return true;
  }

  void GetDownloads(Browser* browser,
                    std::vector<DownloadItem*>* downloads) const {
    DCHECK(downloads);
    DownloadManager* manager = DownloadManagerForBrowser(browser);
    manager->GetAllDownloads(downloads);
  }

  static void ExpectWindowCountAfterDownload(size_t expected) {
    EXPECT_EQ(expected, chrome::GetTotalBrowserCount());
  }

  void EnableFileChooser(bool enable) {
    file_activity_observer_->EnableFileChooser(enable);
  }

  bool DidShowFileChooser() {
    return file_activity_observer_->TestAndResetDidShowFileChooser();
  }

  // Checks that |path| is has |file_size| bytes, and matches the |value|
  // string.
  bool VerifyFile(const base::FilePath& path,
                  const std::string& value,
                  const int64_t file_size) {
    std::string file_contents;

    base::ThreadRestrictions::ScopedAllowIO allow_io;
    bool read = base::ReadFileToString(path, &file_contents);
    EXPECT_TRUE(read) << "Failed reading file: " << path.value() << std::endl;
    if (!read)
      return false;  // Couldn't read the file.

    // Note: we don't handle really large files (more than size_t can hold)
    // so we will fail in that case.
    size_t expected_size = static_cast<size_t>(file_size);

    // Check the size.
    EXPECT_EQ(expected_size, file_contents.size());
    if (expected_size != file_contents.size())
      return false;

    // Check the contents.
    EXPECT_EQ(value, file_contents);
    if (memcmp(file_contents.c_str(), value.c_str(), expected_size) != 0)
      return false;

    return true;
  }

  // Attempts to download a file, based on information in |download_info|.
  // If a Select File dialog opens, will automatically choose the default.
  void DownloadFilesCheckErrorsSetup() {
    embedded_test_server()->ServeFilesFromDirectory(GetTestDataDirectory());
    ASSERT_TRUE(embedded_test_server()->Start());
    std::vector<DownloadItem*> download_items;
    GetDownloads(browser(), &download_items);
    ASSERT_TRUE(download_items.empty());

    EnableFileChooser(true);
  }

  void DownloadFilesCheckErrorsLoopBody(const DownloadInfo& download_info,
                                        size_t i) {
    SCOPED_TRACE(testing::Message()
                 << " " << __FUNCTION__ << "()"
                 << " index = " << i << " starting_url = '"
                 << download_info.starting_url << "'"
                 << " download_url = '" << download_info.expected_download_url
                 << "'"
                 << " method = "
                 << ((download_info.download_method == DOWNLOAD_DIRECT)
                         ? "DOWNLOAD_DIRECT"
                         : "DOWNLOAD_NAVIGATE")
                 << " show_item = " << download_info.show_download_item
                 << " reason = "
                 << DownloadInterruptReasonToString(download_info.reason));

    std::vector<DownloadItem*> download_items;
    GetDownloads(browser(), &download_items);
    size_t downloads_expected = download_items.size();

    // GURL("http://foo/bar").Resolve("baz") => "http://foo/bar/baz"
    // GURL("http://foo/bar").Resolve("http://baz") => "http://baz"
    // I.e. both starting_url and expected_download_url can either be relative
    // to the base test server URL or be an absolute URL.
    GURL base_url = embedded_test_server()->GetURL("/downloads/");
    GURL starting_url = base_url.Resolve(download_info.starting_url);
    GURL download_url = base_url.Resolve(download_info.expected_download_url);
    ASSERT_TRUE(starting_url.is_valid());
    ASSERT_TRUE(download_url.is_valid());

    DownloadManager* download_manager = DownloadManagerForBrowser(browser());
    WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(web_contents);

    std::unique_ptr<content::DownloadTestObserver> observer;
    if (download_info.reason == content::DOWNLOAD_INTERRUPT_REASON_NONE) {
      observer.reset(new content::DownloadTestObserverTerminal(
          download_manager, 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));
    } else {
      observer.reset(new content::DownloadTestObserverInterrupted(
          download_manager, 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));
    }

    if (download_info.download_method == DOWNLOAD_DIRECT) {
      // Go directly to download.  Don't wait for navigation.
      scoped_refptr<content::DownloadTestItemCreationObserver>
          creation_observer(new content::DownloadTestItemCreationObserver);

      std::unique_ptr<DownloadUrlParameters> params(
          DownloadUrlParameters::CreateForWebContentsMainFrame(
              web_contents, starting_url, TRAFFIC_ANNOTATION_FOR_TESTS));
      params->set_callback(creation_observer->callback());
      DownloadManagerForBrowser(browser())->DownloadUrl(std::move(params));

      // Wait until the item is created, or we have determined that it
      // won't be.
      creation_observer->WaitForDownloadItemCreation();

      EXPECT_NE(content::DownloadItem::kInvalidId,
                creation_observer->download_id());
    } else {
      // Navigate to URL normally, wait until done.
      ui_test_utils::NavigateToURLBlockUntilNavigationsComplete(
          browser(), starting_url, 1);
    }

    if (download_info.show_download_item) {
      downloads_expected++;
      observer->WaitForFinished();
      DownloadItem::DownloadState final_state =
          (download_info.reason == content::DOWNLOAD_INTERRUPT_REASON_NONE) ?
              DownloadItem::COMPLETE :
              DownloadItem::INTERRUPTED;
      EXPECT_EQ(1u, observer->NumDownloadsSeenInState(final_state));
    }

    // Wait till the |DownloadFile|s are destroyed.
    content::RunAllTasksUntilIdle();

    // Validate that the correct files were downloaded.
    download_items.clear();
    GetDownloads(browser(), &download_items);
    ASSERT_EQ(downloads_expected, download_items.size());

    if (download_info.show_download_item) {
      // Find the last download item.
      DownloadItem* item = download_items[0];
      for (size_t d = 1; d < downloads_expected; ++d) {
        if (download_items[d]->GetStartTime() > item->GetStartTime())
          item = download_items[d];
      }

      EXPECT_EQ(download_url, item->GetURL());
      EXPECT_EQ(download_info.reason, item->GetLastReason());

      if (item->GetState() == content::DownloadItem::COMPLETE) {
        // Clean up the file, in case it ended up in the My Documents folder.
        base::ThreadRestrictions::ScopedAllowIO allow_io;
        base::FilePath destination_folder = GetDownloadDirectory(browser());
        base::FilePath my_downloaded_file = item->GetTargetFilePath();
        EXPECT_TRUE(base::PathExists(my_downloaded_file));
        EXPECT_TRUE(base::DeleteFile(my_downloaded_file, false));

        EXPECT_EQ(download_info.should_redirect_to_documents ?
                      std::string::npos :
                      0u,
                  my_downloaded_file.value().find(destination_folder.value()));
        if (download_info.should_redirect_to_documents) {
          // If it's not where we asked it to be, it should be in the
          // My Documents folder.
          base::FilePath my_docs_folder;
          EXPECT_TRUE(PathService::Get(chrome::DIR_USER_DOCUMENTS,
                                       &my_docs_folder));
          EXPECT_EQ(0u,
                    my_downloaded_file.value().find(my_docs_folder.value()));
        }
      }
    }
  }

  // Attempts to download a set of files, based on information in the
  // |download_info| array.  |count| is the number of files.
  // If a Select File dialog appears, it will choose the default and return
  // immediately.
  void DownloadFilesCheckErrors(size_t count, DownloadInfo* download_info) {
    DownloadFilesCheckErrorsSetup();

    for (size_t i = 0; i < count; ++i) {
      DownloadFilesCheckErrorsLoopBody(download_info[i], i);
    }
  }

  void DownloadInsertFilesErrorCheckErrorsLoopBody(
      scoped_refptr<content::TestFileErrorInjector> injector,
      const FileErrorInjectInfo& info,
      size_t i) {
    SCOPED_TRACE(
        ::testing::Message()
        << " " << __FUNCTION__ << "()"
        << " index = " << i << " operation code = "
        << content::TestFileErrorInjector::DebugString(info.error_info.code)
        << " instance = " << info.error_info.operation_instance << " error = "
        << content::DownloadInterruptReasonToString(info.error_info.error));

    injector->InjectError(info.error_info);

    DownloadFilesCheckErrorsLoopBody(info.download_info, i);

    size_t expected_successes = info.download_info.show_download_item ? 1u : 0u;
    EXPECT_EQ(expected_successes, injector->TotalFileCount());
    EXPECT_EQ(0u, injector->CurrentFileCount());
  }

  void DownloadInsertFilesErrorCheckErrors(size_t count,
                                           FileErrorInjectInfo* info) {
    DownloadFilesCheckErrorsSetup();

    // Set up file failures.
    scoped_refptr<content::TestFileErrorInjector> injector(
        content::TestFileErrorInjector::Create(
            DownloadManagerForBrowser(browser())));

    for (size_t i = 0; i < count; ++i) {
      DownloadInsertFilesErrorCheckErrorsLoopBody(injector, info[i], i);
    }
  }

  // Attempts to download a file to a read-only folder, based on information
  // in |download_info|.
  void DownloadFilesToReadonlyFolder(size_t count,
                                     DownloadInfo* download_info) {
    DownloadFilesCheckErrorsSetup();

    // Make the test folder unwritable.
    base::FilePath destination_folder = GetDownloadDirectory(browser());
    DVLOG(1) << " " << __FUNCTION__ << "()"
             << " folder = '" << destination_folder.value() << "'";
    base::FilePermissionRestorer permission_restorer(destination_folder);
    EXPECT_TRUE(base::MakeFileUnwritable(destination_folder));

    for (size_t i = 0; i < count; ++i) {
      DownloadFilesCheckErrorsLoopBody(download_info[i], i);
    }
  }

  // This method:
  // * Starts a mock download by navigating browser() to a URLRequestMockHTTPJob
  //   mock URL.
  // * Injects |error| on the first write using |error_injector|.
  // * Waits for the download to be interrupted.
  // * Clears the errors on |error_injector|.
  // * Returns the resulting interrupted download.
  DownloadItem* StartMockDownloadAndInjectError(
      content::TestFileErrorInjector* error_injector,
      content::DownloadInterruptReason error) {
    content::TestFileErrorInjector::FileErrorInfo error_info;
    error_info.code = content::TestFileErrorInjector::FILE_OPERATION_WRITE;
    error_info.operation_instance = 0;
    error_info.error = error;
    error_injector->InjectError(error_info);

    std::unique_ptr<content::DownloadTestObserver> observer(
        new DownloadTestObserverResumable(DownloadManagerForBrowser(browser()),
                                          1));

    GURL url = URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path);
    ui_test_utils::NavigateToURL(browser(), url);
    observer->WaitForFinished();

    content::DownloadManager::DownloadVector downloads;
    DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
    EXPECT_EQ(1u, downloads.size());

    if (downloads.size() != 1)
      return NULL;

    error_injector->ClearError();
    DownloadItem* download = downloads[0];
    EXPECT_EQ(DownloadItem::INTERRUPTED, download->GetState());
    EXPECT_EQ(error, download->GetLastReason());
    return download;
  }

 private:
  static void EnsureNoPendingDownloadJobsOnIO(bool* result) {
    if (net::URLRequestSlowDownloadJob::NumberOutstandingRequests())
      *result = false;
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            base::MessageLoop::QuitWhenIdleClosure());
  }

  // Location of the test data.
  base::FilePath test_dir_;

  // Location of the downloads directory for these tests
  base::ScopedTempDir downloads_directory_;

  std::unique_ptr<DownloadTestFileActivityObserver> file_activity_observer_;
  extensions::ScopedIgnoreContentVerifierForTest ignore_content_verifier_;
};

namespace {

class FakeDownloadProtectionService
    : public safe_browsing::DownloadProtectionService {
 public:
  FakeDownloadProtectionService()
      : safe_browsing::DownloadProtectionService(nullptr) {}

  void CheckClientDownload(
      DownloadItem* download_item,
      const safe_browsing::CheckDownloadCallback& callback) override {
    callback.Run(safe_browsing::DownloadCheckResult::UNCOMMON);
  }
};

#if 0
class FakeSafeBrowsingService
    : public safe_browsing::TestSafeBrowsingService,
      public safe_browsing::ServicesDelegate::ServicesCreator {
 public:
  FakeSafeBrowsingService()
      : TestSafeBrowsingService(
            safe_browsing::V4FeatureList::V4UsageStatus::V4_DISABLED) {
    services_delegate_ =
        safe_browsing::ServicesDelegate::CreateForTest(this, this);
  }

 protected:
  ~FakeSafeBrowsingService() override {}

  // ServicesDelegate::ServicesCreator:
  bool CanCreateDownloadProtectionService() override { return true; }
  bool CanCreateIncidentReportingService() override { return false; }
  bool CanCreateResourceRequestDetector() override { return false; }
  safe_browsing::DownloadProtectionService* CreateDownloadProtectionService()
      override {
    return new FakeDownloadProtectionService();
  }
  safe_browsing::IncidentReportingService* CreateIncidentReportingService()
      override {
    NOTREACHED();
    return nullptr;
  }
  safe_browsing::ResourceRequestDetector* CreateResourceRequestDetector()
      override {
    NOTREACHED();
    return nullptr;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeSafeBrowsingService);
};

// Factory that creates FakeSafeBrowsingService instances.
class TestSafeBrowsingServiceFactory
    : public safe_browsing::SafeBrowsingServiceFactory {
 public:
  TestSafeBrowsingServiceFactory() : fake_safe_browsing_service_(nullptr) {}
  ~TestSafeBrowsingServiceFactory() override {}

  safe_browsing::SafeBrowsingService* CreateSafeBrowsingService() override {
    DCHECK(!fake_safe_browsing_service_);
    fake_safe_browsing_service_ = new FakeSafeBrowsingService();
    return fake_safe_browsing_service_.get();
  }

  scoped_refptr<FakeSafeBrowsingService> fake_safe_browsing_service() {
    return fake_safe_browsing_service_;
  }

 private:
  scoped_refptr<FakeSafeBrowsingService> fake_safe_browsing_service_;
};

class DownloadTestWithFakeSafeBrowsing : public DownloadTest {
 public:
  DownloadTestWithFakeSafeBrowsing()
      : test_safe_browsing_factory_(new TestSafeBrowsingServiceFactory()) {}

  void SetUp() override {
    safe_browsing::SafeBrowsingService::RegisterFactory(
        test_safe_browsing_factory_.get());
    DownloadTest::SetUp();
  }

  void TearDown() override {
    safe_browsing::SafeBrowsingService::RegisterFactory(nullptr);
    DownloadTest::TearDown();
  }

 protected:
  std::unique_ptr<TestSafeBrowsingServiceFactory> test_safe_browsing_factory_;
};

#endif
}  // namespace

// NOTES:
//
// Files for these tests are found in DIR_TEST_DATA (currently
// "chrome\test\data\", see chrome_paths.cc).
// Mock responses have extension .mock-http-headers appended to the file name.

// Download a file due to the associated MIME type.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadMimeType) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));

  // Download the file and wait.  We do not expect the Select File dialog.
  DownloadAndWait(browser(), url);

  // Check state.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  CheckDownload(browser(), file, file);
}

#if defined(OS_WIN) || defined(OS_LINUX)
// Download a file and confirm that the file is correctly quarantined.
//
// TODO(asanka): We should enable the test on Mac as well, but currently
// |browser_tests| aren't run from a process that has LSFileQuarantineEnabled
// bit set.
IN_PROC_BROWSER_TEST_F(DownloadTest, Quarantine_DependsOnLocalConfig) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));

  // Download the file and wait.  We do not expect the Select File dialog.
  DownloadAndWait(browser(), url);

  // Check state.  Special file state must be checked before CheckDownload,
  // as CheckDownload will delete the output file.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  base::FilePath downloaded_file(DestinationFile(browser(), file));
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  EXPECT_TRUE(content::IsFileQuarantined(downloaded_file, url, GURL()));
  CheckDownload(browser(), file, file);
}
#endif

#if defined(OS_WIN)
// A couple of Windows specific tests to make sure we respect OS specific
// restrictions on Mark-Of-The-Web can be applied. While Chrome doesn't directly
// apply these policies, Chrome still needs to make sure the correct APIs are
// invoked during the download process that result in the expected MOTW
// behavior.

// Downloading a file from the local host shouldn't cause the application of a
// zone identifier.
IN_PROC_BROWSER_TEST_F(DownloadTest, CheckLocalhostZone_DependsOnLocalConfig) {
  embedded_test_server()->ServeFilesFromDirectory(GetTestDataDirectory());
  ASSERT_TRUE(embedded_test_server()->Start());

  // Assumes that localhost maps to 127.0.0.1. Otherwise the test will fail
  // since EmbeddedTestServer is listening on that address.
  GURL url =
      embedded_test_server()->GetURL("localhost", "/downloads/a_zip_file.zip");
  DownloadAndWait(browser(), url);
  base::FilePath file(FILE_PATH_LITERAL("a_zip_file.zip"));
  base::FilePath downloaded_file(DestinationFile(browser(), file));
  EXPECT_FALSE(content::IsFileQuarantined(downloaded_file, GURL(), GURL()));
}

// Same as the test above, but uses a file:// URL to a local file.
IN_PROC_BROWSER_TEST_F(DownloadTest, CheckLocalFileZone_DependsOnLocalConfig) {
  base::FilePath source_file = GetTestDataDirectory()
                                   .AppendASCII("downloads")
                                   .AppendASCII("a_zip_file.zip");

  GURL url = net::FilePathToFileURL(source_file);
  DownloadAndWait(browser(), url);
  base::FilePath file(FILE_PATH_LITERAL("a_zip_file.zip"));
  base::FilePath downloaded_file(DestinationFile(browser(), file));
  EXPECT_FALSE(content::IsFileQuarantined(downloaded_file, GURL(), GURL()));
}
#endif

// Put up a Select File dialog when the file is downloaded, due to
// downloads preferences settings.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadMimeTypeSelect) {
  // Re-enable prompting.
  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kPromptForDownload, true);
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));

  EnableFileChooser(true);

  // Download the file and wait.  We expect the Select File dialog to appear
  // due to the MIME type, but we still wait until the download completes.
  std::unique_ptr<content::DownloadTestObserver> observer(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));
  ui_test_utils::NavigateToURL(browser(), url);
  observer->WaitForFinished();
  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);
  EXPECT_TRUE(DidShowFileChooser());

  // Check state.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  CheckDownload(browser(), file, file);
}

// Access a file with a viewable mime-type, verify that a download
// did not initiate.
IN_PROC_BROWSER_TEST_F(DownloadTest, NoDownload) {
  base::FilePath file(FILE_PATH_LITERAL("download-test2.html"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl("download-test2.html"));
  base::FilePath file_path(DestinationFile(browser(), file));

  // Open a web page and wait.
  ui_test_utils::NavigateToURL(browser(), url);

  // Check that we did not download the web page.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  EXPECT_FALSE(base::PathExists(file_path));

  // Check state.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  EXPECT_TRUE(VerifyNoDownloads());
}

// EmbeddedTestServer::HandleRequestCallback function that returns the relative
// URL as the MIME type.
// E.g.:
//   C -> S: GET /foo/bar =>
//   S -> C: HTTP/1.1 200 OK
//           Content-Type: foo/bar
//           ...
static std::unique_ptr<net::test_server::HttpResponse>
RespondWithContentTypeHandler(const net::test_server::HttpRequest& request) {
  std::unique_ptr<net::test_server::BasicHttpResponse> response(
      new net::test_server::BasicHttpResponse());
  response->set_content_type(request.relative_url.substr(1));
  response->set_code(net::HTTP_OK);
  response->set_content("ooogaboogaboogabooga");
  return std::move(response);
}

IN_PROC_BROWSER_TEST_F(DownloadTest, MimeTypesToShowNotDownload) {
  embedded_test_server()->RegisterRequestHandler(
      base::Bind(&RespondWithContentTypeHandler));
  ASSERT_TRUE(embedded_test_server()->Start());

  // These files should all be displayed in the browser.
  const char* mime_types[] = {
    // It is unclear whether to display text/css or download it.
    //   Firefox 3: Display
    //   Internet Explorer 7: Download
    //   Safari 3.2: Download
    // We choose to match Firefox due to the lot of complains
    // from the users if css files are downloaded:
    // http://code.google.com/p/chromium/issues/detail?id=7192
    "text/css",
    "text/javascript",
    "text/plain",
    "application/x-javascript",
    "text/html",
    "text/xml",
    "text/xsl",
    "application/xhtml+xml",
    "image/png",
    "image/gif",
    "image/jpeg",
    "image/bmp",
  };
  for (size_t i = 0; i < arraysize(mime_types); ++i) {
    const char* mime_type = mime_types[i];
    GURL url(
        embedded_test_server()->GetURL(std::string("/").append(mime_type)));
    ui_test_utils::NavigateToURL(browser(), url);

    // Check state.
    EXPECT_EQ(1, browser()->tab_strip_model()->count());
    EXPECT_TRUE(VerifyNoDownloads());
  }
}

// Verify that when the DownloadResourceThrottle cancels a download, the
// download never makes it to the downloads system.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadResourceThrottleCancels) {
  // Navigate to a page with the same domain as the file to download.  We can't
  // navigate directly to the file we don't want to download because cross-site
  // navigations reset the TabDownloadState.
  GURL same_site_url(URLRequestMockHTTPJob::GetMockUrl("download_script.html"));
  ui_test_utils::NavigateToURL(browser(), same_site_url);

  // Make sure the initial navigation didn't trigger a download.
  EXPECT_TRUE(VerifyNoDownloads());

  // Disable downloads for the tab.
  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  DownloadRequestLimiter::TabDownloadState* tab_download_state =
      g_browser_process->download_request_limiter()->GetDownloadState(
          web_contents, web_contents, true);
  ASSERT_TRUE(tab_download_state);
  tab_download_state->SetDownloadStatusAndNotify(
      DownloadRequestLimiter::DOWNLOADS_NOT_ALLOWED);

  // Try to start the download via Javascript and wait for the corresponding
  // load stop event.
  content::TestNavigationObserver observer(web_contents);
  bool download_attempted;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      browser()->tab_strip_model()->GetActiveWebContents(),
      "window.domAutomationController.send(startDownload());",
      &download_attempted));
  ASSERT_TRUE(download_attempted);
  observer.Wait();

  // Check that we did not download the file.
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  base::FilePath file_path(DestinationFile(browser(), file));
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  EXPECT_FALSE(base::PathExists(file_path));

  // Check state.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());

  // Verify that there's no pending download.  The resource throttle
  // should have deleted it before it created a download item, so it
  // shouldn't be available as a cancelled download either.
  EXPECT_TRUE(VerifyNoDownloads());
}

// Download a 0-size file with a content-disposition header, verify that the
// download tab opened and the file exists as the filename specified in the
// header.  This also ensures we properly handle empty file downloads.
IN_PROC_BROWSER_TEST_F(DownloadTest, ContentDisposition) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl("download-test3.gif"));
  base::FilePath download_file(
      FILE_PATH_LITERAL("download-test3-attachment.gif"));

  // Download a file and wait.
  DownloadAndWait(browser(), url);

  base::FilePath file(FILE_PATH_LITERAL("download-test3.gif"));
  CheckDownload(browser(), download_file, file);

  // Check state.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
}

// UnknownSize and KnownSize are tests which depend on
// URLRequestSlowDownloadJob to serve content in a certain way. Data will be
// sent in two chunks where the first chunk is 35K and the second chunk is 10K.
// The test will first attempt to download a file; but the server will "pause"
// in the middle until the server receives a second request for
// "download-finish".  At that time, the download will finish.
// These tests don't currently test much due to holes in |RunSizeTest()|.  See
// comments in that routine for details.
IN_PROC_BROWSER_TEST_F(DownloadTest, UnknownSize) {
  ASSERT_TRUE(RunSizeTest(browser(), SIZE_TEST_TYPE_UNKNOWN,
                          "32.0 KB - ", "100% - "));
}

IN_PROC_BROWSER_TEST_F(DownloadTest, KnownSize) {
  ASSERT_TRUE(RunSizeTest(browser(), SIZE_TEST_TYPE_KNOWN,
                          "71% - ", "100% - "));
}

// Test that when downloading an item in Incognito mode, we don't crash when
// closing the last Incognito window (http://crbug.com/13983).
IN_PROC_BROWSER_TEST_F(DownloadTest, IncognitoDownload) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  int window_count = chrome::GetTotalBrowserCount();
  EXPECT_EQ(2, window_count);

  // Download a file in the Incognito window and wait.
  CreateAndSetDownloadsDirectory(incognito);
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));
  // Since |incognito| is a separate browser, we have to set it up explicitly.
  incognito->profile()->GetPrefs()->SetBoolean(prefs::kPromptForDownload,
                                               false);
  DownloadAndWait(incognito, url);

  // We should still have 2 windows.
  ExpectWindowCountAfterDownload(2);

#if !defined(OS_MACOSX)
  // On Mac OS X, the UI window close is delayed until the outermost
  // message loop runs.  So it isn't possible to get a BROWSER_CLOSED
  // notification inside of a test.
  content::WindowedNotificationObserver signal(
      chrome::NOTIFICATION_BROWSER_CLOSED,
      content::Source<Browser>(incognito));
#endif

  // Close the Incognito window and don't crash.
  chrome::CloseWindow(incognito);

#if !defined(OS_MACOSX)
  signal.Wait();
  ExpectWindowCountAfterDownload(1);
#endif

  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  CheckDownload(browser(), file, file);
}

// Download one file on-record, then download the same file off-record, and test
// that the filename is deduplicated.  The previous test tests for a specific
// bug; this next test tests that filename deduplication happens independently
// of DownloadManager/CDMD.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadTest_IncognitoRegular) {
  GURL url = net::URLRequestMockHTTPJob::GetMockUrl("downloads/a_zip_file.zip");

  // Read the origin file now so that we can compare the downloaded files to it
  // later.
  base::FilePath origin(OriginFile(base::FilePath(FILE_PATH_LITERAL(
      "downloads/a_zip_file.zip"))));
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  ASSERT_TRUE(base::PathExists(origin));
  int64_t origin_file_size = 0;
  EXPECT_TRUE(base::GetFileSize(origin, &origin_file_size));
  std::string original_contents;
  EXPECT_TRUE(base::ReadFileToString(origin, &original_contents));

  std::vector<DownloadItem*> download_items;
  GetDownloads(browser(), &download_items);
  ASSERT_TRUE(download_items.empty());

  // Download a file in the on-record browser and check that it was downloaded
  // correctly.
  DownloadAndWaitWithDisposition(browser(), url,
                                 WindowOpenDisposition::CURRENT_TAB,
                                 ui_test_utils::BROWSER_TEST_NONE);
  GetDownloads(browser(), &download_items);
  ASSERT_EQ(1UL, download_items.size());
  ASSERT_EQ(base::FilePath(FILE_PATH_LITERAL("a_zip_file.zip")),
            download_items[0]->GetTargetFilePath().BaseName());
  ASSERT_TRUE(base::PathExists(download_items[0]->GetTargetFilePath()));
  EXPECT_TRUE(VerifyFile(download_items[0]->GetTargetFilePath(),
                         original_contents, origin_file_size));

  // Setup an incognito window.
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  int window_count = BrowserList::GetInstance()->size();
  EXPECT_EQ(2, window_count);
  incognito->profile()->GetPrefs()->SetFilePath(
      prefs::kDownloadDefaultDirectory,
      GetDownloadsDirectory());
  incognito->profile()->GetPrefs()->SetFilePath(
      prefs::kSaveFileDefaultDirectory,
      GetDownloadsDirectory());

  download_items.clear();
  GetDownloads(incognito, &download_items);
  ASSERT_TRUE(download_items.empty());

  // Download a file in the incognito browser and check that it was downloaded
  // correctly.
  DownloadAndWaitWithDisposition(incognito, url,
                                 WindowOpenDisposition::CURRENT_TAB,
                                 ui_test_utils::BROWSER_TEST_NONE);
  GetDownloads(incognito, &download_items);
  ASSERT_EQ(1UL, download_items.size());
  ASSERT_EQ(base::FilePath(FILE_PATH_LITERAL("a_zip_file (1).zip")),
            download_items[0]->GetTargetFilePath().BaseName());
  ASSERT_TRUE(base::PathExists(download_items[0]->GetTargetFilePath()));
  EXPECT_TRUE(VerifyFile(download_items[0]->GetTargetFilePath(),
                         original_contents, origin_file_size));
}

// Navigate to a new background page, but don't download.
IN_PROC_BROWSER_TEST_F(DownloadTest, DontCloseNewTab1) {
  // Because it's an HTML link, it should open a web page rather than
  // downloading.
  GURL url(URLRequestMockHTTPJob::GetMockUrl("download-test2.html"));

  // Open a web page and wait.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  // We should have two tabs now.
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_TRUE(VerifyNoDownloads());
}

// Download a file in a background tab. Verify that the tab is closed
// automatically.
IN_PROC_BROWSER_TEST_F(DownloadTest, CloseNewTab1) {
  // Download a file in a new background tab and wait.  The tab is automatically
  // closed when the download begins.
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));
  DownloadAndWaitWithDisposition(browser(), url,
                                 WindowOpenDisposition::NEW_BACKGROUND_TAB, 0);

  // When the download finishes, we should still have one tab.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());

  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  CheckDownload(browser(), file, file);
}

// Open a web page in the current tab, then download a file in another tab via
// a Javascript call.
// Verify that we have 2 tabs.
//
// The download_page1.html page contains an openNew() function that opens a
// tab and then downloads download-test1.lib.
IN_PROC_BROWSER_TEST_F(DownloadTest, DontCloseNewTab2) {
  // Because it's an HTML link, it should open a web page rather than
  // downloading.
  GURL url(URLRequestMockHTTPJob::GetMockUrl("download_page1.html"));

  // Open a web page and wait.
  ui_test_utils::NavigateToURL(browser(), url);

  // Download a file in a new tab and wait (via Javascript).
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  DownloadAndWaitWithDisposition(browser(), GURL("javascript:openNew()"),
                                 WindowOpenDisposition::CURRENT_TAB,
                                 ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  // When the download finishes, we should have two tabs.
  EXPECT_EQ(2, browser()->tab_strip_model()->count());

  CheckDownload(browser(), file, file);
}

// Open a web page in the current tab, open another tab via a Javascript call,
// then download a file in the new tab.
// Verify that we have 2 tabs.
//
// The download_page2.html page contains an openNew() function that opens a
// tab.
IN_PROC_BROWSER_TEST_F(DownloadTest, DontCloseNewTab3) {
  // Because it's an HTML link, it should open a web page rather than
  // downloading.
  GURL url1(URLRequestMockHTTPJob::GetMockUrl("download_page2.html"));

  // Open a web page and wait.
  ui_test_utils::NavigateToURL(browser(), url1);

  // Open a new tab and wait.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("javascript:openNew()"),
      WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  EXPECT_EQ(2, browser()->tab_strip_model()->count());

  // Download a file and wait.
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));
  DownloadAndWaitWithDisposition(browser(), url,
                                 WindowOpenDisposition::CURRENT_TAB,
                                 ui_test_utils::BROWSER_TEST_NONE);

  // When the download finishes, we should have two tabs.
  EXPECT_EQ(2, browser()->tab_strip_model()->count());

  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  CheckDownload(browser(), file, file);
}

// Open a web page in the current tab, then download a file via Javascript,
// which will do so in a temporary tab. Verify that we have 1 tab.
//
// The download_page3.html page contains an openNew() function that opens a
// tab with download-test1.lib in the URL.  When the URL is determined to be
// a download, the tab is closed automatically.
IN_PROC_BROWSER_TEST_F(DownloadTest, CloseNewTab2) {
  // Because it's an HTML link, it should open a web page rather than
  // downloading.
  GURL url(URLRequestMockHTTPJob::GetMockUrl("download_page3.html"));

  // Open a web page and wait.
  ui_test_utils::NavigateToURL(browser(), url);

  // Download a file and wait.
  // The file to download is "download-test1.lib".
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  DownloadAndWaitWithDisposition(browser(), GURL("javascript:openNew()"),
                                 WindowOpenDisposition::CURRENT_TAB,
                                 ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  // When the download finishes, we should still have one tab.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());

  CheckDownload(browser(), file, file);
}

// Open a web page in the current tab, then call Javascript via a button to
// download a file in a new tab, which is closed automatically when the
// download begins.
// Verify that we have 1 tab.
//
// The download_page4.html page contains a form with download-test1.lib as the
// action.
IN_PROC_BROWSER_TEST_F(DownloadTest, CloseNewTab3) {
  // Because it's an HTML link, it should open a web page rather than
  // downloading.
  GURL url(URLRequestMockHTTPJob::GetMockUrl("download_page4.html"));

  // Open a web page and wait.
  ui_test_utils::NavigateToURL(browser(), url);

  // Download a file in a new tab and wait.  The tab will automatically close
  // when the download begins.
  // The file to download is "download-test1.lib".
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  DownloadAndWaitWithDisposition(
      browser(), GURL("javascript:document.getElementById('form').submit()"),
      WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  // When the download finishes, we should still have one tab.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());

  CheckDownload(browser(), file, file);
}

// Open a second tab, then download a file in that tab. However, have the
// download be canceled by having the file picker act like the user canceled
// the download. The 2nd tab should be closed automatically.
IN_PROC_BROWSER_TEST_F(DownloadTest, CloseNewTab4) {
  std::unique_ptr<content::DownloadTestObserver> observer(
      CreateWaiter(browser(), 1));
  DownloadManager* manager = DownloadManagerForBrowser(browser());
  EXPECT_EQ(0, manager->InProgressCount());
  EnableFileChooser(false);

  // Get the download URL
  GURL slow_download_url(net::URLRequestSlowDownloadJob::kUnknownSizeUrl);

  // Open a new tab for the download
  content::WebContents* tab =
        browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* new_tab = content::WebContents::Create(
        content::WebContents::CreateParams(tab->GetBrowserContext()));
  ASSERT_TRUE(new_tab);
  ASSERT_TRUE(new_tab->GetController().IsInitialNavigation());
  browser()->tab_strip_model()->AppendWebContents(new_tab, true);
  EXPECT_EQ(2, browser()->tab_strip_model()->count());

  // Download a file in that new tab, having it open a file picker
  std::unique_ptr<DownloadUrlParameters> params(
      DownloadUrlParameters::CreateForWebContentsMainFrame(
          new_tab, slow_download_url, TRAFFIC_ANNOTATION_FOR_TESTS));
  params->set_prompt(true);
  manager->DownloadUrl(std::move(params));
  observer->WaitForFinished();

  DownloadManager::DownloadVector items;
  manager->GetAllDownloads(&items);
  ASSERT_NE(0u, items.size());
  DownloadItem* item = items[0];
  EXPECT_TRUE(item != nullptr);

  // When the download is canceled, the second tab should close.
  EXPECT_EQ(item->GetState(), DownloadItem::CANCELLED);
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
}

// EmbeddedTestServer::HandleRequestCallback function that responds with a
// redirect to the URL specified via a query string.
// E.g.:
//   C -> S: GET /redirect?http://example.com
//   S -> C: HTTP/1.1 301 Moved Permanently
//           Location: http://example.com
//           ...
static std::unique_ptr<net::test_server::HttpResponse>
ServerRedirectRequestHandler(const net::test_server::HttpRequest& request) {
  if (!base::StartsWith(request.relative_url, "/redirect",
                        base::CompareCase::SENSITIVE)) {
    return std::unique_ptr<net::test_server::HttpResponse>();
  }

  std::unique_ptr<net::test_server::BasicHttpResponse> response(
      new net::test_server::BasicHttpResponse());
  size_t query_position = request.relative_url.find('?');

  if (query_position == std::string::npos) {
    response->set_code(net::HTTP_PERMANENT_REDIRECT);
    response->AddCustomHeader("Location",
                              "https://request-had-no-query-string");
    response->set_content_type("text/plain");
    response->set_content("Error");
    return std::move(response);
  }

  response->set_code(net::HTTP_PERMANENT_REDIRECT);
  response->AddCustomHeader("Location",
                            request.relative_url.substr(query_position + 1));
  response->set_content_type("text/plain");
  response->set_content("It's gone!");
  return std::move(response);
}

IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadHistoryCheck) {
  GURL download_url(net::URLRequestSlowDownloadJob::kKnownSizeUrl);
  base::FilePath file(net::GenerateFileName(download_url,
                                            std::string(),
                                            std::string(),
                                            std::string(),
                                            std::string(),
                                            std::string()));

  // We use the server so that we can get a redirect and test url_chain
  // persistence.
  embedded_test_server()->RegisterRequestHandler(
      base::Bind(&ServerRedirectRequestHandler));
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL redirect_url =
      embedded_test_server()->GetURL("/redirect?" + download_url.spec());

  // Download the url and wait until the object has been stored.
  base::Time start(base::Time::Now());
  HistoryObserver observer(browser()->profile());
  observer.SetFilterCallback(base::Bind(&HasDataAndName));
  ui_test_utils::NavigateToURL(browser(), redirect_url);
  observer.WaitForStored();

  // Get the details on what was stored into the history.
  std::unique_ptr<std::vector<history::DownloadRow>> downloads_in_database;
  ASSERT_TRUE(DownloadsHistoryDataCollector(
      browser()->profile()).WaitForDownloadInfo(&downloads_in_database));
  ASSERT_EQ(1u, downloads_in_database->size());

  // Confirm history storage is what you expect for a partially completed
  // slow download job.
  history::DownloadRow& row(downloads_in_database->at(0));
  EXPECT_EQ(DestinationFile(browser(), file), row.target_path);
  EXPECT_EQ(DownloadTargetDeterminer::GetCrDownloadPath(
                DestinationFile(browser(), file)),
            row.current_path);
  ASSERT_EQ(2u, row.url_chain.size());
  EXPECT_EQ(redirect_url.spec(), row.url_chain[0].spec());
  EXPECT_EQ(download_url.spec(), row.url_chain[1].spec());
  EXPECT_EQ(history::DownloadDangerType::NOT_DANGEROUS, row.danger_type);
  EXPECT_LE(start, row.start_time);
  EXPECT_EQ(net::URLRequestSlowDownloadJob::kFirstDownloadSize,
            row.received_bytes);
  EXPECT_EQ(net::URLRequestSlowDownloadJob::kFirstDownloadSize
            + net::URLRequestSlowDownloadJob::kSecondDownloadSize,
            row.total_bytes);
  EXPECT_EQ(history::DownloadState::IN_PROGRESS, row.state);
  EXPECT_FALSE(row.opened);

  // Finish the download.  We're ok relying on the history to be flushed
  // at this point as our queries will be behind the history updates
  // invoked by completion.
  std::unique_ptr<content::DownloadTestObserver> download_observer(
      new content::DownloadTestObserverInterrupted(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));
  ui_test_utils::NavigateToURL(
      browser(), GURL(net::URLRequestSlowDownloadJob::kErrorDownloadUrl));
  download_observer->WaitForFinished();
  EXPECT_EQ(1u, download_observer->NumDownloadsSeenInState(
      DownloadItem::INTERRUPTED));
  base::Time end(base::Time::Now());

  // Get what was stored in the history.
  ASSERT_TRUE(DownloadsHistoryDataCollector(
      browser()->profile()).WaitForDownloadInfo(&downloads_in_database));
  ASSERT_EQ(1u, downloads_in_database->size());

  // Confirm history storage is what you expect for an interrupted slow download
  // job. The download isn't continuable, so there's no intermediate file.
  history::DownloadRow& row1(downloads_in_database->at(0));
  EXPECT_EQ(DestinationFile(browser(), file), row1.target_path);
  EXPECT_TRUE(row1.current_path.empty());
  ASSERT_EQ(2u, row1.url_chain.size());
  EXPECT_EQ(redirect_url.spec(), row1.url_chain[0].spec());
  EXPECT_EQ(download_url.spec(), row1.url_chain[1].spec());
  EXPECT_EQ(history::DownloadDangerType::NOT_DANGEROUS, row1.danger_type);
  EXPECT_LE(start, row1.start_time);
  EXPECT_GE(end, row1.end_time);
  EXPECT_EQ(0, row1.received_bytes);  // There's no ETag. So the intermediate
                                      // state is discarded.
  EXPECT_EQ(net::URLRequestSlowDownloadJob::kFirstDownloadSize +
                net::URLRequestSlowDownloadJob::kSecondDownloadSize,
            row1.total_bytes);
  EXPECT_EQ(history::DownloadState::INTERRUPTED, row1.state);
  EXPECT_EQ(history::ToHistoryDownloadInterruptReason(
                content::DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED),
            row1.interrupt_reason);
  EXPECT_FALSE(row1.opened);
}

// Make sure a dangerous file shows up properly in the history.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadHistoryDangerCheck) {
  // Disable SafeBrowsing so that danger will be determined by downloads system.
  browser()->profile()->GetPrefs()->SetBoolean(prefs::kSafeBrowsingEnabled,
                                               false);

  // .swf file so that it's dangerous on all platforms (including CrOS).
  GURL download_url(
      URLRequestMockHTTPJob::GetMockUrl("downloads/dangerous/dangerous.swf"));

  // Download the url and wait until the object has been stored.
  std::unique_ptr<content::DownloadTestObserver> download_observer(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_IGNORE));
  base::Time start(base::Time::Now());
  HistoryObserver observer(browser()->profile());
  observer.SetFilterCallback(base::Bind(&HasDataAndName));
  ui_test_utils::NavigateToURL(browser(), download_url);
  observer.WaitForStored();

  // Get the details on what was stored into the history.
  std::unique_ptr<std::vector<history::DownloadRow>> downloads_in_database;
  ASSERT_TRUE(DownloadsHistoryDataCollector(
      browser()->profile()).WaitForDownloadInfo(&downloads_in_database));
  ASSERT_EQ(1u, downloads_in_database->size());

  // Confirm history storage is what you expect for an unvalidated
  // dangerous file.
  base::FilePath file(FILE_PATH_LITERAL("downloads/dangerous/dangerous.swf"));
  history::DownloadRow& row(downloads_in_database->at(0));
  EXPECT_EQ(DestinationFile(browser(), file), row.target_path);
  EXPECT_NE(DownloadTargetDeterminer::GetCrDownloadPath(
                DestinationFile(browser(), file)),
            row.current_path);
  EXPECT_EQ(history::DownloadDangerType::DANGEROUS_FILE, row.danger_type);
  EXPECT_LE(start, row.start_time);
  EXPECT_EQ(history::DownloadState::IN_PROGRESS, row.state);
  EXPECT_FALSE(row.opened);

  // Validate the download and wait for it to finish.
  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  downloads[0]->ValidateDangerousDownload();
  download_observer->WaitForFinished();

  // Get history details and confirm it's what you expect.
  downloads_in_database->clear();
  ASSERT_TRUE(DownloadsHistoryDataCollector(
      browser()->profile()).WaitForDownloadInfo(&downloads_in_database));
  ASSERT_EQ(1u, downloads_in_database->size());
  history::DownloadRow& row1(downloads_in_database->at(0));
  EXPECT_EQ(DestinationFile(browser(), file), row1.target_path);
  EXPECT_EQ(DestinationFile(browser(), file), row1.current_path);
  EXPECT_EQ(history::DownloadDangerType::USER_VALIDATED, row1.danger_type);
  EXPECT_LE(start, row1.start_time);
  EXPECT_EQ(history::DownloadState::COMPLETE, row1.state);
  EXPECT_FALSE(row1.opened);
  // Not checking file size--not relevant to the point of the test, and
  // the file size is actually different on Windows and other platforms,
  // because for source control simplicity it's actually a text file, and
  // there are CRLF transformations for those files.
}

// Test for crbug.com/14505. This tests that chrome:// urls are still functional
// after download of a file while viewing another chrome://.
IN_PROC_BROWSER_TEST_F(DownloadTest, ChromeURLAfterDownload) {
  GURL download_url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));
  GURL flags_url(chrome::kChromeUIFlagsURL);
  GURL extensions_url(chrome::kChromeUIExtensionsFrameURL);

  ui_test_utils::NavigateToURL(browser(), flags_url);
  DownloadAndWait(browser(), download_url);
  ui_test_utils::NavigateToURL(browser(), extensions_url);
  WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  bool webui_responded = false;
  EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
      contents,
      "window.domAutomationController.send(window.webuiResponded);",
      &webui_responded));
  EXPECT_TRUE(webui_responded);
}

// Test for crbug.com/12745. This tests that if a download is initiated from
// a chrome:// page that has registered and onunload handler, the browser
// will be able to close.
IN_PROC_BROWSER_TEST_F(DownloadTest, BrowserCloseAfterDownload) {
  GURL downloads_url(chrome::kChromeUIFlagsURL);
  GURL download_url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));

  ui_test_utils::NavigateToURL(browser(), downloads_url);
  WebContents* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  bool result = false;
  EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
      contents,
      "window.onunload = function() { var do_nothing = 0; }; "
      "window.domAutomationController.send(true);",
      &result));
  EXPECT_TRUE(result);

  DownloadAndWait(browser(), download_url);

  // Close the notifications as they would prevent the browser from quitting.
  g_browser_process->notification_ui_manager()->CancelAll();

  content::WindowedNotificationObserver signal(
      chrome::NOTIFICATION_BROWSER_CLOSED,
      content::Source<Browser>(browser()));
  chrome::CloseWindow(browser());
  signal.Wait();
}

// Test to make sure the 'download' attribute in anchor tag is respected.
IN_PROC_BROWSER_TEST_F(DownloadTest, AnchorDownloadTag) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl("download-anchor-attrib.html"));

  // Create a download, wait until it's complete, and confirm
  // we're in the expected state.
  std::unique_ptr<content::DownloadTestObserver> observer(
      CreateWaiter(browser(), 1));
  ui_test_utils::NavigateToURL(browser(), url);
  observer->WaitForFinished();
  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);

  // Confirm the downloaded data exists.
  base::FilePath downloaded_file = GetDownloadDirectory(browser());
  downloaded_file = downloaded_file.Append(FILE_PATH_LITERAL("a_red_dot.png"));
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  EXPECT_TRUE(base::PathExists(downloaded_file));
}

// Test to make sure auto-open works.
IN_PROC_BROWSER_TEST_F(DownloadTest, AutoOpen) {
  base::FilePath file(FILE_PATH_LITERAL("download-autoopen.txt"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl("download-autoopen.txt"));

  ASSERT_TRUE(
      GetDownloadPrefs(browser())->EnableAutoOpenBasedOnExtension(file));

  DownloadAndWait(browser(), url);

  // Find the download and confirm it was opened.
  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  EXPECT_EQ(DownloadItem::COMPLETE, downloads[0]->GetState());

  // Unfortunately, this will block forever, causing a timeout, if
  // the download is never opened.
  content::DownloadUpdatedObserver(
      downloads[0], base::Bind(&WasAutoOpened)).WaitForEvent();
  EXPECT_TRUE(downloads[0]->GetOpened());  // Confirm it anyway.

  // As long as we're here, confirmed everything else is good.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  CheckDownload(browser(), file, file);
}

// Download an extension. Expect a dangerous download warning.
// Deny the download.
IN_PROC_BROWSER_TEST_F(DownloadTest, CrxDenyInstall) {
  std::unique_ptr<base::AutoReset<bool>> allow_offstore_install =
      download_crx_util::OverrideOffstoreInstallAllowedForTesting(true);

  GURL extension_url(URLRequestMockHTTPJob::GetMockUrl(kGoodCrxPath));

  std::unique_ptr<content::DownloadTestObserver> observer(
      DangerousDownloadWaiter(
          browser(), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_DENY));
  ui_test_utils::NavigateToURL(browser(), extension_url);

  observer->WaitForFinished();
  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::CANCELLED));
  EXPECT_EQ(1u, observer->NumDangerousDownloadsSeen());
  EXPECT_TRUE(VerifyNoDownloads());

  // Check that the CRX is not installed.
  ExtensionService* extension_service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  ASSERT_FALSE(extension_service->GetExtensionById(kGoodCrxId, false));
}

// Download an extension.  Expect a dangerous download warning.
// Allow the download, deny the install.
IN_PROC_BROWSER_TEST_F(DownloadTest, CrxInstallDenysPermissions) {
  std::unique_ptr<base::AutoReset<bool>> allow_offstore_install =
      download_crx_util::OverrideOffstoreInstallAllowedForTesting(true);
  extensions::ScopedTestDialogAutoConfirm auto_confirm_install_prompt(
      extensions::ScopedTestDialogAutoConfirm::CANCEL);

  GURL extension_url(URLRequestMockHTTPJob::GetMockUrl(kGoodCrxPath));

  std::unique_ptr<content::DownloadTestObserver> observer(
      DangerousDownloadWaiter(
          browser(), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_ACCEPT));
  ui_test_utils::NavigateToURL(browser(), extension_url);

  observer->WaitForFinished();
  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);
  EXPECT_EQ(1u, observer->NumDangerousDownloadsSeen());

  content::DownloadManager::DownloadVector downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  content::DownloadUpdatedObserver(
      downloads[0], base::Bind(&WasAutoOpened)).WaitForEvent();

  // Check that the extension was not installed.
  ExtensionService* extension_service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  ASSERT_FALSE(extension_service->GetExtensionById(kGoodCrxId, false));
}

// Download an extension.  Expect a dangerous download warning.
// Allow the download, and the install.
IN_PROC_BROWSER_TEST_F(DownloadTest, CrxInstallAcceptPermissions) {
  std::unique_ptr<base::AutoReset<bool>> allow_offstore_install =
      download_crx_util::OverrideOffstoreInstallAllowedForTesting(true);

  GURL extension_url(URLRequestMockHTTPJob::GetMockUrl(kGoodCrxPath));

  // Simulate the user allowing permission to finish the install.
  extensions::ScopedTestDialogAutoConfirm auto_confirm_install_prompt(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);

  std::unique_ptr<content::DownloadTestObserver> observer(
      DangerousDownloadWaiter(
          browser(), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_ACCEPT));
  ui_test_utils::NavigateToURL(browser(), extension_url);

  observer->WaitForFinished();
  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);
  EXPECT_EQ(1u, observer->NumDangerousDownloadsSeen());

  // Download shelf should close from auto-open.
  content::DownloadManager::DownloadVector downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  content::DownloadUpdatedObserver(
      downloads[0], base::Bind(&WasAutoOpened)).WaitForEvent();

  // Check that the extension was installed.
  ExtensionService* extension_service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  ASSERT_TRUE(extension_service->GetExtensionById(kGoodCrxId, false));
}

// Test installing a CRX that fails integrity checks.
IN_PROC_BROWSER_TEST_F(DownloadTest, CrxInvalid) {
  GURL extension_url(
      URLRequestMockHTTPJob::GetMockUrl("extensions/bad_signature.crx"));

  // Simulate the user allowing permission to finish the install.
  extensions::ScopedTestDialogAutoConfirm auto_confirm_install_prompt(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);

  std::unique_ptr<content::DownloadTestObserver> observer(
      DangerousDownloadWaiter(
          browser(), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_ACCEPT));
  ui_test_utils::NavigateToURL(browser(), extension_url);

  observer->WaitForFinished();
  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);

  // Check that the extension was not installed.
  ExtensionService* extension_service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  ASSERT_FALSE(extension_service->GetExtensionById(kGoodCrxId, false));
}

// Install a large (100kb) theme.
IN_PROC_BROWSER_TEST_F(DownloadTest, CrxLargeTheme) {
  std::unique_ptr<base::AutoReset<bool>> allow_offstore_install =
      download_crx_util::OverrideOffstoreInstallAllowedForTesting(true);

  GURL extension_url(URLRequestMockHTTPJob::GetMockUrl(kLargeThemePath));

  // Simulate the user allowing permission to finish the install.
  extensions::ScopedTestDialogAutoConfirm auto_confirm_install_prompt(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);

  std::unique_ptr<content::DownloadTestObserver> observer(
      DangerousDownloadWaiter(
          browser(), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_ACCEPT));
  ui_test_utils::NavigateToURL(browser(), extension_url);

  observer->WaitForFinished();
  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);
  EXPECT_EQ(1u, observer->NumDangerousDownloadsSeen());

  // Download shelf should close from auto-open.
  content::DownloadManager::DownloadVector downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  content::DownloadUpdatedObserver(
      downloads[0], base::Bind(&WasAutoOpened)).WaitForEvent();

  // Check that the extension was installed.
  ExtensionService* extension_service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  ASSERT_TRUE(extension_service->GetExtensionById(kLargeThemeCrxId, false));
}

// Tests for download initiation functions.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadUrl) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));

  // DownloadUrl always prompts; return acceptance of whatever it prompts.
  EnableFileChooser(true);

  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  content::DownloadTestObserver* observer(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));
  std::unique_ptr<DownloadUrlParameters> params(
      DownloadUrlParameters::CreateForWebContentsMainFrame(
          web_contents, url, TRAFFIC_ANNOTATION_FOR_TESTS));
  params->set_prompt(true);
  DownloadManagerForBrowser(browser())->DownloadUrl(std::move(params));
  observer->WaitForFinished();
  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);
  EXPECT_TRUE(DidShowFileChooser());

  // Check state.
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  ASSERT_TRUE(CheckDownload(browser(), file, file));
}

IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadUrlToPath) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));

  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  base::ThreadRestrictions::ScopedAllowIO allow_io;
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  base::ScopedTempDir other_directory;
  ASSERT_TRUE(other_directory.CreateUniqueTempDir());
  base::FilePath target_file_full_path =
      other_directory.GetPath().Append(file.BaseName());
  content::DownloadTestObserver* observer(CreateWaiter(browser(), 1));
  std::unique_ptr<DownloadUrlParameters> params(
      DownloadUrlParameters::CreateForWebContentsMainFrame(
          web_contents, url, TRAFFIC_ANNOTATION_FOR_TESTS));
  params->set_file_path(target_file_full_path);
  DownloadManagerForBrowser(browser())->DownloadUrl(std::move(params));
  observer->WaitForFinished();
  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));

  // Check state.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  ASSERT_TRUE(CheckDownloadFullPaths(browser(),
                                     target_file_full_path,
                                     OriginFile(file)));

  // Temporary are treated as auto-opened, and after that open won't be
  // visible; wait for auto-open and confirm not visible.
  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  content::DownloadUpdatedObserver(
      downloads[0], base::Bind(&WasAutoOpened)).WaitForEvent();
}

IN_PROC_BROWSER_TEST_F(DownloadTest, TransientDownload) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));

  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  base::ThreadRestrictions::ScopedAllowIO allow_io;
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  base::ScopedTempDir other_directory;
  ASSERT_TRUE(other_directory.CreateUniqueTempDir());
  base::FilePath target_file_full_path =
      other_directory.GetPath().Append(file.BaseName());
  content::DownloadTestObserver* observer(CreateWaiter(browser(), 1));
  std::unique_ptr<DownloadUrlParameters> params(
      DownloadUrlParameters::CreateForWebContentsMainFrame(
          web_contents, url, TRAFFIC_ANNOTATION_FOR_TESTS));
  params->set_file_path(target_file_full_path);
  params->set_transient(true);
  DownloadManagerForBrowser(browser())->DownloadUrl(std::move(params));
  observer->WaitForFinished();
  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));

  // Check state.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  ASSERT_TRUE(CheckDownloadFullPaths(browser(), target_file_full_path,
                                     OriginFile(file)));

  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  ASSERT_TRUE(downloads[0]->IsTransient());
  ASSERT_FALSE(downloads[0]->IsTemporary());
}

IN_PROC_BROWSER_TEST_F(DownloadTest, SavePageNonHTMLViaGet) {
  embedded_test_server()->ServeFilesFromDirectory(GetTestDataDirectory());
  ASSERT_TRUE(embedded_test_server()->Start());
  EnableFileChooser(true);
  std::vector<DownloadItem*> download_items;
  GetDownloads(browser(), &download_items);
  ASSERT_TRUE(download_items.empty());

  // Navigate to a non-HTML resource. The resource also has
  // Cache-Control: no-cache set, which normally requires revalidation
  // each time.
  GURL url = embedded_test_server()->GetURL("/downloads/image.jpg");
  ASSERT_TRUE(url.is_valid());
  ui_test_utils::NavigateToURL(browser(), url);

  // Stop the test server, and then try to save the page. If cache validation
  // is not bypassed then this will fail since the server is no longer
  // reachable.
  ASSERT_TRUE(embedded_test_server()->ShutdownAndWaitUntilComplete());

  std::unique_ptr<content::DownloadTestObserver> waiter(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));
  chrome::SavePage(browser());
  waiter->WaitForFinished();
  EXPECT_EQ(1u, waiter->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);

  // Validate that the correct file was downloaded.
  GetDownloads(browser(), &download_items);
  EXPECT_TRUE(DidShowFileChooser());
  ASSERT_EQ(1u, download_items.size());
  ASSERT_EQ(url, download_items[0]->GetOriginalUrl());

  // Try to download it via a context menu.
  std::unique_ptr<content::DownloadTestObserver> waiter_context_menu(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));
  content::ContextMenuParams context_menu_params;
  context_menu_params.media_type = blink::WebContextMenuData::kMediaTypeImage;
  context_menu_params.src_url = url;
  context_menu_params.page_url = url;
  TestRenderViewContextMenu menu(
      browser()->tab_strip_model()->GetActiveWebContents()->GetMainFrame(),
      context_menu_params);
  menu.Init();
  menu.ExecuteCommand(IDC_CONTENT_CONTEXT_SAVEIMAGEAS, 0);
  waiter_context_menu->WaitForFinished();
  EXPECT_EQ(
      1u, waiter_context_menu->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(2, DownloadItem::COMPLETE);

  // Validate that the correct file was downloaded via the context menu.
  download_items.clear();
  GetDownloads(browser(), &download_items);
  EXPECT_TRUE(DidShowFileChooser());
  ASSERT_EQ(2u, download_items.size());
  ASSERT_EQ(url, download_items[0]->GetOriginalUrl());
  ASSERT_EQ(url, download_items[1]->GetOriginalUrl());
}

// A EmbeddedTestServer::HandleRequestCallback function that checks for requests
// with query string ?allow-post-only, and returns a 404 response if the method
// is not POST.
static std::unique_ptr<net::test_server::HttpResponse>
FilterPostOnlyURLsHandler(const net::test_server::HttpRequest& request) {
  std::unique_ptr<net::test_server::BasicHttpResponse> response;
  if (request.relative_url.find("?allow-post-only") != std::string::npos &&
      request.method != net::test_server::METHOD_POST) {
    response.reset(new net::test_server::BasicHttpResponse());
    response->set_code(net::HTTP_NOT_FOUND);
  }
  return std::move(response);
}

IN_PROC_BROWSER_TEST_F(DownloadTest, SavePageNonHTMLViaPost) {
  embedded_test_server()->RegisterRequestHandler(
      base::Bind(&FilterPostOnlyURLsHandler));
  embedded_test_server()->ServeFilesFromDirectory(GetTestDataDirectory());
  ASSERT_TRUE(embedded_test_server()->Start());
  EnableFileChooser(true);
  std::vector<DownloadItem*> download_items;
  GetDownloads(browser(), &download_items);
  ASSERT_TRUE(download_items.empty());

  // Navigate to a form page.
  GURL form_url =
      embedded_test_server()->GetURL("/downloads/form_page_to_post.html");
  ASSERT_TRUE(form_url.is_valid());
  ui_test_utils::NavigateToURL(browser(), form_url);

  // Submit the form. This will send a POST reqeuest, and the response is a
  // JPEG image. The resource also has Cache-Control: no-cache set,
  // which normally requires revalidation each time.
  GURL jpeg_url =
      embedded_test_server()->GetURL("/downloads/image.jpg?allow-post-only");
  ASSERT_TRUE(jpeg_url.is_valid());
  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents != NULL);
  content::WindowedNotificationObserver observer(
      content::NOTIFICATION_NAV_ENTRY_COMMITTED,
      content::Source<content::NavigationController>(
          &web_contents->GetController()));
  content::RenderFrameHost* render_frame_host = web_contents->GetMainFrame();
  ASSERT_TRUE(render_frame_host != NULL);
  render_frame_host->ExecuteJavaScriptForTests(
      base::ASCIIToUTF16("SubmitForm()"));
  observer.Wait();
  EXPECT_EQ(jpeg_url, web_contents->GetURL());

  // Stop the test server, and then try to save the page. If cache validation
  // is not bypassed then this will fail since the server is no longer
  // reachable. This will also fail if it tries to be retrieved via "GET"
  // rather than "POST".
  ASSERT_TRUE(embedded_test_server()->ShutdownAndWaitUntilComplete());
  std::unique_ptr<content::DownloadTestObserver> waiter(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));
  chrome::SavePage(browser());
  waiter->WaitForFinished();
  EXPECT_EQ(1u, waiter->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);

  // Validate that the correct file was downloaded.
  GetDownloads(browser(), &download_items);
  EXPECT_TRUE(DidShowFileChooser());
  ASSERT_EQ(1u, download_items.size());
  ASSERT_EQ(jpeg_url, download_items[0]->GetOriginalUrl());

  // Try to download it via a context menu.
  std::unique_ptr<content::DownloadTestObserver> waiter_context_menu(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));
  content::ContextMenuParams context_menu_params;
  context_menu_params.media_type = blink::WebContextMenuData::kMediaTypeImage;
  context_menu_params.src_url = jpeg_url;
  context_menu_params.page_url = jpeg_url;
  TestRenderViewContextMenu menu(web_contents->GetMainFrame(),
                                 context_menu_params);
  menu.Init();
  menu.ExecuteCommand(IDC_CONTENT_CONTEXT_SAVEIMAGEAS, 0);
  waiter_context_menu->WaitForFinished();
  EXPECT_EQ(
      1u, waiter_context_menu->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(2, DownloadItem::COMPLETE);

  // Validate that the correct file was downloaded via the context menu.
  download_items.clear();
  GetDownloads(browser(), &download_items);
  EXPECT_TRUE(DidShowFileChooser());
  ASSERT_EQ(2u, download_items.size());
  ASSERT_EQ(jpeg_url, download_items[0]->GetOriginalUrl());
  ASSERT_EQ(jpeg_url, download_items[1]->GetOriginalUrl());
}

IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadErrorsServer) {
  DownloadInfo download_info[] = {
      {// Normal navigated download.
       "a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_NAVIGATE,
       content::DOWNLOAD_INTERRUPT_REASON_NONE, true, false},
      {// Normal direct download.
       "a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_DIRECT,
       content::DOWNLOAD_INTERRUPT_REASON_NONE, true, false},
      {// Direct download with 404 error.
       "there_IS_no_spoon.zip", "there_IS_no_spoon.zip", DOWNLOAD_DIRECT,
       content::DOWNLOAD_INTERRUPT_REASON_SERVER_BAD_CONTENT, true, false},
      {// Navigated download with 404 error.
       "there_IS_no_spoon.zip", "there_IS_no_spoon.zip", DOWNLOAD_NAVIGATE,
       content::DOWNLOAD_INTERRUPT_REASON_SERVER_BAD_CONTENT, false, false},
      {// Direct download with 400 error.
       "zip_file_not_found.zip", "zip_file_not_found.zip", DOWNLOAD_DIRECT,
       content::DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED, true, false},
      {// Navigated download with 400 error.
       "zip_file_not_found.zip", "", DOWNLOAD_NAVIGATE,
       content::DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED, false, false},
      {// Simulates clicking on <a href="http://..." download="">. The name does
       // not resolve. But since this is an explicit download, the download
       // should appear on the shelf and the error should be indicated.
       "download-anchor-attrib-name-not-resolved.html",
       "http://doesnotexist/shouldnotberesolved", DOWNLOAD_NAVIGATE,
       content::DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED, true, false},
      {// Simulates clicking on <a href="http://..." download=""> where the URL
       // leads to a 404 response. This is different from the previous test case
       // in that the ResourceLoader issues a OnResponseStarted() callback since
       // the headers are successfully received.
       "download-anchor-attrib-404.html", "there_IS_no_spoon.zip",
       DOWNLOAD_NAVIGATE, content::DOWNLOAD_INTERRUPT_REASON_SERVER_BAD_CONTENT,
       true, false},
      {// Similar to the above, but the resulting response contains a status
       // code of 400.
       "download-anchor-attrib-400.html", "zip_file_not_found.zip",
       DOWNLOAD_NAVIGATE, content::DOWNLOAD_INTERRUPT_REASON_SERVER_FAILED,
       true, false},
      {// Direct download of a URL where the hostname doesn't resolve.
       "http://doesnotexist/shouldnotdownloadsuccessfully",
       "http://doesnotexist/shouldnotdownloadsuccessfully", DOWNLOAD_DIRECT,
       content::DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED, true, false}};

  DownloadFilesCheckErrors(arraysize(download_info), download_info);
}

#if defined(OS_MACOSX)
// https://crbug.com/739766
#define MAYBE_DownloadErrorsFile DISABLED_DownloadErrorsFile
#else
#define MAYBE_DownloadErrorsFile DownloadErrorsFile
#endif

IN_PROC_BROWSER_TEST_F(DownloadTest, MAYBE_DownloadErrorsFile) {
  FileErrorInjectInfo error_info[] = {
      {// Navigated download with injected "Disk full" error in Initialize().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_NAVIGATE,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_INITIALIZE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE,
       }},
      {// Direct download with injected "Disk full" error in Initialize().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_DIRECT,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_INITIALIZE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE,
       }},
      {// Navigated download with injected "Disk full" error in Write().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_NAVIGATE,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_WRITE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE,
       }},
      {// Direct download with injected "Disk full" error in Write().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_DIRECT,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_WRITE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE,
       }},
      {// Navigated download with injected "Failed" error in Initialize().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_NAVIGATE,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_INITIALIZE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
       }},
      {// Direct download with injected "Failed" error in Initialize().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_DIRECT,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_INITIALIZE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
       }},
      {// Navigated download with injected "Failed" error in Write().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_NAVIGATE,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_WRITE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
       }},
      {// Direct download with injected "Failed" error in Write().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_DIRECT,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_WRITE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
       }},
      {// Navigated download with injected "Name too long" error in
       // Initialize().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_NAVIGATE,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_INITIALIZE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG,
       }},
      {// Direct download with injected "Name too long" error in Initialize().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_DIRECT,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_INITIALIZE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG,
       }},
      {// Navigated download with injected "Name too long" error in Write().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_NAVIGATE,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_WRITE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
       }},
      {// Direct download with injected "Name too long" error in Write().
       {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_DIRECT,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_WRITE, 0,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
       }},
      {// Direct download with injected "Disk full" error in 2nd Write().
       {"06bESSE21Evolution.ppt", "06bESSE21Evolution.ppt", DOWNLOAD_DIRECT,
        content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE, true, false},
       {
           content::TestFileErrorInjector::FILE_OPERATION_WRITE, 1,
           content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE,
       }}};

  DownloadInsertFilesErrorCheckErrors(arraysize(error_info), error_info);
}

IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadErrorReadonlyFolder) {
  DownloadInfo download_info[] = {
      {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_DIRECT,
       // This passes because we switch to the My Documents folder.
       content::DOWNLOAD_INTERRUPT_REASON_NONE, true, true},
      {"a_zip_file.zip", "a_zip_file.zip", DOWNLOAD_NAVIGATE,
       // This passes because we switch to the My Documents folder.
       content::DOWNLOAD_INTERRUPT_REASON_NONE, true, true}};

  DownloadFilesToReadonlyFolder(arraysize(download_info), download_info);
}

// Test that we show a dangerous downloads warning for a dangerous file
// downloaded through a blob: URL.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadDangerousBlobData) {
#if defined(OS_WIN)
  // On Windows, if SafeBrowsing is enabled, certain file types (.exe, .cab,
  // .msi) will be handled by the DownloadProtectionService. However, if the URL
  // is non-standard (e.g. blob:) then those files won't be handled by the
  // DPS. We should be showing the dangerous download warning for any file
  // considered dangerous and isn't handled by the DPS.
  const char kFilename[] = "foo.exe";
#else
  const char kFilename[] = "foo.swf";
#endif

  std::string path("downloads/download-dangerous-blob.html?filename=");
  path += kFilename;

  // Need to use http urls because the blob js doesn't work on file urls for
  // security reasons.
  GURL url = net::URLRequestMockHTTPJob::GetMockUrl(path);

  content::DownloadTestObserver* observer(DangerousDownloadWaiter(
      browser(), 1,
      content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_ACCEPT));
  ui_test_utils::NavigateToURL(browser(), url);
  observer->WaitForFinished();

  EXPECT_EQ(1u, observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  EXPECT_EQ(1u, observer->NumDangerousDownloadsSeen());
}

// A EmbeddedTestServer::HandleRequestCallback function that echoes the Referrer
// header as its contents. Only responds to the relative URL /echoreferrer
// E.g.:
//    C -> S: GET /foo
//            Referer: http://example.com/foo
//    S -> C: HTTP/1.1 200 OK
//            Content-Type: text/plain
//
//            http://example.com/foo
static std::unique_ptr<net::test_server::HttpResponse>
EchoReferrerRequestHandler(const net::test_server::HttpRequest& request) {
  const std::string kReferrerHeader = "Referer";  // SIC

  if (!base::StartsWith(request.relative_url, "/echoreferrer",
                        base::CompareCase::SENSITIVE)) {
    return std::unique_ptr<net::test_server::HttpResponse>();
  }

  std::unique_ptr<net::test_server::BasicHttpResponse> response(
      new net::test_server::BasicHttpResponse());
  response->set_code(net::HTTP_OK);
  response->set_content_type("text/plain");
  auto referrer_header = request.headers.find(kReferrerHeader);
  if (referrer_header != request.headers.end())
    response->set_content(referrer_header->second);
  return std::move(response);
}

IN_PROC_BROWSER_TEST_F(DownloadTest, AltClickDownloadReferrerPolicy) {
  embedded_test_server()->RegisterRequestHandler(
      base::Bind(&EchoReferrerRequestHandler));
  embedded_test_server()->ServeFilesFromDirectory(GetTestDataDirectory());
  ASSERT_TRUE(embedded_test_server()->Start());
  EnableFileChooser(true);
  std::vector<DownloadItem*> download_items;
  GetDownloads(browser(), &download_items);
  ASSERT_TRUE(download_items.empty());

  // Navigate to a page with a referrer policy and a link on it. The link points
  // to /echoreferrer.
  GURL url = embedded_test_server()->GetURL("/downloads/referrer_policy.html");
  ASSERT_TRUE(url.is_valid());
  ui_test_utils::NavigateToURL(browser(), url);

  std::unique_ptr<content::DownloadTestObserver> waiter(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));

  // Click on the link with the alt key pressed. This will download the link
  // target.
  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  blink::WebMouseEvent mouse_event(blink::WebInputEvent::kMouseDown,
                                   blink::WebInputEvent::kAltKey,
                                   blink::WebInputEvent::kTimeStampForTesting);
  mouse_event.button = blink::WebMouseEvent::Button::kLeft;
  mouse_event.SetPositionInWidget(15, 15);
  mouse_event.click_count = 1;
  tab->GetRenderViewHost()->GetWidget()->ForwardMouseEvent(mouse_event);
  mouse_event.SetType(blink::WebInputEvent::kMouseUp);
  tab->GetRenderViewHost()->GetWidget()->ForwardMouseEvent(mouse_event);

  waiter->WaitForFinished();
  EXPECT_EQ(1u, waiter->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);

  // Validate that the correct file was downloaded.
  GetDownloads(browser(), &download_items);
  ASSERT_EQ(1u, download_items.size());
  ASSERT_EQ(embedded_test_server()->GetURL("/echoreferrer"),
            download_items[0]->GetOriginalUrl());

  // Check that the file contains the expected referrer.
  base::FilePath file(download_items[0]->GetTargetFilePath());
  std::string expected_contents = embedded_test_server()->GetURL("/").spec();
  ASSERT_TRUE(VerifyFile(file, expected_contents, expected_contents.length()));
}

// This test ensures that the Referer header is properly sanitized when
// Save Link As is chosen from the context menu.
IN_PROC_BROWSER_TEST_F(DownloadTest, SaveLinkAsReferrerPolicyOrigin) {
  embedded_test_server()->RegisterRequestHandler(
      base::Bind(&EchoReferrerRequestHandler));
  ASSERT_TRUE(embedded_test_server()->Start());
  EnableFileChooser(true);
  std::vector<DownloadItem*> download_items;
  GetDownloads(browser(), &download_items);
  ASSERT_TRUE(download_items.empty());

  // Navigate to the initial page, where Save Link As will be executed.
  GURL url = net::URLRequestMockHTTPJob::GetMockHttpsUrl(
      std::string("referrer_policy/referrer-policy-start.html?policy=origin") +
      "&redirect=" + embedded_test_server()->GetURL("/echoreferrer").spec() +
      "&link=true&target=");
  ASSERT_TRUE(url.is_valid());
  ui_test_utils::NavigateToURL(browser(), url);

  std::unique_ptr<content::DownloadTestObserver> waiter(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));

  // Right-click on the link and choose Save Link As. This will download the
  // link target.
  ContextMenuNotificationObserver context_menu_observer(
      IDC_CONTENT_CONTEXT_SAVELINKAS);

  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  blink::WebMouseEvent mouse_event(blink::WebInputEvent::kMouseDown,
                                   blink::WebInputEvent::kNoModifiers,
                                   blink::WebInputEvent::kTimeStampForTesting);
  mouse_event.button = blink::WebMouseEvent::Button::kRight;
  mouse_event.SetPositionInWidget(15, 15);
  mouse_event.click_count = 1;
  tab->GetRenderViewHost()->GetWidget()->ForwardMouseEvent(mouse_event);
  mouse_event.SetType(blink::WebInputEvent::kMouseUp);
  tab->GetRenderViewHost()->GetWidget()->ForwardMouseEvent(mouse_event);

  waiter->WaitForFinished();
  EXPECT_EQ(1u, waiter->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);

  // Validate that the correct file was downloaded.
  GetDownloads(browser(), &download_items);
  EXPECT_EQ(1u, download_items.size());
  EXPECT_EQ(embedded_test_server()->GetURL("/echoreferrer"),
            download_items[0]->GetOriginalUrl());

  // Check that the file contains the expected referrer.
  base::FilePath file(download_items[0]->GetTargetFilePath());
  std::string expected_contents =
      net::URLRequestMockHTTPJob::GetMockHttpsUrl(std::string()).spec();
  EXPECT_TRUE(VerifyFile(file, expected_contents, expected_contents.length()));
}

// This test ensures that the Referer header is properly sanitized when
// Save Image As is chosen from the context menu.
IN_PROC_BROWSER_TEST_F(DownloadTest, SaveImageAsReferrerPolicyDefault) {
  embedded_test_server()->RegisterRequestHandler(
      base::Bind(&EchoReferrerRequestHandler));
  ASSERT_TRUE(embedded_test_server()->Start());
  EnableFileChooser(true);
  std::vector<DownloadItem*> download_items;
  GetDownloads(browser(), &download_items);
  ASSERT_TRUE(download_items.empty());

  GURL url = net::URLRequestMockHTTPJob::GetMockHttpsUrl("title1.html");
  GURL img_url = embedded_test_server()->GetURL("/echoreferrer");
  ASSERT_TRUE(url.is_valid());
  ui_test_utils::NavigateToURL(browser(), url);

  // Try to download an image via a context menu.
  std::unique_ptr<content::DownloadTestObserver> waiter_context_menu(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));
  content::ContextMenuParams context_menu_params;
  context_menu_params.media_type = blink::WebContextMenuData::kMediaTypeImage;
  context_menu_params.page_url = url;
  context_menu_params.src_url = img_url;
  TestRenderViewContextMenu menu(
      browser()->tab_strip_model()->GetActiveWebContents()->GetMainFrame(),
      context_menu_params);
  menu.Init();
  menu.ExecuteCommand(IDC_CONTENT_CONTEXT_SAVEIMAGEAS, 0);
  waiter_context_menu->WaitForFinished();
  EXPECT_EQ(
      1u, waiter_context_menu->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);

  // Validate that the correct file was downloaded via the context menu.
  download_items.clear();
  GetDownloads(browser(), &download_items);
  EXPECT_TRUE(DidShowFileChooser());
  ASSERT_EQ(1u, download_items.size());
  ASSERT_EQ(img_url, download_items[0]->GetOriginalUrl());
  base::FilePath file = download_items[0]->GetTargetFilePath();
  // The contents of the file is the value of the Referer header if there was
  // one.
  EXPECT_TRUE(VerifyFile(file, "", 0));
}

// This test ensures that a cross-domain download correctly sets the referrer
// according to the referrer policy.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadCrossDomainReferrerPolicy) {
  embedded_test_server()->RegisterRequestHandler(
      base::Bind(&ServerRedirectRequestHandler));
  embedded_test_server()->RegisterRequestHandler(
      base::Bind(&EchoReferrerRequestHandler));
  embedded_test_server()->ServeFilesFromDirectory(GetTestDataDirectory());
  ASSERT_TRUE(embedded_test_server()->Start());
  EnableFileChooser(true);
  std::vector<DownloadItem*> download_items;
  GetDownloads(browser(), &download_items);
  ASSERT_TRUE(download_items.empty());

  // Navigate to a page with a referrer policy and a link on it. The link points
  // to /echoreferrer.
  GURL url = embedded_test_server()->GetURL(
      "/downloads/download_cross_referrer_policy.html");
  ASSERT_TRUE(url.is_valid());
  ui_test_utils::NavigateToURL(browser(), url);

  std::unique_ptr<content::DownloadTestObserver> waiter(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));

  // Click on the link with the alt key pressed. This will download the link
  // target.
  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  blink::WebMouseEvent mouse_event(blink::WebInputEvent::kMouseDown,
                                   blink::WebInputEvent::kAltKey,
                                   blink::WebInputEvent::kTimeStampForTesting);
  mouse_event.button = blink::WebMouseEvent::Button::kLeft;
  mouse_event.SetPositionInWidget(15, 15);
  mouse_event.click_count = 1;
  tab->GetRenderViewHost()->GetWidget()->ForwardMouseEvent(mouse_event);
  mouse_event.SetType(blink::WebInputEvent::kMouseUp);
  tab->GetRenderViewHost()->GetWidget()->ForwardMouseEvent(mouse_event);

  waiter->WaitForFinished();
  EXPECT_EQ(1u, waiter->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  CheckDownloadStates(1, DownloadItem::COMPLETE);

  // Validate that the correct file was downloaded.
  GetDownloads(browser(), &download_items);
  ASSERT_EQ(1u, download_items.size());
  ASSERT_EQ(embedded_test_server()->GetURL("www.a.com", "/echoreferrer"),
            download_items[0]->GetURL());

  // Check that the file contains the expected referrer.
  base::FilePath file(download_items[0]->GetTargetFilePath());
  std::string expected_contents = embedded_test_server()->GetURL("/").spec();
  ASSERT_TRUE(VerifyFile(file, expected_contents, expected_contents.length()));
}

// On mobile, the multiple downloads UI is an infobar. On desktop, it's a
// bubble. Test each as appropriate.
#if defined(OS_ANDROID)
IN_PROC_BROWSER_TEST_F(DownloadTest, TestMultipleDownloadsInfobar) {
  // Ensure that infobars are being used instead of bubbles.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kDisablePermissionsBubbles);

  // Create a downloads observer.
  std::unique_ptr<content::DownloadTestObserver> downloads_observer(
      CreateWaiter(browser(), 2));

  // Create an infobar observer.
  content::WindowedNotificationObserver infobar_added_1(
        chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_ADDED,
        content::NotificationService::AllSources());
  ui_test_utils::NavigateToURL(browser(),
                               net::URLRequestMockHTTPJob::GetMockUrl(
                                   "downloads/download-a_zip_file.html"));
  infobar_added_1.Wait();

  InfoBarService* infobar_service = InfoBarService::FromWebContents(
       browser()->tab_strip_model()->GetActiveWebContents());
  // Verify that there is only one infobar.
  ASSERT_EQ(1u, infobar_service->infobar_count());

  // Get the infobar at index 0.
  infobars::InfoBar* infobar = infobar_service->infobar_at(0);
  ConfirmInfoBarDelegate* confirm_infobar =
      infobar->delegate()->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(confirm_infobar != NULL);

  // Verify multi download warning infobar message.
  EXPECT_EQ(confirm_infobar->GetMessageText(),
            l10n_util::GetStringUTF16(IDS_MULTI_DOWNLOAD_WARNING));

  // Click on the "Allow" button to allow multiple downloads.
  if (confirm_infobar->Accept())
    infobar_service->RemoveInfoBar(infobar);
  // Verify that there are no more infobars.
  EXPECT_EQ(0u, infobar_service->infobar_count());

  // Waits for the download to complete.
  downloads_observer->WaitForFinished();
  EXPECT_EQ(2u, downloads_observer->NumDownloadsSeenInState(
      DownloadItem::COMPLETE));
}
#else
IN_PROC_BROWSER_TEST_F(DownloadTest, TestMultipleDownloadsRequests) {
  // Create a downloads observer.
  std::unique_ptr<content::DownloadTestObserver> downloads_observer(
      CreateWaiter(browser(), 2));

  PermissionRequestManager* permission_request_manager =
      PermissionRequestManager::FromWebContents(
          browser()->tab_strip_model()->GetActiveWebContents());
  permission_request_manager->set_auto_response_for_test(
      PermissionRequestManager::ACCEPT_ALL);

  ui_test_utils::NavigateToURLBlockUntilNavigationsComplete(
      browser(), net::URLRequestMockHTTPJob::GetMockUrl(
                     "downloads/download-a_zip_file.html"),
      1);

  // Waits for the download to complete.
  downloads_observer->WaitForFinished();
  EXPECT_EQ(2u, downloads_observer->NumDownloadsSeenInState(
      DownloadItem::COMPLETE));

  browser()->tab_strip_model()->GetActiveWebContents()->Close();
  g_browser_process->notification_ui_manager()->CancelAll();
}
#endif

IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadTest_Renaming) {
  GURL url = net::URLRequestMockHTTPJob::GetMockUrl("downloads/a_zip_file.zip");
  content::DownloadManager* manager = DownloadManagerForBrowser(browser());
  base::FilePath origin_file(OriginFile(base::FilePath(FILE_PATH_LITERAL(
      "downloads/a_zip_file.zip"))));
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  ASSERT_TRUE(base::PathExists(origin_file));
  std::string origin_contents;
  ASSERT_TRUE(base::ReadFileToString(origin_file, &origin_contents));

  // Download the same url several times and expect that all downloaded files
  // after the zero-th contain a deduplication counter.
  for (int index = 0; index < 5; ++index) {
    DownloadAndWait(browser(), url);
    content::DownloadItem* item = manager->GetDownload(
        content::DownloadItem::kInvalidId + 1 + index);
    ASSERT_TRUE(item);
    ASSERT_EQ(DownloadItem::COMPLETE, item->GetState());
    base::FilePath target_path(item->GetTargetFilePath());
    EXPECT_EQ(std::string("a_zip_file") +
        (index == 0 ? std::string(".zip") :
                      base::StringPrintf(" (%d).zip", index)),
              target_path.BaseName().AsUTF8Unsafe());
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    ASSERT_TRUE(base::PathExists(target_path));
    ASSERT_TRUE(VerifyFile(target_path, origin_contents,
                           origin_contents.size()));
  }
}

// Test that the entire download pipeline handles unicode correctly.
// Disabled on Windows due to flaky timeouts: crbug.com/446695
// Disabled on chromeos due to flaky crash: crbug.com/577332
#if defined(OS_WIN) || defined(OS_CHROMEOS)
#define MAYBE_DownloadTest_CrazyFilenames DISABLED_DownloadTest_CrazyFilenames
#else
#define MAYBE_DownloadTest_CrazyFilenames DownloadTest_CrazyFilenames
#endif
IN_PROC_BROWSER_TEST_F(DownloadTest, MAYBE_DownloadTest_CrazyFilenames) {
  static constexpr const wchar_t* kCrazyFilenames[] = {
      L"a_file_name.zip",
      L"\u89c6\u9891\u76f4\u64ad\u56fe\u7247.zip",  // chinese chars
      L"\u0412\u043e "
      L"\u0424\u043b\u043e\u0440\u0438\u0434\u0435\u043e\u0431\u044a"
      L"\u044f\u0432\u043b\u0435\u043d\u0440\u0435\u0436\u0438\u043c \u0427"
      L"\u041f \u0438\u0437-\u0437\u0430 \u0443\u0442\u0435\u0447\u043a\u0438 "
      L"\u043d\u0435\u0444\u0442\u0438.zip",  // russian
      L"Desocupa\xe7\xe3o est\xe1vel.zip",
      // arabic:
      L"\u0638\u2026\u0638\u02c6\u0637\xa7\u0638\u201a\u0637\xb9 \u0638\u201e"
      L"\u0638\u201e\u0637\xb2\u0638\u0679\u0637\xa7\u0637\xb1\u0637\xa9.zip",
      L"\u05d4\u05e2\u05d3\u05e4\u05d5\u05ea.zip",  // hebrew
      L"\u092d\u093e\u0930\u0924.zip",              // hindi
      L"d\xe9stabilis\xe9.zip",                     // french
      // korean
      L"\u97d3-\u4e2d \uc815\uc0c1, \ucc9c\uc548\ud568 \uc758\uacac.zip",
      L"jiho....tiho...miho.zip",
      L"jiho!@#$tiho$%^&-()_+=miho copy.zip",  // special chars
      L"Wohoo-to hoo+I.zip", L"Picture 1.zip",
      L"This is a very very long english sentence with spaces and , and +.zip",
  };

  std::vector<DownloadItem*> download_items;
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  base::FilePath origin_directory =
      GetDownloadDirectory(browser()).Append(FILE_PATH_LITERAL("origin"));
  ASSERT_TRUE(base::CreateDirectory(origin_directory));

  for (size_t index = 0; index < arraysize(kCrazyFilenames); ++index) {
    SCOPED_TRACE(testing::Message() << "Index " << index);
    base::string16 crazy16;
    std::string crazy8;
    const wchar_t* const crazy_w = kCrazyFilenames[index];
    ASSERT_TRUE(base::WideToUTF8(crazy_w, wcslen(crazy_w), &crazy8));
    ASSERT_TRUE(base::WideToUTF16(crazy_w, wcslen(crazy_w), &crazy16));
    base::FilePath file_path(origin_directory.Append(
#if defined(OS_WIN)
        crazy16
#elif defined(OS_POSIX)
        crazy8
#endif
        ));

    // Create the file.
    EXPECT_EQ(static_cast<int>(crazy8.size()),
              base::WriteFile(file_path, crazy8.c_str(), crazy8.size()));
    GURL file_url(net::FilePathToFileURL(file_path));

    // Download the file and check that the filename is correct.
    DownloadAndWait(browser(), file_url);
    GetDownloads(browser(), &download_items);
    ASSERT_EQ(1UL, download_items.size());
    base::FilePath downloaded(download_items[0]->GetTargetFilePath());
    download_items[0]->Remove();
    download_items.clear();
    ASSERT_TRUE(CheckDownloadFullPaths(
        browser(),
        downloaded,
        file_path));
  }
}

IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadTest_Remove) {
  GURL url = net::URLRequestMockHTTPJob::GetMockUrl("downloads/a_zip_file.zip");
  std::vector<DownloadItem*> download_items;
  GetDownloads(browser(), &download_items);
  ASSERT_TRUE(download_items.empty());

  // Download a file.
  DownloadAndWaitWithDisposition(browser(), url,
                                 WindowOpenDisposition::CURRENT_TAB,
                                 ui_test_utils::BROWSER_TEST_NONE);
  GetDownloads(browser(), &download_items);
  ASSERT_EQ(1UL, download_items.size());
  base::FilePath downloaded(download_items[0]->GetTargetFilePath());

  // Remove the DownloadItem but not the file, then check that the file still
  // exists.
  download_items[0]->Remove();
  download_items.clear();
  GetDownloads(browser(), &download_items);
  ASSERT_EQ(0UL, download_items.size());
  ASSERT_TRUE(CheckDownloadFullPaths(
      browser(), downloaded, OriginFile(base::FilePath(
          FILE_PATH_LITERAL("downloads/a_zip_file.zip")))));
}

IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadTest_PauseResumeCancel) {
  DownloadItem* download_item = CreateSlowTestDownload();
  ASSERT_TRUE(download_item);
  ASSERT_FALSE(download_item->GetTargetFilePath().empty());
  EXPECT_FALSE(download_item->IsPaused());
  EXPECT_NE(DownloadItem::CANCELLED, download_item->GetState());
  download_item->Pause();
  EXPECT_TRUE(download_item->IsPaused());
  download_item->Resume();
  EXPECT_FALSE(download_item->IsPaused());
  EXPECT_NE(DownloadItem::CANCELLED, download_item->GetState());
  download_item->Cancel(true);
  EXPECT_EQ(DownloadItem::CANCELLED, download_item->GetState());
}

// The Mac downloaded files quarantine feature is implemented by the
// Contents/Info.plist file in cocoa apps. browser_tests cannot test
// quarantining files on Mac because it is not a cocoa app.
// TODO(benjhayden) test the equivalents on other platforms.

#if defined(OS_LINUX) && !defined(OS_CHROMEOS) && defined(ARCH_CPU_ARM_FAMILY)
// Timing out on ARM linux: http://crbug.com/238459
#define MAYBE_DownloadTest_PercentComplete DISABLED_DownloadTest_PercentComplete
#elif defined(OS_MACOSX)
// Disable on mac: http://crbug.com/238831
#define MAYBE_DownloadTest_PercentComplete DISABLED_DownloadTest_PercentComplete
#else
#define MAYBE_DownloadTest_PercentComplete DownloadTest_PercentComplete
#endif
IN_PROC_BROWSER_TEST_F(DownloadTest, MAYBE_DownloadTest_PercentComplete) {
  // Write a huge file. Make sure the test harness can supply "Content-Length"
  // header to indicate the file size, or the download will not have valid
  // percentage progression.
  GURL url = GURL("http://example.com/large_file");
  content::TestDownloadRequestHandler request_handler(url);
  content::TestDownloadRequestHandler::Parameters parameters;
  parameters.size = 1024 * 1024 * 32; /* 32MB file. */
  request_handler.StartServing(parameters);

  // Ensure that we have enough disk space to download the large file.
  {
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    int64_t free_space =
        base::SysInfo::AmountOfFreeDiskSpace(GetDownloadDirectory(browser()));
    ASSERT_LE(parameters.size, free_space)
        << "Not enough disk space to download. Got " << free_space;
  }

  std::unique_ptr<content::DownloadTestObserver> progress_waiter(
      CreateInProgressWaiter(browser(), 1));

  // Start downloading a file, wait for it to be created.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_NONE);
  progress_waiter->WaitForFinished();
  EXPECT_EQ(1u, progress_waiter->NumDownloadsSeenInState(
      DownloadItem::IN_PROGRESS));
  std::vector<DownloadItem*> download_items;
  GetDownloads(browser(), &download_items);
  ASSERT_EQ(1u, download_items.size());

  // Wait for the download to complete, checking along the way that the
  // PercentComplete() never regresses.
  PercentWaiter waiter(download_items[0]);
  EXPECT_TRUE(waiter.WaitForFinished());
  EXPECT_EQ(DownloadItem::COMPLETE, download_items[0]->GetState());
  ASSERT_EQ(100, download_items[0]->PercentComplete());

  // Check that the file downloaded correctly.
  ASSERT_EQ(parameters.size, download_items[0]->GetReceivedBytes());
  ASSERT_EQ(parameters.size, download_items[0]->GetTotalBytes());

  // Delete the file.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  ASSERT_TRUE(base::DieFileDie(download_items[0]->GetTargetFilePath(), false));
}

// A download that is interrupted due to a file error should be able to be
// resumed.
IN_PROC_BROWSER_TEST_F(DownloadTest, Resumption_NoPrompt) {
  scoped_refptr<content::TestFileErrorInjector> error_injector(
      content::TestFileErrorInjector::Create(
          DownloadManagerForBrowser(browser())));
  std::unique_ptr<content::DownloadTestObserver> completion_observer(
      CreateWaiter(browser(), 1));
  EnableFileChooser(true);

  DownloadItem* download = StartMockDownloadAndInjectError(
      error_injector.get(), content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED);
  ASSERT_TRUE(download);

  download->Resume();
  completion_observer->WaitForFinished();

  EXPECT_EQ(
      1u, completion_observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  EXPECT_FALSE(DidShowFileChooser());
}

// A download that's interrupted due to a reason that indicates that the target
// path is invalid or unusable should cause a prompt to be displayed on
// resumption.
IN_PROC_BROWSER_TEST_F(DownloadTest, Resumption_WithPrompt) {
  scoped_refptr<content::TestFileErrorInjector> error_injector(
      content::TestFileErrorInjector::Create(
          DownloadManagerForBrowser(browser())));
  std::unique_ptr<content::DownloadTestObserver> completion_observer(
      CreateWaiter(browser(), 1));
  EnableFileChooser(true);

  DownloadItem* download = StartMockDownloadAndInjectError(
      error_injector.get(), content::DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE);
  ASSERT_TRUE(download);

  download->Resume();
  completion_observer->WaitForFinished();

  EXPECT_EQ(
      1u, completion_observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  EXPECT_TRUE(DidShowFileChooser());
}

// The user shouldn't be prompted on a resumed download unless a prompt is
// necessary due to the interrupt reason.
IN_PROC_BROWSER_TEST_F(DownloadTest, Resumption_WithPromptAlways) {
  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kPromptForDownload, true);
  scoped_refptr<content::TestFileErrorInjector> error_injector(
      content::TestFileErrorInjector::Create(
          DownloadManagerForBrowser(browser())));
  std::unique_ptr<content::DownloadTestObserver> completion_observer(
      CreateWaiter(browser(), 1));
  EnableFileChooser(true);

  DownloadItem* download = StartMockDownloadAndInjectError(
      error_injector.get(), content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED);
  ASSERT_TRUE(download);

  // Prompts the user initially because of the kPromptForDownload preference.
  EXPECT_TRUE(DidShowFileChooser());

  download->Resume();
  completion_observer->WaitForFinished();

  EXPECT_EQ(
      1u, completion_observer->NumDownloadsSeenInState(DownloadItem::COMPLETE));
  // Shouldn't prompt for resumption.
  EXPECT_FALSE(DidShowFileChooser());
}

// A download that is interrupted due to a transient error should be resumed
// automatically.
IN_PROC_BROWSER_TEST_F(DownloadTest, Resumption_Automatic) {
  scoped_refptr<content::TestFileErrorInjector> error_injector(
      content::TestFileErrorInjector::Create(
          DownloadManagerForBrowser(browser())));

  DownloadItem* download = StartMockDownloadAndInjectError(
      error_injector.get(),
      content::DOWNLOAD_INTERRUPT_REASON_FILE_TRANSIENT_ERROR);
  ASSERT_TRUE(download);

  // The number of times this the download is resumed automatically is defined
  // in DownloadItemImpl::kMaxAutoResumeAttempts. The number of DownloadFiles
  // created should be that number + 1 (for the original download request). We
  // only care that it is greater than 1.
  EXPECT_GT(1u, error_injector->TotalFileCount());

  std::unique_ptr<content::DownloadTestObserver> completion_observer(
      CreateWaiter(browser(), 1));
  download->Resume();
  completion_observer->WaitForFinished();

  // Automatic resumption causes download target determination to be run
  // multiple times. Make sure we end up with the correct filename at the end.
  EXPECT_STREQ(kDownloadTest1Path,
               download->GetTargetFilePath().BaseName().AsUTF8Unsafe().c_str());
}

// An interrupting download should be resumable multiple times.
IN_PROC_BROWSER_TEST_F(DownloadTest, Resumption_MultipleAttempts) {
  scoped_refptr<content::TestFileErrorInjector> error_injector(
      content::TestFileErrorInjector::Create(
          DownloadManagerForBrowser(browser())));
  std::unique_ptr<DownloadTestObserverNotInProgress> completion_observer(
      new DownloadTestObserverNotInProgress(
          DownloadManagerForBrowser(browser()), 1));
  // Wait for two transitions to a resumable state
  std::unique_ptr<content::DownloadTestObserver> resumable_observer(
      new DownloadTestObserverResumable(DownloadManagerForBrowser(browser()),
                                        2));

  EnableFileChooser(true);
  DownloadItem* download = StartMockDownloadAndInjectError(
      error_injector.get(), content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED);
  ASSERT_TRUE(download);

  content::TestFileErrorInjector::FileErrorInfo error_info;
  error_info.code = content::TestFileErrorInjector::FILE_OPERATION_WRITE;
  error_info.operation_instance = 0;
  error_info.error = content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED;
  error_injector->InjectError(error_info);

  // Resuming should cause the download to be interrupted again due to the
  // errors we are injecting.
  download->Resume();
  resumable_observer->WaitForFinished();
  ASSERT_EQ(DownloadItem::INTERRUPTED, download->GetState());
  ASSERT_EQ(content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
            download->GetLastReason());

  error_injector->ClearError();

  // No errors this time. The download should complete successfully.
  EXPECT_FALSE(completion_observer->IsFinished());
  completion_observer->StartObserving();
  download->Resume();
  completion_observer->WaitForFinished();
  EXPECT_EQ(DownloadItem::COMPLETE, download->GetState());

  EXPECT_FALSE(DidShowFileChooser());
}

// The file empty.bin is served with a MIME type of application/octet-stream.
// The content body is empty. Make sure this case is handled properly and we
// don't regress on http://crbug.com/320394.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadTest_GZipWithNoContent) {
  GURL url = net::URLRequestMockHTTPJob::GetMockUrl("downloads/empty.bin");
  // Downloading the same URL twice causes the second request to be served from
  // cached (with a high probability). This test verifies that that doesn't
  // happen regardless of whether the request is served via the cache or from
  // the network.
  DownloadAndWait(browser(), url);
  DownloadAndWait(browser(), url);
}

#if defined(FULL_SAFE_BROWSING)

namespace {

// This is a custom DownloadTestObserver for
// DangerousFileWithSBDisabledBeforeCompletion test that disables the
// SafeBrowsing service when a single download is IN_PROGRESS and has a target
// path assigned.  DownloadItemImpl is expected to call MaybeCompleteDownload
// soon afterwards and we want to disable the service before then.
class DisableSafeBrowsingOnInProgressDownload
    : public content::DownloadTestObserver {
 public:
  explicit DisableSafeBrowsingOnInProgressDownload(Browser* browser)
      : DownloadTestObserver(DownloadManagerForBrowser(browser),
                             1,
                             ON_DANGEROUS_DOWNLOAD_QUIT),
        browser_(browser),
        final_state_seen_(false) {
    Init();
  }
  ~DisableSafeBrowsingOnInProgressDownload() override {}

  bool IsDownloadInFinalState(DownloadItem* download) override {
    if (download->GetState() != DownloadItem::IN_PROGRESS ||
        download->GetTargetFilePath().empty())
      return false;

    if (final_state_seen_)
      return true;

    final_state_seen_ = true;
    browser_->profile()->GetPrefs()->SetBoolean(prefs::kSafeBrowsingEnabled,
                                                false);
    EXPECT_EQ(content::DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT,
              download->GetDangerType());
    EXPECT_FALSE(download->IsDangerous());
    EXPECT_NE(safe_browsing::DownloadFileType::NOT_DANGEROUS,
              DownloadItemModel(download).GetDangerLevel());
    return true;
  }

 private:
  Browser* browser_;
  bool final_state_seen_;
};

#if defined(OS_WIN)
const char kDangerousMockFilePath[] = "downloads/dangerous/dangerous.exe";
#elif defined(OS_POSIX)
const char kDangerousMockFilePath[] = "downloads/dangerous/dangerous.sh";
#endif

}  // namespace

IN_PROC_BROWSER_TEST_F(DownloadTest,
                       DangerousFileWithSBDisabledBeforeCompletion) {
  browser()->profile()->GetPrefs()->SetBoolean(prefs::kSafeBrowsingEnabled,
                                               true);
  GURL download_url =
      net::URLRequestMockHTTPJob::GetMockUrl(kDangerousMockFilePath);
  std::unique_ptr<content::DownloadTestObserver> dangerous_observer(
      DangerousDownloadWaiter(
          browser(), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_QUIT));
  std::unique_ptr<content::DownloadTestObserver> in_progress_observer(
      new DisableSafeBrowsingOnInProgressDownload(browser()));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), download_url, WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_NONE);
  in_progress_observer->WaitForFinished();

  // SafeBrowsing should have been disabled by our observer.
  ASSERT_FALSE(browser()->profile()->GetPrefs()->GetBoolean(
      prefs::kSafeBrowsingEnabled));

  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  DownloadItem* download = downloads[0];

  dangerous_observer->WaitForFinished();

  EXPECT_TRUE(download->IsDangerous());
  EXPECT_EQ(content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE,
            download->GetDangerType());
  download->Cancel(true);
}

IN_PROC_BROWSER_TEST_F(DownloadTest, DangerousFileWithSBDisabledBeforeStart) {
  // Disable SafeBrowsing
  browser()->profile()->GetPrefs()->SetBoolean(prefs::kSafeBrowsingEnabled,
                                               false);

  GURL download_url =
      net::URLRequestMockHTTPJob::GetMockUrl(kDangerousMockFilePath);
  std::unique_ptr<content::DownloadTestObserver> dangerous_observer(
      DangerousDownloadWaiter(
          browser(), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_QUIT));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), download_url, WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_NONE);
  dangerous_observer->WaitForFinished();

  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());

  DownloadItem* download = downloads[0];
  EXPECT_TRUE(download->IsDangerous());
  EXPECT_EQ(content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE,
            download->GetDangerType());

  download->Cancel(true);
}

IN_PROC_BROWSER_TEST_F(DownloadTest, SafeSupportedFile) {
  GURL download_url =
      net::URLRequestMockHTTPJob::GetMockUrl("downloads/a_zip_file.zip");
  DownloadAndWait(browser(), download_url);

  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());

  DownloadItem* download = downloads[0];
  EXPECT_FALSE(download->IsDangerous());
  EXPECT_EQ(content::DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT,
            download->GetDangerType());

  download->Cancel(true);
}

IN_PROC_BROWSER_TEST_F(DownloadTest, FeedbackServiceDiscardDownload) {
  PrefService* prefs = browser()->profile()->GetPrefs();
  prefs->SetBoolean(prefs::kSafeBrowsingEnabled, true);
  safe_browsing::SetExtendedReportingPref(prefs, true);

  // Make a dangerous file.
  GURL download_url(net::URLRequestMockHTTPJob::GetMockUrl(
      "downloads/dangerous/dangerous.swf"));
  std::unique_ptr<content::DownloadTestObserverInterrupted> observer(
      new content::DownloadTestObserverInterrupted(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_QUIT));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(download_url), WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_NONE);
  observer->WaitForFinished();

  // Get the download from the DownloadManager.
  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  EXPECT_TRUE(downloads[0]->IsDangerous());

  // Save fake pings for the download.
  safe_browsing::ClientDownloadReport fake_metadata;
  fake_metadata.mutable_download_request()->set_url("http://test");
  fake_metadata.mutable_download_request()->set_length(1);
  fake_metadata.mutable_download_request()->mutable_digests()->set_sha1("hi");
  fake_metadata.mutable_download_response()->set_verdict(
      safe_browsing::ClientDownloadResponse::UNCOMMON);
  std::string ping_request(
      fake_metadata.download_request().SerializeAsString());
  std::string ping_response(
      fake_metadata.download_response().SerializeAsString());
  safe_browsing::SafeBrowsingService* sb_service =
      g_browser_process->safe_browsing_service();
  safe_browsing::DownloadProtectionService* download_protection_service =
      sb_service->download_protection_service();
  download_protection_service->feedback_service()->MaybeStorePingsForDownload(
      safe_browsing::DownloadCheckResult::UNCOMMON, true /* upload_requested */,
      downloads[0], ping_request, ping_response);
  ASSERT_TRUE(safe_browsing::DownloadFeedbackService::IsEnabledForDownload(
      *(downloads[0])));

  // Begin feedback and check that the file is "stolen".
  download_protection_service->feedback_service()->BeginFeedbackForDownload(
      downloads[0], DownloadCommands::DISCARD);
  std::vector<DownloadItem*> updated_downloads;
  GetDownloads(browser(), &updated_downloads);
  ASSERT_TRUE(updated_downloads.empty());
}

IN_PROC_BROWSER_TEST_F(DownloadTest, FeedbackServiceKeepDownload) {
  PrefService* prefs = browser()->profile()->GetPrefs();
  prefs->SetBoolean(prefs::kSafeBrowsingEnabled, true);
  safe_browsing::SetExtendedReportingPref(prefs, true);

  // Make a dangerous file.
  GURL download_url(net::URLRequestMockHTTPJob::GetMockUrl(
      "downloads/dangerous/dangerous.swf"));
  std::unique_ptr<content::DownloadTestObserverInterrupted>
      interruption_observer(new content::DownloadTestObserverInterrupted(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_QUIT));
  std::unique_ptr<content::DownloadTestObserver> completion_observer(
      new content::DownloadTestObserverTerminal(
          DownloadManagerForBrowser(browser()), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_IGNORE));
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(download_url), WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_NONE);
  interruption_observer->WaitForFinished();

  // Get the download from the DownloadManager.
  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  EXPECT_TRUE(downloads[0]->IsDangerous());

  // Save fake pings for the download.
  safe_browsing::ClientDownloadReport fake_metadata;
  fake_metadata.mutable_download_request()->set_url("http://test");
  fake_metadata.mutable_download_request()->set_length(1);
  fake_metadata.mutable_download_request()->mutable_digests()->set_sha1("hi");
  fake_metadata.mutable_download_response()->set_verdict(
      safe_browsing::ClientDownloadResponse::UNCOMMON);
  std::string ping_request(
      fake_metadata.download_request().SerializeAsString());
  std::string ping_response(
      fake_metadata.download_response().SerializeAsString());
  safe_browsing::SafeBrowsingService* sb_service =
      g_browser_process->safe_browsing_service();
  safe_browsing::DownloadProtectionService* download_protection_service =
      sb_service->download_protection_service();
  download_protection_service->feedback_service()->MaybeStorePingsForDownload(
      safe_browsing::DownloadCheckResult::UNCOMMON, true /* upload_requested */,
      downloads[0], ping_request, ping_response);
  ASSERT_TRUE(safe_browsing::DownloadFeedbackService::IsEnabledForDownload(
      *(downloads[0])));

  // Begin feedback and check that file is still there.
  download_protection_service->feedback_service()->BeginFeedbackForDownload(
      downloads[0], DownloadCommands::KEEP);
  completion_observer->WaitForFinished();

  std::vector<DownloadItem*> updated_downloads;
  GetDownloads(browser(), &updated_downloads);
  ASSERT_EQ(std::size_t(1), updated_downloads.size());
  ASSERT_FALSE(updated_downloads[0]->IsDangerous());
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  ASSERT_TRUE(PathExists(updated_downloads[0]->GetTargetFilePath()));
  updated_downloads[0]->Cancel(true);
}

IN_PROC_BROWSER_TEST_F(DownloadTestWithFakeSafeBrowsing,
                       SendUncommonDownloadReportIfUserProceed) {
  browser()->profile()->GetPrefs()->SetBoolean(prefs::kSafeBrowsingEnabled,
                                               true);
  // Make a dangerous file.
  GURL download_url(
      net::URLRequestMockHTTPJob::GetMockUrl(kDangerousMockFilePath));
  std::unique_ptr<content::DownloadTestObserver> dangerous_observer(
      DangerousDownloadWaiter(
          browser(), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_QUIT));
  ui_test_utils::NavigateToURL(browser(), download_url);
  dangerous_observer->WaitForFinished();

  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  DownloadItem* download = downloads[0];
  DownloadCommands(download).ExecuteCommand(DownloadCommands::KEEP);

  safe_browsing::ClientSafeBrowsingReportRequest actual_report;
  actual_report.ParseFromString(
      test_safe_browsing_factory_->fake_safe_browsing_service()
          ->serilized_download_report());
  EXPECT_EQ(safe_browsing::ClientSafeBrowsingReportRequest::
                DANGEROUS_DOWNLOAD_WARNING,
            actual_report.type());
  EXPECT_EQ(safe_browsing::ClientDownloadResponse::UNCOMMON,
            actual_report.download_verdict());
  EXPECT_EQ(download_url.spec(), actual_report.url());
  EXPECT_TRUE(actual_report.did_proceed());

  download->Cancel(true);
}

IN_PROC_BROWSER_TEST_F(
     DownloadTestWithFakeSafeBrowsing,
     NoUncommonDownloadReportWithoutUserProceed) {
  browser()->profile()->GetPrefs()->SetBoolean(prefs::kSafeBrowsingEnabled,
                                               true);
  // Make a dangerous file.
  GURL download_url(
      net::URLRequestMockHTTPJob::GetMockUrl(kDangerousMockFilePath));
  std::unique_ptr<content::DownloadTestObserver> dangerous_observer(
      DangerousDownloadWaiter(
          browser(), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_QUIT));
  ui_test_utils::NavigateToURL(browser(), download_url);
  dangerous_observer->WaitForFinished();

  std::vector<DownloadItem*> downloads;
  DownloadManagerForBrowser(browser())->GetAllDownloads(&downloads);
  ASSERT_EQ(1u, downloads.size());
  DownloadItem* download = downloads[0];
  DownloadCommands(download).ExecuteCommand(DownloadCommands::DISCARD);

  EXPECT_TRUE(test_safe_browsing_factory_->fake_safe_browsing_service()
                  ->serilized_download_report()
                  .empty());
}
#endif  // FULL_SAFE_BROWSING

// The rest of these tests rely on the download shelf, which ChromeOS doesn't
// use.
#if !defined(OS_CHROMEOS)
// Test that the download shelf is shown by starting a download.
IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadAndWait) {
  GURL url = net::URLRequestMockHTTPJob::GetMockUrl("downloads/a_zip_file.zip");
  DownloadAndWait(browser(), url);

  // The download shelf should be visible.
  EXPECT_TRUE(browser()->window()->IsDownloadShelfVisible());
}

// Test that the download shelf is per-window by starting a download in one
// tab, opening a second tab, closing the shelf, going back to the first tab,
// and checking that the shelf is closed.
IN_PROC_BROWSER_TEST_F(DownloadTest, PerWindowShelf) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl("download-test3.gif"));
  base::FilePath download_file(
      FILE_PATH_LITERAL("download-test3-attachment.gif"));

  // Download a file and wait.
  DownloadAndWait(browser(), url);

  base::FilePath file(FILE_PATH_LITERAL("download-test3.gif"));
  CheckDownload(browser(), download_file, file);

  // Check state.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  EXPECT_TRUE(browser()->window()->IsDownloadShelfVisible());

  // Open a second tab and wait.
  EXPECT_NE(static_cast<WebContents*>(NULL),
            chrome::AddSelectedTabWithURL(browser(), GURL(url::kAboutBlankURL),
                                          ui::PAGE_TRANSITION_TYPED));
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_TRUE(browser()->window()->IsDownloadShelfVisible());

  // Hide the download shelf.
  browser()->window()->GetDownloadShelf()->Close(DownloadShelf::AUTOMATIC);
  EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());

  // Go to the first tab.
  browser()->tab_strip_model()->ActivateTabAt(0, true);
  EXPECT_EQ(2, browser()->tab_strip_model()->count());

  // The download shelf should not be visible.
  EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());
}

// Check whether the downloads shelf is closed when the downloads tab is
// invoked.
IN_PROC_BROWSER_TEST_F(DownloadTest, CloseShelfOnDownloadsTab) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));

  // Download the file and wait.  We do not expect the Select File dialog.
  DownloadAndWait(browser(), url);

  // Check state.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  EXPECT_TRUE(browser()->window()->IsDownloadShelfVisible());

  // Open the downloads tab.
  chrome::ShowDownloads(browser());
  // The shelf should now be closed.
  EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());
}

// Test that when downloading an item in Incognito mode, the download shelf is
// not visible after closing the Incognito window.
IN_PROC_BROWSER_TEST_F(DownloadTest, IncognitoDownloadShelfVisibility) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);

  // Download a file in the Incognito window and wait.
  CreateAndSetDownloadsDirectory(incognito);
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));
  // Since |incognito| is a separate browser, we have to set it up explicitly.
  incognito->profile()->GetPrefs()->SetBoolean(prefs::kPromptForDownload,
                                               false);
  DownloadAndWait(incognito, url);

  // Verify that the download shelf is showing for the Incognito window.
  EXPECT_TRUE(incognito->window()->IsDownloadShelfVisible());

  // Verify that the regular window does not have a download shelf.
  EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());
}

// Download a file in a new window.
// Verify that we have 2 windows, and the download shelf is not visible in the
// first window, but is visible in the second window.
// Close the new window.
// Verify that we have 1 window, and the download shelf is not visible.
//
// Regression test for http://crbug.com/44454
IN_PROC_BROWSER_TEST_F(DownloadTest, NewWindow) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));
#if !defined(OS_MACOSX)
  // See below.
  Browser* first_browser = browser();
#endif

  // Download a file in a new window and wait.
  DownloadAndWaitWithDisposition(browser(), url,
                                 WindowOpenDisposition::NEW_WINDOW,
                                 ui_test_utils::BROWSER_TEST_NONE);

  // When the download finishes, the download shelf SHOULD NOT be visible in
  // the first window.
  ExpectWindowCountAfterDownload(2);
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  // Download shelf should close.
  EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());

  // The download shelf SHOULD be visible in the second window.
  std::set<Browser*> original_browsers;
  original_browsers.insert(browser());
  Browser* download_browser =
      ui_test_utils::GetBrowserNotInSet(original_browsers);
  ASSERT_TRUE(download_browser != NULL);
  EXPECT_NE(download_browser, browser());
  EXPECT_EQ(1, download_browser->tab_strip_model()->count());
  EXPECT_TRUE(download_browser->window()->IsDownloadShelfVisible());

#if !defined(OS_MACOSX)
  // On Mac OS X, the UI window close is delayed until the outermost
  // message loop runs.  So it isn't possible to get a BROWSER_CLOSED
  // notification inside of a test.
  content::WindowedNotificationObserver signal(
      chrome::NOTIFICATION_BROWSER_CLOSED,
      content::Source<Browser>(download_browser));
#endif

  // Close the new window.
  chrome::CloseWindow(download_browser);

#if !defined(OS_MACOSX)
  signal.Wait();
  EXPECT_EQ(first_browser, browser());
  ExpectWindowCountAfterDownload(1);
#endif

  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  // Download shelf should close.
  EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());

  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  CheckDownload(browser(), file, file);
}

IN_PROC_BROWSER_TEST_F(DownloadTest, PRE_DownloadTest_History) {
  // Download a file and wait for it to be stored.
  GURL download_url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));
  HistoryObserver observer(browser()->profile());
  DownloadAndWait(browser(), download_url);
  observer.WaitForStored();
  HistoryServiceFactory::GetForProfile(browser()->profile(),
                                       ServiceAccessType::IMPLICIT_ACCESS)
      ->FlushForTest(base::Bind(&base::RunLoop::QuitCurrentWhenIdleDeprecated));
  content::RunMessageLoop();
}

IN_PROC_BROWSER_TEST_F(DownloadTest, DownloadTest_History) {
  // This starts up right after PRE_DownloadTest_History and shares the same
  // profile directory.
  GURL download_url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));
  std::vector<DownloadItem*> downloads;
  content::DownloadManager* manager = DownloadManagerForBrowser(browser());

  // Wait for the history to be loaded with a single DownloadItem. Check that
  // it's the file that was downloaded in PRE_DownloadTest_History.
  base::FilePath file(FILE_PATH_LITERAL("download-test1.lib"));
  CreatedObserver created_observer(manager);
  created_observer.Wait();
  manager->GetAllDownloads(&downloads);
  ASSERT_EQ(1UL, downloads.size());
  DownloadItem* item = downloads[0];
  EXPECT_EQ(file.value(), item->GetFullPath().BaseName().value());
  EXPECT_EQ(file.value(), item->GetTargetFilePath().BaseName().value());
  EXPECT_EQ(download_url, item->GetURL());
  // The following are set by download-test1.lib.mock-http-headers.
  std::string etag = item->GetETag();
  base::TrimWhitespaceASCII(etag, base::TRIM_ALL, &etag);
  EXPECT_EQ("abracadabra", etag);

  std::string last_modified = item->GetLastModifiedTime();
  base::TrimWhitespaceASCII(last_modified, base::TRIM_ALL, &last_modified);
  EXPECT_EQ("Mon, 13 Nov 2006 20:31:09 GMT", last_modified);

  // Downloads that were restored from history shouldn't cause the download
  // shelf to be displayed.
  EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());
}

IN_PROC_BROWSER_TEST_F(DownloadTest, HiddenDownload) {
  GURL url(URLRequestMockHTTPJob::GetMockUrl(kDownloadTest1Path));

  DownloadManager* download_manager = DownloadManagerForBrowser(browser());
  std::unique_ptr<content::DownloadTestObserver> observer(
      new content::DownloadTestObserverTerminal(
          download_manager, 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL));

  // Download and set IsHiddenDownload to true.
  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  std::unique_ptr<DownloadUrlParameters> params(
      DownloadUrlParameters::CreateForWebContentsMainFrame(
          web_contents, url, TRAFFIC_ANNOTATION_FOR_TESTS));
  params->set_callback(base::Bind(&SetHiddenDownloadCallback));
  download_manager->DownloadUrl(std::move(params));
  observer->WaitForFinished();

  // Verify that download shelf is not shown.
  EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());
}

IN_PROC_BROWSER_TEST_F(DownloadTest, AutoOpenClosesShelf) {
  base::FilePath file(FILE_PATH_LITERAL("download-autoopen.txt"));
  GURL url(URLRequestMockHTTPJob::GetMockUrl("download-autoopen.txt"));

  ASSERT_TRUE(
      GetDownloadPrefs(browser())->EnableAutoOpenBasedOnExtension(file));

  DownloadAndWait(browser(), url);

  // Download shelf should close.
  EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());
}

IN_PROC_BROWSER_TEST_F(DownloadTest, CrxDenyInstallClosesShelf) {
  std::unique_ptr<base::AutoReset<bool>> allow_offstore_install =
      download_crx_util::OverrideOffstoreInstallAllowedForTesting(true);

  GURL extension_url(URLRequestMockHTTPJob::GetMockUrl(kGoodCrxPath));

  std::unique_ptr<content::DownloadTestObserver> observer(
      DangerousDownloadWaiter(
          browser(), 1,
          content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_DENY));
  ui_test_utils::NavigateToURL(browser(), extension_url);

  observer->WaitForFinished();

  // Download shelf should close.
  EXPECT_FALSE(browser()->window()->IsDownloadShelfVisible());
}
#endif  // defined(OS_CHROMEOS)
