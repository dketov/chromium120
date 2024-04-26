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

#ifndef NEVA_BROWSER_SERVICE_BROWSER_WEBRISK_CORE_WEBRISK_LOCAL_FILE_STORE_H_
#define NEVA_BROWSER_SERVICE_BROWSER_WEBRISK_CORE_WEBRISK_LOCAL_FILE_STORE_H_

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "neva/browser_service/browser/webrisk/core/webrisk.pb.h"
#include "neva/browser_service/browser/webrisk/core/webrisk_data_store.h"

namespace webrisk {

class WebRiskLocalFileStore : public webrisk::WebRiskDataStore {
 public:
  explicit WebRiskLocalFileStore(const base::FilePath& file_path);
  ~WebRiskLocalFileStore() override = default;

  bool IsHashPrefixListEmpty();

  bool Initialize() override;
  bool WriteDataToDisk(
      const ComputeThreatListDiffResponse& file_format) override;
  bool IsHashPrefixExpired() override;
  bool IsHashPrefixAvailable(const std::string& hash_prefix) override;

 private:
  bool ReadFromDisk();
  void FillHashPrefixListFromRawHashes(const std::string& raw_hashes);

  const base::FilePath file_path_;
  std::vector<std::string> hash_prefix_list_;
  base::TimeDelta update_time_;
};

}  // namespace webrisk

#endif  // NEVA_BROWSER_SERVICE_BROWSER_WEBRISK_CORE_WEBRISK_LOCAL_FILE_STORE_H_
