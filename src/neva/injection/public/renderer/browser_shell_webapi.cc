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

#include "neva/injection/public/renderer/browser_shell_webapi.h"

#include "neva/injection/renderer/browser_shell/browser_shell_injection.h"

namespace injections {

// static
void BrowserShellWebAPI::Install(blink::WebLocalFrame* frame, const std::string&) {
  BrowserShellInjection::Install(frame);
}

// static
void BrowserShellWebAPI::Uninstall(blink::WebLocalFrame* frame) {
  BrowserShellInjection::Uninstall(frame);
}

}  // namespace injections
