// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/loader/url_loader_factory_impl.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/loader/mojo_async_resource_handler.h"
#include "content/browser/loader/navigation_resource_throttle.h"
#include "content/browser/loader/resource_dispatcher_host_impl.h"
#include "content/browser/loader/resource_message_filter.h"
#include "content/browser/loader/resource_request_info_impl.h"
#include "content/browser/loader_delegate_impl.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/resource_dispatcher_host_delegate.h"
#include "content/public/common/content_paths.h"
#include "content/public/common/resource_request.h"
#include "content/public/common/resource_request_completion_status.h"
#include "content/public/common/url_loader.mojom.h"
#include "content/public/common/url_loader_factory.mojom.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/test_url_loader_client.h"
#include "mojo/public/c/system/data_pipe.h"
#include "mojo/public/c/system/types.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/http/http_status_code.h"
#include "net/http/http_util.h"
#include "net/test/url_request/url_request_failed_job.h"
#include "net/test/url_request/url_request_mock_http_job.h"
#include "net/test/url_request/url_request_slow_download_job.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request_filter.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {

namespace {

constexpr int kChildId = 99;

class RejectingResourceDispatcherHostDelegate final
    : public ResourceDispatcherHostDelegate {
 public:
  RejectingResourceDispatcherHostDelegate() {}
  bool ShouldBeginRequest(const std::string& method,
                          const GURL& url,
                          ResourceType resource_type,
                          ResourceContext* resource_context) override {
    return false;
  }

  DISALLOW_COPY_AND_ASSIGN(RejectingResourceDispatcherHostDelegate);
};

// The test parameter is the number of bytes allocated for the buffer in the
// data pipe, for testing the case where the allocated size is smaller than the
// size the mime sniffer *implicitly* requires.
class URLLoaderFactoryImplTest : public ::testing::TestWithParam<size_t> {
 public:
  URLLoaderFactoryImplTest()
      : thread_bundle_(
            new TestBrowserThreadBundle(TestBrowserThreadBundle::IO_MAINLOOP)),
        browser_context_(new TestBrowserContext()),
        resource_message_filter_(new ResourceMessageFilter(
            kChildId,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            base::Bind(&URLLoaderFactoryImplTest::GetContexts,
                       base::Unretained(this)),
            BrowserThread::GetTaskRunnerForThread(BrowserThread::IO))) {
    // Some tests specify request.report_raw_headers, but the RDH checks the
    // CanReadRawCookies permission before enabling it.
    ChildProcessSecurityPolicyImpl::GetInstance()->Add(kChildId);
    ChildProcessSecurityPolicyImpl::GetInstance()->GrantReadRawCookies(
        kChildId);

    resource_message_filter_->InitializeForTest();
    MojoAsyncResourceHandler::SetAllocationSizeForTesting(GetParam());
    rdh_.SetLoaderDelegate(&loader_deleate_);

    URLLoaderFactoryImpl::Create(
        resource_message_filter_->requester_info_for_test(),
        mojo::MakeRequest(&factory_),
        BrowserThread::GetTaskRunnerForThread(BrowserThread::IO));

    // Calling this function creates a request context.
    browser_context_->GetResourceContext()->GetRequestContext();
    base::RunLoop().RunUntilIdle();
  }

  ~URLLoaderFactoryImplTest() override {
    ChildProcessSecurityPolicyImpl::GetInstance()->Remove(kChildId);
    rdh_.SetDelegate(nullptr);
    net::URLRequestFilter::GetInstance()->ClearHandlers();

    resource_message_filter_->OnChannelClosing();
    rdh_.CancelRequestsForProcess(resource_message_filter_->child_id());
    base::RunLoop().RunUntilIdle();
    MojoAsyncResourceHandler::SetAllocationSizeForTesting(
        MojoAsyncResourceHandler::kDefaultAllocationSize);
    thread_bundle_.reset(nullptr);
  }

  void GetContexts(ResourceType resource_type,
                   ResourceContext** resource_context,
                   net::URLRequestContext** request_context) {
    *resource_context = browser_context_->GetResourceContext();
    *request_context =
        browser_context_->GetResourceContext()->GetRequestContext();
  }

  std::unique_ptr<TestBrowserThreadBundle> thread_bundle_;
  LoaderDelegateImpl loader_deleate_;
  ResourceDispatcherHostImpl rdh_;
  std::unique_ptr<TestBrowserContext> browser_context_;
  scoped_refptr<ResourceMessageFilter> resource_message_filter_;
  mojom::URLLoaderFactoryPtr factory_;

  DISALLOW_COPY_AND_ASSIGN(URLLoaderFactoryImplTest);
};

TEST_P(URLLoaderFactoryImplTest, GetResponse) {
  constexpr int32_t kRoutingId = 81;
  constexpr int32_t kRequestId = 28;
  NavigationResourceThrottle::set_ui_checks_always_succeed_for_testing(true);
  mojom::URLLoaderPtr loader;
  base::FilePath root;
  PathService::Get(DIR_TEST_DATA, &root);
  net::URLRequestMockHTTPJob::AddUrlHandlers(root);
  ResourceRequest request;
  TestURLLoaderClient client;
  // Assume the file contents is small enough to be stored in the data pipe.
  request.url = net::URLRequestMockHTTPJob::GetMockUrl("hello.html");
  request.method = "GET";
  // |resource_type| can't be a frame type. It is because when PlzNavigate is
  // enabled, the url scheme of frame type requests from the renderer process
  // must be blob scheme.
  request.resource_type = RESOURCE_TYPE_XHR;
  // Need to set |request_initiator| for non main frame type request.
  request.request_initiator = url::Origin();
  factory_->CreateLoaderAndStart(
      mojo::MakeRequest(&loader), kRoutingId, kRequestId,
      mojom::kURLLoadOptionNone, request, client.CreateInterfacePtr(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));

  ASSERT_FALSE(client.has_received_response());
  ASSERT_FALSE(client.response_body().is_valid());
  ASSERT_FALSE(client.has_received_completion());

  client.RunUntilResponseReceived();

  net::URLRequest* url_request =
      rdh_.GetURLRequest(GlobalRequestID(kChildId, kRequestId));
  ASSERT_TRUE(url_request);
  ResourceRequestInfoImpl* request_info =
      ResourceRequestInfoImpl::ForRequest(url_request);
  ASSERT_TRUE(request_info);
  EXPECT_EQ(kChildId, request_info->GetChildID());
  EXPECT_EQ(kRoutingId, request_info->GetRouteID());
  EXPECT_EQ(kRequestId, request_info->GetRequestID());

  ASSERT_FALSE(client.has_received_completion());

  client.RunUntilComplete();
  ASSERT_TRUE(client.response_body().is_valid());
  ASSERT_TRUE(client.has_received_completion());

  EXPECT_EQ(200, client.response_head().headers->response_code());
  std::string content_type;
  client.response_head().headers->GetNormalizedHeader("content-type",
                                                      &content_type);
  EXPECT_EQ("text/html", content_type);
  EXPECT_EQ(0, client.completion_status().error_code);

  std::string contents;
  while (true) {
    char buffer[16];
    uint32_t read_size = sizeof(buffer);
    MojoResult r = client.response_body().ReadData(buffer, &read_size,
                                                   MOJO_READ_DATA_FLAG_NONE);
    if (r == MOJO_RESULT_FAILED_PRECONDITION)
      break;
    if (r == MOJO_RESULT_SHOULD_WAIT)
      continue;
    ASSERT_EQ(MOJO_RESULT_OK, r);
    contents += std::string(buffer, read_size);
  }
  std::string expected;
  base::ReadFileToString(
      root.Append(base::FilePath(FILE_PATH_LITERAL("hello.html"))), &expected);
  EXPECT_EQ(expected, contents);
  EXPECT_EQ(static_cast<int64_t>(expected.size()) +
                client.response_head().encoded_data_length,
            client.completion_status().encoded_data_length);
  EXPECT_EQ(static_cast<int64_t>(expected.size()),
            client.completion_status().encoded_body_length);
  // OnTransferSizeUpdated is not dispatched as report_raw_headers is not set.
  EXPECT_EQ(0, client.body_transfer_size());
  EXPECT_GT(client.response_head().encoded_data_length, 0);
  EXPECT_GT(client.completion_status().encoded_data_length, 0);
}

TEST_P(URLLoaderFactoryImplTest, GetFailedResponse) {
  NavigationResourceThrottle::set_ui_checks_always_succeed_for_testing(true);
  mojom::URLLoaderPtr loader;
  ResourceRequest request;
  TestURLLoaderClient client;
  net::URLRequestFailedJob::AddUrlHandler();
  request.url = net::URLRequestFailedJob::GetMockHttpUrlWithFailurePhase(
      net::URLRequestFailedJob::START, net::ERR_TIMED_OUT);
  request.method = "GET";
  // |resource_type| can't be a frame type. It is because when PlzNavigate is
  // enabled, the url scheme of frame type requests from the renderer process
  // must be blob scheme.
  request.resource_type = RESOURCE_TYPE_XHR;
  // Need to set |request_initiator| for non main frame type request.
  request.request_initiator = url::Origin();
  factory_->CreateLoaderAndStart(
      mojo::MakeRequest(&loader), 2, 1, mojom::kURLLoadOptionNone, request,
      client.CreateInterfacePtr(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));

  client.RunUntilComplete();
  ASSERT_FALSE(client.has_received_response());
  ASSERT_FALSE(client.response_body().is_valid());

  EXPECT_EQ(net::ERR_TIMED_OUT, client.completion_status().error_code);
  EXPECT_EQ(0, client.completion_status().encoded_data_length);
  EXPECT_EQ(0, client.completion_status().encoded_body_length);
}

// In this case, the loading fails after receiving a response.
TEST_P(URLLoaderFactoryImplTest, GetFailedResponse2) {
  NavigationResourceThrottle::set_ui_checks_always_succeed_for_testing(true);
  mojom::URLLoaderPtr loader;
  ResourceRequest request;
  TestURLLoaderClient client;
  net::URLRequestFailedJob::AddUrlHandler();
  request.url = net::URLRequestFailedJob::GetMockHttpUrlWithFailurePhase(
      net::URLRequestFailedJob::READ_ASYNC, net::ERR_TIMED_OUT);
  request.method = "GET";
  // |resource_type| can't be a frame type. It is because when PlzNavigate is
  // enabled, the url scheme of frame type requests from the renderer process
  // must be blob scheme.
  request.resource_type = RESOURCE_TYPE_XHR;
  // Need to set |request_initiator| for non main frame type request.
  request.request_initiator = url::Origin();
  factory_->CreateLoaderAndStart(
      mojo::MakeRequest(&loader), 2, 1, mojom::kURLLoadOptionNone, request,
      client.CreateInterfacePtr(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));

  client.RunUntilComplete();
  ASSERT_FALSE(client.has_received_response());
  ASSERT_FALSE(client.response_body().is_valid());

  EXPECT_EQ(net::ERR_TIMED_OUT, client.completion_status().error_code);
  EXPECT_GT(client.completion_status().encoded_data_length, 0);
  EXPECT_EQ(0, client.completion_status().encoded_body_length);
}

// This test tests a case where resource loading is cancelled before started.
TEST_P(URLLoaderFactoryImplTest, InvalidURL) {
  mojom::URLLoaderPtr loader;
  ResourceRequest request;
  TestURLLoaderClient client;
  request.url = GURL();
  request.method = "GET";
  // |resource_type| can't be a frame type. It is because when PlzNavigate is
  // enabled, the url scheme of frame type requests from the renderer process
  // must be blob scheme.
  request.resource_type = RESOURCE_TYPE_XHR;
  // Need to set |request_initiator| for non main frame type request.
  request.request_initiator = url::Origin();
  ASSERT_FALSE(request.url.is_valid());
  factory_->CreateLoaderAndStart(
      mojo::MakeRequest(&loader), 2, 1, mojom::kURLLoadOptionNone, request,
      client.CreateInterfacePtr(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));

  client.RunUntilComplete();
  ASSERT_FALSE(client.has_received_response());
  ASSERT_FALSE(client.response_body().is_valid());

  EXPECT_EQ(net::ERR_ABORTED, client.completion_status().error_code);
}

// This test tests a case where resource loading is cancelled before started.
TEST_P(URLLoaderFactoryImplTest, ShouldNotRequestURL) {
  mojom::URLLoaderPtr loader;
  RejectingResourceDispatcherHostDelegate rdh_delegate;
  rdh_.SetDelegate(&rdh_delegate);
  ResourceRequest request;
  TestURLLoaderClient client;
  request.url = GURL("http://localhost/");
  request.method = "GET";
  // |resource_type| can't be a frame type. It is because when PlzNavigate is
  // enabled, the url scheme of frame type requests from the renderer process
  // must be blob scheme.
  request.resource_type = RESOURCE_TYPE_XHR;
  // Need to set |request_initiator| for non main frame type request.
  request.request_initiator = url::Origin();
  factory_->CreateLoaderAndStart(
      mojo::MakeRequest(&loader), 2, 1, mojom::kURLLoadOptionNone, request,
      client.CreateInterfacePtr(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));

  client.RunUntilComplete();
  rdh_.SetDelegate(nullptr);

  ASSERT_FALSE(client.has_received_response());
  ASSERT_FALSE(client.response_body().is_valid());

  EXPECT_EQ(net::ERR_ABORTED, client.completion_status().error_code);
}

TEST_P(URLLoaderFactoryImplTest, DownloadToFile) {
  constexpr int32_t kRoutingId = 1;
  constexpr int32_t kRequestId = 2;

  mojom::URLLoaderPtr loader;
  base::FilePath root;
  PathService::Get(DIR_TEST_DATA, &root);
  net::URLRequestMockHTTPJob::AddUrlHandlers(root);

  ResourceRequest request;
  TestURLLoaderClient client;
  request.url = net::URLRequestMockHTTPJob::GetMockUrl("hello.html");
  request.method = "GET";
  request.resource_type = RESOURCE_TYPE_XHR;
  request.download_to_file = true;
  request.request_initiator = url::Origin();
  factory_->CreateLoaderAndStart(
      mojo::MakeRequest(&loader), kRoutingId, kRequestId, 0, request,
      client.CreateInterfacePtr(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
  ASSERT_FALSE(client.has_received_response());
  ASSERT_FALSE(client.has_data_downloaded());
  ASSERT_FALSE(client.has_received_completion());

  client.RunUntilResponseReceived();

  net::URLRequest* url_request =
      rdh_.GetURLRequest(GlobalRequestID(kChildId, kRequestId));
  ASSERT_TRUE(url_request);
  ResourceRequestInfoImpl* request_info =
      ResourceRequestInfoImpl::ForRequest(url_request);
  ASSERT_TRUE(request_info);
  EXPECT_EQ(kChildId, request_info->GetChildID());
  EXPECT_EQ(kRoutingId, request_info->GetRouteID());
  EXPECT_EQ(kRequestId, request_info->GetRequestID());

  ASSERT_FALSE(client.has_received_completion());

  client.RunUntilComplete();
  ASSERT_TRUE(client.has_data_downloaded());
  ASSERT_TRUE(client.has_received_completion());

  EXPECT_EQ(200, client.response_head().headers->response_code());
  std::string content_type;
  client.response_head().headers->GetNormalizedHeader("content-type",
                                                      &content_type);
  EXPECT_EQ("text/html", content_type);
  EXPECT_EQ(0, client.completion_status().error_code);

  std::string contents;
  base::ReadFileToString(client.response_head().download_file_path, &contents);

  EXPECT_EQ(static_cast<int64_t>(contents.size()),
            client.download_data_length());
  EXPECT_EQ(static_cast<int64_t>(contents.size()),
            client.encoded_download_data_length());

  std::string expected;
  base::ReadFileToString(
      root.Append(base::FilePath(FILE_PATH_LITERAL("hello.html"))), &expected);
  EXPECT_EQ(expected, contents);
  EXPECT_EQ(static_cast<int64_t>(expected.size()) +
                client.response_head().encoded_data_length,
            client.completion_status().encoded_data_length);
  EXPECT_EQ(static_cast<int64_t>(expected.size()),
            client.completion_status().encoded_body_length);
}

TEST_P(URLLoaderFactoryImplTest, DownloadToFileFailure) {
  constexpr int32_t kRoutingId = 1;
  constexpr int32_t kRequestId = 2;

  mojom::URLLoaderPtr loader;
  base::FilePath root;
  PathService::Get(DIR_TEST_DATA, &root);
  net::URLRequestSlowDownloadJob::AddUrlHandler();

  ResourceRequest request;
  TestURLLoaderClient client;
  request.url = GURL(net::URLRequestSlowDownloadJob::kKnownSizeUrl);
  request.method = "GET";
  request.resource_type = RESOURCE_TYPE_XHR;
  request.download_to_file = true;
  request.request_initiator = url::Origin();
  factory_->CreateLoaderAndStart(
      mojo::MakeRequest(&loader), kRoutingId, kRequestId, 0, request,
      client.CreateInterfacePtr(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
  ASSERT_FALSE(client.has_received_response());
  ASSERT_FALSE(client.has_data_downloaded());
  ASSERT_FALSE(client.has_received_completion());

  client.RunUntilResponseReceived();

  net::URLRequest* url_request =
      rdh_.GetURLRequest(GlobalRequestID(kChildId, kRequestId));
  ASSERT_TRUE(url_request);
  ResourceRequestInfoImpl* request_info =
      ResourceRequestInfoImpl::ForRequest(url_request);
  ASSERT_TRUE(request_info);
  EXPECT_EQ(kChildId, request_info->GetChildID());
  EXPECT_EQ(kRoutingId, request_info->GetRouteID());
  EXPECT_EQ(kRequestId, request_info->GetRequestID());

  ASSERT_FALSE(client.has_received_completion());

  client.RunUntilDataDownloaded();
  ASSERT_TRUE(client.has_data_downloaded());
  ASSERT_FALSE(client.has_received_completion());
  EXPECT_LT(0, client.download_data_length());
  EXPECT_GE(
      static_cast<int64_t>(net::URLRequestSlowDownloadJob::kFirstDownloadSize),
      client.download_data_length());
  EXPECT_LT(0, client.encoded_download_data_length());
  EXPECT_GE(
      static_cast<int64_t>(net::URLRequestSlowDownloadJob::kFirstDownloadSize),
      client.encoded_download_data_length());

  url_request->Cancel();
  client.RunUntilComplete();

  ASSERT_TRUE(client.has_received_completion());

  EXPECT_EQ(200, client.response_head().headers->response_code());
  EXPECT_EQ(net::ERR_ABORTED, client.completion_status().error_code);
}

TEST_P(URLLoaderFactoryImplTest, OnTransferSizeUpdated) {
  constexpr int32_t kRoutingId = 81;
  constexpr int32_t kRequestId = 28;
  NavigationResourceThrottle::set_ui_checks_always_succeed_for_testing(true);
  mojom::URLLoaderPtr loader;
  base::FilePath root;
  PathService::Get(DIR_TEST_DATA, &root);
  net::URLRequestMockHTTPJob::AddUrlHandlers(root);
  ResourceRequest request;
  TestURLLoaderClient client;
  // Assume the file contents is small enough to be stored in the data pipe.
  request.url = net::URLRequestMockHTTPJob::GetMockUrl("gzip-content.svgz");
  request.method = "GET";
  // |resource_type| can't be a frame type. It is because when PlzNavigate is
  // enabled, the url scheme of frame type requests from the renderer process
  // must be blob scheme.
  request.resource_type = RESOURCE_TYPE_XHR;
  // Need to set |request_initiator| for non main frame type request.
  request.request_initiator = url::Origin();
  request.report_raw_headers = true;
  factory_->CreateLoaderAndStart(
      mojo::MakeRequest(&loader), kRoutingId, kRequestId,
      mojom::kURLLoadOptionNone, request, client.CreateInterfacePtr(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));

  client.RunUntilComplete();

  std::string contents;
  while (true) {
    char buffer[16];
    uint32_t read_size = sizeof(buffer);
    MojoResult r = client.response_body().ReadData(buffer, &read_size,
                                                   MOJO_READ_DATA_FLAG_NONE);
    if (r == MOJO_RESULT_FAILED_PRECONDITION)
      break;
    if (r == MOJO_RESULT_SHOULD_WAIT)
      continue;
    ASSERT_EQ(MOJO_RESULT_OK, r);
    contents.append(buffer, read_size);
  }

  std::string expected_encoded_body;
  base::ReadFileToString(
      root.Append(base::FilePath(FILE_PATH_LITERAL("gzip-content.svgz"))),
      &expected_encoded_body);

  EXPECT_GT(client.response_head().encoded_data_length, 0);
  EXPECT_GT(client.completion_status().encoded_data_length, 0);
  EXPECT_EQ(static_cast<int64_t>(expected_encoded_body.size()),
            client.body_transfer_size());
  EXPECT_EQ(200, client.response_head().headers->response_code());
  EXPECT_EQ(
      client.response_head().encoded_data_length + client.body_transfer_size(),
      client.completion_status().encoded_data_length);
  EXPECT_NE(client.body_transfer_size(), static_cast<int64_t>(contents.size()));
  EXPECT_EQ(client.body_transfer_size(),
            client.completion_status().encoded_body_length);
  EXPECT_EQ(contents, "Hello World!\n");
}

// Removing the loader in the remote side will cancel the request.
TEST_P(URLLoaderFactoryImplTest, CancelFromRenderer) {
  constexpr int32_t kRoutingId = 81;
  constexpr int32_t kRequestId = 28;
  NavigationResourceThrottle::set_ui_checks_always_succeed_for_testing(true);
  mojom::URLLoaderPtr loader;
  base::FilePath root;
  PathService::Get(DIR_TEST_DATA, &root);
  net::URLRequestFailedJob::AddUrlHandler();
  ResourceRequest request;
  TestURLLoaderClient client;
  // Assume the file contents is small enough to be stored in the data pipe.
  request.url = net::URLRequestFailedJob::GetMockHttpUrl(net::ERR_IO_PENDING);
  request.method = "GET";
  request.is_main_frame = true;
  // |resource_type| can't be a frame type. It is because when PlzNavigate is
  // enabled, the url scheme of frame type requests from the renderer process
  // must be blob scheme.
  request.resource_type = RESOURCE_TYPE_XHR;
  // Need to set |request_initiator| for non main frame type request.
  request.request_initiator = url::Origin();
  factory_->CreateLoaderAndStart(
      mojo::MakeRequest(&loader), kRoutingId, kRequestId,
      mojom::kURLLoadOptionNone, request, client.CreateInterfacePtr(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));

  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(rdh_.GetURLRequest(GlobalRequestID(kChildId, kRequestId)));
  ASSERT_FALSE(client.has_received_response());
  ASSERT_FALSE(client.response_body().is_valid());
  ASSERT_FALSE(client.has_received_completion());

  loader = nullptr;
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(rdh_.GetURLRequest(GlobalRequestID(kChildId, kRequestId)));
}

INSTANTIATE_TEST_CASE_P(URLLoaderFactoryImplTest,
                        URLLoaderFactoryImplTest,
                        ::testing::Values(128, 32 * 1024));

}  // namespace

}  // namespace content
