// Copyright 2023 LG Electronics, Inc.
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

#ifndef NEVA_EXTENSIONS_RENDERER_NEVA_EXTENSIONS_DISPATCHER_DELEGATE_H_
#define NEVA_EXTENSIONS_RENDERER_NEVA_EXTENSIONS_DISPATCHER_DELEGATE_H_

#include "extensions/renderer/dispatcher_delegate.h"

namespace neva {

class NevaExtensionsDispatcherDelegate : public extensions::DispatcherDelegate {
 public:
  NevaExtensionsDispatcherDelegate() = default;

  NevaExtensionsDispatcherDelegate(const NevaExtensionsDispatcherDelegate&) =
      delete;
  NevaExtensionsDispatcherDelegate& operator=(
      const NevaExtensionsDispatcherDelegate&) = delete;

  ~NevaExtensionsDispatcherDelegate() override = default;

 private:
  // extensions::DispatcherDelegate implementation.
  void InitializeBindingsSystem(
      extensions::Dispatcher* dispatcher,
      extensions::NativeExtensionBindingsSystem* bindings_system) override;
};

}  // namespace neva

#endif  // NEVA_EXTENSIONS_RENDERER_NEVA_EXTENSIONS_DISPATCHER_DELEGATE_H_
