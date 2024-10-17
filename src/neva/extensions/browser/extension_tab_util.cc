// Copyright 2023 LG Electronics, Inc.
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

#include "neva/extensions/browser/extension_tab_util.h"

#include "base/json/json_reader.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/event_router.h"
#include "neva/extensions/browser/api/tabs/tabs_constants.h"
#include "neva/extensions/browser/neva_extensions_service_factory.h"
#include "neva/extensions/browser/neva_extensions_service_impl.h"
#include "neva/extensions/browser/tab_helper.h"
#include "neva/extensions/common/api/tabs.h"

namespace neva {

namespace tabs = extensions::api::tabs;
namespace windows = extensions::api::windows;

// TODO(neva): There is no way to distinguish among types (normal, popup, etc.)
// at the moment, however, the chrome.windows.create only allows to create popup
// type, so will return as popup type for now.
base::Value::Dict ExtensionTabUtil::CreateWindowValueForExtension(
    int window_id,
    bool focused,
    windows::WindowType type,
    bool always_on_top,
    bool incognito) {
  base::Value::Dict dict;
  dict.Set("id", window_id);
  dict.Set("alwaysOnTop", always_on_top);
  dict.Set("focused", focused);
  dict.Set("incognito", incognito);
  dict.Set("left", 0);
  dict.Set("top", 0);
  dict.Set("width", 0);

  std::string type_str;
  switch (type) {
    case windows::WINDOW_TYPE_POPUP:
      type_str = "popup";
      break;
    case windows::WINDOW_TYPE_PANEL:
      type_str = "panel";
      break;
    case windows::WINDOW_TYPE_APP:
      type_str = "app";
      break;
    case windows::WINDOW_TYPE_DEVTOOLS:
      type_str = "devtools";
      break;
    case windows::WINDOW_TYPE_NORMAL:
    default:
      type_str = "normal";
      break;
  }
  dict.Set("type", type_str);

  return dict;
}

void ExtensionTabUtil::DispatchTabsOnActivated(content::BrowserContext* context,
                                               uint64_t tab_id) {
  VLOG(1) << __func__ << " tab_id: " << tab_id << " / context: " << context;
  if (!context) {
    return;
  }

  tabs::OnActivated::ActiveInfo details;
  details.tab_id = tab_id;
  details.window_id = tab_id;

  const std::string event_name = tabs::OnActivated::kEventName;
  if (extensions::EventRouter::Get(context)->HasEventListener(event_name)) {
    auto event = std::make_unique<extensions::Event>(
        extensions::events::TABS_ON_ACTIVATED, event_name,
        tabs::OnActivated::Create(details), context);
    extensions::EventRouter::Get(context)->BroadcastEvent(std::move(event));
  }
}

void ExtensionTabUtil::DispatchTabsOnUpdated(content::BrowserContext* context,
                                             uint64_t tab_id,
                                             const std::string& change_info) {
  VLOG(1) << __func__ << " tab_id: " << tab_id << " / context: " << context;
  if (!context) {
    return;
  }

  tabs::OnUpdated::ChangeInfo details;
  std::optional<base::Value::Dict> dict =
      base::JSONReader::ReadDict(change_info);
  if (!dict) {
    LOG(ERROR) << __func__ << " parsing change_info JSON has failed.";
    return;
  }
  const std::string* status = dict->FindString(tabs_constants::kStatusKey);
  if (status && *status == tabs_constants::kStatusLoading) {
    details.status = tabs::TabStatus::TAB_STATUS_LOADING;
  } else {
    details.status = tabs::TabStatus::TAB_STATUS_COMPLETE;
  }

  content::WebContents* web_contents =
      NevaExtensionsServiceFactory::GetService(context)
          ->GetTabHelper()
          ->GetWebContentsFromId(tab_id);
  tabs::Tab tab_object;
  tab_object.id = tab_id;
  tab_object.window_id = tab_id;
  tab_object.active =
      (web_contents->GetVisibility() != content::Visibility::HIDDEN);
  tab_object.selected = tab_object.active;
  tab_object.url = web_contents->GetLastCommittedURL().spec();
  tab_object.title = base::UTF16ToUTF8(web_contents->GetTitle());
  tab_object.group_id = -1;
  tab_object.incognito = context->IsOffTheRecord();
  tab_object.highlighted = tab_object.active;

  const std::string event_name = tabs::OnUpdated::kEventName;
  if (extensions::EventRouter::Get(context)->HasEventListener(event_name)) {
    auto event = std::make_unique<extensions::Event>(
        extensions::events::TABS_ON_UPDATED, event_name,
        tabs::OnUpdated::Create(tab_id, details, tab_object), context);
    extensions::EventRouter::Get(context)->BroadcastEvent(std::move(event));
  }
}

void ExtensionTabUtil::DispatchTabsOnRemoved(content::BrowserContext* context,
                                             uint64_t tab_id) {
  if (!context) {
    return;
  }

  tabs::OnRemoved::RemoveInfo details;
  details.window_id = tab_id;
  details.is_window_closing = false;

  const std::string event_name = tabs::OnRemoved::kEventName;
  if (extensions::EventRouter::Get(context)->HasEventListener(event_name)) {
    auto event = std::make_unique<extensions::Event>(
        extensions::events::TABS_ON_REMOVED, event_name,
        tabs::OnRemoved::Create(tab_id, details), context);
    extensions::EventRouter::Get(context)->BroadcastEvent(std::move(event));
  }
}

}  // namespace neva
