// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/renderer/content_setting.h"

#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "content/public/common/console_message_level.h"
#include "extensions/renderer/bindings/api_request_handler.h"
#include "extensions/renderer/bindings/api_signature.h"
#include "extensions/renderer/bindings/api_type_reference_map.h"
#include "extensions/renderer/bindings/binding_access_checker.h"
#include "extensions/renderer/console.h"
#include "extensions/renderer/script_context_set.h"
#include "gin/arguments.h"
#include "gin/converter.h"
#include "gin/handle.h"
#include "gin/object_template_builder.h"

namespace extensions {

namespace {

// Content settings that are deprecated.
const char* const kDeprecatedTypes[] = {
    "fullscreen", "mouselock",
};

bool IsDeprecated(base::StringPiece type) {
  return std::find(std::begin(kDeprecatedTypes), std::end(kDeprecatedTypes),
                   type) != std::end(kDeprecatedTypes);
}
}

v8::Local<v8::Object> ContentSetting::Create(
    const binding::RunJSFunction& run_js,
    v8::Isolate* isolate,
    const std::string& property_name,
    const base::ListValue* property_values,
    APIRequestHandler* request_handler,
    APIEventHandler* event_handler,
    APITypeReferenceMap* type_refs,
    const BindingAccessChecker* access_checker) {
  std::string pref_name;
  CHECK(property_values->GetString(0u, &pref_name));
  const base::DictionaryValue* value_spec = nullptr;
  CHECK(property_values->GetDictionary(1u, &value_spec));

  gin::Handle<ContentSetting> handle = gin::CreateHandle(
      isolate, new ContentSetting(run_js, request_handler, type_refs,
                                  access_checker, pref_name, *value_spec));
  return handle.ToV8().As<v8::Object>();
}

ContentSetting::ContentSetting(const binding::RunJSFunction& run_js,
                               APIRequestHandler* request_handler,
                               const APITypeReferenceMap* type_refs,
                               const BindingAccessChecker* access_checker,
                               const std::string& pref_name,
                               const base::DictionaryValue& set_value_spec)
    : run_js_(run_js),
      request_handler_(request_handler),
      type_refs_(type_refs),
      access_checker_(access_checker),
      pref_name_(pref_name),
      argument_spec_(ArgumentType::OBJECT) {
  // The set() call takes an object { setting: { type: <t> }, ... }, where <t>
  // is the custom set() argument specified above by value_spec.
  ArgumentSpec::PropertiesMap properties;
  properties["primaryPattern"] =
      std::make_unique<ArgumentSpec>(ArgumentType::STRING);
  {
    auto secondary_pattern_spec =
        std::make_unique<ArgumentSpec>(ArgumentType::STRING);
    secondary_pattern_spec->set_optional(true);
    properties["secondaryPattern"] = std::move(secondary_pattern_spec);
  }
  {
    auto resource_identifier_spec =
        std::make_unique<ArgumentSpec>(ArgumentType::REF);
    resource_identifier_spec->set_ref("contentSettings.ResourceIdentifier");
    resource_identifier_spec->set_optional(true);
    properties["resourceIdentifier"] = std::move(resource_identifier_spec);
  }
  {
    auto scope_spec = std::make_unique<ArgumentSpec>(ArgumentType::REF);
    scope_spec->set_ref("contentSettings.Scope");
    scope_spec->set_optional(true);
    properties["scope"] = std::move(scope_spec);
  }
  properties["setting"] = std::make_unique<ArgumentSpec>(set_value_spec);
  argument_spec_.set_properties(std::move(properties));
}

ContentSetting::~ContentSetting() = default;

gin::WrapperInfo ContentSetting::kWrapperInfo = {gin::kEmbedderNativeGin};

gin::ObjectTemplateBuilder ContentSetting::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return Wrappable<ContentSetting>::GetObjectTemplateBuilder(isolate)
      .SetMethod("get", &ContentSetting::Get)
      .SetMethod("set", &ContentSetting::Set)
      .SetMethod("clear", &ContentSetting::Clear)
      .SetMethod("getResourceIdentifiers",
                 &ContentSetting::GetResourceIdentifiers);
}

void ContentSetting::Get(gin::Arguments* arguments) {
  HandleFunction("get", arguments);
}

void ContentSetting::Set(gin::Arguments* arguments) {
  HandleFunction("set", arguments);
}

void ContentSetting::Clear(gin::Arguments* arguments) {
  HandleFunction("clear", arguments);
}

void ContentSetting::GetResourceIdentifiers(gin::Arguments* arguments) {
  HandleFunction("getResourceIdentifiers", arguments);
}

void ContentSetting::HandleFunction(const std::string& method_name,
                                    gin::Arguments* arguments) {
  v8::Isolate* isolate = arguments->isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = arguments->GetHolderCreationContext();

  std::vector<v8::Local<v8::Value>> argument_list = arguments->GetAll();

  std::string full_name = "contentSettings.ContentSetting." + method_name;

  if (!access_checker_->HasAccessOrThrowError(context, full_name))
    return;

  std::unique_ptr<base::ListValue> converted_arguments;
  v8::Local<v8::Function> callback;
  std::string error;
  if (!type_refs_->GetTypeMethodSignature(full_name)->ParseArgumentsToJSON(
          context, argument_list, *type_refs_, &converted_arguments, &callback,
          &error)) {
    arguments->ThrowTypeError("Invalid invocation: " + error);
    return;
  }

  if (IsDeprecated(pref_name_)) {
    console::AddMessage(ScriptContextSet::GetContextByV8Context(context),
                        content::CONSOLE_MESSAGE_LEVEL_WARNING,
                        base::StringPrintf("contentSettings.%s is deprecated.",
                                           pref_name_.c_str()));
    // If a callback was provided, call it immediately.
    if (!callback.IsEmpty()) {
      std::vector<v8::Local<v8::Value>> args;
      if (method_name == "get") {
        // Deprecated settings are always set to "allow". Populate the result to
        // avoid breaking extensions.
        v8::Local<v8::Object> object = v8::Object::New(isolate);
        v8::Maybe<bool> result = object->DefineOwnProperty(
            context, gin::StringToSymbol(isolate, "setting"),
            gin::StringToSymbol(isolate, "allow"));
        // Since we just defined this object, DefineOwnProperty() should never
        // fail.
        CHECK(result.ToChecked());
        args.push_back(object);
      }
      run_js_.Run(callback, context, args.size(), args.data());
    }
    return;
  }

  if (method_name == "set") {
    v8::Local<v8::Value> value = argument_list[0];
    // The set schema included in the Schema object is generic, since it varies
    // per-setting. However, this is only ever for a single setting, so we can
    // enforce the types more thoroughly.
    // Note: we do this *after* checking if the setting is deprecated, since
    // this validation will fail for deprecated settings.
    std::string error;
    if (!value.IsEmpty() && !argument_spec_.ParseArgument(
                                context, value, *type_refs_, nullptr, &error)) {
      arguments->ThrowTypeError("Invalid invocation: " + error);
      return;
    }
  }

  converted_arguments->Insert(0u, std::make_unique<base::Value>(pref_name_));
  request_handler_->StartRequest(
      context, "contentSettings." + method_name, std::move(converted_arguments),
      callback, v8::Local<v8::Function>(), binding::RequestThread::UI);
}

}  // namespace extensions
