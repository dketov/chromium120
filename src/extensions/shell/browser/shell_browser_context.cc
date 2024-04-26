// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/shell/browser/shell_browser_context.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "components/guest_view/browser/guest_view_manager.h"
#include "extensions/shell/browser/shell_special_storage_policy.h"

#if defined(USE_NEVA_APPRUNTIME)
#include "neva/user_agent/browser/client_hints.h"
#endif  // defined(USE_NEVA_APPRUNTIME)

#if defined(USE_NEVA_BROWSER_SERVICE)
#include "neva/app_runtime/browser/permissions/permission_manager_factory.h"
#endif

namespace extensions {

// Create a normal recording browser context. If we used an incognito context
// then app_shell would also have to create a normal context and manage both.
ShellBrowserContext::ShellBrowserContext()
    : content::ShellBrowserContext(false /* off_the_record */,
                                   true /* delay_services_creation */),
      storage_policy_(base::MakeRefCounted<ShellSpecialStoragePolicy>()) {}

ShellBrowserContext::~ShellBrowserContext() {
  NotifyWillBeDestroyed();
}

content::BrowserPluginGuestManager* ShellBrowserContext::GetGuestManager() {
  return guest_view::GuestViewManager::FromBrowserContext(this);
}

storage::SpecialStoragePolicy* ShellBrowserContext::GetSpecialStoragePolicy() {
  return storage_policy_.get();
}

#if defined(USE_NEVA_APPRUNTIME)
content::ClientHintsControllerDelegate*
ShellBrowserContext::GetClientHintsControllerDelegate() {
  return new neva_user_agent::ClientHints();
}
#endif  // defined(USE_NEVA_APPRUNTIME)

#if defined(USE_NEVA_BROWSER_SERVICE)
content::PermissionControllerDelegate*
ShellBrowserContext::GetPermissionControllerDelegate() {
  return PermissionManagerFactory::GetForBrowserContext(this);
}
#endif

}  // namespace extensions
