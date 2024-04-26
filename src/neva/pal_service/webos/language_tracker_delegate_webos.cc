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

#include "neva/pal_service/webos/language_tracker_delegate_webos.h"

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/values.h"
#include "neva/pal_service/luna/luna_names.h"

namespace {

const char kGetSystemSettingsMethod[] = "getSystemSettings";
const char kGetLanguageRequest[] =
    R"JSON({"keys":["localeInfo"], "subscribe":true})JSON";
const char kUILanguagePath[] = "settings.localeInfo.locales.UI";

}  // namespace

namespace pal {
namespace webos {

LanguageTrackerDelegateWebOS::LanguageTrackerDelegateWebOS(
    const std::string& application_name,
    RepeatingResponse callback)
    : subscription_callback_(std::move(callback)) {
  pal::luna::Client::Params params;
  params.name = application_name;
  luna_client_ = luna::GetSharedClient(params);
  if (luna_client_ && luna_client_->IsInitialized()) {
    const bool subscribed = luna_client_->Subscribe(
        luna::GetServiceURI(luna::service_uri::kSettings,
                            kGetSystemSettingsMethod),
        std::string(kGetLanguageRequest),
        base::BindRepeating(&LanguageTrackerDelegateWebOS::OnResponse,
                            base::Unretained(this)));
    status_ = subscribed ? Status::kSuccess : Status::kFailed;
  }
}

LanguageTrackerDelegateWebOS::~LanguageTrackerDelegateWebOS() {}

LanguageTrackerDelegate::Status LanguageTrackerDelegateWebOS::GetStatus()
    const {
  return status_;
}

std::string LanguageTrackerDelegateWebOS::GetLanguageString() const {
  return language_string_;
}

void LanguageTrackerDelegateWebOS::OnResponse(pal::luna::Client::ResponseStatus,
                                              unsigned,
                                              const std::string& json) {
  absl::optional<base::Value> root(base::JSONReader::Read(json));
  if (!root || !root->is_dict())
    return;

  const base::Value::Dict& dict = root->GetDict();
  absl::optional<bool> return_value = dict.FindBool("returnValue");

  if (return_value.has_value() && !return_value.value()) {
    const std::string* message = dict.FindString("errorText");
    LOG(ERROR) << __func__ << "(): Failed to get UI language. Error: "
               << (message ? *message : "");

    return;
  }

  const base::Value* language = dict.FindByDottedPath(kUILanguagePath);
  if (language) {
    language_string_ = language->GetString();
    subscription_callback_.Run(language_string_);
  }
}

}  // namespace webos
}  // namespace pal
