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

#include "neva/app_runtime/browser/permissions/permission_prompt_delegate.h"

#include <iterator>
#include <memory>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "components/permissions/permission_uma_util.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

BrowserShellPermissionPromptDelegate::BrowserShellPermissionPromptDelegate(
    permissions::PermissionPrompt::Delegate* delegate)
    : PermissionPromptWebOS::PlatformDelegate(delegate) {}

void BrowserShellPermissionPromptDelegate::ShowBubble(const GURL& origin_url,
                                                      RequestTypes types) {
  browser::UserPermissionServiceImpl::Get()->ShowPrompt(
      origin_url, types,
      base::BindPostTask(
          base::SequencedTaskRunner::GetCurrentDefault(),
          base::BindRepeating(
              &BrowserShellPermissionPromptDelegate::OnPromptResponse,
              base::Unretained(this))));
}

void BrowserShellPermissionPromptDelegate::OnPromptResponse(
    browser::UserPermissionServiceImpl::Response type) {
  VLOG(1) << __func__ << " type=" << type;

  switch (type) {
    case browser::UserPermissionServiceImpl::kAcceptPermission:
      AcceptPermission();
      break;
    case browser::UserPermissionServiceImpl::kDenyPermission:
      DenyPermission();
      break;
    case browser::UserPermissionServiceImpl::kClosingPermission:
      ClosingPermission();
      break;
    default:
      break;
  }
}

void BrowserShellPermissionPromptDelegate::AcceptPermission() {
  if (delegate_) {
    delegate_->Accept();
  }
}

void BrowserShellPermissionPromptDelegate::DenyPermission() {
  if (delegate_) {
    delegate_->Deny();
  }
}

void BrowserShellPermissionPromptDelegate::ClosingPermission() {
  if (delegate_) {
    delegate_->Dismiss();
  }
}

std::unique_ptr<permissions::PermissionPrompt>
BrowserShellPermissionsClientDelegate::CreatePrompt(
    content::WebContents* web_contents,
    permissions::PermissionPrompt::Delegate* delegate) {
  CHECK(delegate);
  return std::make_unique<PermissionPromptWebOS>(
      web_contents, delegate,
      std::make_unique<BrowserShellPermissionPromptDelegate>(delegate));
}
