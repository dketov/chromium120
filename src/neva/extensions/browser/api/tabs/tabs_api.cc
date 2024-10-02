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

}  // namespace neva
