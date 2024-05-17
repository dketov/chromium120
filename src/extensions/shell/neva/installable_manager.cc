// Copyright 2023 LG Electronics, Inc.
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

#include "extensions/shell/neva/installable_manager.h"

#include "base/functional/bind.h"
#include "base/functional/callback.h"

namespace neva_app_runtime {

InstallableManager::InstallableManager(content::WebContents* web_contents)
    : content::WebContentsUserData<InstallableManager>(*web_contents),
      web_contents_(web_contents),
      weak_factory_(this) {}

InstallableManager::~InstallableManager() {}

// static
void InstallableManager::BindInstallableManager(
    mojo::PendingReceiver<mojom::InstallableManager> receiver,
    content::RenderFrameHost* rfh) {
  auto* web_contents = content::WebContents::FromRenderFrameHost(rfh);
  if (!web_contents)
    return;

  auto* installable_manager = InstallableManager::FromWebContents(web_contents);
  if (!installable_manager)
    return;

  installable_manager->receivers_.Add(installable_manager, std::move(receiver));
}

void InstallableManager::GetInfo(GetInfoCallback callback) {
  installable_manager_.CheckInstallability(
      web_contents_,
      base::BindOnce(&InstallableManager::OnGetInfo, weak_factory_.GetWeakPtr(),
                     std::move(callback)));
}

void InstallableManager::OnGetInfo(GetInfoCallback callback,
                                   bool installable,
                                   bool installed) {
  std::move(callback).Run(installable, installed);
}

void InstallableManager::InstallApp(InstallAppCallback callback) {
  installable_manager_.InstallWebApp(
      web_contents_,
      base::BindOnce(&InstallableManager::OnInstallApp,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void InstallableManager::OnInstallApp(InstallAppCallback callback,
                                      bool success) {
  std::move(callback).Run(success);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(InstallableManager);

}  // namespace neva_app_runtime
