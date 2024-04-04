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

#include "neva/browser_shell/app/platform_registration.h"

#include "base/command_line.h"
#include "base/json/json_writer.h"
#include "neva/app_runtime/app/app_runtime_page_contents.h"
#include "neva/app_runtime/app/app_runtime_page_view.h"
#include "neva/app_runtime/app/app_runtime_shell_window.h"
#include "neva/browser_shell/common/browser_shell_switches.h"
#include "neva/pal_service/pal_platform_factory.h"
#include "neva/pal_service/public/application_registrator_delegate.h"

namespace {

const char kAction[] = "action";
const char kIntentService[] = "com.webos.service.intent";
const char kTarget[] = "target";
const char kUri[] = "uri";

}  // namespace

namespace browser_shell {

PlatformRegistration::PlatformRegistration(
    neva_app_runtime::ShellWindow* main_window)
    : main_window_(main_window) {
  base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(switches::kShellAppId) &&
      cmd->HasSwitch(switches::kWebOSLunaServiceName)) {
    delegate_ =
        pal::PlatformFactory::Get()->CreateApplicationRegistratorDelegate(
            cmd->GetSwitchValueASCII(switches::kShellAppId),
            cmd->GetSwitchValueASCII(switches::kWebOSLunaServiceName),
            base::BindRepeating(&PlatformRegistration::OnMessage,
                                base::Unretained(this)));
    if (!delegate_) {
      LOG(ERROR) << __func__ << "(): Application registration is not supported";
      return;
    }

    if (delegate_->GetStatus() !=
        pal::ApplicationRegistratorDelegate::Status::kSuccess) {
      LOG(ERROR) << __func__ << "(): Application "
                 << cmd->GetSwitchValueASCII(switches::kWebOSLunaServiceName)
                 << " was not registered.";
    }
  }
}

PlatformRegistration::~PlatformRegistration() = default;

void PlatformRegistration::OnMessage(const std::string& event,
                                     const std::string& reason,
                                     const base::Value::Dict* params) {
  if (main_window_ && (event == "relaunch")) {
    // TODO(neva): Workaround params. in SetFullscreen()
    // For workaround, set param. fullscreen to true.
    // WaylandToplevelWindow only supports display::kInvalidDisplayId for
    // target_display_id. This needs to be updated if WaylandToplevelWindow
    // implementation is changed.
    main_window_->SetFullscreen(true, display::kInvalidDisplayId);
    if (!params) {
      LOG(ERROR) << "params field is absent in relaunch event.";
      return;
    }

    base::Value::Dict js_detail;
    if (reason == kIntentService) {
      const std::string* action = params->FindString(kAction);
      const std::string* uri = params->FindString(kUri);
      if (action && !action->empty() && uri && !uri->empty()) {
        js_detail.Set("action", action->c_str());
        js_detail.Set("uri", uri->c_str());
      }
    } else {
      const std::string* target_url = params->FindString(kTarget);
      if (target_url && target_url->size()) {
        js_detail.Set("url", target_url->c_str());
      }
    }

    if (!js_detail.empty()) {
      base::Value::Dict js_data;
      js_data.Set("detail", std::move(js_detail));
      const auto js_data_string = base::WriteJson(js_data);
      if (js_data_string.has_value()) {
        std::string js_line = base::StringPrintf(
            R"JS(var e_tab_open = new CustomEvent("webOSRelaunch", %s);
                document.dispatchEvent(e_tab_open);)JS",
            js_data_string->c_str());
        main_window_->GetPageView()
            ->GetPageContents()
            ->ExecuteJavaScriptInMainFrame(js_line);
      } else {
        NOTREACHED();
      }
    }
  }
}

void PlatformRegistration::OnMainWindowClosing() {
  main_window_ = nullptr;
}

}  // namespace browser_shell
