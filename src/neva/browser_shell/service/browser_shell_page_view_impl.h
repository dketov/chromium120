// Copyright 2021 LG Electronics, Inc.
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

#ifndef NEVA_BROWSER_SHELL_SERVICE_BROWSER_SHELL_PAGE_VIEW_IMPL_H_
#define NEVA_BROWSER_SHELL_SERVICE_BROWSER_SHELL_PAGE_VIEW_IMPL_H_

#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "neva/app_runtime/app/app_runtime_page_view_delegate.h"
#include "neva/browser_shell/service/public/mojom/browser_shell_page_view.mojom.h"

namespace neva_app_runtime {
class PageView;
}

namespace browser_shell {

class ShellServiceImpl;

class PageViewImpl : public mojom::PageView,
                     public neva_app_runtime::PageViewDelegate {
 public:
  PageViewImpl(ShellServiceImpl* shell_service,
               neva_app_runtime::PageView* page_view);
  PageViewImpl(const PageViewImpl&) = delete;
  PageViewImpl& operator=(const PageViewImpl&) = delete;
  ~PageViewImpl() override;

  uint64_t GetID() const;
  neva_app_runtime::PageView* GetPageView() const;

  // mojom::PageView
  void BindPageContents(mojo::PendingReceiver<mojom::PageContents> receiver,
                        BindPageContentsCallback callback) override;
  void BindClient(BindClientCallback callback) override;

  void SyncId(SyncIdCallback callback) override;

  void SetBounds(int32_t x, int32_t y, int32_t w, int32_t h) override;
  void SetVisible(bool visible) override;
  void BringToFront() override;
  void SendToBack() override;
  void AddChildView(uint64_t id) override;
  void RemoveChildView(uint64_t id) override;
  void SetPageContents(uint64_t id) override;

  // neva_app_runtime::PageViewDelegate
  void OnDestroying(neva_app_runtime::PageView* view) override;
  void OnVisibilityChanged(
      bool visible,
      neva_app_runtime::VisibilityChangeReason reason) override;

 private:
  ShellServiceImpl* const shell_service_;
  neva_app_runtime::PageView* const page_view_;
  mojo::AssociatedRemote<mojom::PageViewClient> remote_client_;
};

}  // namespace browser_shell

#endif  // NEVA_BROWSER_SHELL_SERVICE_BROWSER_SHELL_PAGE_VIEW_IMPL_H_
