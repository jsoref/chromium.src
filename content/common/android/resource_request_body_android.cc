// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/android/resource_request_body_android.h"

#include <jni.h>

#include <string>

#include "base/android/jni_array.h"
#include "base/android/scoped_java_ref.h"
#include "base/memory/ref_counted.h"
#include "content/common/page_state_serialization.h"
#include "content/public/common/resource_request_body.h"
#include "jni/ResourceRequestBody_jni.h"

using base::android::JavaParamRef;

namespace content {

namespace {

base::android::ScopedJavaLocalRef<jbyteArray>
ConvertResourceRequestBodyToJavaArray(JNIEnv* env,
                                      const ResourceRequestBody& body) {
  std::string encoded = EncodeResourceRequestBody(body);
  return base::android::ToJavaByteArray(
      env, reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
}

}  // namespace

base::android::ScopedJavaLocalRef<jbyteArray>
CreateResourceRequestBodyFromBytes(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jbyteArray>& j_post_data) {
  base::android::ScopedJavaLocalRef<jbyteArray> result;
  if (!j_post_data)
    return result;

  std::vector<uint8_t> post_data;
  base::android::JavaByteArrayToByteVector(env, j_post_data, &post_data);
  scoped_refptr<ResourceRequestBody> body =
      ResourceRequestBody::CreateFromBytes(
          reinterpret_cast<const char*>(post_data.data()), post_data.size());

  return ConvertResourceRequestBodyToJavaArray(
      env, static_cast<const ResourceRequestBody&>(*body));
}

base::android::ScopedJavaLocalRef<jobject>
ConvertResourceRequestBodyToJavaObject(
    JNIEnv* env,
    const scoped_refptr<ResourceRequestBody>& body) {
  if (!body)
    return base::android::ScopedJavaLocalRef<jobject>();

  // TODO(lukasza): Avoid repeatedly copying the bytes.
  // See also https://goo.gl/ITiLGI.
  base::android::ScopedJavaLocalRef<jbyteArray> j_encoded =
      ConvertResourceRequestBodyToJavaArray(env, *body);

  return Java_ResourceRequestBody_createFromEncodedNativeForm(env, j_encoded);
}

scoped_refptr<ResourceRequestBody> ExtractResourceRequestBodyFromJavaObject(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& j_body) {
  if (!j_body)
    return nullptr;

  base::android::ScopedJavaLocalRef<jbyteArray> j_encoded =
      Java_ResourceRequestBody_getEncodedNativeForm(env, j_body);
  if (j_encoded.is_null())
    return nullptr;

  std::vector<uint8_t> encoded;
  base::android::JavaByteArrayToByteVector(env, j_encoded.obj(), &encoded);

  return DecodeResourceRequestBody(
      reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

}  // namespace content
