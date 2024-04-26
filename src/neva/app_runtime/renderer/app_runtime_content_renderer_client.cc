// Copyright 2016 LG Electronics, Inc.
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

#include "neva/app_runtime/renderer/app_runtime_content_renderer_client.h"

#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "components/watchdog/switches.h"
#include "content/public/common/url_constants.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/render_thread_observer.h"
#include "net/base/filename_util.h"
#include "neva/app_runtime/app/app_runtime_main_delegate.h"
#include "neva/app_runtime/common/app_runtime_file_access_controller.h"
#include "neva/app_runtime/grit/app_runtime_network_error_resources.h"
#include "neva/app_runtime/public/webview_info.h"
#include "neva/app_runtime/renderer/app_runtime_localized_error.h"
#include "neva/app_runtime/renderer/app_runtime_page_load_timing_render_frame_observer.h"
#include "neva/app_runtime/renderer/app_runtime_render_frame_observer.h"
#include "third_party/blink/public/mojom/fetch/fetch_api_request.mojom.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_url_error.h"
#include "third_party/blink/public/web/web_console_message.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_security_policy.h"
#include "third_party/blink/public/web/web_view.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/webui/jstemplate_builder.h"
#include "url/url_constants.h"

#if defined(USE_NEVA_CDM)
#include "components/cdm/renderer/neva/key_systems_util.h"
#endif

#if defined(USE_NEVA_MEDIA)
#include "content/renderer/render_thread_impl.h"
#endif

#if defined(USE_NEVA_CHROME_EXTENSIONS)
#include "base/process/current_process.h"
#include "extensions/common/switches.h"
#include "extensions/renderer/extension_frame_helper.h"
#include "third_party/blink/public/platform/scheduler/web_renderer_process_type.h"
#endif  // defined(USE_NEVA_CHROME_EXTENSIONS)

using blink::mojom::FetchCacheMode;

namespace neva_app_runtime {

class AppRuntimeRenderObserver : public content::RenderFrameObserver,
                                 public content::RenderThreadObserver {
 public:
  AppRuntimeRenderObserver(content::RenderFrame* render_frame,
                           AppRuntimeContentRendererClient* renderer_client)
      : content::RenderFrameObserver(render_frame),
        renderer_client_(renderer_client) {
    content::RenderThread::Get()->AddObserver(this);
  }

  ~AppRuntimeRenderObserver() override {
    content::RenderThread::Get()->RemoveObserver(this);
  }

  void OnDestruct() override {
    if (renderer_client_)
      renderer_client_->DestructObserver();
  }

  void NetworkStateChanged(bool online) override {
    if (online) {
      if (render_frame() && render_frame()->GetWebFrame()) {
        render_frame()->GetWebFrame()->StartReload(
            blink::WebFrameLoadType::kReload);
      }
      OnDestruct();
    }
  }

 private:
  AppRuntimeContentRendererClient* renderer_client_;
};

AppRuntimeContentRendererClient::AppRuntimeContentRendererClient() {}

AppRuntimeContentRendererClient::~AppRuntimeContentRendererClient() {}

void AppRuntimeContentRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
#if defined(USE_NEVA_CHROME_EXTENSIONS)
  extensions::Dispatcher* dispatcher =
      extensions_renderer_client_->GetDispatcher();
  // ExtensionFrameHelper destroys itself when the RenderFrame is destroyed.
  new extensions::ExtensionFrameHelper(render_frame, dispatcher);
  dispatcher->OnRenderFrameCreated(render_frame);
#endif  // defined(USE_NEVA_CHROME_EXTENSIONS)

  // AppRuntimeRenderFrameObserver destroys itself when the RenderFrame is
  // destroyed.
  new AppRuntimeRenderFrameObserver(render_frame);
  // Only attach AppRuntimePageLoadTimingRenderFrameObserver to the main frame,
  // since we only want to observe page load timing for the main frame.
  if (render_frame->IsMainFrame()) {
    new AppRuntimePageLoadTimingRenderFrameObserver(render_frame);
  }
}

bool AppRuntimeContentRendererClient::IsAccessAllowedForURL(
    const blink::WebURL& url) {
  // Ignore non-file scheme requests
  if (!static_cast<GURL>(url).SchemeIsFile())
    return true;

  const neva_app_runtime::AppRuntimeFileAccessController*
      file_access_controller = GetFileAccessController();

  base::FilePath file_path;
  if (!net::FileURLToFilePath(url, &file_path))
    return false;

  if (file_access_controller)
    return file_access_controller->IsAccessAllowed(file_path, webview_info_);

  return false;
}

void AppRuntimeContentRendererClient::RegisterSchemes() {
  // webapp needs the file scheme to register a service worker. Used from
  // third_party/blink/renderer/modules/service_worker/service_worker_container.cc
  blink::WebString file_scheme(blink::WebString::FromASCII(url::kFileScheme));
  blink::WebSecurityPolicy::RegisterURLSchemeAsAllowingServiceWorkers(
      file_scheme);
}

void AppRuntimeContentRendererClient::RenderThreadStarted() {
#if defined(USE_NEVA_CHROME_EXTENSIONS)
  InitRenderThreadForExtension();
#endif  // defined(USE_NEVA_CHROME_EXTENSIONS)

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(watchdog::switches::kEnableWatchdog)) {
    watchdog_.reset(new watchdog::Watchdog());

    std::string env_timeout = command_line->GetSwitchValueASCII(
        watchdog::switches::kWatchdogRendererTimeout);
    if (!env_timeout.empty()) {
      int timeout;
      if (base::StringToInt(env_timeout, &timeout))
        watchdog_->SetTimeout(timeout);
    }

    std::string env_period = command_line->GetSwitchValueASCII(
        watchdog::switches::kWatchdogRendererPeriod);
    if (!env_period.empty()) {
      int period;
      if (base::StringToInt(env_period, &period))
        watchdog_->SetPeriod(period);
    }

    watchdog_->StartWatchdog();

    // Check it's currently running on RenderThread.
    CHECK(content::RenderThread::Get());
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&AppRuntimeContentRendererClient::ArmWatchdog,
                                  base::Unretained(this)));
  }
}

void AppRuntimeContentRendererClient::PrepareErrorPage(
    content::RenderFrame* render_frame,
    const blink::WebURLError& error,
    const std::string& http_method,
    content::mojom::AlternativeErrorPageOverrideInfoPtr
        alternative_error_page_info,
    std::string* error_html) {
  if (error_html) {
    error_html->clear();

    // Resource will change to net error specific page
    int resource_id = IDR_APP_RUNTIME_NETWORK_ERROR_PAGE;
    const std::string template_html =
        ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
            resource_id);
    if (template_html.empty()) {
      LOG(ERROR) << "unable to load template.";
    } else {
      base::Value::Dict error_strings;
      AppRuntimeLocalizedError::GetErrorStrings(error.reason(), error_strings);
      // "t" is the id of the template's root node.
      *error_html = webui::GetTemplatesHtml(template_html, error_strings, "t");
    }

    render_observer_.reset(new AppRuntimeRenderObserver(render_frame, this));
  }
}

void AppRuntimeContentRendererClient::WillSendRequest(
    blink::WebLocalFrame* frame,
    ui::PageTransition transition_type,
    const blink::WebURL& url,
    const net::SiteForCookies& site_for_cookies,
    const url::Origin* initiator_origin,
    GURL* new_url) {
  // Ignore non-file scheme requests
  if (!static_cast<GURL>(url).SchemeIsFile())
    return;

  // Ignore file scheme requests from non-file scheme origins granted
  // with allow_local_resource_load permission
  if (!initiator_origin->GetURL().SchemeIsFile())
    return;

  const AppRuntimeFileAccessController* file_access_controller =
      GetFileAccessController();

  if (file_access_controller) {
    // Ignore navigations since they are handled on browser side
    if (!ui::PageTransitionTypeIncludingQualifiersIs(transition_type,
                                                     ui::PAGE_TRANSITION_FIRST))
      return;

    base::FilePath file_path;
    if (!net::FileURLToFilePath(GURL(url), &file_path) ||
        !file_access_controller->IsAccessAllowed(file_path, webview_info_)) {
      blink::WebConsoleMessage error_msg;
      error_msg.level = blink::mojom::ConsoleMessageLevel::kError;
      error_msg.text = blink::WebString::FromASCII(
          "Access is blocked to resource: " + url.GetString().Ascii());
      frame->AddMessageToConsole(error_msg);

      // Redirect to unreachable URL
      *new_url = GURL(url::kIllegalDataURL);
    }
  }
}

void AppRuntimeContentRendererClient::SetWebViewInfo(
    const std::string& app_path, const std::string& trust_level) {
  webview_info_.app_path = app_path;
  webview_info_.trust_level = trust_level;
}

void AppRuntimeContentRendererClient::DestructObserver() {
  render_observer_.reset();
}

void AppRuntimeContentRendererClient::ArmWatchdog() {
  watchdog_->Arm();
  if (!watchdog_->HasThreadInfo())
    watchdog_->SetCurrentThreadInfo();

  // Check it's currently running on RenderThread.
  CHECK(content::RenderThread::Get());
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&AppRuntimeContentRendererClient::ArmWatchdog,
                     base::Unretained(this)),
      base::Seconds(watchdog_->GetPeriod()));
}

#if defined(USE_NEVA_CDM)
void AppRuntimeContentRendererClient::GetSupportedKeySystems(
    media::GetSupportedKeySystemsCB cb) {
  cdm::AddSupportedKeySystems(std::move(cb));
}
#endif

#if defined(USE_NEVA_MEDIA)
void AppRuntimeContentRendererClient::SetUseVideoDecodeAccelerator(bool use) {
  if (content::RenderThreadImpl::current())
    content::RenderThreadImpl::current()->SetUseVideoDecodeAccelerator(use);
}
#endif

#if defined(USE_NEVA_CHROME_EXTENSIONS)
void AppRuntimeContentRendererClient::WebViewCreated(
    blink::WebView* web_view,
    bool was_created_by_renderer,
    const url::Origin* outermost_origin) {
  extensions_renderer_client_->WebViewCreated(web_view, outermost_origin);
}

void AppRuntimeContentRendererClient::InitRenderThreadForExtension() {
  content::RenderThread* thread = content::RenderThread::Get();

  extensions_client_.reset(new neva::NevaExtensionsClient);
  extensions::ExtensionsClient::Set(extensions_client_.get());

  extensions_renderer_client_.reset(new neva::NevaExtensionsRendererClient);
  extensions::ExtensionsRendererClient::Set(extensions_renderer_client_.get());
  thread->AddObserver(extensions_renderer_client_->GetDispatcher());

  const bool is_extension = base::CommandLine::ForCurrentProcess()->HasSwitch(
      extensions::switches::kExtensionProcess);

  thread->SetRendererProcessType(
      is_extension
          ? blink::scheduler::WebRendererProcessType::kExtensionRenderer
          : blink::scheduler::WebRendererProcessType::kRenderer);

  if (is_extension) {
    // The process name was set to "Renderer" in RendererMain(). Update it to
    // "Extension Renderer" to highlight that it's hosting an extension.
    base::CurrentProcess::GetInstance().SetProcessType(
        base::CurrentProcessType::PROCESS_RENDERER_EXTENSION);
  }
}

void AppRuntimeContentRendererClient::RunScriptsAtDocumentStart(
    content::RenderFrame* render_frame) {
  extensions_renderer_client_->GetDispatcher()->RunScriptsAtDocumentStart(
      render_frame);
}

void AppRuntimeContentRendererClient::RunScriptsAtDocumentEnd(
    content::RenderFrame* render_frame) {
  extensions_renderer_client_->GetDispatcher()->RunScriptsAtDocumentEnd(
      render_frame);
}

bool AppRuntimeContentRendererClient::AllowScriptExtensionForServiceWorker(
    const url::Origin& script_origin) {
  return true;
}

void AppRuntimeContentRendererClient::
    DidInitializeServiceWorkerContextOnWorkerThread(
        blink::WebServiceWorkerContextProxy* context_proxy,
        const GURL& service_worker_scope,
        const GURL& script_url) {
  extensions_renderer_client_->GetDispatcher()
      ->DidInitializeServiceWorkerContextOnWorkerThread(
          context_proxy, service_worker_scope, script_url);
}

void AppRuntimeContentRendererClient::WillEvaluateServiceWorkerOnWorkerThread(
    blink::WebServiceWorkerContextProxy* context_proxy,
    v8::Local<v8::Context> v8_context,
    int64_t service_worker_version_id,
    const GURL& service_worker_scope,
    const GURL& script_url) {
  extensions_renderer_client_->GetDispatcher()
      ->WillEvaluateServiceWorkerOnWorkerThread(
          context_proxy, v8_context, service_worker_version_id,
          service_worker_scope, script_url);
}

void AppRuntimeContentRendererClient::
    DidStartServiceWorkerContextOnWorkerThread(
        int64_t service_worker_version_id,
        const GURL& service_worker_scope,
        const GURL& script_url) {
  extensions_renderer_client_->GetDispatcher()
      ->DidStartServiceWorkerContextOnWorkerThread(
          service_worker_version_id, service_worker_scope, script_url);
}

void AppRuntimeContentRendererClient::
    WillDestroyServiceWorkerContextOnWorkerThread(
        v8::Local<v8::Context> context,
        int64_t service_worker_version_id,
        const GURL& service_worker_scope,
        const GURL& script_url) {
  extensions_renderer_client_->GetDispatcher()
      ->WillDestroyServiceWorkerContextOnWorkerThread(
          context, service_worker_version_id, service_worker_scope, script_url);
}
#endif  // defined(USE_NEVA_CHROME_EXTENSIONS)

}  // namespace neva_app_runtime
