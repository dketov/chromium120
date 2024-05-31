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

#include "neva/browser_service/browser/sitefilter_navigation_throttle.h"

#include "content/public/browser/navigation_handle.h"
#include "neva/browser_service/browser/sitefilter_service_impl.h"

namespace neva {

SiteFilterNavigationThrottle::SiteFilterNavigationThrottle(
    content::NavigationHandle* navigation_handle)
    : content::NavigationThrottle(navigation_handle) {}

SiteFilterNavigationThrottle::~SiteFilterNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
SiteFilterNavigationThrottle::WillStartRequest() {
  return WillStartOrRedirectRequest();
}

content::NavigationThrottle::ThrottleCheckResult
SiteFilterNavigationThrottle::WillRedirectRequest() {
  return WillStartOrRedirectRequest();
}

content::NavigationThrottle::ThrottleCheckResult
SiteFilterNavigationThrottle::WillStartOrRedirectRequest() {

  // Calling SiteFilterService::IsBlocked() to check if the current URL
  // should be blocked or not as per the filter type and URL list set by
  // user(blocked/allowed/off).
  // Returns true if the URL has to be blocked.
  if (browser::SiteFilterServiceImpl::Get()->IsBlocked(
          navigation_handle()->GetURL(),
          navigation_handle()->WasServerRedirect())) {
    return content::NavigationThrottle::BLOCK_BY_SITEFILTER;
  }

  return content::NavigationThrottle::PROCEED;

}

const char* SiteFilterNavigationThrottle::GetNameForLogging() {
  return "SiteFilterNavigationThrottle";
}

}  // namespace neva

