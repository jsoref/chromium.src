// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/compositor/scene_layer/contextual_search_scene_layer.h"

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/memory/ptr_util.h"
#include "cc/layers/solid_color_layer.h"
#include "chrome/browser/android/compositor/layer/contextual_search_layer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_android.h"
#include "content/public/browser/android/compositor.h"
#include "content/public/browser/web_contents.h"
#include "jni/ContextualSearchSceneLayer_jni.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_request_context_getter.h"
#include "ui/android/resources/resource_manager_impl.h"
#include "ui/android/view_android.h"
#include "ui/gfx/android/java_bitmap.h"

using base::android::JavaParamRef;
using base::android::JavaRef;

namespace android {

ContextualSearchSceneLayer::ContextualSearchSceneLayer(
    JNIEnv* env,
    const JavaRef<jobject>& jobj)
    : SceneLayer(env, jobj),
      env_(env),
      object_(jobj),
      base_page_brightness_(1.0f),
      content_container_(cc::Layer::Create()) {
  // Responsible for moving the base page without modifying the layer itself.
  content_container_->SetIsDrawable(true);
  content_container_->SetPosition(gfx::PointF(0.0f, 0.0f));
  layer()->AddChild(content_container_);
}

void ContextualSearchSceneLayer::CreateContextualSearchLayer(
    JNIEnv* env,
    const JavaParamRef<jobject>& object,
    const JavaParamRef<jobject>& jresource_manager) {
  ui::ResourceManager* resource_manager =
      ui::ResourceManagerImpl::FromJavaObject(jresource_manager);
  contextual_search_layer_ = ContextualSearchLayer::Create(resource_manager);

  // The Contextual Search layer is initially invisible.
  contextual_search_layer_->layer()->SetHideLayerAndSubtree(true);

  layer()->AddChild(contextual_search_layer_->layer());
}

ContextualSearchSceneLayer::~ContextualSearchSceneLayer() {
}

void ContextualSearchSceneLayer::UpdateContextualSearchLayer(
    JNIEnv* env,
    const JavaParamRef<jobject>& object,
    jint search_bar_background_resource_id,
    jint search_context_resource_id,
    jint search_term_resource_id,
    jint search_caption_resource_id,
    jint search_bar_shadow_resource_id,
    jint search_provider_icon_resource_id,
    jint quick_action_icon_resource_id,
    jint arrow_up_resource_id,
    jint close_icon_resource_id,
    jint progress_bar_background_resource_id,
    jint progress_bar_resource_id,
    jint search_promo_resource_id,
    jint peek_promo_ripple_resource_id,
    jint peek_promo_text_resource_id,
    jfloat dp_to_px,
    jfloat base_page_brightness,
    jfloat base_page_offset,
    const JavaParamRef<jobject>& jweb_contents,
    jboolean search_promo_visible,
    jfloat search_promo_height,
    jfloat search_promo_opacity,
    jboolean search_peek_promo_visible,
    jfloat search_peek_promo_height,
    jfloat search_peek_promo_padding,
    jfloat search_peek_promo_ripple_width,
    jfloat search_peek_promo_ripple_opacity,
    jfloat search_peek_promo_text_opacity,
    jfloat search_panel_x,
    jfloat search_panel_y,
    jfloat search_panel_width,
    jfloat search_panel_height,
    jfloat search_bar_margin_side,
    jfloat search_bar_height,
    jfloat search_context_opacity,
    jfloat search_text_layer_min_height,
    jfloat search_term_opacity,
    jfloat search_term_caption_spacing,
    jfloat search_caption_animation_percentage,
    jboolean search_caption_visible,
    jboolean search_bar_border_visible,
    jfloat search_bar_border_height,
    jboolean search_bar_shadow_visible,
    jfloat search_bar_shadow_opacity,
    jboolean quick_action_icon_visible,
    jboolean thumbnail_visible,
    jstring j_thumbnail_url,
    jfloat custom_image_visibility_percentage,
    jint bar_image_size,
    jfloat arrow_icon_opacity,
    jfloat arrow_icon_rotation,
    jfloat close_icon_opacity,
    jboolean progress_bar_visible,
    jfloat progress_bar_height,
    jfloat progress_bar_opacity,
    jint progress_bar_completion,
    jfloat divider_line_visibility_percentage,
    jfloat divider_line_width,
    jfloat divider_line_height,
    jint divider_line_color,
    jfloat divider_line_x_offset,
    jboolean touch_highlight_visible,
    jfloat touch_highlight_x_offset,
    jfloat touch_highlight_width,
    const JavaRef<jobject>& j_profile) {
  // Load the thumbnail if necessary.
  std::string thumbnail_url =
      base::android::ConvertJavaStringToUTF8(env, j_thumbnail_url);
  if (thumbnail_url != thumbnail_url_) {
    thumbnail_url_ = thumbnail_url;
    FetchThumbnail(j_profile);
  }

  // NOTE(pedrosimonetti): The WebContents might not exist at this time if
  // the Contextual Search Result has not been requested yet. In this case,
  // we'll pass NULL to Contextual Search's Layer Tree.
  content::WebContents* web_contents =
      content::WebContents::FromJavaWebContents(jweb_contents);

  scoped_refptr<cc::Layer> content_layer =
      web_contents ? web_contents->GetNativeView()->GetLayer() : nullptr;

  // Fade the base page out.
  if (base_page_brightness_ != base_page_brightness) {
    base_page_brightness_ = base_page_brightness;
    cc::FilterOperations filters;
    if (base_page_brightness < 1.f) {
      filters.Append(
          cc::FilterOperation::CreateBrightnessFilter(base_page_brightness));
    }
    content_container_->SetFilters(filters);
  }

  // Move the base page contents up.
  content_container_->SetPosition(gfx::PointF(0.0f, base_page_offset));

  contextual_search_layer_->SetProperties(
      search_bar_background_resource_id, search_context_resource_id,
      search_term_resource_id, search_caption_resource_id,
      search_bar_shadow_resource_id, search_provider_icon_resource_id,
      quick_action_icon_resource_id, arrow_up_resource_id,
      close_icon_resource_id, progress_bar_background_resource_id,
      progress_bar_resource_id, search_promo_resource_id,
      peek_promo_ripple_resource_id, peek_promo_text_resource_id, dp_to_px,
      content_layer, search_promo_visible, search_promo_height,
      search_promo_opacity, search_peek_promo_visible, search_peek_promo_height,
      search_peek_promo_padding, search_peek_promo_ripple_width,
      search_peek_promo_ripple_opacity, search_peek_promo_text_opacity,
      search_panel_x, search_panel_y, search_panel_width, search_panel_height,
      search_bar_margin_side, search_bar_height, search_context_opacity,
      search_text_layer_min_height, search_term_opacity,
      search_term_caption_spacing, search_caption_animation_percentage,
      search_caption_visible, search_bar_border_visible,
      search_bar_border_height, search_bar_shadow_visible,
      search_bar_shadow_opacity, quick_action_icon_visible, thumbnail_visible,
      custom_image_visibility_percentage, bar_image_size, arrow_icon_opacity,
      arrow_icon_rotation, close_icon_opacity, progress_bar_visible,
      progress_bar_height, progress_bar_opacity, progress_bar_completion,
      divider_line_visibility_percentage, divider_line_width,
      divider_line_height, divider_line_color, divider_line_x_offset,
      touch_highlight_visible, touch_highlight_x_offset, touch_highlight_width);

  // Make the layer visible if it is not already.
  contextual_search_layer_->layer()->SetHideLayerAndSubtree(false);
}

void ContextualSearchSceneLayer::FetchThumbnail(
    const JavaRef<jobject>& j_profile) {
  if (thumbnail_url_.empty())
    return;

  GURL gurl(thumbnail_url_);
  Profile* profile = ProfileAndroid::FromProfileAndroid(j_profile);
  fetcher_ = base::MakeUnique<chrome::BitmapFetcher>(gurl, this,
                                                     NO_TRAFFIC_ANNOTATION_YET);
  fetcher_->Init(
      profile->GetRequestContext(),
      std::string(),
      net::URLRequest::CLEAR_REFERRER_ON_TRANSITION_FROM_SECURE_TO_INSECURE,
      net::LOAD_NORMAL);
  fetcher_->Start();
}

void ContextualSearchSceneLayer::OnFetchComplete(const GURL& url,
                                                 const SkBitmap* bitmap) {
  bool success = bitmap && !bitmap->drawsNothing();
  Java_ContextualSearchSceneLayer_onThumbnailFetched(env_, object_, success);
  if (success)
    contextual_search_layer_->SetThumbnail(bitmap);

  fetcher_.reset();
}

void ContextualSearchSceneLayer::SetContentTree(
    JNIEnv* env,
    const JavaParamRef<jobject>& jobj,
    const JavaParamRef<jobject>& jcontent_tree) {
  SceneLayer* content_tree = FromJavaObject(env, jcontent_tree);
  if (!content_tree || !content_tree->layer()) return;

  if (!content_tree->layer()->parent()
      || (content_tree->layer()->parent()->id() != content_container_->id())) {
    content_container_->AddChild(content_tree->layer());
  }
}

void ContextualSearchSceneLayer::HideTree(JNIEnv* env,
    const JavaParamRef<jobject>& jobj) {
  // TODO(mdjones): Create super class for this logic.
  if (contextual_search_layer_) {
    contextual_search_layer_->layer()->SetHideLayerAndSubtree(true);
  }
  // Reset base page brightness.
  cc::FilterOperations filters;
  filters.Append(cc::FilterOperation::CreateBrightnessFilter(1.0f));
  content_container_->SetFilters(filters);
  // Reset base page offset.
  content_container_->SetPosition(gfx::PointF(0.0f, 0.0f));
}

static jlong Init(JNIEnv* env, const JavaParamRef<jobject>& jobj) {
  // This will automatically bind to the Java object and pass ownership there.
  ContextualSearchSceneLayer* tree_provider =
      new ContextualSearchSceneLayer(env, jobj);
  return reinterpret_cast<intptr_t>(tree_provider);
}

}  // namespace android
