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

#ifndef NEVA_BROWSER_SERVICE_BROWSER_SITEFILTER_NAVIGATION_THROTTLE_H_
#define NEVA_BROWSER_SERVICE_BROWSER_SITEFILTER_NAVIGATION_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationHandle;
}

namespace neva {

class SiteFilterNavigationThrottle : public content::NavigationThrottle {
 public:
  explicit SiteFilterNavigationThrottle(
      content::NavigationHandle* navigation_handle);

  SiteFilterNavigationThrottle(const SiteFilterNavigationThrottle&) = delete;
  SiteFilterNavigationThrottle& operator=(const SiteFilterNavigationThrottle&) =
      delete;

  ~SiteFilterNavigationThrottle() override;

  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;

 private:

  // Shared throttle handler.
  ThrottleCheckResult WillStartOrRedirectRequest();
};

}  // namespace neva

#endif  // NEVA_BROWSER_SERVICE_BROWSER_SITEFILTER_NAVIGATION_THROTTLE_H_