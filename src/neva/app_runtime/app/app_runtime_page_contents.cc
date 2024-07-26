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

#include "neva/app_runtime/app/app_runtime_page_contents.h"

#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "base/values.h"
#include "content/browser/renderer_host/navigation_entry_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/color_parser.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/user_agent.h"
#include "net/base/auth.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "neva/app_runtime/app/app_runtime_js_dialog_manager.h"
#include "neva/app_runtime/app/app_runtime_main_delegate.h"
#include "neva/app_runtime/app/app_runtime_page_contents_delegate.h"
#include "neva/app_runtime/app/app_runtime_page_view.h"
#include "neva/app_runtime/app/app_runtime_shell_environment.h"
#include "neva/app_runtime/app/app_runtime_visible_region_capture.h"
#include "neva/app_runtime/browser/app_runtime_browser_context.h"
#include "neva/app_runtime/browser/app_runtime_webview_controller_impl.h"
#include "neva/app_runtime/browser/browsing_data/browsing_data_remover.h"
#include "neva/app_runtime/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "neva/app_runtime/public/mojom/app_runtime_webview.mojom.h"
#include "neva/app_runtime/public/webview_controller_delegate.h"
#include "neva/app_runtime/webapp_injection_manager.h"
#include "neva/browser_shell/common/browser_shell_switches.h"
#include "neva/logging.h"
#include "neva/user_agent/common/user_agent.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/mojom/renderer_preferences.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "url/gurl.h"

#if defined(USE_NEVA_BROWSER_SERVICE)
#include "components/permissions/permission_request_manager.h"
#include "neva/browser_service/browser_service.h"
#endif  // USE_NEVA_BROWSER_SERVICE

#if defined(USE_NEVA_MEDIA)
#include "content/public/browser/neva/media_state_manager.h"
#endif

namespace neva_app_runtime {
namespace {

const char kIdentifier[] = "identifier";
const char kInitialize[] = "initialize";
const char kShellLaunchArgs[] = "launch-args";
const char kLaunchParams[] = "launchParams";

class BrowserShellWebViewControllerDelegate : public WebViewControllerDelegate {
 public:
  BrowserShellWebViewControllerDelegate() {}
  ~BrowserShellWebViewControllerDelegate() override {}

  void RunCommand(const std::string& name,
                  const std::vector<std::string>& arguments) override {}

  std::string RunFunction(const std::string& name,
                          const std::vector<std::string>& arguments) override {
    if (name == kInitialize) {
      return GetLaunchArgs();
    } else if (name == kIdentifier) {
      return GetIdentifier();
    }
    return std::string();
  }

 private:
  static std::string GetIdentifier() {
    base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
    return cmd->GetSwitchValueASCII(switches::kShellAppId);
  }

  static std::string GetLaunchArgs() {
    base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
    std::string launch_value = cmd->GetSwitchValueASCII(kShellLaunchArgs);
    absl::optional<base::Value> json = base::JSONReader::Read(launch_value);
    if (!json || !json->is_dict()) {
      return std::string();
    }
    auto dict = base::Value::Dict().Set(kLaunchParams, launch_value);
    return base::WriteJson(dict).value_or(std::string());
  }
};

bool IsStreamDevicesEmpty(const blink::mojom::StreamDevices& devices) {
  return !devices.audio_device.has_value() && !devices.video_device.has_value();
}

std::string WindowOpenDispositionToString(WindowOpenDisposition disposition) {
  switch (disposition) {
    case WindowOpenDisposition::IGNORE_ACTION:
      return "ignore";
    case WindowOpenDisposition::SAVE_TO_DISK:
      return "save_to_disk";
    case WindowOpenDisposition::CURRENT_TAB:
      return "current_tab";
    case WindowOpenDisposition::NEW_BACKGROUND_TAB:
      return "new_background_tab";
    case WindowOpenDisposition::NEW_FOREGROUND_TAB:
      return "new_foreground_tab";
    case WindowOpenDisposition::NEW_WINDOW:
      return "new_window";
    case WindowOpenDisposition::NEW_POPUP:
      return "new_popup";
    case WindowOpenDisposition::SINGLETON_TAB:
      return "singleton_tab";
    case WindowOpenDisposition::OFF_THE_RECORD:
      return "off_the_record";
    default:
      return "unknown";
  }
  NOTREACHED();
}

std::string TerminationStatusToString(base::TerminationStatus status) {
  // TERMINATION_STATUS_NORMAL_TERMINATION and TERMINATION_STATUS_STILL_RUNNING
  // are expected results of process shutdown with use of FastShutdownIfPossible
  switch (status) {
    case base::TERMINATION_STATUS_NORMAL_TERMINATION:
    case base::TERMINATION_STATUS_STILL_RUNNING:
      return "normal";
    case base::TERMINATION_STATUS_ABNORMAL_TERMINATION:
      return "abnormal";
    case base::TERMINATION_STATUS_PROCESS_WAS_KILLED:
      return "killed";
    case base::TERMINATION_STATUS_PROCESS_CRASHED:
      return "crashed";
    case base::TERMINATION_STATUS_LAUNCH_FAILED:
      return "launch_failed";
    case base::TERMINATION_STATUS_OOM:
      return "oom";
    default:
      return "unexpected";
  }
}

}  // namespace

PageContents::CreateParams::CreateParams() = default;

PageContents::CreateParams::CreateParams(const CreateParams&) = default;

PageContents::CreateParams&
PageContents::CreateParams::operator=(const CreateParams&) = default;

PageContents::CreateParams::~CreateParams() = default;

uint64_t PageContents::MediaAccessPermissionInfo::id = 0;

PageContents::MediaAccessPermissionInfo::MediaAccessPermissionInfo() = default;

PageContents::MediaAccessPermissionInfo::MediaAccessPermissionInfo(
    const blink::mojom::StreamDevicesSet& stream_devices_set,
    content::MediaResponseCallback callback)
    : stream_devices_set_(stream_devices_set.Clone()),
      callback(std::move(callback)) {
  MediaAccessPermissionInfo::id++;
}

PageContents::MediaAccessPermissionInfo::MediaAccessPermissionInfo(
    PageContents::MediaAccessPermissionInfo&&) = default;
PageContents::MediaAccessPermissionInfo::~MediaAccessPermissionInfo() = default;

// static
PageContents* PageContents::From(content::WebContents* web_contents) {
  return ShellEnvironment::GetInstance()->GetPageContentsFrom(web_contents);
}

PageContents::PageContents(const CreateParams& params)
    : PageContents(CreateWebContents(params), params) {}

PageContents::~PageContents() {
  if (delegate_)
    delegate_->OnDestroying(this);

  if (web_contents_.get())
    web_contents_->SetDelegate(nullptr);

  ShellEnvironment::GetInstance()->Release(this).reset();
}

void PageContents::Activate() {
  if (!web_contents_) {
    web_contents_ = ReCreateWebContents(
        last_browser_context_, session_storage_namespace_map_);
    Initialize();
    SetUserAgentOverride(user_agent_);
    if (parent_page_view_)
      parent_page_view_->UpdatePageContents();
    LoadURL(last_commited_url_);
    last_browser_context_ = nullptr;
    last_commited_url_.clear();
  }
}

void PageContents::ClearData(const std::string& clear_options,
                             const std::string& clear_data_type_set) {
  if (!web_contents_)
    return;

  absl::optional<base::Value> options = base::JSONReader::Read(clear_options);
  BrowsingDataRemover::TimeRange delete_time_range =
      BrowsingDataRemover::Unbounded();
  if (options && options->is_dict()) {
    delete_time_range = BrowsingDataRemover::TimeRange(
        base::Time::FromSecondsSinceUnixEpoch(options->GetDict().Find("since")->GetDouble()),
        base::Time::Now());
  }

  absl::optional<base::Value> data_type_set =
      base::JSONReader::Read(clear_data_type_set);
  uint32_t remove_data_mask = 0;
  if (data_type_set && data_type_set->is_dict()) {
    for (auto&& it : data_type_set->GetDict()) {
      if (it.second.GetBool()) {
        if (it.first == "cache") {
          remove_data_mask |= BrowsingDataRemover::RemoveBrowsingDataMask::
              REMOVE_CACHE;
        } else if (it.first == "cookies") {
          remove_data_mask |= BrowsingDataRemover::RemoveBrowsingDataMask::
              REMOVE_COOKIES;
        } else if (it.first == "sessionCookies") {
          remove_data_mask |= BrowsingDataRemover::RemoveBrowsingDataMask::
              REMOVE_SESSION_COOKIES;
        } else if (it.first == "persistentCookies") {
          remove_data_mask |= BrowsingDataRemover::RemoveBrowsingDataMask::
              REMOVE_PERSISTENT_COOKIES;
        } else if (it.first == "fileSystems") {
          remove_data_mask |= BrowsingDataRemover::RemoveBrowsingDataMask::
              REMOVE_FILE_SYSTEMS;
        } else if (it.first == "indexedDB") {
          remove_data_mask |= BrowsingDataRemover::RemoveBrowsingDataMask::
              REMOVE_INDEXEDDB;
        } else if (it.first == "localStorage") {
          remove_data_mask |= BrowsingDataRemover::RemoveBrowsingDataMask::
              REMOVE_LOCAL_STORAGE;
        } else if (it.first == "webSQL") {
          remove_data_mask |= BrowsingDataRemover::RemoveBrowsingDataMask::
              REMOVE_WEBSQL;
        }
      }
    }
  }

  content::BrowserContext* browser_context = web_contents_->GetBrowserContext();
  content::StoragePartition* partition = browser_context->GetStoragePartition(
      web_contents_->GetSiteInstance(), false);
  if (partition) {
    BrowsingDataRemover* remover =
        BrowsingDataRemover::GetForStoragePartition(partition);
    remover->Remove(delete_time_range, remove_data_mask, GURL(),
                    base::DoNothing());
  }
}

void PageContents::CaptureVisibleRegion(const std::string& format,
                                        int quality) {
  if (!web_contents_)
    return;

  if (visible_region_capture_.get() == nullptr) {
    VisibleRegionCapture::ImageFormat image_format(
        VisibleRegionCapture::ImageFormat::kNone);
    if (format == "jpeg")
      image_format = VisibleRegionCapture::ImageFormat::kJpeg;
    else if (format == "png")
      image_format = VisibleRegionCapture::ImageFormat::kPng;

    if (image_format != VisibleRegionCapture::ImageFormat::kNone) {
      visible_region_capture_ = std::make_unique<VisibleRegionCapture>(
          base::BindOnce(&PageContents::OnCaptureVisibleRegion,
                         base::Unretained(this)),
          web_contents_.get(), image_format,
          std::max(std::min(100, quality), 0));
      return;
    }
  }
  delegate_->OnVisibleRegionCaptured(std::string());
}

void PageContents::Deactivate() {
  if (!web_contents_)
    return;

  // TODO(neva): Workaround to fix crash caused by deactivate suspended page.
  // Suspending of DOM stops pages incompletely. After the deletion of
  // web_contents_ there were free resources in blink related to this page,
  // but in renderer process have not stopped subframes, and in time of
  // stopping that frames FrameScheduler already not existed.
  ResumeDOM();

  session_storage_namespace_map_ =
      web_contents_->GetController().GetSessionStorageNamespaceMap();
  last_commited_url_ = web_contents_->GetLastCommittedURL().spec();
  last_browser_context_ = web_contents_->GetBrowserContext();

  if (parent_page_view_)
    parent_page_view_->ResetPageContents();

  // TODO(neva): Workaround to fix display issue when close a web page.
  // After the renderer pocess shutdown by this call, the display isn't refeshed
  // and keeps showing the closed page. Since it doesn't happen with every page,
  // we temporarily disable this for now and will enable it later.
  //
  // content::RenderProcessHost* render_process =
  //     web_contents_->GetPrimaryMainFrame()->GetProcess();
  // if (render_process)
  //   render_process->FastShutdownIfPossible(1);

  web_contents_.reset();
}

bool PageContents::IsActive() {
  return web_contents_.get() != nullptr;
}

void PageContents::SetDelegate(PageContentsDelegate* delegate) {
  delegate_ = delegate ? delegate : &stub_delegate_;
}

PageContentsDelegate* PageContents::GetDelegate() const {
  return (delegate_ == &stub_delegate_) ? nullptr : delegate_;
}

uint64_t PageContents::GetID() const {
  return id_;
}

PageContents::Type PageContents::GetType() const {
  return create_params_.type;
}

content::WebContents* PageContents::GetWebContents() const {
  return web_contents_.get();
}

void PageContents::ExecuteJavaScriptInAllFrames(
    const std::string& code_string) {
  if (!web_contents_)
    return;

  const std::u16string js_code = base::UTF8ToUTF16(code_string);
  web_contents_->ForEachRenderFrameHost(
      [&js_code](content::RenderFrameHost* render_frame_host) {
        if (render_frame_host->IsRenderFrameLive())
          render_frame_host->ExecuteJavaScript(js_code, base::NullCallback());
      });
}

void PageContents::ExecuteJavaScriptInMainFrame(
    const std::string& code_string) {
  if (!web_contents_)
    return;

  content::RenderFrameHost* rfh = web_contents_->GetPrimaryMainFrame();
  if (rfh && rfh->IsRenderFrameLive()) {
    rfh->ExecuteJavaScriptForTests(base::UTF8ToUTF16(code_string),
                                   base::NullCallback());
  }
}

std::string PageContents::GetAcceptedLanguages() const {
  auto* renderer_prefs(web_contents_->GetMutableRendererPrefs());
  if (renderer_prefs)
    return renderer_prefs->accept_languages;
  return std::string();
}

bool PageContents::GetErrorPageHiding() const {
  return error_page_hiding_;
}

std::string PageContents::GetUserAgent() const {
  return user_agent_;
}

double PageContents::GetZoomFactor() const {
  return blink::PageZoomLevelToZoomFactor(
      content::HostZoomMap::GetZoomLevel(web_contents_.get()));
}

bool PageContents::CanGoBack() const {
  return web_contents_.get() ? web_contents_->GetController().CanGoBack()
                             : false;
}

bool PageContents::CanGoForward() const {
  return web_contents_.get() ? web_contents_->GetController().CanGoForward()
                             : false;
}

void PageContents::CloseJSDialog(bool success, const std::string& response) {
  if (js_dialog_manager_.get()) {
    js_dialog_manager_->CloseDialog(success, response);
  } else {
    LOG(WARNING) << __func__ << ", JavaScript dialog has never been created"
                 << " for this page contents.";
  }
}

void PageContents::GoBack() {
  if (web_contents_.get())
    web_contents_->GetController().GoBack();
}

void PageContents::GoForward() {
  if (web_contents_.get())
    web_contents_->GetController().GoForward();
}

bool PageContents::LoadURL(std::string url_string) {
  if (!web_contents_)
    return false;

  const GURL url(url_string);
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PageTransitionFromInt(
      ui::PAGE_TRANSITION_TYPED | ui::PAGE_TRANSITION_FROM_API);
  params.frame_name = std::string("");
  params.override_user_agent = content::NavigationController::UA_OVERRIDE_TRUE;
  web_contents_->GetController().LoadURLWithParams(params);
  return true;
}

bool PageContents::Reload() {
  if (!web_contents_)
    return false;

  web_contents_->GetController().Reload(content::ReloadType::BYPASSING_CACHE,
                                        true);
  return true;
}

void PageContents::ReloadNoWarranty() {
  std::ignore = Reload();
}

void PageContents::ResumeDOM() {
  if (!web_contents_)
    return;

  if (auto* frame_host = web_contents_->GetPrimaryMainFrame()) {
    mojo::AssociatedRemote<mojom::AppRuntimeWebViewClient> client;
    frame_host->GetRemoteAssociatedInterfaces()->GetInterface(&client);
    if (client)
      client->ResumeDOM();
  }
}

void PageContents::ResumeMedia() {
  if (!web_contents_)
    return;

#if defined(USE_NEVA_MEDIA)
  content::MediaStateManager::GetInstance()->ResumeAllMedia(
      web_contents_.get());
#else
  NOTIMPLEMENTED();
#endif
}

void PageContents::ScrollByY(int y_shift) {
  if (!web_contents_)
    return;

  content::RenderWidgetHostViewBase* renderer_host_view =
      static_cast<content::RenderWidgetHostViewBase*>(
          web_contents_->GetRenderWidgetHostView());
  if (renderer_host_view) {
    blink::WebMouseWheelEvent scroll_event(
        blink::WebInputEvent::Type::kMouseWheel,
        blink::WebInputEvent::kNoModifiers,
        base::TimeTicks::UnixEpoch());

    scroll_event.delta_units = ui::ScrollGranularity::kScrollByPrecisePixel;
    scroll_event.delta_x = 0.0f;
    scroll_event.delta_y = (-y_shift);
    scroll_event.phase = blink::WebMouseWheelEvent::kPhaseBegan;
    renderer_host_view->ProcessMouseWheelEvent(scroll_event, ui::LatencyInfo());
  }
}

void PageContents::SetAcceptedLanguages(std::string languages) {
  if (!web_contents_)
    return;

  auto* renderer_prefs(web_contents_->GetMutableRendererPrefs());
  if (renderer_prefs &&
      renderer_prefs->accept_languages.compare(languages) != 0) {
    renderer_prefs->accept_languages = std::move(languages);
    web_contents_->SyncRendererPrefs();
    delegate_->OnAcceptedLanguagesChanged(renderer_prefs->accept_languages);
  }
}

void PageContents::SetErrorPageHiding(bool enable) {
  error_page_hiding_ = enable;
}

void PageContents::SetFocus() {
  if (!web_contents_)
    return;

  auto* renderer_host_view = web_contents_->GetRenderWidgetHostView();
  if (renderer_host_view)
    renderer_host_view->Focus();
}

void PageContents::SetKeyCodesFilter(
    const std::vector<std::string>& key_codes) {
  for (auto& code : key_codes) {
    ui::DomKey dom_key = ui::KeycodeConverter::KeyStringToDomKey(code);
    if (dom_key.IsValid()) {
      key_codes_filter_.insert(dom_key);
    } else {
      int native_key_code;
      if (base::StringToInt(base::StringPiece(code), &native_key_code)) {
        key_codes_filter_.insert(native_key_code);
      } else {
        LOG(INFO) << "Invalid value for keycode filter: " << code;
      }
    }
  }
}

void PageContents::SetPageBaseBackgroundColor(std::string color) {
  if (!web_contents_)
    return;

  SkColor parsed_color;
  if (!content::ParseHexColorString(color, &parsed_color))
    parsed_color = SK_ColorWHITE;
  web_contents_->SetPageBaseBackgroundColor(parsed_color);
}

void PageContents::SetZoomFactor(double factor) {
  if (!web_contents_)
    return;
  zoom_factor_ = factor;
  content::HostZoomMap::SetZoomLevel(
      web_contents_.get(), blink::PageZoomFactorToZoomLevel(zoom_factor_));
}

void PageContents::Stop() {
  if (web_contents_.get())
    web_contents_->Stop();
}

void PageContents::SetUserAgentOverride(const std::string& user_agent) {
  if (user_agent.empty()) {
    user_agent_ = neva_user_agent::GetDefaultUserAgent();
  } else {
    user_agent_ = user_agent;
    if (web_contents_.get()) {
      auto user_agent_blink =
          blink::UserAgentOverride::UserAgentOnly(user_agent_);
      web_contents_->SetUserAgentOverride(user_agent_blink, false);
    }
  }
}

void PageContents::SetSSLCertErrorPolicy(SSLCertErrorPolicy policy) {
  ssl_cert_error_policy_ = policy;
}

void PageContents::SuspendDOM() {
  if (!web_contents_)
    return;

  if (auto* frame_host = web_contents_->GetPrimaryMainFrame()) {
    mojo::AssociatedRemote<mojom::AppRuntimeWebViewClient> client;
    frame_host->GetRemoteAssociatedInterfaces()->GetInterface(&client);
    if (client)
      client->SuspendDOM();
  }
}

void PageContents::SuspendMedia() {
  if (!web_contents_)
    return;

#if defined(USE_NEVA_MEDIA)
  content::MediaStateManager::GetInstance()->SuspendAllMedia(
      web_contents_.get());
#else
  NOTIMPLEMENTED();
#endif
}

void PageContents::UpdatePreferredLanguage(std::string language) {
  if (!web_contents_)
    return;

  auto* renderer_prefs(web_contents_->GetMutableRendererPrefs());
  if (renderer_prefs) {
    // If the language was missing, add it to the beginning of accept_languages.
    // Otherwise move language to the beginning of the accept_languages string.
    std::vector<base::StringPiece> languages =
        base::SplitStringPiece(renderer_prefs->accept_languages, ",",
                               base::TRIM_WHITESPACE,
                               base::SPLIT_WANT_NONEMPTY);

    if (!languages.empty() && languages.front().compare(language) == 0)
      return;

    std::vector<std::string> updated_languages;
    updated_languages.push_back(std::move(language));
    for (const base::StringPiece& lang : languages) {
      if (lang.compare(updated_languages.front()) != 0)
        updated_languages.push_back(std::string(lang));
    }

    renderer_prefs->accept_languages = base::JoinString(updated_languages, ",");
    web_contents_->SyncRendererPrefs();
    delegate_->OnAcceptedLanguagesChanged(renderer_prefs->accept_languages);
  }
}

PageView* PageContents::GetParentPageView() const {
  return parent_page_view_;
}

SSLCertErrorPolicy PageContents::GetSSLCertErrorPolicy() const {
  return ssl_cert_error_policy_;
}

void PageContents::DidFailLoad(content::RenderFrameHost* render_frame_host,
                               const GURL& validated_url,
                               int error_code) {
  bool is_main_frame = !render_frame_host->GetParent();
  delegate_->DidFailLoad(validated_url.spec(), is_main_frame,
                         net::ErrorToShortString(error_code), error_code);
}

void PageContents::DidFinishLoad(content::RenderFrameHost* render_frame_host,
                                 const GURL& validated_url) {
  bool is_main_frame = !render_frame_host->GetParent();
  delegate_->DidFinishLoad(validated_url.spec(), is_main_frame);
}

void PageContents::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsErrorPage()) {
    if (error_page_hiding_ && parent_page_view_)
      parent_page_view_->SetVisible(false, VisibilityChangeReason::kErrorPage);

    delegate_->DidFailLoad(
        navigation_handle->GetURL().spec(),
        navigation_handle->IsInPrimaryMainFrame(),
        net::ErrorToShortString(navigation_handle->GetNetErrorCode()),
        navigation_handle->GetNetErrorCode());
  }

  if (navigation_handle->HasCommitted() &&
      navigation_handle->IsInPrimaryMainFrame() &&
      !navigation_handle->IsSameDocument() &&
      !navigation_handle->IsErrorPage()) {
    delegate_->DidFinishNavigation(navigation_handle->GetURL().spec());
  }

  if (navigation_handle->HasCommitted() &&
      navigation_handle->IsSameDocument() &&
      ui::PageTransitionIsMainFrame(navigation_handle->GetPageTransition())) {
    delegate_->DidPushHistoryNavigation(navigation_handle->GetURL().spec());
  }

  // Set zoom when we navigate to different site
  if (navigation_handle->HasCommitted() &&
      !navigation_handle->IsSameOrigin() &&
      !navigation_handle->IsSameDocument()) {
    SetZoomFactor(zoom_factor_);
  }

  // Check for authentication request
  if (!navigation_handle->GetAuthChallengeInfo()) {
    auth_challenge_.reset();
    return;
  }

  auth_challenge_ = navigation_handle->GetAuthChallengeInfo();
  network_anonymization_key_ =
      navigation_handle->GetIsolationInfo().network_anonymization_key();

  neva_app_runtime::AuthChallengeInfo sending_challenge_info;

  sending_challenge_info.is_proxy = auth_challenge_.value().is_proxy;
  sending_challenge_info.port = auth_challenge_.value().challenger.port();
  sending_challenge_info.url = auth_challenge_.value().challenger.GetURL().spec();
  sending_challenge_info.scheme = auth_challenge_.value().scheme;
  sending_challenge_info.host = auth_challenge_.value().challenger.host();
  sending_challenge_info.realm = auth_challenge_.value().realm;

  delegate_->OnAuthChallenge(sending_challenge_info);

  if (auth_challenge_.value().is_proxy) {
    navigation_handle->GetWebContents()->DidChangeVisibleSecurityState();
  }
}

void PageContents::DidStartLoading() {
  delegate_->DidStartLoading();
}

void PageContents::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle)
    delegate_->DidStartNavigation(navigation_handle->GetURL().spec());
}

void PageContents::DidStopLoading() {
  delegate_->DidStopLoading();
}

void PageContents::DidUpdateFaviconURL(
    content::RenderFrameHost*,
    const std::vector<blink::mojom::FaviconURLPtr>& candidates) {
  std::vector<FaviconInfo> sending_info;
  sending_info.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    FaviconInfo info;
    info.url = candidate->icon_url.spec();
    info.type = ContentIconTypeToString(candidate->icon_type);
    for (const auto& icon_size : candidate->icon_sizes) {
      FaviconSize favicon_size;
      favicon_size.width = icon_size.width();
      favicon_size.height = icon_size.height();
      info.sizes.push_back(std::move(favicon_size));
    }
    sending_info.push_back(std::move(info));
  }

  delegate_->DidUpdateFaviconUrl(sending_info);
}

void PageContents::DidGetUserInteraction(const blink::WebInputEvent& event) {
  blink::mojom::EventType event_type = event.GetType();
  switch (event_type) {
    case blink::mojom::EventType::kMouseDown:
    case blink::mojom::EventType::kTouchStart: {
      auto mouse_event = static_cast<const blink::WebMouseEvent&>(event);
      delegate_->OnMouseClickEvent(static_cast<int>(mouse_event.button));
      break;
    }
    case blink::mojom::EventType::kRawKeyDown: {
      auto keyboard_event = static_cast<const blink::WebKeyboardEvent&>(event);
      if (key_codes_filter_.find(keyboard_event.dom_key) !=
          key_codes_filter_.end()) {
        delegate_->OnKeyEvent(
            ui::KeycodeConverter::DomKeyToKeyString(keyboard_event.dom_key),
            keyboard_event.dom_key);
      }

      if (key_codes_filter_.find(keyboard_event.native_key_code) !=
          key_codes_filter_.end()) {
        delegate_->OnKeyEvent("native", keyboard_event.native_key_code);
      }

      break;
    }
    default:
      break;
  }
}

void PageContents::DOMContentLoaded(
    content::RenderFrameHost* render_frame_host) {
  if (!render_frame_host->GetParent())
    delegate_->DOMReady();
}

void PageContents::LoadProgressChanged(double progress) {
  const unsigned int percent =
      static_cast<unsigned int>(std::ceil(progress*100.));
  delegate_->LoadProgressChanged(percent);
}

void PageContents::NavigationEntryCommitted(
    const content::LoadCommittedDetails& load_details) {
  delegate_->NavigationEntryCommitted();
}

void PageContents::OnRendererUnresponsive(
    content::RenderProcessHost* render_process_host) {
  delegate_->OnRendererUnresponsive();
}

void PageContents::OnRendererResponsive(
    content::RenderProcessHost* render_process_host){
  delegate_->OnRendererResponsive();
}

void PageContents::OnWebContentsFocused(
    content::RenderWidgetHost* render_widget_host) {
  delegate_->OnFocusChanged(true);
}

void PageContents::OnWebContentsLostFocus(
    content::RenderWidgetHost* render_widget_host) {
  delegate_->OnFocusChanged(false);
}

void PageContents::AckPermission(bool ack, uint64_t id) {
  auto find_info = media_access_requests_.find(id);
  if (find_info == media_access_requests_.end()) {
    LOG(WARNING) << __func__ << " Not found request";
    return;
  }

  MediaAccessPermissionInfo info = std::move(find_info->second);
  media_access_requests_.erase(find_info);
  blink::mojom::MediaStreamRequestResult result;
  if (ack) {
    result =
        IsStreamDevicesEmpty(*(info.stream_devices_set_->stream_devices[0]))
            ? blink::mojom::MediaStreamRequestResult::NO_HARDWARE
            : blink::mojom::MediaStreamRequestResult::OK;
  } else {
    result = blink::mojom::MediaStreamRequestResult::PERMISSION_DENIED;
  }

  std::move(info.callback).Run(*info.stream_devices_set_, result, nullptr);
}

void PageContents::AckAuthChallenge(const std::string& login,
                                    const std::string& passwd,
                                    const std::string& url) {
  if (!web_contents_)
    return;

  if (!auth_challenge_ ||
      (auth_challenge_.value().challenger.GetURL().spec() != url))
    return;

  net::AuthCredentials credentials(base::UTF8ToUTF16(login),
                                   base::UTF8ToUTF16(passwd));

  content::StoragePartition* storage_partition =
      web_contents_->GetBrowserContext()->GetStoragePartition(
          web_contents_->GetSiteInstance());

  storage_partition->GetNetworkContext()->AddAuthCacheEntry(
      auth_challenge_.value(), network_anonymization_key_, credentials,
      base::BindOnce(&PageContents::ReloadNoWarranty,
                     weak_ptr_factory_.GetWeakPtr()));
  auth_challenge_.reset();

  if (credentials.Empty())
    Reload();
}

void PageContents::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  if (callback.is_null())
    return;

  MediaCaptureDevicesDispatcher::GetInstance()->ProcessMediaAccessRequest(
      web_contents, request, std::move(callback));
}

bool PageContents::CheckMediaAccessPermission(
    content::RenderFrameHost* render_frame_host,
    const GURL& security_origin,
    blink::mojom::MediaStreamType type) {
  return MediaCaptureDevicesDispatcher::GetInstance()
      ->CheckMediaAccessPermission(render_frame_host, security_origin, type);
}

void PageContents::OverrideWebkitPrefs(
    blink::web_pref::WebPreferences* web_prefs) {
  web_prefs->allow_file_access_from_file_urls =
      create_params_.allow_file_access_from_file_urls;
  web_prefs->allow_universal_access_from_file_urls =
      create_params_.allow_universal_access_from_file_urls;
}

void PageContents::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  delegate_->OnExit(TerminationStatusToString(status));
}

void PageContents::RenderFrameCreated(
    content::RenderFrameHost* render_frame_host) {
  injection_manager_->RequestReloadInjections(render_frame_host);
}

void PageContents::RenderFrameHostChanged(content::RenderFrameHost* old_host,
                                          content::RenderFrameHost* new_host) {
  // If our associated HostZoomMap changes, update our subscription.
  content::HostZoomMap* new_host_zoom_map =
      content::HostZoomMap::GetForWebContents(web_contents_.get());
  if (new_host_zoom_map == host_zoom_map_)
    return;

  host_zoom_map_ = new_host_zoom_map;
  zoom_changed_subscription_ =
      host_zoom_map_->AddZoomLevelChangedCallback(base::BindRepeating(
          &PageContents::OnZoomLevelChanged, base::Unretained(this)));
}

void PageContents::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  NewWindowInfo window_info;
  window_info.target_url = target_url.spec();
  window_info.initial_width = window_features.bounds.width();
  window_info.initial_height = window_features.bounds.height();
  if (new_contents->GetPrimaryMainFrame())
    window_info.name = new_contents->GetPrimaryMainFrame()->GetFrameName();

  window_info.window_open_disposition =
      WindowOpenDispositionToString(disposition);
  window_info.user_gesture = user_gesture;

  CreateParams params;
  params.width = window_features.bounds.width();
  params.height = window_features.bounds.height();
  params.user_agent = user_agent_;
  params.error_page_hiding = error_page_hiding_;
  params.default_access_to_media = default_access_to_media_;
  params.zoom_factor = GetZoomFactor();
  if (was_blocked)
    *was_blocked = false;

  if (source) {
    auto* opener_rfh = source->GetPrimaryMainFrame();
    const GURL& opener_url =
        opener_rfh ? opener_rfh->GetLastCommittedURL() : GURL();

    if (browser::PopupBlockerServiceImpl::GetInstance()->IsBlocked(
            opener_url, user_gesture, disposition)) {
      LOG(INFO) << __func__
                << " Pop up window is blocked for this site: " << opener_url;
      window_info.popup_blocked = true;
    } else {
      window_info.popup_blocked = false;
    }
  }
  delegate_->OnNewWindowOpen(
      std::unique_ptr<PageContents>(
          new PageContents(std::move(new_contents), params)),
      window_info);
}

void PageContents::CloseContents(content::WebContents* source) {
  delegate_->OnClose();
}

// static
std::unique_ptr<content::WebContents> PageContents::CreateWebContents(
    const PageContents::CreateParams& params) {
  AppRuntimeBrowserContext* browser_context;
  browser_context =
      AppRuntimeBrowserContext::From(params.storage_partition_name,
                                     params.storage_partition_off_the_record);
  content::WebContents::CreateParams contents_params(browser_context, nullptr);
  return content::WebContents::Create(contents_params);
}

// static
std::unique_ptr<content::WebContents> PageContents::ReCreateWebContents(
    content::BrowserContext* browser_context,
    const content::SessionStorageNamespaceMap& session_storage_namespace_map) {
  NEVA_DCHECK(browser_context != nullptr);
  content::WebContents::CreateParams contents_params(browser_context, nullptr);
  return content::WebContents::CreateWithSessionStorage(
      contents_params, session_storage_namespace_map);
}

PageContents::PageContents(std::unique_ptr<content::WebContents> new_contents,
                           const CreateParams& params)
    : create_params_(params),
      id_(ShellEnvironment::GetInstance()->GetNextIDFor(this)),
      delegate_(params.delegate ? params.delegate : &stub_delegate_),
      web_contents_(std::move(new_contents)),
      default_access_to_media_(params.default_access_to_media),
      injection_manager_(
          std::make_unique<WebAppInjectionManager>(params.injections)),
      key_codes_filter_(std::set<uint32_t>()),
      error_page_hiding_(params.error_page_hiding) {
  Initialize();
  AppRuntimeWebViewControllerImpl::CreateForWebContents(web_contents_.get());
  auto* webview_controller_impl =
      AppRuntimeWebViewControllerImpl::FromWebContents(web_contents_.get());
  if (webview_controller_impl) {
    webview_controller_delegate_ =
        std::make_unique<BrowserShellWebViewControllerDelegate>();
    webview_controller_impl->SetDelegate(webview_controller_delegate_.get());
  }
  SetUserAgentOverride(params.user_agent);
}

void PageContents::Initialize() {
  if (!web_contents_)
    return;

  web_contents_->SetDelegate(this);
  Observe(web_contents_.get());
  ResetRendererPreferences();

  host_zoom_map_ = content::HostZoomMap::GetForWebContents(web_contents_.get());
  zoom_changed_subscription_ =
      host_zoom_map_->AddZoomLevelChangedCallback(base::BindRepeating(
          &PageContents::OnZoomLevelChanged, base::Unretained(this)));
  SetZoomFactor(create_params_.zoom_factor.value_or(1));
#if defined(USE_NEVA_BROWSER_SERVICE)
  permissions::PermissionRequestManager::CreateForWebContents(
      web_contents_.get());
#endif

  web_contents_->SetPinchToZoomEnabled(create_params_.pinch_to_zoom_enabled);
}

void PageContents::SetParentPageView(PageView* page_view) {
  NEVA_DCHECK(!page_view || !parent_page_view_);
  parent_page_view_ = page_view;
  if (parent_page_view_)
    Activate();
  else
    Deactivate();
}

void PageContents::ResetRendererPreferences() {
  if (!web_contents_)
    return;

  auto* renderer_prefs(web_contents_->GetMutableRendererPrefs());
  if (!renderer_prefs)
    return;

  if (!create_params_.app_id.empty() &&
      renderer_prefs->application_id.compare(create_params_.app_id) != 0) {
    renderer_prefs->application_id = create_params_.app_id;
  }

  renderer_prefs->is_enact_browser =
    (create_params_.type == PageContents::Type::kTab);

  if (!create_params_.accepted_languages.empty() &&
      renderer_prefs->accept_languages.compare(
          create_params_.accepted_languages) != 0) {
    renderer_prefs->accept_languages = create_params_.accepted_languages;
  }

  web_contents_->SyncRendererPrefs();
}

void PageContents::OnCaptureVisibleRegion(std::string base64_data) {
  visible_region_capture_.reset();
  delegate_->OnVisibleRegionCaptured(base64_data);
}

void PageContents::OnZoomLevelChanged(
    const content::HostZoomMap::ZoomLevelChange& change) {
  if (!web_contents_)
    return;

  if (net::GetHostOrSpecFromURL(web_contents_->GetLastCommittedURL()) ==
      change.host) {
    delegate_->OnZoomFactorChanged(
        blink::PageZoomLevelToZoomFactor(change.zoom_level));
  }
}

content::WebContents* PageContents::OpenURLFromTab(
    content::WebContents*,
    const content::OpenURLParams& params) {
  if (ui::PageTransitionCoreTypeIs(
        params.transition, ui::PAGE_TRANSITION_LINK)) {
    OpenURLInfo open_info;
    open_info.target_url = params.url.spec();
    open_info.window_open_disposition =
      WindowOpenDispositionToString(params.disposition);
    open_info.user_gesture = params.user_gesture;
    delegate_->OnOpenURLFromTab(open_info);
  }

  return nullptr;
}

content::JavaScriptDialogManager* PageContents::GetJavaScriptDialogManager(
    content::WebContents* source) {
  if (!js_dialog_manager_.get())
    js_dialog_manager_ = std::make_unique<JSDialogManager>(this);
  return js_dialog_manager_.get();
}

void PageContents::EnterFullscreenModeForTab(
    content::RenderFrameHost* requesting_frame,
    const blink::mojom::FullscreenOptions& options) {
  page_requested_fullscreen_ = true;
  delegate_->EnterHtmlFullscreen();
}

void PageContents::FullscreenStateChangedForTab(
    content::RenderFrameHost* requesting_frame,
    const blink::mojom::FullscreenOptions& options) {}

void PageContents::ExitFullscreenModeForTab(content::WebContents*) {
  if (page_requested_fullscreen_) {
    delegate_->LeaveHtmlFullscreen();
    page_requested_fullscreen_ = false;
  }
}

void PageContents::ExitFullscreen() {
  if (!web_contents_)
    return;

  if (page_requested_fullscreen_) {
    web_contents_->ExitFullscreen(false);
    page_requested_fullscreen_ = false;
  }
}

bool PageContents::IsFullscreenForTabOrPending(
    const content::WebContents* web_contents) {
  return page_requested_fullscreen_;
}

void PageContents::NavigationStateChanged(
    content::WebContents* source,
    content::InvalidateTypes changed_flags) {
  if (content::INVALIDATE_TYPE_TITLE & changed_flags) {
    const std::string title = base::UTF16ToUTF8(source->GetTitle());
    const auto& url = source->GetURL().spec();
    delegate_->TitleUpdated(title, url);
  }
}

bool PageContents::RunJSDialog(const std::string& type,
                               const std::string& message,
                               const std::string& default_prompt_text) {
  return delegate_->RunJSDialog(type, message, default_prompt_text);
}

}  // namespace neva_app_runtime
