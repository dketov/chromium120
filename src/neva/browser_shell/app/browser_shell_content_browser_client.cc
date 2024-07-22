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

#include "neva/browser_shell/app/browser_shell_content_browser_client.h"

#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "neva/app_runtime/browser/app_runtime_browser_context.h"
#include "neva/browser_service/browser_service.h"
#include "neva/browser_service/browser/cookiemanager_service_impl.h"
#include "neva/browser_service/browser/popupblocker_service_impl.h"
#include "neva/browser_shell/app/browser_shell_browser_main_parts.h"

#if defined(USE_NEVA_BROWSER_SERVICE)
#include "neva/app_runtime/browser/app_runtime_web_contents_delegate.h"
#include "neva/browser_service/browser/malware_url_loader_throttle.h"
#include "neva/browser_service/browser_service.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#endif

namespace browser_shell {

std::unique_ptr<content::BrowserMainParts>
BrowserShellContentBrowserClient::CreateBrowserMainParts(
    bool /* is_integration_test */) {
  main_parts_ = new BrowserShellBrowserMainParts();

  if (browser_extra_parts_) {
    main_parts_->AddParts(browser_extra_parts_);
  }

  return base::WrapUnique(main_parts_);
}

void BrowserShellContentBrowserClient::ExposeInterfacesToRenderer(
    service_manager::BinderRegistry* registry,
    blink::AssociatedInterfaceRegistry* associated_registry,
    content::RenderProcessHost* render_process_host) {
  AppRuntimeContentBrowserClient::ExposeInterfacesToRenderer(
      registry, associated_registry, render_process_host);
  auto cookiemanager_service =
      [](mojo::PendingReceiver<browser::mojom::CookieManagerService> receiver) {
        browser::BrowserService::GetBrowserService()->BindCookieManagerService(
            std::move(receiver));
      };
  registry->AddInterface(base::BindRepeating(cookiemanager_service),
                         content::GetUIThreadTaskRunner({}));

  auto userpermission_service =
      [](mojo::PendingReceiver<browser::mojom::UserPermissionService>
             receiver) {
        browser::BrowserService::GetBrowserService()->BindUserPermissionService(
            std::move(receiver));
      };
  registry->AddInterface(base::BindRepeating(userpermission_service),
                         content::GetUIThreadTaskRunner({}));

  auto customuseragent_service =
      [](mojo::PendingReceiver<browser::mojom::CustomUserAgentService>
             receiver) {
        browser::BrowserService::GetBrowserService()
            ->BindCustomUserAgentService(std::move(receiver));
      };
  registry->AddInterface(base::BindRepeating(customuseragent_service),
                         content::GetUIThreadTaskRunner({}));

  auto mediacapture_service =
      [](mojo::PendingReceiver<browser::mojom::MediaCaptureService> receiver) {
        browser::BrowserService::GetBrowserService()->BindMediaCaptureService(
            std::move(receiver));
      };
  registry->AddInterface(base::BindRepeating(mediacapture_service),
                         content::GetUIThreadTaskRunner({}));

  auto popupblocker_service =
      [](mojo::PendingReceiver<browser::mojom::PopupBlockerService> receiver) {
        browser::BrowserService::GetBrowserService()->BindPopupBlockerService(
            std::move(receiver));
      };
  registry->AddInterface(base::BindRepeating(popupblocker_service),
                         content::GetUIThreadTaskRunner({}));

  auto sitefilter_service =
      [](mojo::PendingReceiver<browser::mojom::SiteFilterService> receiver) {
         browser::BrowserService::GetBrowserService()->BindSiteFilterService(
             std::move(receiver));
      };
  registry->AddInterface(base::BindRepeating(sitefilter_service),
                           content::GetUIThreadTaskRunner({}));
}

void BrowserShellContentBrowserClient::SiteInstanceGotProcess(
    content::SiteInstance* site_instance) {
  content::BrowserContext* browser_context = site_instance->GetBrowserContext();
  // We need to set the new cookie manager instance as it is changed
  // after a web page is opened because storage partition is changed
  network::mojom::CookieManager* cookie_manager =
      browser_context->GetStoragePartition(site_instance, false)
          ->GetCookieManagerForBrowserProcess();
  browser::CookieManagerServiceImpl::Get()->SetNetworkCookieManager(
      cookie_manager);
  AppRuntimeContentBrowserClient::SiteInstanceGotProcess(site_instance);
}

bool BrowserShellContentBrowserClient::IsNevaDynamicProxyEnabled() {
  return true;
}

#if defined(USE_NEVA_BROWSER_SERVICE)
std::vector<std::unique_ptr<blink::URLLoaderThrottle>>
BrowserShellContentBrowserClient::CreateURLLoaderThrottles(
    const network::ResourceRequest& request,
    content::BrowserContext* browser_context,
    const base::RepeatingCallback<content::WebContents*()>& wc_getter,
    content::NavigationUIData* navigation_ui_data,
    int frame_tree_node_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::vector<std::unique_ptr<blink::URLLoaderThrottle>> result;
  BrowserShellBrowserMainParts* main_parts =
      static_cast<BrowserShellBrowserMainParts*>(main_parts_);
  if (main_parts->malware_detection_service()) {
    result.push_back(std::make_unique<neva::MalwareURLLoaderThrottle>(
        main_parts->malware_detection_service()));
  }
  return result;
}

void BrowserShellContentBrowserClient::OverrideWebkitPrefs(
    content::WebContents* web_contents,
    blink::web_pref::WebPreferences* prefs) {
  prefs->cookie_enabled =
      browser::CookieManagerServiceImpl::Get()->IsCookieEnabled();
  content::WebContentsDelegate* delegate = web_contents->GetDelegate();
  if (delegate)
    delegate->OverrideWebkitPrefs(prefs);
}

scoped_refptr<network::SharedURLLoaderFactory>
BrowserShellContentBrowserClient::GetSystemSharedURLLoaderFactory() {
  return main_parts_->GetDefaultBrowserContext()
      ->GetDefaultStoragePartition()
      ->GetURLLoaderFactoryForBrowserProcess();
}

#endif

}  // namespace browser_shell
