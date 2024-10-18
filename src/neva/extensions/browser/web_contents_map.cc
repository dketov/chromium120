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

#include "neva/extensions/browser/web_contents_map.h"

#include "content/public/browser/render_frame_host.h"
#include "neva/extensions/browser/neva_extensions_service_factory.h"
#include "neva/extensions/browser/neva_extensions_service_impl.h"
#include "neva/extensions/browser/tab_helper.h"
#include "neva/logging.h"

namespace neva {

WebContentsItem::WebContentsItem(WebContentsMap* web_contents_map,
                                 content::WebContents* web_contents)
    : ExtensionWebContentsObserver(web_contents),
      web_contents_map_(web_contents_map) {
  Observe(web_contents);
}

void WebContentsItem::RenderFrameCreated(
    content::RenderFrameHost* render_frame_host) {
  ExtensionWebContentsObserver::RenderFrameCreated(render_frame_host);
  web_contents_map_->SetTabIdForRenderFrame(render_frame_host);
}

void WebContentsItem::WebContentsDestroyed() {
  WebContentsMap::GetInstance()->OnWebContentsWillDestroyed(this);
  delete this;
}

WebContentsMap::WebContentsMap() = default;

WebContentsMap::~WebContentsMap() = default;

WebContentsMap* WebContentsMap::GetInstance() {
  return base::Singleton<WebContentsMap>::get();
}

void WebContentsMap::OnWebContentsCreated(content::WebContents* web_contents) {
  WebContentsItem* item = new WebContentsItem(this, web_contents);
  item->Initialize();
  items_[item] = web_contents;

  web_contents->ForEachRenderFrameHost(
      [this](content::RenderFrameHost* host) { SetTabIdForRenderFrame(host); });

  neva::NevaExtensionsServiceImpl* neva_extensions_service_impl =
      neva::NevaExtensionsServiceFactory::GetService(
          web_contents->GetBrowserContext());
  if (neva_extensions_service_impl) {
    neva_extensions_service_impl->UpdatePreferredLanguage(web_contents);
  }
}

void WebContentsMap::OnWebContentsWillDestroyed(WebContentsItem* item) {
  script_executors_.erase(items_[item]);
  items_.erase(item);
}

extensions::ExtensionWebContentsObserver* WebContentsMap::GetObserver(
    content::WebContents* web_contents) {
  for (auto& item : items_) {
    if (item.second == web_contents) {
      return item.first;
    }
  }

  return nullptr;
}

void WebContentsMap::SetTabIdForRenderFrame(content::RenderFrameHost* host) {
  // When this is called during WebContents creation, the renderer-side Frame
  // object would not have been created yet. We should wait for
  // RenderFrameCreated() to happen, to avoid sending this message twice.
  if (host->IsRenderFrameLive()) {
    content::WebContents* web_contents =
        content::WebContents::FromRenderFrameHost(host);
    content::BrowserContext* browser_context =
        web_contents->GetBrowserContext();

    NevaExtensionsServiceImpl* neva_extensions_service_impl =
        NevaExtensionsServiceFactory::GetService(browser_context);
    if (!neva_extensions_service_impl) {
      return;
    }
    TabHelper* tab_helper = neva_extensions_service_impl->GetTabHelper();

    // TODO(pikulik): This check is for when we try to use an extension in
    // a BrowserContext for which no TabHelper was created.
    if (tab_helper) {
      uint64_t id = tab_helper->GetIdFromWebContents(web_contents);

      if (id > 0) {
        extensions::ExtensionWebContentsObserver::GetForWebContents(
            web_contents)
            ->GetLocalFrame(host)
            ->SetTabId(id);
      }
    }
  }
}

void WebContentsMap::ForEachExtensionWebContents(
    const base::RepeatingCallback<void(content::WebContents*)>& cb) {
  for (auto& item : items_) {
    if (!item.second) {
      continue;
    }

    NevaExtensionsServiceImpl* neva_extensions_service_impl =
        neva::NevaExtensionsServiceFactory::GetService(
            item.second->GetBrowserContext());
    if (neva_extensions_service_impl) {
      uint64_t id =
          neva_extensions_service_impl->GetTabHelper()->GetIdFromWebContents(
              item.second);
      // If the id is zero then it means it is not managed by the
      // neva_app_runtime::ShellEnvironment so it might be the
      // WebContents created by the extension
      if (id == 0) {
        cb.Run(item.second);
      }
    }
  }
}

extensions::ScriptExecutor* WebContentsMap::GetScriptExecutor(
    content::WebContents* web_contents) {
  auto it = script_executors_.find(web_contents);
  if (it != script_executors_.end()) {
    return it->second.get();
  }

  script_executors_.emplace(
      web_contents, std::make_unique<extensions::ScriptExecutor>(web_contents));
  return script_executors_[web_contents].get();
}

}  // namespace neva
