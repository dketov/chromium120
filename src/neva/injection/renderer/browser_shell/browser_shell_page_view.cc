// Copyright 2021 LG Electronics, Inc.
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

#include "neva/injection/renderer/browser_shell/browser_shell_page_view.h"

#include "base/functional/bind.h"
#include "gin/arguments.h"
#include "gin/dictionary.h"
#include "gin/function_template.h"
#include "gin/handle.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "neva/browser_shell/service/public/mojom/browser_shell_page_contents.mojom.h"
#include "neva/injection/renderer/browser_shell/browser_shell_page_contents.h"

namespace injections {

namespace events {
const char kVisibilityChanged[] = "visibility-changed";
}  // namespace events

gin::WrapperInfo BrowserShellPageView::kWrapperInfo = {gin::kEmbedderNativeGin};

char BrowserShellPageView::kIdPropertyName[] = "id";
char BrowserShellPageView::kPageContentsPropertyName[] = "pageContents";

char BrowserShellPageView::kAddChildViewMethodName[] = "addChildView";
char BrowserShellPageView::kBringToFrontMethodName[] = "bringToFront";
char BrowserShellPageView::kGetBoundsMethodName[] = "getBounds";
char BrowserShellPageView::kGetChildViewsMethodName[] = "getChildViews";
char BrowserShellPageView::kHasChildViewMethodName[] = "hasChildView";
char BrowserShellPageView::kHasParentViewMethodName[] = "hasParentView";
char BrowserShellPageView::kIsVisibleMethodName[] = "isVisible";
char BrowserShellPageView::kRemoveChildViewMethodName[] = "removeChildView";
char BrowserShellPageView::kSendToBackMethodName[] = "sendToBack";
char BrowserShellPageView::kSetBoundsMethodName[] = "setBounds";
char BrowserShellPageView::kSetVisibleMethodName[] = "setVisible";

// static
void BrowserShellPageView::ConstructorCallback(
    mojo::Remote<browser_shell::mojom::ShellService>* shell_service,
    gin::Arguments* args) {
  v8::Isolate* isolate = args->isolate();
  v8::HandleScope handle_scope(isolate);

  if (!args->IsConstructCall()) {
    isolate->ThrowException(v8::Exception::Error(
        gin::StringToV8(args->isolate(), "Must be a constructor call")));
    return;
  }

  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Value> json_value = args->PeekNext();
  v8::Local<v8::Object> json_obj = BuildOptions(isolate, json_value);

  v8::MaybeLocal<v8::String> maybe_json_str =
      v8::JSON::Stringify(context, json_obj);

  v8::Local<v8::String> json_str;
  std::string json;
  if (maybe_json_str.ToLocal(&json_str))
    json = gin::V8ToString(args->isolate(), json_str);

  CreateParams params;
  v8::Local<v8::Object> content_params_obj;
  gin::Dictionary json_dict(isolate, json_obj);
  if (json_dict.Get("page-contents-params", &content_params_obj)) {
    gin::Dictionary content_params_dict(isolate, content_params_obj);
    if (!content_params_dict.Get("error-page-hiding",
                                 &params.error_page_hiding)) {
      params.error_page_hiding = false;
    }
  }

  mojo::Remote<browser_shell::mojom::PageView> remote_view;
  auto pending_receiver = remote_view.BindNewPipeAndPassReceiver();
  auto* shell_page_view = new injections::BrowserShellPageView(
      isolate, std::move(remote_view), params);
  (*shell_service)
      ->CreatePageView(std::move(pending_receiver), json,
                       base::BindOnce(&BrowserShellPageView::Setup,
                                      base::Unretained(shell_page_view)));

  gin::Handle<injections::BrowserShellPageView> handle =
      gin::CreateHandle(isolate, shell_page_view);

  if (!handle.IsEmpty())
    args->Return(handle.ToV8());
}

// static
v8::Local<v8::Object> BrowserShellPageView::BuildOptions(
    v8::Isolate* isolate,
    v8::Local<v8::Value> json_value) {
  if (json_value.IsEmpty() || !json_value->IsObject())
    return v8::Object::New(isolate);

  auto options_obj = v8::Local<v8::Object>::Cast(json_value)->Clone();
  gin::Dictionary options_dict(isolate, options_obj);
  v8::Local<v8::Object> content_params_obj;
  if (options_dict.Get("page-contents-params", &content_params_obj)) {
    v8::Local<v8::Object> content_options_obj =
        BrowserShellPageContents::BuildOptions(isolate, content_params_obj);
    options_dict.Set("page-contents-params", content_options_obj);
  }

  return options_obj;
}

BrowserShellPageView::BrowserShellPageView(
    v8::Isolate* isolate,
    mojo::Remote<browser_shell::mojom::PageView> remote,
    const CreateParams& params)
    : is_main_view_(params.is_main_view),
      remote_(std::move(remote)),
      client_receiver_(this) {
  remote_->BindClient(base::BindOnce(&BrowserShellPageView::SetupClient,
                                     base::Unretained(this)));
  mojo::Remote<browser_shell::mojom::PageContents> remote_contents;
  auto pending_receiver = remote_contents.BindNewPipeAndPassReceiver();

  BrowserShellPageContents::CreateParams page_content_params;
  page_content_params.error_page_hiding = params.error_page_hiding;
  page_content_params.is_main_contents = params.is_main_view;

  auto* shell_page_contents = new injections::BrowserShellPageContents(
      isolate, std::move(remote_contents), page_content_params);

  remote_->BindPageContents(
      std::move(pending_receiver),
      base::BindOnce(&BrowserShellPageContents::Setup,
                     shell_page_contents->GetAsWeakPtr()));

  gin::Handle<injections::BrowserShellPageContents> handle =
      gin::CreateHandle(isolate, shell_page_contents);

  if (!handle.IsEmpty()) {
    page_contents_object_.Reset(isolate,
                                handle->GetWrapper(isolate).ToLocalChecked());
  }
}

BrowserShellPageView::~BrowserShellPageView() = default;

uint64_t BrowserShellPageView::GetID() {
  if (!id_ && remote_.is_connected())
    remote_->SyncId(&id_);
  return id_;
}

bool BrowserShellPageView::IsMainView() const {
  return is_main_view_;
}

void BrowserShellPageView::Setup(uint64_t id) {
  id_ = id;
}

void BrowserShellPageView::SetupClient(
    mojo::PendingAssociatedReceiver<browser_shell::mojom::PageViewClient>
        receiver) {
  client_receiver_.Bind(std::move(receiver));
}

void BrowserShellPageView::VisibilityChanged(bool visible,
                                             uint32_t reason_code) {
  visible_ = visible;
  DoEmit(events::kVisibilityChanged, visible, reason_code);
}

bool BrowserShellPageView::HasParentView() const {
  return !is_main_view_ && (parent_view_ != nullptr);
}

void BrowserShellPageView::SetParentView(BrowserShellPageView* parent_view) {
  if (!is_main_view_)
    parent_view_ = parent_view;
}

v8::Local<v8::Object> BrowserShellPageView::GetPageContents(
    v8::Isolate* isolate) {
  return page_contents_object_.Get(isolate);
}

void BrowserShellPageView::SetPageContents(v8::Isolate* isolate,
                                           v8::Local<v8::Object> object) {
  if (is_main_view_)
    return;

  BrowserShellPageContents* page_contents = nullptr;
  gin::Converter<BrowserShellPageContents*>::FromV8(isolate, object,
                                                    &page_contents);
  if (page_contents) {
    const uint64_t id = page_contents->GetID();
    remote_->SetPageContents(id);
    page_contents_object_.Reset(isolate, object);
  }
}

bool BrowserShellPageView::AddChildView(v8::Isolate* isolate,
                                        v8::Local<v8::Object> object) {
  BrowserShellPageView* page_view = nullptr;
  gin::Converter<BrowserShellPageView*>::FromV8(isolate, object, &page_view);

  if (!page_view || page_view->IsMainView() || page_view->HasParentView())
    return false;

  const uint64_t id = page_view->GetID();
  if (!id)
    return false;

  v8::Global<v8::Object> child_obj(isolate, object);
  if (child_view_objects_.insert({id, std::move(child_obj)}).second) {
    remote_->AddChildView(id);
    page_view->SetParentView(this);
    return true;
  }
  return false;
}

void BrowserShellPageView::RemoveChildView(v8::Isolate* isolate,
                                           v8::Local<v8::Object> object) {
  BrowserShellPageView* page_view = nullptr;
  gin::Converter<BrowserShellPageView*>::FromV8(isolate, object, &page_view);

  if (page_view) {
    const uint64_t id = page_view->GetID();
    if (id && child_view_objects_.erase(id)) {
      remote_->RemoveChildView(id);
      page_view->SetParentView(nullptr);
    }
  }
}

void BrowserShellPageView::GetChildViews(gin::Arguments* args) {
  v8::Isolate* isolate = args->isolate();
  v8::Local<v8::Set> result = v8::Set::New(isolate);
  for (auto& child_view_item : child_view_objects_) {
    if (!result
             ->Add(isolate->GetCurrentContext(),
                   child_view_item.second.Get(isolate))
             .ToLocal(&result))
      return;
  }
  args->Return(result.As<v8::Object>());
}

bool BrowserShellPageView::HasChildView(v8::Isolate* isolate,
                                        v8::Local<v8::Object> object) const {
  BrowserShellPageView* page_view = nullptr;
  gin::Converter<BrowserShellPageView*>::FromV8(isolate, object, &page_view);
  if (page_view) {
    const uint64_t id = page_view->GetID();
    if (id && child_view_objects_.count(id))
      return true;
  }
  return false;
}

void BrowserShellPageView::SetVisible(bool visible) {
  if (visible_ != visible) {
    remote_->SetVisible(visible);
    visible_ = visible;
  }
}

bool BrowserShellPageView::IsVisible() const {
  return visible_;
}

void BrowserShellPageView::SetBounds(int x, int y, int w, int h) {
  bounds_.SetRect(x, y, w, h);
  remote_->SetBounds(x, y, w, h);
}

void BrowserShellPageView::GetBounds(gin::Arguments* args) const {
  v8::Isolate* isolate = args->isolate();
  v8::Local<v8::Object> result = v8::Object::New(isolate);
  gin::Dictionary dict(isolate, result);

  if (dict.Set("x", bounds_.x()) &&
      dict.Set("y", bounds_.y()) &&
      dict.Set("width", bounds_.width()) &&
      dict.Set("height", bounds_.height()))
    args->Return(result);
}

void BrowserShellPageView::BringToFront() {
  remote_->BringToFront();
}

void BrowserShellPageView::SendToBack() {
  remote_->SendToBack();
}

gin::ObjectTemplateBuilder
BrowserShellPageView::GetObjectTemplateBuilder(v8::Isolate* isolate) {
  return gin::Wrappable<BrowserShellPageView>::GetObjectTemplateBuilder(isolate)
      .SetProperty(kIdPropertyName, &BrowserShellPageView::GetID)
      .SetProperty(kPageContentsPropertyName,
                   &BrowserShellPageView::GetPageContents,
                   &BrowserShellPageView::SetPageContents)
      .SetMethod(kEmitMethodName, &BrowserShellPageView::RunEmit)
      .SetMethod(kEventNamesMethodName, &BrowserShellPageView::RunGetEventNames)
      .SetMethod(kListenerCountMethodName,
                 &BrowserShellPageView::RunGetListenerCount)
      .SetMethod(kOnMethodName, &BrowserShellPageView::RunAddEventListener)
      .SetMethod(kOnceMethodName,
                 &BrowserShellPageView::RunAddOnceEventListener)
      .SetMethod(kRemoveEventListenerMethodName,
                 &BrowserShellPageView::RunRemoveEventListener)
      .SetMethod(kRemoveAllEventListenersMethodName,
                 &BrowserShellPageView::RunRemoveAllEventListeners)
      .SetMethod(kSetBoundsMethodName, &BrowserShellPageView::SetBounds)
      .SetMethod(kGetBoundsMethodName, &BrowserShellPageView::GetBounds)
      .SetMethod(kSetVisibleMethodName, &BrowserShellPageView::SetVisible)
      .SetMethod(kIsVisibleMethodName, &BrowserShellPageView::IsVisible)
      .SetMethod(kAddChildViewMethodName, &BrowserShellPageView::AddChildView)
      .SetMethod(kRemoveChildViewMethodName,
                 &BrowserShellPageView::RemoveChildView)
      .SetMethod(kGetChildViewsMethodName, &BrowserShellPageView::GetChildViews)
      .SetMethod(kHasChildViewMethodName, &BrowserShellPageView::HasChildView)
      .SetMethod(kHasParentViewMethodName, &BrowserShellPageView::HasParentView)
      .SetMethod(kBringToFrontMethodName, &BrowserShellPageView::BringToFront)
      .SetMethod(kSendToBackMethodName, &BrowserShellPageView::SendToBack);
}

}  // namespace injections
