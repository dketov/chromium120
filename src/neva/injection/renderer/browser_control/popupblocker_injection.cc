// Copyright 2022 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "neva/injection/renderer/browser_control/popupblocker_injection.h"

#include "base/functional/bind.h"
#include "gin/arguments.h"
#include "gin/handle.h"
#include "third_party/blink/public/common/thread_safe_browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace injections {

namespace {

const char kPopupBlockerObjectName[] = "popupblocker";

const char kSetEnabledMethodName[] = "setEnabled";
const char kGetEnabledMethodName[] = "getEnabled";
const char kGetURLsMethodName[] = "getURLs";
const char kAddMethodName[] = "addURL";
const char kDeletesMethodName[] = "deleteURLs";
const char kUpdateMethodName[] = "updateURL";

// Returns true if |maybe| is both a value, and that value is true.
inline bool IsTrue(v8::Maybe<bool> maybe) {
  return maybe.IsJust() && maybe.FromJust();
}

}  // anonymous namespace

gin::WrapperInfo PopupBlockerInjection::kWrapperInfo = {
    gin::kEmbedderNativeGin};

// static
void PopupBlockerInjection::Install(blink::WebLocalFrame* frame) {
  v8::Isolate* isolate = blink::MainThreadIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = frame->MainWorldScriptContext();
  if (context.IsEmpty())
    return;

  v8::Local<v8::Object> global = context->Global();
  v8::Context::Scope context_scope(context);

  v8::Local<v8::String> navigator_name = gin::StringToV8(isolate, "navigator");
  v8::Local<v8::Object> navigator;
  if (!gin::Converter<v8::Local<v8::Object>>::FromV8(
          isolate, global->Get(context, navigator_name).ToLocalChecked(),
          &navigator))
    return;

  v8::Local<v8::String> popupblocker_name =
      gin::StringToV8(isolate, kPopupBlockerObjectName);
  if (IsTrue(navigator->Has(context, popupblocker_name)))
    return;

  CreatePopupBlockerObject(isolate, navigator);
}

// static
void PopupBlockerInjection::Uninstall(blink::WebLocalFrame* frame) {
  v8::Isolate* isolate = blink::MainThreadIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = frame->MainWorldScriptContext();
  if (context.IsEmpty())
    return;

  v8::Local<v8::Object> global = context->Global();
  v8::Context::Scope context_scope(context);

  v8::Local<v8::String> navigator_name = gin::StringToV8(isolate, "navigator");
  v8::Local<v8::Object> navigator;
  if (gin::Converter<v8::Local<v8::Object>>::FromV8(
          isolate, global->Get(context, navigator_name).ToLocalChecked(),
          &navigator)) {
    v8::Local<v8::String> popupblocker_name =
        gin::StringToV8(isolate, kPopupBlockerObjectName);
    if (IsTrue(navigator->Has(context, popupblocker_name)))
      navigator->Delete(context, popupblocker_name);
  }
}

// static
void PopupBlockerInjection::CreatePopupBlockerObject(
    v8::Isolate* isolate,
    v8::Local<v8::Object> parent) {
  gin::Handle<PopupBlockerInjection> popupblocker =
      gin::CreateHandle(isolate, new PopupBlockerInjection());
  parent
      ->Set(isolate->GetCurrentContext(),
            gin::StringToV8(isolate, kPopupBlockerObjectName),
            popupblocker.ToV8())
      .Check();
}

PopupBlockerInjection::PopupBlockerInjection() {
  blink::Platform::Current()->GetBrowserInterfaceBroker()->GetInterface(
      remote_popupblocker_.BindNewPipeAndPassReceiver());
}

PopupBlockerInjection::~PopupBlockerInjection() = default;

bool PopupBlockerInjection::SetEnabled(gin::Arguments* args) {
  bool popupblocker_state = false;
  if (!args->GetNext(&popupblocker_state)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  bool result = false;
  remote_popupblocker_->SetEnabled(popupblocker_state, &result);
  return result;
}

bool PopupBlockerInjection::GetEnabled(gin::Arguments* args) {
  v8::Local<v8::Function> local_func;
  if (!args->GetNext(&local_func)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  auto callback_ptr = std::make_unique<v8::Persistent<v8::Function>>(
      args->isolate(), local_func);
  remote_popupblocker_->GetEnabled(
      base::BindOnce(&PopupBlockerInjection::OnGetEnabledRespond,
                     base::Unretained(this), std::move(callback_ptr)));
  return true;
}

bool PopupBlockerInjection::GetURLs(gin::Arguments* args) {
  v8::Local<v8::Function> local_func;
  if (!args->GetNext(&local_func)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  auto callback_ptr = std::make_unique<v8::Persistent<v8::Function>>(
      args->isolate(), local_func);
  remote_popupblocker_->GetURLs(
      base::BindOnce(&PopupBlockerInjection::OnGetURLsRespond,
                     base::Unretained(this), std::move(callback_ptr)));
  return true;
}

bool PopupBlockerInjection::AddURL(gin::Arguments* args) {
  std::string new_url;
  if (!args->GetNext(&new_url)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  bool result = false;
  remote_popupblocker_->AddURL(new_url, &result);
  return result;
}

bool PopupBlockerInjection::DeleteURLs(gin::Arguments* args) {
  std::vector<std::string> url_list;
  if (!args->GetNext(&url_list)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  v8::Local<v8::Function> local_func;
  if (!args->GetNext(&local_func)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  auto callback_ptr = std::make_unique<v8::Persistent<v8::Function>>(
      args->isolate(), local_func);
  remote_popupblocker_->DeleteURLs(
      url_list,
      base::BindOnce(&PopupBlockerInjection::OnDeleteURLsRespond,
                     base::Unretained(this), std::move(callback_ptr)));
  return true;
}

bool PopupBlockerInjection::updateURL(gin::Arguments* args) {
  std::string old_url;
  std::string new_url;
  if (!args->GetNext(&old_url) || !args->GetNext(&new_url)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  bool result = false;
  remote_popupblocker_->updateURL(old_url, new_url, &result);
  return result;
}

gin::ObjectTemplateBuilder PopupBlockerInjection::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<PopupBlockerInjection>::GetObjectTemplateBuilder(
             isolate)
      .SetMethod(kSetEnabledMethodName, &PopupBlockerInjection::SetEnabled)
      .SetMethod(kGetEnabledMethodName, &PopupBlockerInjection::GetEnabled)
      .SetMethod(kGetURLsMethodName, &PopupBlockerInjection::GetURLs)
      .SetMethod(kAddMethodName, &PopupBlockerInjection::AddURL)
      .SetMethod(kDeletesMethodName, &PopupBlockerInjection::DeleteURLs)
      .SetMethod(kUpdateMethodName, &PopupBlockerInjection::updateURL);
}

void PopupBlockerInjection::OnGetEnabledRespond(
    std::unique_ptr<v8::Persistent<v8::Function>> callback,
    bool is_enabled) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Object> wrapper;
  if (!GetWrapper(isolate).ToLocal(&wrapper)) {
    LOG(ERROR) << __func__ << "(): can not get wrapper";
    return;
  }

  v8::Local<v8::Context> context;
  if (!wrapper->GetCreationContext().ToLocal(&context)) {
    return;
  }
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Function> local_callback = callback->Get(isolate);

  const int argc = 1;
  v8::Local<v8::Value> result;
  if (gin::TryConvertToV8(isolate, is_enabled, &result)) {
    local_callback->Call(context, wrapper, argc, &result);
  }
}

void PopupBlockerInjection::OnGetURLsRespond(
    std::unique_ptr<v8::Persistent<v8::Function>> callback,
    const std::vector<std::string>& url_list) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Object> wrapper;
  if (!GetWrapper(isolate).ToLocal(&wrapper)) {
    LOG(ERROR) << __func__ << "(): can not get wrapper";
    return;
  }

  v8::Local<v8::Context> context;
  if (!wrapper->GetCreationContext().ToLocal(&context))
    return;
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Function> local_callback = callback->Get(isolate);

  const int argc = 1;
  v8::Local<v8::Value> result;
  if (gin::TryConvertToV8(isolate, url_list, &result))
    local_callback->Call(context, wrapper, argc, &result);
}

void PopupBlockerInjection::OnDeleteURLsRespond(
    std::unique_ptr<v8::Persistent<v8::Function>> callback,
    bool is_success) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Object> wrapper;
  if (!GetWrapper(isolate).ToLocal(&wrapper)) {
    LOG(ERROR) << __func__ << "(): can not get wrapper";
    return;
  }

  v8::Local<v8::Context> context;
  if (!wrapper->GetCreationContext().ToLocal(&context))
    return;
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Function> local_callback = callback->Get(isolate);

  const int argc = 1;
  v8::Local<v8::Value> result;
  if (gin::TryConvertToV8(isolate, is_success, &result))
    local_callback->Call(context, wrapper, argc, &result);
}

}  // namespace injections
