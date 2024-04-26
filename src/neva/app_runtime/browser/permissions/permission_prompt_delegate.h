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

#ifndef NEVA_APP_RUNTIME_BROWSER_PERMISSIONS_PERMISSION_PROMPT_DELEGATE_H_
#define NEVA_APP_RUNTIME_BROWSER_PERMISSIONS_PERMISSION_PROMPT_DELEGATE_H_

#include "neva/app_runtime/browser/permissions/neva_permissions_client.h"
#include "neva/app_runtime/browser/permissions/permission_prompt_webos.h"
#include "neva/browser_service/browser/userpermission_service_impl.h"

// This object will create or trigger system UI to reflect that a website is
// requesting a permission.
class BrowserShellPermissionPromptDelegate
    : public PermissionPromptWebOS::PlatformDelegate {
 public:
  BrowserShellPermissionPromptDelegate() = default;
  BrowserShellPermissionPromptDelegate(
      permissions::PermissionPrompt::Delegate* delegate);

  BrowserShellPermissionPromptDelegate(
      const BrowserShellPermissionPromptDelegate&) = delete;
  BrowserShellPermissionPromptDelegate& operator=(
      const BrowserShellPermissionPromptDelegate&) = delete;
  ~BrowserShellPermissionPromptDelegate() override = default;

  // PermissionPromptWebOS::Delegate
  void ShowBubble(const GURL& origin_url, RequestTypes types) override;

 private:
  void OnPromptResponse(browser::UserPermissionServiceImpl::Response type);

  void AcceptPermission();
  void DenyPermission();
  void ClosingPermission();
};

class BrowserShellPermissionsClientDelegate
    : public neva_app_runtime::NevaPermissionsClient::Delegate {
 public:
  BrowserShellPermissionsClientDelegate() = default;
  BrowserShellPermissionsClientDelegate(
      const BrowserShellPermissionsClientDelegate&) = delete;
  BrowserShellPermissionsClientDelegate& operator=(
      const BrowserShellPermissionsClientDelegate&) = delete;
  ~BrowserShellPermissionsClientDelegate() override = default;

  std::unique_ptr<permissions::PermissionPrompt> CreatePrompt(
      content::WebContents* web_contents,
      permissions::PermissionPrompt::Delegate* delegate) override;
};

#endif  // NEVA_APP_RUNTIME_BROWSER_PERMISSIONS_PERMISSION_PROMPT_DELEGATE_H_
