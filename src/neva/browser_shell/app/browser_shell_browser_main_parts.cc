// Copyright 2024 LG Electronics, Inc.
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

#include "neva/browser_shell/app/browser_shell_browser_main_parts.h"

#include "content/public/common/result_codes.h"
#include "neva/app_runtime/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "neva/app_runtime/browser/media/webrtc/media_stream_capture_indicator.h"
#include "neva/app_runtime/browser/permissions/permission_prompt_delegate.h"

namespace browser_shell {

BrowserShellBrowserMainParts::BrowserShellBrowserMainParts()
    : AppRuntimeBrowserMainParts() {}

BrowserShellBrowserMainParts::~BrowserShellBrowserMainParts() {
  neva_app_runtime::MediaCaptureDevicesDispatcher::GetInstance()
      ->GetMediaStreamCaptureIndicator()
      ->RemoveObserver(media_capture_observer_.get());
}

int BrowserShellBrowserMainParts::PreCreateThreads() {
  // Make sure permissions client has been set.
  if (!browser_shell_permission_client_delegate_) {
    browser_shell_permission_client_delegate_.reset(
        new BrowserShellPermissionsClientDelegate());
  }

  neva_app_runtime::NevaPermissionsClient::GetInstance()->SetDelegate(
      browser_shell_permission_client_delegate_.get());

  if (!media_capture_observer_)
    media_capture_observer_.reset(new MediaCaptureObserver());
  neva_app_runtime::MediaCaptureDevicesDispatcher::GetInstance()
      ->GetMediaStreamCaptureIndicator()
      ->AddObserver(media_capture_observer_.get());

  return content::RESULT_CODE_NORMAL_EXIT;
}

int BrowserShellBrowserMainParts::PreMainMessageLoopRun() {
  int response = AppRuntimeBrowserMainParts::PreMainMessageLoopRun();
#if defined(USE_NEVA_BROWSER_SERVICE)
  if (!malware_detection_service_.get()) {
    malware_detection_service_ = neva::MalwareDetectionService::Create();
  }

  if (!malware_detection_service_->Initialize(GetDefaultBrowserContext())) {
    malware_detection_service_.reset();
  }
#endif
  return response;
}

}  // namespace browser_shell
