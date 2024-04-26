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

#ifndef NEVA_BROWSER_SHELL_APP_PLATFORM_REGISTRATION_H_
#define NEVA_BROWSER_SHELL_APP_PLATFORM_REGISTRATION_H_

#include "base/values.h"
#include "neva/app_runtime/app/app_runtime_shell_observer.h"

#include <memory>

namespace neva_app_runtime {
class ShellWindow;
}  // namespace neva_app_runtime

namespace pal {
class ApplicationRegistratorDelegate;
}  // namespace pal

namespace browser_shell {

class PlatformRegistration : public neva_app_runtime::ShellObserver {
 public:
  explicit PlatformRegistration(neva_app_runtime::ShellWindow* main_window);
  ~PlatformRegistration() override;
  void OnMessage(const std::string& event,
                 const std::string& reason,
                 const base::Value::Dict* params);
  void OnMainWindowClosing() override;

 private:
  neva_app_runtime::ShellWindow* main_window_;
  std::unique_ptr<pal::ApplicationRegistratorDelegate> delegate_;
};

}  // browser_shell

#endif  // NEVA_BROWSER_SHELL_APP_PLATFORM_REGISTRATION_H_;
