// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/chrome_render_frame_observer.h"

#include <stddef.h>
#include <string.h>

#include <limits>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/crash_keys.h"
#include "chrome/common/open_search_description_document_handler.mojom.h"
#include "chrome/common/prerender_messages.h"
#include "chrome/common/render_messages.h"
#include "chrome/renderer/prerender/prerender_helper.h"
#include "chrome/renderer/safe_browsing/phishing_classifier_delegate.h"
#include "chrome/renderer/web_apps.h"
#include "components/translate/content/renderer/translate_helper.h"
#include "content/public/common/associated_interface_provider.h"
#include "content/public/common/associated_interface_registry.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_view.h"
#include "content/public/renderer/window_features_converter.h"
#include "extensions/common/constants.h"
#include "printing/features/features.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "skia/ext/image_operations.h"
#include "third_party/WebKit/public/platform/WebImage.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebDocumentLoader.h"
#include "third_party/WebKit/public/web/WebElement.h"
#include "third_party/WebKit/public/web/WebFrameContentDumper.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebNode.h"
#include "third_party/WebKit/public/web/WebSecurityPolicy.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size_f.h"
#include "url/gurl.h"

#if BUILDFLAG(ENABLE_PRINTING)
#include "components/printing/common/print_messages.h"
#include "components/printing/renderer/print_render_frame_helper.h"
#endif

using blink::WebDocumentLoader;
using blink::WebElement;
using blink::WebFrameContentDumper;
using blink::WebLocalFrame;
using blink::WebNode;
using blink::WebString;
using content::RenderFrame;

// Maximum number of characters in the document to index.
// Any text beyond this point will be clipped.
static const size_t kMaxIndexChars = 65535;

// Constants for UMA statistic collection.
static const char kTranslateCaptureText[] = "Translate.CaptureText";

// For a page that auto-refreshes, we still show the bubble, if
// the refresh delay is less than this value (in seconds).
static const double kLocationChangeIntervalInSeconds = 10;

// For the context menu, we want to keep transparency as is instead of
// replacing transparent pixels with black ones
static const bool kDiscardTransparencyForContextMenu = false;

namespace {

// If the source image is null or occupies less area than
// |thumbnail_min_area_pixels|, we return the image unmodified.  Otherwise, we
// scale down the image so that the width and height do not exceed
// |thumbnail_max_size_pixels|, preserving the original aspect ratio.
SkBitmap Downscale(const blink::WebImage& image,
                   int thumbnail_min_area_pixels,
                   const gfx::Size& thumbnail_max_size_pixels) {
  if (image.IsNull())
    return SkBitmap();

  gfx::Size image_size = image.Size();

  if (image_size.GetArea() < thumbnail_min_area_pixels)
    return image.GetSkBitmap();

  if (image_size.width() <= thumbnail_max_size_pixels.width() &&
      image_size.height() <= thumbnail_max_size_pixels.height())
    return image.GetSkBitmap();

  gfx::SizeF scaled_size = gfx::SizeF(image_size);

  if (scaled_size.width() > thumbnail_max_size_pixels.width()) {
    scaled_size.Scale(thumbnail_max_size_pixels.width() / scaled_size.width());
  }

  if (scaled_size.height() > thumbnail_max_size_pixels.height()) {
    scaled_size.Scale(
        thumbnail_max_size_pixels.height() / scaled_size.height());
  }

  return skia::ImageOperations::Resize(image.GetSkBitmap(),
                                       skia::ImageOperations::RESIZE_GOOD,
                                       static_cast<int>(scaled_size.width()),
                                       static_cast<int>(scaled_size.height()));
}

}  // namespace

ChromeRenderFrameObserver::ChromeRenderFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame),
      translate_helper_(nullptr),
      phishing_classifier_(nullptr) {
  registry_.AddInterface(
      base::Bind(&ChromeRenderFrameObserver::OnImageContextMenuRendererRequest,
                 base::Unretained(this)));
  registry_.AddInterface(
      base::Bind(&ChromeRenderFrameObserver::OnThumbnailCapturerRequest,
                 base::Unretained(this)));

  // Don't do anything else for subframes.
  if (!render_frame->IsMainFrame())
    return;
  render_frame->GetAssociatedInterfaceRegistry()->AddInterface(
      base::Bind(&ChromeRenderFrameObserver::OnRenderFrameObserverRequest,
                 base::Unretained(this)));
#if defined(SAFE_BROWSING_CSD)
  registry_.AddInterface(
      base::Bind(&ChromeRenderFrameObserver::OnPhishingDetectorRequest,
                 base::Unretained(this)));
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(switches::kDisableClientSidePhishingDetection))
    SetClientSidePhishingDetection(true);
#endif
#if !defined(OS_ANDROID)
  render_frame->GetAssociatedInterfaceRegistry()->AddInterface(
      base::Bind(&ChromeRenderFrameObserver::OnWebUITesterRequest,
                 base::Unretained(this)));
#endif
#if 0
  translate_helper_ = new translate::TranslateHelper(
      render_frame, chrome::ISOLATED_WORLD_ID_TRANSLATE,
      extensions::kExtensionScheme);
#endif
}

ChromeRenderFrameObserver::~ChromeRenderFrameObserver() {
}

void ChromeRenderFrameObserver::OnInterfaceRequestForFrame(
    const std::string& interface_name,
    mojo::ScopedMessagePipeHandle* interface_pipe) {
  registry_.TryBindInterface(interface_name, interface_pipe);
}

bool ChromeRenderFrameObserver::OnMessageReceived(const IPC::Message& message) {
  // Filter only.
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ChromeRenderFrameObserver, message)
    IPC_MESSAGE_HANDLER(PrerenderMsg_SetIsPrerendering, OnSetIsPrerendering)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  if (handled)
    return false;

  IPC_BEGIN_MESSAGE_MAP(ChromeRenderFrameObserver, message)
    IPC_MESSAGE_HANDLER(ChromeFrameMsg_GetWebApplicationInfo,
                        OnGetWebApplicationInfo)
#if BUILDFLAG(ENABLE_PRINTING)
    IPC_MESSAGE_HANDLER(PrintMsg_PrintNodeUnderContextMenu,
                        OnPrintNodeUnderContextMenu)
#endif
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

void ChromeRenderFrameObserver::OnSetIsPrerendering(
    prerender::PrerenderMode mode) {
  if (mode != prerender::NO_PRERENDER) {
    // If the PrerenderHelper for this frame already exists, don't create it. It
    // can already be created for subframes during handling of
    // RenderFrameCreated, if the parent frame was prerendering at time of
    // subframe creation.
    if (prerender::PrerenderHelper::Get(render_frame()))
      return;

    // The PrerenderHelper will destroy itself either after recording histograms
    // or on destruction of the RenderView.
    new prerender::PrerenderHelper(render_frame(), mode);
  }
}

void ChromeRenderFrameObserver::RequestReloadImageForContextNode() {
  WebLocalFrame* frame = render_frame()->GetWebFrame();
  // TODO(dglazkov): This code is clearly in the wrong place. Need
  // to investigate what it is doing and fix (http://crbug.com/606164).
  WebNode context_node = frame->ContextMenuNode();
  if (!context_node.IsNull() && context_node.IsElementNode()) {
    frame->ReloadImage(context_node);
  }
}

void ChromeRenderFrameObserver::RequestThumbnailForContextNode(
    int32_t thumbnail_min_area_pixels,
    const gfx::Size& thumbnail_max_size_pixels,
    chrome::mojom::ImageFormat image_format,
    const RequestThumbnailForContextNodeCallback& callback) {
  WebNode context_node = render_frame()->GetWebFrame()->ContextMenuNode();
  SkBitmap thumbnail;
  gfx::Size original_size;
  if (!context_node.IsNull() && context_node.IsElementNode()) {
    blink::WebImage image = context_node.To<WebElement>().ImageContents();
    original_size = image.Size();
    thumbnail = Downscale(image,
                          thumbnail_min_area_pixels,
                          thumbnail_max_size_pixels);
  }

  SkBitmap bitmap;
  if (thumbnail.colorType() == kN32_SkColorType) {
    bitmap = thumbnail;
  } else {
    SkImageInfo info = thumbnail.info().makeColorType(kN32_SkColorType);
    if (bitmap.tryAllocPixels(info)) {
      thumbnail.readPixels(info, bitmap.getPixels(), bitmap.rowBytes(), 0, 0);
    }
  }

  std::vector<uint8_t> thumbnail_data;
  constexpr int kDefaultQuality = 90;
  std::vector<unsigned char> data;

  switch (image_format) {
    case chrome::mojom::ImageFormat::PNG:
      if (gfx::PNGCodec::EncodeBGRASkBitmap(
              bitmap, kDiscardTransparencyForContextMenu, &data)) {
        thumbnail_data.swap(data);
        break;
      }
    case chrome::mojom::ImageFormat::JPEG:
      if (gfx::JPEGCodec::Encode(bitmap, kDefaultQuality, &data)) {
        thumbnail_data.swap(data);
        break;
      }
  }

  callback.Run(thumbnail_data, original_size);
}

void ChromeRenderFrameObserver::OnPrintNodeUnderContextMenu() {
#if BUILDFLAG(ENABLE_PRINTING)
  printing::PrintRenderFrameHelper* helper =
      printing::PrintRenderFrameHelper::Get(render_frame());
  if (helper)
    helper->PrintNode(render_frame()->GetWebFrame()->ContextMenuNode());
#endif
}

void ChromeRenderFrameObserver::OnGetWebApplicationInfo() {
  WebLocalFrame* frame = render_frame()->GetWebFrame();

  WebApplicationInfo web_app_info;
  web_apps::ParseWebAppFromWebDocument(frame, &web_app_info);

  // The warning below is specific to mobile but it doesn't hurt to show it even
  // if the Chromium build is running on a desktop. It will get more exposition.
  if (web_app_info.mobile_capable == WebApplicationInfo::MOBILE_CAPABLE_APPLE) {
    blink::WebConsoleMessage message(
        blink::WebConsoleMessage::kLevelWarning,
        "<meta name=\"apple-mobile-web-app-capable\" content=\"yes\"> is "
        "deprecated. Please include <meta name=\"mobile-web-app-capable\" "
        "content=\"yes\"> - "
        "http://developers.google.com/chrome/mobile/docs/installtohomescreen");
    frame->AddMessageToConsole(message);
  }

  // Prune out any data URLs in the set of icons.  The browser process expects
  // any icon with a data URL to have originated from a favicon.  We don't want
  // to decode arbitrary data URLs in the browser process.  See
  // http://b/issue?id=1162972
  for (std::vector<WebApplicationInfo::IconInfo>::iterator it =
           web_app_info.icons.begin();
       it != web_app_info.icons.end();) {
    if (it->url.SchemeIs(url::kDataScheme))
      it = web_app_info.icons.erase(it);
    else
      ++it;
  }

  // Truncate the strings we send to the browser process.
  web_app_info.title =
      web_app_info.title.substr(0, chrome::kMaxMetaTagAttributeLength);
  web_app_info.description =
      web_app_info.description.substr(0, chrome::kMaxMetaTagAttributeLength);

  Send(new ChromeFrameHostMsg_DidGetWebApplicationInfo(routing_id(),
                                                       web_app_info));
}

#if defined(SAFE_BROWSING_CSD)
void ChromeRenderFrameObserver::SetClientSidePhishingDetection(
    bool enable_phishing_detection) {
  phishing_classifier_ =
      enable_phishing_detection
          ? safe_browsing::PhishingClassifierDelegate::Create(render_frame(),
                                                              nullptr)
          : nullptr;
}
#endif

#if !defined(OS_ANDROID)
void ChromeRenderFrameObserver::ExecuteWebUIJavaScript(
    const base::string16& javascript) {
  webui_javascript_.push_back(javascript);
}
#endif

void ChromeRenderFrameObserver::DidFinishLoad() {
  WebLocalFrame* frame = render_frame()->GetWebFrame();
  // Don't do anything for subframes.
  if (frame->Parent())
    return;

  GURL osdd_url = frame->GetDocument().OpenSearchDescriptionURL();
  if (!osdd_url.is_empty()) {
    chrome::mojom::OpenSearchDescriptionDocumentHandlerAssociatedPtr
        osdd_handler;
    render_frame()->GetRemoteAssociatedInterfaces()->GetInterface(
        &osdd_handler);
    osdd_handler->PageHasOpenSearchDescriptionDocument(
        frame->GetDocument().Url(), osdd_url);
  }
}

void ChromeRenderFrameObserver::DidStartProvisionalLoad(
    WebDocumentLoader* document_loader) {
#if 0
  // Let translate_helper do any preparatory work for loading a URL.
  if (!translate_helper_)
    return;

  translate_helper_->PrepareForUrl(
      render_frame()->GetWebFrame()->GetDocument().Url());
#endif
}

void ChromeRenderFrameObserver::DidCommitProvisionalLoad(
    bool is_new_navigation,
    bool is_same_document_navigation) {
  WebLocalFrame* frame = render_frame()->GetWebFrame();

  // Don't do anything for subframes.
  if (frame->Parent())
    return;

  base::debug::SetCrashKeyValue(
      crash_keys::kViewCount,
      base::SizeTToString(content::RenderView::GetRenderViewCount()));

#if !defined(OS_ANDROID)
  if ((render_frame()->GetEnabledBindings() &
       content::BINDINGS_POLICY_WEB_UI)) {
    for (const auto& script : webui_javascript_)
      render_frame()->ExecuteJavaScript(script);
    webui_javascript_.clear();
  }
#endif
}

void ChromeRenderFrameObserver::CapturePageText(TextCaptureType capture_type) {
  WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame)
    return;

  // Don't capture pages that have pending redirect or location change.
  if (frame->IsNavigationScheduledWithin(kLocationChangeIntervalInSeconds))
    return;

  // Don't index/capture pages that are in view source mode.
  if (frame->IsViewSourceModeEnabled())
    return;

  // Don't capture text of the error pages.
  WebDocumentLoader* document_loader = frame->GetDocumentLoader();
  if (document_loader && document_loader->HasUnreachableURL())
    return;

  // Don't index/capture pages that are being prerendered.
  if (prerender::PrerenderHelper::IsPrerendering(render_frame()))
    return;

  base::TimeTicks capture_begin_time = base::TimeTicks::Now();

  // Retrieve the frame's full text (up to kMaxIndexChars), and pass it to the
  // translate helper for language detection and possible translation.
  // TODO(dglazkov): WebFrameContentDumper should only be used for
  // testing purposes. See http://crbug.com/585164.
  base::string16 contents =
      WebFrameContentDumper::DeprecatedDumpFrameTreeAsText(frame,
                                                           kMaxIndexChars)
          .Utf16();

  UMA_HISTOGRAM_TIMES(kTranslateCaptureText,
                      base::TimeTicks::Now() - capture_begin_time);

#if 0
  // We should run language detection only once. Parsing finishes before
  // the page loads, so let's pick that timing.
  if (translate_helper_ && capture_type == PRELIMINARY_CAPTURE)
    translate_helper_->PageCaptured(contents);
#endif

  TRACE_EVENT0("renderer", "ChromeRenderFrameObserver::CapturePageText");

#if defined(SAFE_BROWSING_CSD)
  // Will swap out the string.
  if (phishing_classifier_)
    phishing_classifier_->PageCaptured(&contents,
                                       capture_type == PRELIMINARY_CAPTURE);
#endif
}

void ChromeRenderFrameObserver::DidMeaningfulLayout(
    blink::WebMeaningfulLayout layout_type) {
  // Don't do any work for subframes.
  if (!render_frame()->IsMainFrame())
    return;

  switch (layout_type) {
    case blink::WebMeaningfulLayout::kFinishedParsing:
      CapturePageText(PRELIMINARY_CAPTURE);
      break;
    case blink::WebMeaningfulLayout::kFinishedLoading:
      CapturePageText(FINAL_CAPTURE);
      break;
    default:
      break;
  }
}

void ChromeRenderFrameObserver::OnDestruct() {
  delete this;
}

void ChromeRenderFrameObserver::OnImageContextMenuRendererRequest(
    chrome::mojom::ImageContextMenuRendererRequest request) {
  image_context_menu_renderer_bindings_.AddBinding(this, std::move(request));
}

#if defined(SAFE_BROWSING_CSD)
void ChromeRenderFrameObserver::OnPhishingDetectorRequest(
    chrome::mojom::PhishingDetectorRequest request) {
  phishing_detector_bindings_.AddBinding(this, std::move(request));
}
#endif

#if !defined(OS_ANDROID)
void ChromeRenderFrameObserver::OnWebUITesterRequest(
    chrome::mojom::WebUITesterAssociatedRequest request) {
  web_ui_tester_bindings_.AddBinding(this, std::move(request));
}
#endif

void ChromeRenderFrameObserver::OnThumbnailCapturerRequest(
    chrome::mojom::ThumbnailCapturerRequest request) {
  thumbnail_capturer_bindings_.AddBinding(this, std::move(request));
}

void ChromeRenderFrameObserver::OnRenderFrameObserverRequest(
    chrome::mojom::ChromeRenderFrameAssociatedRequest request) {
  window_features_client_bindings_.AddBinding(this, std::move(request));
}

void ChromeRenderFrameObserver::SetWindowFeatures(
    blink::mojom::WindowFeaturesPtr window_features) {
  render_frame()->GetRenderView()->GetWebView()->SetWindowFeatures(
      content::ConvertMojoWindowFeaturesToWebWindowFeatures(*window_features));
}
