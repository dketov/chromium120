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

#ifndef NEVA_EXTENSIONS_BROWSER_EXTENSION_TAB_UTIL_H_
#define NEVA_EXTENSIONS_BROWSER_EXTENSION_TAB_UTIL_H_

#include "base/values.h"
#include "neva/extensions/common/api/windows.h"

namespace neva {

class ExtensionTabUtil {
 public:
  static base::Value::Dict CreateWindowValueForExtension(
      int window_id,
      bool focused,
      extensions::api::windows::WindowType type =
          extensions::api::windows::WINDOW_TYPE_POPUP,
      bool always_on_top = false,
      bool incognito = false);
};

}  // namespace neva

#endif  // NEVA_EXTENSIONS_BROWSER_EXTENSION_TAB_UTIL_H_
