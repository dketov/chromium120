// Copyright 2016 LG Electronics, Inc.
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

#include "neva/app_runtime/browser/app_runtime_browser_context.h"

#include "base/base_paths_posix.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/escape.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/pref_filter.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_switches.h"
#include "net/base/http_user_agent_settings.h"
#include "net/cookies/cookie_store.h"
#include "net/http/http_network_session.h"
#include "net/http/http_request_headers.h"
#include "net/proxy_resolution/proxy_config_service.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/proxy_resolution/proxy_resolution_service.h"
#include "neva/app_runtime/browser/app_runtime_browser_switches.h"
#include "neva/app_runtime/browser/app_runtime_download_manager_delegate.h"
#include "neva/app_runtime/browser/browsing_data/browsing_data_remover.h"
#include "neva/app_runtime/browser/media/webrtc/device_media_stream_access_handler.h"
#include "neva/app_runtime/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "neva/app_runtime/browser/notifications/notifier_client.h"
#include "neva/app_runtime/browser/notifications/platform_notification_service_factory.h"
#include "neva/app_runtime/browser/notifications/platform_notification_service_impl.h"
#include "neva/app_runtime/browser/permissions/permission_manager_factory.h"
#include "neva/app_runtime/browser/push_messaging/push_messaging_app_identifier.h"
#include "neva/app_runtime/browser/push_messaging/push_messaging_service_factory.h"
#include "neva/app_runtime/browser/push_messaging/push_messaging_service_impl.h"
#include "neva/app_runtime/public/notifier_settings_controller.h"
#include "neva/browser_shell/service/browser_shell_storage_partition_name.h"
#include "neva/user_agent/browser/client_hints.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

#if defined(USE_NEVA_CHROME_EXTENSIONS)
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/language/core/browser/language_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/session_id_generator.h"
#include "extensions/browser/api/api_browser_context_keyed_service_factories.h"
#include "extensions/browser/api/audio/audio_api.h"
#include "extensions/browser/browser_context_keyed_service_factories.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/permissions_manager.h"
#include "neva/app_runtime/browser/extensions/tab_helper_impl.h"
#include "neva/extensions/browser/browser_context_keyed_service_factories.h"
#include "neva/extensions/browser/neva_extension_system.h"
#include "neva/extensions/browser/neva_extensions_browser_client.h"
#include "neva/extensions/browser/neva_extensions_service_factory.h"
#include "neva/extensions/browser/neva_extensions_service_impl.h"
#include "neva/extensions/browser/neva_extensions_util.h"
#include "neva/extensions/browser/neva_prefs.h"
#include "neva/extensions/common/neva_extensions_client.h"
#endif  // defined(USE_NEVA_CHROME_EXTENSIONS)

namespace neva_app_runtime {

bool IsAllowedToLoadExtensionsIn(const std::string& partition, bool off_the_record) {
  base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  if (cmd_line->HasSwitch(kAllowLoadExtensionsIn)) {
    std::vector<std::string> allowed_list =
        base::SplitString(cmd_line->GetSwitchValueASCII(kAllowLoadExtensionsIn),
                          ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

    std::string partition_name;
    bool off_the_record;
    for (auto& it : allowed_list) {
      browser_shell::ParseStoragePartitionName(it, partition_name, off_the_record);
      if ((partition == partition_name) &&
          (off_the_record == off_the_record)) {
        return true;
      }
    }

    return false;
  }

  return true;
}

// static
AppRuntimeBrowserContext* AppRuntimeBrowserContext::From(std::string partition,
                                                         bool off_the_record) {
  bool allow_to_load_extensions =
      IsAllowedToLoadExtensionsIn(partition, off_the_record);

  if (partition == "default" && !off_the_record)
    partition = "";

  BrowserContextMap& registry = off_the_record
                                    ? off_the_record_browser_context_map()
                                    : browser_context_map();
  AppRuntimeBrowserContext* browser_context = registry[partition].get();
  if (!browser_context) {
    browser_context = new AppRuntimeBrowserContext(partition, off_the_record,
                                                   allow_to_load_extensions);
    registry[partition] =
        std::unique_ptr<AppRuntimeBrowserContext>(browser_context);
  }
  return browser_context;
}

// static
void AppRuntimeBrowserContext::Clear() {
  auto default_context = std::move(browser_context_map()[""]);
  off_the_record_browser_context_map().clear();
  browser_context_map().clear();
}

AppRuntimeBrowserContext::~AppRuntimeBrowserContext() {
#if defined(USE_NEVA_CHROME_EXTENSIONS)
  user_pref_service_->CommitPendingWrite();
  user_pref_service_.reset();
  local_state_->CommitPendingWrite();
  local_state_.reset();
#endif

  // Need to set object of AppRuntimeDownloadManagerDelegate = nullptr to avoid
  // crashing when BrowserContextImpl destructed
  this->GetDownloadManager()->SetDelegate(nullptr);

  if (off_the_record_) {
    ForEachLoadedStoragePartition(
        base::BindRepeating([](content::StoragePartition* storage_partition) {
          BrowsingDataRemover* remover =
              BrowsingDataRemover::GetForStoragePartition(storage_partition);
          remover->Remove(
              BrowsingDataRemover::Unbounded(),
              BrowsingDataRemover::RemoveBrowsingDataMask::REMOVE_ALL, GURL(),
              base::DoNothing());
        }));
  }

  NotifyWillBeDestroyed();

#if defined(USE_NEVA_CHROME_EXTENSIONS)
  BrowserContextDependencyManager::GetInstance()->DestroyBrowserContextServices(
      this);
#endif

  ShutdownStoragePartitions();
}

base::FilePath AppRuntimeBrowserContext::GetPath() {
  return path_;
}

bool AppRuntimeBrowserContext::IsOffTheRecord() {
  return off_the_record_;
}

content::ResourceContext* AppRuntimeBrowserContext::GetResourceContext() {
  return resource_context_.get();
}

content::DownloadManagerDelegate*
AppRuntimeBrowserContext::GetDownloadManagerDelegate() {
  if (!download_manager_delegate_) {
    download_manager_delegate_ =
        std::make_unique<AppRuntimeDownloadManagerDelegate>();
  }
  return download_manager_delegate_.get();
}

content::BrowserPluginGuestManager*
AppRuntimeBrowserContext::GetGuestManager() {
  return nullptr;
}

storage::SpecialStoragePolicy*
AppRuntimeBrowserContext::GetSpecialStoragePolicy() {
  return nullptr;
}

content::PlatformNotificationService*
AppRuntimeBrowserContext::GetPlatformNotificationService() {
  return PlatformNotificationServiceFactory::GetForProfile(this);
}

content::PushMessagingService*
AppRuntimeBrowserContext::GetPushMessagingService() {
  return PushMessagingServiceFactory::GetForProfile(this);
}

content::StorageNotificationService*
AppRuntimeBrowserContext::GetStorageNotificationService() {
  return nullptr;
}

content::SSLHostStateDelegate*
AppRuntimeBrowserContext::GetSSLHostStateDelegate() {
  return nullptr;
}

std::unique_ptr<content::ZoomLevelDelegate>
AppRuntimeBrowserContext::CreateZoomLevelDelegate(const base::FilePath&) {
  return nullptr;
}

content::PermissionControllerDelegate*
AppRuntimeBrowserContext::GetPermissionControllerDelegate() {
  return PermissionManagerFactory::GetForBrowserContext(this);
}

content::ReduceAcceptLanguageControllerDelegate*
AppRuntimeBrowserContext::GetReduceAcceptLanguageControllerDelegate() {
  return nullptr;
}

content::BackgroundFetchDelegate*
AppRuntimeBrowserContext::GetBackgroundFetchDelegate() {
  return nullptr;
}

content::BackgroundSyncController*
AppRuntimeBrowserContext::GetBackgroundSyncController() {
  return nullptr;
}

content::BrowsingDataRemoverDelegate*
AppRuntimeBrowserContext::GetBrowsingDataRemoverDelegate() {
  return nullptr;
}

content::LocalStorageTracker*
AppRuntimeBrowserContext::GetLocalStorageTracker() {
#if defined(USE_LOCAL_STORAGE_TRACKER)
  return local_storage_tracker_;
#else
  return nullptr;
#endif
}

bool AppRuntimeBrowserContext::ExtensionsAreAllowed() {
  return extensions_are_allowed_;
}

void AppRuntimeBrowserContext::FlushCookieStore() {
  GetDefaultStoragePartition()
      ->GetCookieManagerForBrowserProcess()
      ->FlushCookieStore(
          network::mojom::CookieManager::FlushCookieStoreCallback());
}

content::ClientHintsControllerDelegate*
AppRuntimeBrowserContext::GetClientHintsControllerDelegate() {
  return new neva_user_agent::ClientHints();
}

NotifierSettingsController*
AppRuntimeBrowserContext::GetNotifierSettingsController() {
  return NotifierSettingsController::Get();
}

AppRuntimeBrowserContext::AppRuntimeBrowserContext(const std::string& partition,
                                                   bool off_the_record,
                                                   bool allow_to_load_extensions)
    : off_the_record_(off_the_record),
      extensions_are_allowed_(allow_to_load_extensions),
      resource_context_(new content::ResourceContext()),
      path_(InitPath(partition)),
      notifier_client_(std::make_unique<NotifierClient>(this)) {
#if defined(USE_LOCAL_STORAGE_TRACKER)
  local_storage_tracker_ = content::LocalStorageTracker::GetInstance();
  local_storage_tracker_->Initialize(GetPath());
#endif

  base::FilePath filepath = path_.AppendASCII("wam_prefs.json");
  LOG(INFO) << __func__ << " json_pref_store_path=" << filepath;
  scoped_refptr<JsonPrefStore> pref_store =
      base::MakeRefCounted<JsonPrefStore>(filepath);
  pref_store->ReadPrefs();  // Synchronous.

  PrefServiceFactory factory;
  factory.set_user_prefs(pref_store);

  user_prefs::PrefRegistrySyncable* pref_registry =
      new user_prefs::PrefRegistrySyncable;

  PlatformNotificationServiceImpl::RegisterProfilePrefs(pref_registry);
  PushMessagingAppIdentifier::RegisterProfilePrefs(pref_registry);
  HostContentSettingsMap::RegisterProfilePrefs(pref_registry);
  MediaCaptureDevicesDispatcher::RegisterProfilePrefs(pref_registry);
  DeviceMediaStreamAccessHandler::RegisterProfilePrefs(pref_registry);
#if defined(USE_NEVA_CHROME_EXTENSIONS)
  extensions::ExtensionPrefs::RegisterProfilePrefs(pref_registry);
  extensions::AudioAPI::RegisterUserPrefs(pref_registry);
  extensions::PermissionsManager::RegisterProfilePrefs(pref_registry);
  language::LanguagePrefs::RegisterProfilePrefs(pref_registry);
#endif

  user_pref_service_ = factory.Create(pref_registry);
  user_prefs::UserPrefs::Set(this, user_pref_service_.get());

  PushMessagingServiceImpl::InitializeForProfile(this);

#if defined(__clang__)
  LOG(INFO) << "Compiler: clang";
#elif defined(COMPILER_GCC)
  LOG(INFO) << "Compiler: gcc";
#endif

#if defined(USE_NEVA_CHROME_EXTENSIONS)
  local_state_ = neva::prefs::CreateLocalState(GetPath());
  sessions::SessionIdGenerator::GetInstance()->Init(local_state_.get());

  neva::NevaExtensionsBrowserClient* extensions_browser_client =
      static_cast<neva::NevaExtensionsBrowserClient*>(
          extensions::ExtensionsBrowserClient::Get());
  extensions_browser_client->AssociateWithBrowserContext(
      this, user_pref_service_.get());

  // Create BrowserContextKeyedServices now that we have an
  // ExtensionsBrowserClient that BrowserContextKeyedAPIServices can query.
  BrowserContextDependencyManager::GetInstance()->CreateBrowserContextServices(
      this);

  raw_ptr<neva::NevaExtensionSystem> extension_system =
      static_cast<neva::NevaExtensionSystem*>(
          extensions::ExtensionSystem::Get(this));
  extension_system->InitForRegularProfile(extensions_are_allowed_);
  extension_system->FinishInitialization();

  if (extensions_are_allowed_) {
    neva::LoadExtensionsFromCommandLine(extension_system);
  }

  neva::NevaExtensionsServiceImpl* extension_service =
      neva::NevaExtensionsServiceFactory::GetService(this);
  if (extension_service) {
    extension_service->SetTabHelper(std::make_unique<neva::TabHelperImpl>());
  }
#endif  // defined(USE_NEVA_CHROME_EXTENSIONS)
}

// static
AppRuntimeBrowserContext::BrowserContextMap&
AppRuntimeBrowserContext::browser_context_map() {
  static base::NoDestructor<AppRuntimeBrowserContext::BrowserContextMap>
      browser_context_registry;
  return *browser_context_registry;
}

// static
AppRuntimeBrowserContext::BrowserContextMap&
AppRuntimeBrowserContext::off_the_record_browser_context_map() {
  static base::NoDestructor<AppRuntimeBrowserContext::BrowserContextMap>
      off_the_record_registry;
  return *off_the_record_registry;
}

base::FilePath AppRuntimeBrowserContext::InitPath(
    const std::string& partition) const {
  // Default value
  base::FilePath path;
  base::PathService::Get(base::DIR_CACHE, &path);

  // Overwrite default path value
  base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  if (cmd_line->HasSwitch(kUserDataDir)) {
    base::FilePath new_path = cmd_line->GetSwitchValuePath(kUserDataDir);
    if (!new_path.empty()) {
      path = std::move(new_path);
      LOG(INFO) << "kUserDataDir is set.";
    } else {
      LOG(INFO) << "kUserDataDir is empty.";
    }
  } else {
    LOG(INFO) << "kUserDataDir isn't set.";
  }

  // Append storage name
  if (!off_the_record_) {
    if (partition.empty()) {
      path = path.Append(FILE_PATH_LITERAL("Default"));
    } else {
      path = path.Append(FILE_PATH_LITERAL("Partitions"))
                 .Append(base::FilePath::FromUTF8Unsafe(
                     base::EscapePath(base::ToLowerASCII(partition))));
    }
  }

  LOG(INFO) << "Will use path=" << path.value();
  return path;
}
}  // namespace neva_app_runtime
