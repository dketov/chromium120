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

namespace neva {

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

}  // namespace neva
