// Copyright 2019 LG Electronics, Inc.
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

#include "extensions/shell/neva/platform_register_app.h"

#include "base/command_line.h"
#include "base/json/json_writer.h"
#include "base/strings/stringprintf.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/switches.h"
#include "neva/pal_service/pal_platform_factory.h"
#include "neva/pal_service/public/application_registrator_delegate.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"

namespace {

const char kAction[] = "action";
const char kIntentService[] = "com.webos.service.intent";
const char kRelaunchEvent[] = "relaunch";
const char kTarget[] = "target";
const char kUri[] = "uri";

}  // namespace

PlatformRegisterApp::PlatformRegisterApp(content::WebContents* web_contents)
    : content::WebContentsUserData<PlatformRegisterApp>(*web_contents),
      content::WebContentsObserver(web_contents),
      weak_factory_(this) {
  base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(extensions::switches::kWebOSAppId) &&
      cmd->HasSwitch(extensions::switches::kWebOSLunaServiceName)) {
    const std::string& application_id =
        cmd->GetSwitchValueASCII(extensions::switches::kWebOSAppId);
    const std::string& application_name =
        cmd->GetSwitchValueASCII(extensions::switches::kWebOSLunaServiceName);
    delegate_ =
        pal::PlatformFactory::Get()->CreateApplicationRegistratorDelegate(
            application_id, application_name,
            base::BindRepeating(&PlatformRegisterApp::OnEvent,
                                weak_factory_.GetWeakPtr()));

    if (delegate_->GetStatus() !=
        pal::ApplicationRegistratorDelegate::Status::kSuccess)
      LOG(ERROR) << __func__ << "(): error during delegate creation";
  }
}

PlatformRegisterApp::~PlatformRegisterApp() = default;

void PlatformRegisterApp::OnEvent(const std::string& event,
                                  const std::string& reason,
                                  const base::Value::Dict* parameters) {
  content::WebContents* contents = web_contents();
  if (!contents)
    return;

  if (event == kRelaunchEvent) {
    aura::Window* top_window = contents->GetTopLevelNativeWindow();
    if (top_window && top_window->GetHost()) {
      // TODO(neva): Workaround params. in SetFullscreen()
      // For workaround, set param. fullscreen to true.
      // WaylandToplevelWindow only supports display::kInvalidDisplayId for
      // target_display_id. This needs to be updated if WaylandToplevelWindow
      // implementation is changed.
      top_window->GetHost()->SetFullscreen(true, display::kInvalidDisplayId);
    }

    if (!parameters) {
      LOG(ERROR) << "Parameters field is absent in relaunch event.";
      return;
    }

    base::Value::Dict js_detail;
    if (reason == kIntentService) {
      const std::string* action = parameters->FindString(kAction);
      const std::string* uri = parameters->FindString(kUri);
      if (action && action->size() && uri && uri->size()) {
        js_detail.Set("action", action->c_str());
        js_detail.Set("uri", uri->c_str());
      }
    } else {
      const std::string* target_url = parameters->FindString(kTarget);
      if (target_url && target_url->size()) {
        js_detail.Set("url", target_url->c_str());
      }
    }

    if (js_detail.size() > 0) {
      content::RenderFrameHost* rfh = contents->GetPrimaryMainFrame();
      if (rfh) {
        base::Value::Dict js_data;
        js_data.Set("detail", std::move(js_detail));
        std::string js_data_string;
        base::JSONWriter::Write(js_data, &js_data_string);
        std::string js_line = base::StringPrintf(
            R"JS(var e_tab_open = new CustomEvent("webOSRelaunch", %s);
                document.dispatchEvent(e_tab_open);)JS",
            js_data_string.c_str());
        rfh->ExecuteJavaScript(base::UTF8ToUTF16(js_line),
                               base::NullCallback());
      }
    }
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(PlatformRegisterApp);
