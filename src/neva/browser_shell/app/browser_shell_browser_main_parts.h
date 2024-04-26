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

#ifndef NEVA_BROWSER_SHELL_APP_BROWRSER_SHELL_BROWSER_MAIN_PART_H_
#define NEVA_BROWSER_SHELL_APP_BROWRSER_SHELL_BROWSER_MAIN_PART_H_

#include "neva/app_runtime/browser/app_runtime_browser_main_parts.h"
#include "neva/app_runtime/browser/media/media_capture_observer.h"

class BrowserShellPermissionsClientDelegate;

#if defined(USE_NEVA_BROWSER_SERVICE)
#include "neva/browser_service/browser/malware_detection_service.h"
#endif

namespace neva {
class MalwareDetectionService;
}

namespace browser_shell {

class BrowserShellBrowserMainParts
    : public neva_app_runtime::AppRuntimeBrowserMainParts {
 public:
  BrowserShellBrowserMainParts();
  ~BrowserShellBrowserMainParts() override;

  int PreCreateThreads() override;
  int PreMainMessageLoopRun() override;

#if defined(USE_NEVA_BROWSER_SERVICE)
  // Store instance of malware detection service
  neva::MalwareDetectionService* malware_detection_service() {
    return malware_detection_service_.get();
  }
#endif

 private:
  std::unique_ptr<BrowserShellPermissionsClientDelegate>
      browser_shell_permission_client_delegate_;
  std::unique_ptr<MediaCaptureObserver> media_capture_observer_;

#if defined(USE_NEVA_BROWSER_SERVICE)
  scoped_refptr<neva::MalwareDetectionService> malware_detection_service_;
#endif
};

}  // namespace browser_shell

#endif  // NEVA_BROWSER_SHELL_APP_BROWRSER_SHELL_BROWSER_MAIN_PART_H_
