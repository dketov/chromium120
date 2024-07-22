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

#ifndef NEVA_BROWSER_SHELL_APP_BROWRSER_SHELL_CONTENT_BROWSER_CLIENT_H_
#define NEVA_BROWSER_SHELL_APP_BROWRSER_SHELL_CONTENT_BROWSER_CLIENT_H_

#include "neva/app_runtime/browser/app_runtime_content_browser_client.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace browser_shell {

class BrowserShellBrowserMainParts;

class BrowserShellContentBrowserClient
    : public neva_app_runtime::AppRuntimeContentBrowserClient {
 public:
  BrowserShellContentBrowserClient() = default;
  ~BrowserShellContentBrowserClient() override = default;

  // content::ContentBrowserClient implementations
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;

  void ExposeInterfacesToRenderer(
      service_manager::BinderRegistry* registry,
      blink::AssociatedInterfaceRegistry* associated_registry,
      content::RenderProcessHost* render_process_host) override;

#if defined(USE_NEVA_BROWSER_SERVICE)
  std::vector<std::unique_ptr<blink::URLLoaderThrottle>>
  CreateURLLoaderThrottles(
      const network::ResourceRequest& request,
      content::BrowserContext* browser_context,
      const base::RepeatingCallback<content::WebContents*()>& wc_getter,
      content::NavigationUIData* navigation_ui_data,
      int frame_tree_node_id) override;

  void OverrideWebkitPrefs(content::WebContents* web_contents,
                           blink::web_pref::WebPreferences* prefs) override;

  scoped_refptr<network::SharedURLLoaderFactory>
       GetSystemSharedURLLoaderFactory() override;
#endif

  void SiteInstanceGotProcess(content::SiteInstance* site_instance) override;

  bool IsNevaDynamicProxyEnabled() override;

};

}  // namespace browser_shell

#endif  // NEVA_BROWSER_SHELL_APP_BROWRSER_SHELL_CONTENT_BROWSER_CLIENT_H_
