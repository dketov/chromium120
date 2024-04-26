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

#include "neva/browser_service/browser/webrisk/core/webrisk_utils.h"

#include "base/command_line.h"
#include "content/shell/common/shell_neva_switches.h"

namespace webrisk {
const base::FilePath GetFilePath(const char* file_name) {
  base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  base::FilePath file_path(cmd_line->GetSwitchValuePath(switches::kUserDataDir)
                               .AppendASCII(file_name));
  return file_path;
}
}  // namespace webrisk
