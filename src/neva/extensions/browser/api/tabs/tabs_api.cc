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

#include "neva/extensions/browser/api/tabs/tabs_api.h"

#include "base/strings/pattern.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/permissions/api_permission.h"
#include "extensions/common/permissions/permissions_data.h"
#include "neva/extensions/browser/api/tabs/tabs_constants.h"
#include "neva/extensions/browser/extension_tab_util.h"
#include "neva/extensions/browser/neva_extensions_service_factory.h"
#include "neva/extensions/browser/neva_extensions_service_impl.h"
#include "neva/extensions/browser/tab_helper.h"
#include "neva/extensions/browser/web_contents_map.h"
#include "neva/extensions/common/api/tabs.h"
#include "neva/extensions/common/api/windows.h"

namespace neva {

namespace windows = extensions::api::windows;
namespace tabs = extensions::api::tabs;

using extensions::api::extension_types::InjectDetails;

// Returns true if either |boolean| is disengaged, or if |boolean| and
// |value| are equal. This function is used to check if a tab's parameters match
// those of the browser.
bool MatchesBool(const absl::optional<bool>& boolean, bool value) {
  return !boolean || *boolean == value;
}

base::Value::Dict getCurrentTab(content::BrowserContext* browser_context) {
  base::Value::Dict dict;

  WebContentsMap* web_contents_map = WebContentsMap::GetInstance();
  for (auto& it : *web_contents_map) {
    content::WebContents* web_contents = it.second;

    uint64_t tab_id = NevaExtensionsServiceFactory::GetService(browser_context)
                          ->GetTabHelper()
                          ->GetTabIdFromWebContents(web_contents);
    if (tab_id == 0) {
      continue;
    }

    if (web_contents->GetVisibility() != content::Visibility::HIDDEN) {
      dict = ExtensionTabUtil::CreateWindowValueForExtension(
          static_cast<int>(tab_id), true);
      break;
    }
  }

  return dict;
}

// Windows ---------------------------------------------------------------------

ExtensionFunction::ResponseAction WindowsGetCurrentFunction::Run() {
  base::Value::Dict dict = getCurrentTab(browser_context());
  if (dict.empty()) {
    return RespondNow(Error("No current window"));
  }

  return RespondNow(WithArguments(std::move(dict)));
}

ExtensionFunction::ResponseAction WindowsGetLastFocusedFunction::Run() {
  base::Value::Dict dict = getCurrentTab(browser_context());
  if (dict.empty()) {
    return RespondNow(Error("No current window"));
  }

  return RespondNow(WithArguments(std::move(dict)));
}

ExtensionFunction::ResponseAction WindowsGetAllFunction::Run() {
  absl::optional<windows::GetAll::Params> params =
      windows::GetAll::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  WebContentsMap* web_contents_map = WebContentsMap::GetInstance();
  base::Value::List window_list;

  for (auto& it : *web_contents_map) {
    content::WebContents* web_contents = it.second;

    uint64_t tab_id =
        NevaExtensionsServiceFactory::GetService(browser_context())
            ->GetTabHelper()
            ->GetTabIdFromWebContents(web_contents);
    if (tab_id == 0) {
      continue;
    }

    bool focused = web_contents->GetVisibility() != content::Visibility::HIDDEN;
    window_list.Append(
        ExtensionTabUtil::CreateWindowValueForExtension(tab_id, focused));
  }

  return RespondNow(WithArguments(std::move(window_list)));
}

ExtensionFunction::ResponseAction WindowsCreateFunction::Run() {
  absl::optional<windows::Create::Params> params =
      windows::Create::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  if (!params->create_data || !params->create_data->url.has_value()) {
    return RespondNow(Error("We don't support missing url yet."));
  }

  if (params->create_data->type != windows::CREATE_TYPE_POPUP) {
    return RespondNow(Error("Only popup type window are supported now."));
  }

  GURL url = GURL(params->create_data->url->as_string.value());
  if (!url.is_valid() && extension()) {
    url = extension()->GetResourceURL(
        params->create_data->url->as_string.value());
  }

  NevaExtensionsServiceFactory::GetService(browser_context())
      ->OnExtensionTabCreationRequested(
          url.spec(),
          base::BindOnce(&WindowsCreateFunction::OnWindowsCreated, this));
  return RespondLater();
}

void WindowsCreateFunction::OnWindowsCreated(int window_id) {
  if (has_callback()) {
    Respond(WithArguments(
        ExtensionTabUtil::CreateWindowValueForExtension(window_id, true)));
    return;
  }
  Respond(NoArguments());
}

ExtensionFunction::ResponseAction WindowsUpdateFunction::Run() {
  absl::optional<windows::Update::Params> params =
      windows::Update::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  if (params->update_info.focused) {
    NevaExtensionsServiceFactory::GetService(browser_context())
        ->OnExtensionTabFocusRequested(
            static_cast<uint64_t>(params->window_id));
  }

  return RespondNow(
      WithArguments(ExtensionTabUtil::CreateWindowValueForExtension(
          params->window_id, true)));
}

ExtensionFunction::ResponseAction WindowsRemoveFunction::Run() {
  absl::optional<windows::Remove::Params> params =
      windows::Remove::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  NevaExtensionsServiceFactory::GetService(browser_context())
      ->OnExtensionTabCloseRequested(static_cast<uint64_t>(params->window_id));

  return RespondNow(NoArguments());
}

// Tabs ------------------------------------------------------------------------

ExtensionFunction::ResponseAction TabsCreateFunction::Run() {
  absl::optional<tabs::Create::Params> params =
      tabs::Create::Params::Create(args());
  if (!params->create_properties.url)
    return RespondNow(Error("We don't support missing url yet."));

  NevaExtensionsServiceFactory::GetService(browser_context())
      ->OnExtensionTabCreationRequested(
          *(params->create_properties.url),
          base::BindOnce(&TabsCreateFunction::OnTabCreated, this));
  return RespondLater();
}

void TabsCreateFunction::OnTabCreated(int tab_id) {
  if (has_callback()) {
    tabs::Tab tab_object;
    tab_object.id = tab_id;
    Respond(WithArguments(tab_object.ToValue()));
    return;
  }
  Respond(NoArguments());
}

ExtensionFunction::ResponseAction TabsQueryFunction::Run() {
  absl::optional<tabs::Query::Params> params =
      tabs::Query::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  extensions::URLPatternSet url_patterns;
  if (params->query_info.url) {
    std::vector<std::string> url_pattern_strings;
    if (params->query_info.url->as_string) {
      url_pattern_strings.push_back(*params->query_info.url->as_string);
    } else if (params->query_info.url->as_strings) {
      url_pattern_strings.swap(*params->query_info.url->as_strings);
    }
    // It is o.k. to use URLPattern::SCHEME_ALL here because this function does
    // not grant access to the content of the tabs, only to seeing their URLs
    // and meta data.
    std::string error;
    if (!url_patterns.Populate(url_pattern_strings, URLPattern::SCHEME_ALL,
                               true, &error)) {
      return RespondNow(Error(std::move(error)));
    }
  }

  std::string title = params->query_info.title.value_or(std::string());

  WebContentsMap* web_contents_map = WebContentsMap::GetInstance();
  base::Value::List result;

  for (auto& it : *web_contents_map) {
    content::WebContents* web_contents = it.second;

    uint64_t tab_id =
        NevaExtensionsServiceFactory::GetService(browser_context())
            ->GetTabHelper()
            ->GetTabIdFromWebContents(web_contents);
    if (tab_id == 0) {
      continue;
    }

    if (!MatchesBool(
            params->query_info.active,
            web_contents->GetVisibility() != content::Visibility::HIDDEN)) {
      continue;
    }

    if (!title.empty() || !url_patterns.is_empty()) {
      if (!title.empty() && !base::MatchPattern(web_contents->GetTitle(),
                                                base::UTF8ToUTF16(title))) {
        continue;
      }

      if (!url_patterns.is_empty() &&
          !url_patterns.MatchesURL(web_contents->GetURL())) {
        continue;
      }
    }

    extensions::api::tabs::Tab tab_object;
    tab_object.id = tab_id;
    tab_object.active =
        (web_contents->GetVisibility() != content::Visibility::HIDDEN);
    tab_object.selected = tab_object.active;
    tab_object.url = web_contents->GetLastCommittedURL().spec();
    tab_object.title = base::UTF16ToUTF8(web_contents->GetTitle());
    tab_object.group_id = -1;
    tab_object.incognito = web_contents->GetBrowserContext()->IsOffTheRecord();
    tab_object.highlighted = tab_object.active;
    result.Append(tab_object.ToValue());
  }

  return RespondNow(WithArguments(std::move(result)));
}

ExtensionFunction::ResponseAction TabsGetFunction::Run() {
  std::optional<tabs::Get::Params> params = tabs::Get::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  int tab_id = params->tab_id;

  content::WebContents* web_contents =
      NevaExtensionsServiceFactory::GetService(browser_context())
          ->GetTabHelper()
          ->GetWebContentsFromId(tab_id);

  if (!web_contents) {
    return RespondNow(
        Error(base::StringPrintf("No tab with id: %d.", tab_id)));
  }

  extensions::api::tabs::Tab tab_object;
  tab_object.id = tab_id;
  tab_object.window_id = tab_id;
  tab_object.active =
      (web_contents->GetVisibility() != content::Visibility::HIDDEN);
  tab_object.selected = tab_object.active;
  tab_object.url = web_contents->GetLastCommittedURL().spec();
  tab_object.title = base::UTF16ToUTF8(web_contents->GetTitle());
  tab_object.group_id = -1;
  tab_object.incognito = web_contents->GetBrowserContext()->IsOffTheRecord();
  tab_object.highlighted = tab_object.active;
  base::Value::Dict result{tab_object.ToValue()};
  return RespondNow(WithArguments(std::move(result)));
}

ExecuteCodeInTabFunction::ExecuteCodeInTabFunction() : execute_tab_id_(0) {}

ExecuteCodeInTabFunction::~ExecuteCodeInTabFunction() {}

extensions::ExecuteCodeFunction::InitResult ExecuteCodeInTabFunction::Init() {
  if (init_result_) {
    return init_result_.value();
  }

  if (args().size() < 2) {
    return set_init_result(VALIDATION_FAILURE);
  }

  const auto& tab_id_value = args()[0];
  // |tab_id| is optional so it's ok if it's not there.
  int tab_id = 0;
  if (tab_id_value.is_int()) {
    // But if it is present, it needs to be positive.
    tab_id = tab_id_value.GetInt();
    if (tab_id < 1) {
      return set_init_result(VALIDATION_FAILURE);
    }
  }

  // |details| are not optional.
  const base::Value& details_value = args()[1];
  if (!details_value.is_dict()) {
    return set_init_result(VALIDATION_FAILURE);
  }
  auto details = InjectDetails::FromValue(details_value.GetDict());
  if (!details) {
    return set_init_result(VALIDATION_FAILURE);
  }

  // If the tab ID wasn't given then it needs to be converted to the
  // currently active tab's ID.
  if (tab_id == 0) {
    WebContentsMap* web_contents_map = WebContentsMap::GetInstance();
    for (auto& it : *web_contents_map) {
      content::WebContents* web_contents = it.second;
      uint64_t contents_tab_id =
          NevaExtensionsServiceFactory::GetService(browser_context())
              ->GetTabHelper()
              ->GetTabIdFromWebContents(web_contents);
      if (contents_tab_id == 0) {
        continue;
      }
      if (web_contents->GetVisibility() != content::Visibility::HIDDEN) {
        tab_id = contents_tab_id;
      }
    }
    CHECK_GE(tab_id, 0);
  }

  execute_tab_id_ = tab_id;
  details_ = std::make_unique<InjectDetails>(std::move(*details));
  set_host_id(extensions::mojom::HostID(
      extensions::mojom::HostID::HostType::kExtensions, extension()->id()));
  return set_init_result(SUCCESS);
}

bool ExecuteCodeInTabFunction::ShouldInsertCSS() const {
  return false;
}

bool ExecuteCodeInTabFunction::ShouldRemoveCSS() const {
  return false;
}

bool ExecuteCodeInTabFunction::CanExecuteScriptOnPage(std::string* error) {
  content::WebContents* contents = nullptr;

  // If |tab_id| is specified, look for the tab. Otherwise default to selected
  // tab in the current window.
  CHECK_GE(execute_tab_id_, 1);
  contents = NevaExtensionsServiceFactory::GetService(browser_context())
                 ->GetTabHelper()
                 ->GetWebContentsFromId(execute_tab_id_);
  if (!contents) {
    return false;
  }

  int frame_id = details_->frame_id
                     ? *details_->frame_id
                     : extensions::ExtensionApiFrameIdMap::kTopFrameId;
  content::RenderFrameHost* render_frame_host =
      extensions::ExtensionApiFrameIdMap::GetRenderFrameHostById(contents,
                                                                 frame_id);
  if (!render_frame_host) {
    *error = extensions::ErrorUtils::FormatErrorMessage(
        tabs_constants::kFrameNotFoundError, base::NumberToString(frame_id),
        base::NumberToString(execute_tab_id_));
    return false;
  }

  // Content scripts declared in manifest.json can access frames at about:-URLs
  // if the extension has permission to access the frame's origin, so also allow
  // programmatic content scripts at about:-URLs for allowed origins.
  GURL effective_document_url(render_frame_host->GetLastCommittedURL());
  bool is_about_url = effective_document_url.SchemeIs(url::kAboutScheme);
  if (is_about_url && details_->match_about_blank &&
      *details_->match_about_blank) {
    effective_document_url =
        GURL(render_frame_host->GetLastCommittedOrigin().Serialize());
  }

  if (!effective_document_url.is_valid()) {
    // Unknown URL, e.g. because no load was committed yet. Allow for now, the
    // renderer will check again and fail the injection if needed.
    return true;
  }

  // NOTE: This can give the wrong answer due to race conditions, but it is OK,
  // we check again in the renderer.
  if (!extension()->permissions_data()->CanAccessPage(effective_document_url,
                                                      execute_tab_id_, error)) {
    if (is_about_url &&
        extension()->permissions_data()->active_permissions().HasAPIPermission(
            extensions::mojom::APIPermissionID::kTab)) {
      *error = extensions::ErrorUtils::FormatErrorMessage(
          extensions::manifest_errors::kCannotAccessAboutUrl,
          render_frame_host->GetLastCommittedURL().spec(),
          render_frame_host->GetLastCommittedOrigin().Serialize());
    }
    return false;
  }

  return true;
}

extensions::ScriptExecutor* ExecuteCodeInTabFunction::GetScriptExecutor(
    std::string* error) {
  content::WebContents* contents =
      NevaExtensionsServiceFactory::GetService(browser_context())
          ->GetTabHelper()
          ->GetWebContentsFromId(execute_tab_id_);

  if (!contents) {
    return nullptr;
  }

  return WebContentsMap::GetInstance()->GetScriptExecutor(contents);
}

bool ExecuteCodeInTabFunction::IsWebView() const {
  return false;
}

const GURL& ExecuteCodeInTabFunction::GetWebViewSrc() const {
  return GURL::EmptyGURL();
}

bool TabsInsertCSSFunction::ShouldInsertCSS() const {
  return true;
}

}  // namespace neva
