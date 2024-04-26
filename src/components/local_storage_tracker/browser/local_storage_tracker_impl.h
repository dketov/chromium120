// Copyright 2020 LG Electronics, Inc.
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

#ifndef COMPONENTS_LOCAL_STORAGE_TRACKER_BROWSER_LOCAL_STORAGE_TRACKER_IMPL_H_
#define COMPONENTS_LOCAL_STORAGE_TRACKER_BROWSER_LOCAL_STORAGE_TRACKER_IMPL_H_

#include "base/memory/singleton.h"
#include "components/local_storage_tracker/public/local_storage_tracker.h"

namespace content {

class LocalStorageTrackerImpl final : public LocalStorageTracker {
 public:
  LocalStorageTrackerImpl(const LocalStorageTrackerImpl&) = delete;
  LocalStorageTrackerImpl& operator=(const LocalStorageTrackerImpl&) = delete;

  static LocalStorageTrackerImpl* GetInstance();

  void Initialize(const base::FilePath& data_file_path) override;

  void OnAppInstalled(const std::string& app_id) override;
  void OnAppRemoved(const std::string& app_id) override;
  void OnAccessOrigin(const std::string& app_id,
                      const GURL& origin,
                      base::OnceCallback<void()> callback) override;
  void OnDeleteCompleted(const GURL& origin);
  base::WeakPtr<LocalStorageTracker> GetWeakPtr() override;

 private:
  LocalStorageTrackerImpl() = default;

  std::set<GURL> GetSubOrigins(const GURL& origin);
  enum class InitializationStatus { kNone, kPending, kFailed, kSucceeded };
  void OnAccessesLoaded(bool success, const AccessDataList& access_list);
  void OnApplicationsLoaded(bool success, const ApplicationDataList& apps_list);
  enum class StoreModificationOperation {
    kAddAccess,
    kAddApplication,
    kAddOrigin,
    kDeleteApplication,
    kDeleteOrigin
  };
  void OnStoreModified(StoreModificationOperation modification_operation,
                       bool succeess);
  void OnStoreInitialized(bool success);
  void OnInitializeFailed();
  void OnInitializeSucceeded();
  bool IsInitialized();
  bool IsHTTPOrHTTPSOriginUniqueForHost(const std::string& host);
  void StartDeleteOriginData(const GURL& origin, bool delete_cookies = true);

  enum class AppLinkVerifyResult {
    kExist,
    kAdded,
    kDeletionInProgress,
    kAddedNewOriginEntry
  };
  AppLinkVerifyResult VerifyOriginAppLink(const GURL& origin,
                                          const std::string& app_id);

  using AppToStatusMap = std::map<std::string, bool>;
  using AppsSet = std::set<std::string>;
  struct OriginData {
    OriginData() = default;
    bool deletion_in_progress = false;
    AppsSet apps;
  };
  using OriginToAppsMap = std::map<GURL, OriginData>;

  class DataDeleteCompletion {
   public:
    DataDeleteCompletion(std::set<GURL> origins, base::OnceClosure callback);
    bool OnDataDeleted(const GURL& origin);

   private:
    std::set<GURL> origins_;
    base::OnceClosure callback_;
  };
  using CompletionList = std::list<DataDeleteCompletion>;

  AppToStatusMap apps_;
  OriginToAppsMap origins_;
  std::unique_ptr<LocalStorageTrackerStore> store_;
  CompletionList data_delete_completions_;

  InitializationStatus init_status_ = InitializationStatus::kNone;

  base::WeakPtrFactory<LocalStorageTrackerImpl> weak_ptr_factory_{this};
  friend struct base::DefaultSingletonTraits<LocalStorageTrackerImpl>;
};

}  // namespace content

#endif  // COMPONENTS_LOCAL_STORAGE_TRACKER_BROWSER_LOCAL_STORAGE_TRACKER_IMPL_H_
