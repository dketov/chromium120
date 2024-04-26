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

#include "neva/extensions/browser/neva_prefs.h"

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/pref_filter.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/sessions/core/session_id_generator.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"

using base::FilePath;
using user_prefs::PrefRegistrySyncable;

namespace neva {
namespace {

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  sessions::SessionIdGenerator::RegisterPrefs(registry);
}

// Creates a JsonPrefStore from a file at |filepath| and synchronously loads
// the preferences.
scoped_refptr<JsonPrefStore> CreateAndLoadPrefStore(const FilePath& filepath) {
  scoped_refptr<JsonPrefStore> pref_store =
      base::MakeRefCounted<JsonPrefStore>(filepath);
  pref_store->ReadPrefs();  // Synchronous.
  return pref_store;
}

}  // namespace

namespace prefs {

std::unique_ptr<PrefService> CreateLocalState(const FilePath& data_dir) {
  FilePath filepath = data_dir.AppendASCII("local_state.json");
  scoped_refptr<JsonPrefStore> pref_store = CreateAndLoadPrefStore(filepath);

  // Local state is considered "user prefs" from the factory's perspective.
  PrefServiceFactory factory;
  factory.set_user_prefs(pref_store);

  // Local state preferences are not syncable.
  PrefRegistrySimple* registry = new PrefRegistrySimple;
  RegisterLocalStatePrefs(registry);

  return factory.Create(registry);
}

}  // namespace prefs

}  // namespace neva
