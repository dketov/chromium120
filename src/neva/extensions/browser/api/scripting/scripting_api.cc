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

#include "neva/extensions/browser/api/scripting/scripting_api.h"

#include "base/json/json_writer.h"
#include "base/types/optional_util.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/api/scripting/scripting_constants.h"
#include "extensions/browser/extension_file_task_runner.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/load_and_localize_file.h"
#include "extensions/browser/user_script_manager.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/mojom/execution_world.mojom-shared.h"
#include "extensions/common/mojom/host_id.mojom.h"
#include "extensions/common/permissions/api_permission.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/utils/content_script_utils.h"
#include "extensions/common/utils/extension_types_utils.h"
#include "neva/extensions/browser/neva_extensions_service_factory.h"
#include "neva/extensions/browser/neva_extensions_service_impl.h"
#include "neva/extensions/browser/tab_helper.h"
#include "neva/extensions/browser/web_contents_map.h"

namespace neva {

namespace {

constexpr char kCouldNotLoadFileError[] = "Could not load file: '*'.";
constexpr char kDuplicateFileSpecifiedError[] =
    "Duplicate file specified: '*'.";

extensions::mojom::ExecutionWorld ConvertExecutionWorld(
    extensions::api::scripting::ExecutionWorld world) {
  extensions::mojom::ExecutionWorld execution_world =
      extensions::mojom::ExecutionWorld::kIsolated;
  switch (world) {
    case extensions::api::scripting::EXECUTION_WORLD_NONE:
    case extensions::api::scripting::EXECUTION_WORLD_ISOLATED:
      break;  // Default to mojom::ExecutionWorld::kIsolated.
    case extensions::api::scripting::EXECUTION_WORLD_MAIN:
      execution_world = extensions::mojom::ExecutionWorld::kMain;
  }

  return execution_world;
}

// Constructs an array of file sources from the read file `data`.
std::vector<InjectedFileSource> ConstructFileSources(
    std::vector<std::unique_ptr<std::string>> data,
    std::vector<std::string> file_names) {
  // Note: CHECK (and not DCHECK) because if it fails, we have an out-of-bounds
  // access.
  CHECK_EQ(data.size(), file_names.size());
  const size_t num_sources = data.size();
  std::vector<InjectedFileSource> sources;
  sources.reserve(num_sources);
  for (size_t i = 0; i < num_sources; ++i)
    sources.emplace_back(std::move(file_names[i]), std::move(data[i]));

  return sources;
}

std::vector<extensions::mojom::JSSourcePtr> FileSourcesToJSSources(
    const extensions::Extension& extension,
    std::vector<InjectedFileSource> file_sources) {
  std::vector<extensions::mojom::JSSourcePtr> js_sources;
  js_sources.reserve(file_sources.size());
  for (auto& file_source : file_sources) {
    js_sources.push_back(extensions::mojom::JSSource::New(
        std::move(*file_source.data),
        extension.GetResourceURL(file_source.file_name)));
  }

  return js_sources;
}

// Checks `files` and populates `resources_out` with the appropriate extension
// resource. Returns true on success; on failure, populates `error_out`.
bool GetFileResources(const std::vector<std::string>& files,
                      const extensions::Extension& extension,
                      std::vector<extensions::ExtensionResource>* resources_out,
                      std::string* error_out) {
  if (files.empty()) {
    static constexpr char kAtLeastOneFileError[] =
        "At least one file must be specified.";
    *error_out = kAtLeastOneFileError;
    return false;
  }

  std::vector<extensions::ExtensionResource> resources;
  for (const auto& file : files) {
    extensions::ExtensionResource resource = extension.GetResource(file);
    if (resource.extension_root().empty() || resource.relative_path().empty()) {
      *error_out = extensions::ErrorUtils::FormatErrorMessage(
          kCouldNotLoadFileError, file);
      return false;
    }

    // extensions::ExtensionResource doesn't implement an operator==.
    auto existing = base::ranges::find_if(
        resources, [&resource](const extensions::ExtensionResource& other) {
          return resource.relative_path() == other.relative_path();
        });

    if (existing != resources.end()) {
      // Disallow duplicates. Note that we could allow this, if we wanted (and
      // there *might* be reason to with JS injection, to perform an operation
      // twice?). However, this matches content script behavior, and injecting
      // twice can be done by chaining calls to executeScript() / insertCSS().
      // This isn't a robust check, and could probably be circumvented by
      // passing two paths that look different but are the same - but in that
      // case, we just try to load and inject the script twice, which is
      // inefficient, but safe.
      *error_out = extensions::ErrorUtils::FormatErrorMessage(
          kDuplicateFileSpecifiedError, file);
      return false;
    }

    resources.push_back(std::move(resource));
  }

  resources_out->swap(resources);
  return true;
}

using ResourcesLoadedCallback =
    base::OnceCallback<void(std::vector<InjectedFileSource>,
                            absl::optional<std::string>)>;

// Checks the loaded content of extension resources. Invokes `callback` with
// the constructed file sources on success or with an error on failure.
void CheckLoadedResources(std::vector<std::string> file_names,
                          ResourcesLoadedCallback callback,
                          std::vector<std::unique_ptr<std::string>> file_data,
                          absl::optional<std::string> load_error) {
  if (load_error) {
    std::move(callback).Run({}, std::move(load_error));
    return;
  }

  std::vector<InjectedFileSource> file_sources =
      ConstructFileSources(std::move(file_data), std::move(file_names));

  for (const auto& source : file_sources) {
    DCHECK(source.data);
    // TODO(devlin): What necessitates this encoding requirement? Is it needed
    // for blink injection?
    if (!base::IsStringUTF8(*source.data)) {
      static constexpr char kBadFileEncodingError[] =
          "Could not load file '*'. It isn't UTF-8 encoded.";
      std::string error = extensions::ErrorUtils::FormatErrorMessage(
          kBadFileEncodingError, source.file_name);
      std::move(callback).Run({}, std::move(error));
      return;
    }
  }

  std::move(callback).Run(std::move(file_sources), absl::nullopt);
}

// Checks the specified `files` for validity, and attempts to load and localize
// them, invoking `callback` with the result. Returns true on success; on
// failure, populates `error`.
bool CheckAndLoadFiles(std::vector<std::string> files,
                       const extensions::Extension& extension,
                       bool requires_localization,
                       ResourcesLoadedCallback callback,
                       std::string* error) {
  std::vector<extensions::ExtensionResource> resources;
  if (!GetFileResources(files, extension, &resources, error))
    return false;

  LoadAndLocalizeResources(
      extension, resources, requires_localization,
      extensions::script_parsing::GetMaxScriptLength(),
      base::BindOnce(&CheckLoadedResources, std::move(files),
                     std::move(callback)));
  return true;
}

std::unique_ptr<extensions::UserScript> ParseUserScript(
    content::BrowserContext* browser_context,
    const extensions::Extension& extension,
    const extensions::api::scripting::RegisteredContentScript& content_script,
    int definition_index,
    int valid_schemes,
    std::u16string* error) {
  auto result = std::make_unique<extensions::UserScript>();
  result->set_id(content_script.id);
  result->set_host_id(extensions::mojom::HostID(
      extensions::mojom::HostID::HostType::kExtensions, extension.id()));

  if (content_script.run_at != extensions::api::extension_types::RunAt::kNone) {
    result->set_run_location(
        extensions::ConvertRunLocation(content_script.run_at));
  }

  if (content_script.all_frames) {
    result->set_match_all_frames(*content_script.all_frames);
  }

  DCHECK(content_script.matches);
  if (!extensions::script_parsing::ParseMatchPatterns(
          *content_script.matches,
          base::OptionalToPtr(content_script.exclude_matches),
          extension.creation_flags(),
          extensions::scripting::kScriptsCanExecuteEverywhere, valid_schemes,
          extensions::scripting::kAllUrlsIncludesChromeUrls,
          /*definition_index=*/absl::nullopt, result.get(), error,
          /*wants_file_access=*/nullptr)) {
    return nullptr;
  }

  if (content_script.match_origin_as_fallback.value_or(false)) {
    if (!extensions::script_parsing::ValidateMatchOriginAsFallback(
            extensions::MatchOriginAsFallbackBehavior::kAlways,
            result->url_patterns(), error)) {
      return nullptr;
    }

    // Default value for MatchOriginAsFallbackBehavior is `kNever`, so this only
    // needs to be set if `content_script.match_origin_as_fallback` is true.
    result->set_match_origin_as_fallback(
        extensions::MatchOriginAsFallbackBehavior::kAlways);
  }

  if (!extensions::script_parsing::ParseFileSources(
          &extension, base::OptionalToPtr(content_script.js),
          base::OptionalToPtr(content_script.css), definition_index,
          result.get(), error)) {
    return nullptr;
  }

  result->set_incognito_enabled(
      extensions::util::IsIncognitoEnabled(extension.id(), browser_context));
  result->set_execution_world(ConvertExecutionWorld(content_script.world));
  return result;
}

}  // namespace

InjectedFileSource::InjectedFileSource(std::string file_name,
                                       std::unique_ptr<std::string> data)
    : file_name(std::move(file_name)), data(std::move(data)) {}
InjectedFileSource::InjectedFileSource(InjectedFileSource&&) = default;
InjectedFileSource::~InjectedFileSource() = default;

ScriptingExecuteScriptFunction::ScriptingExecuteScriptFunction() = default;
ScriptingExecuteScriptFunction::~ScriptingExecuteScriptFunction() = default;

ExtensionFunction::ResponseAction ScriptingExecuteScriptFunction::Run() {
  absl::optional<extensions::api::scripting::ExecuteScript::Params> params =
      extensions::api::scripting::ExecuteScript::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  injection_ = std::move(params->injection);

  // Silently alias `function` to `func` for backwards compatibility.
  // TODO(devlin): Remove this in M95.
  if (injection_.function) {
    if (injection_.func) {
      return RespondNow(
          Error("Both 'func' and 'function' were specified. "
                "Only 'func' should be used."));
    }
    injection_.func = std::move(injection_.function);
  }

  if ((injection_.files && injection_.func) ||
      (!injection_.files && !injection_.func)) {
    return RespondNow(
        Error("Exactly one of 'func' and 'files' must be specified"));
  }

  if (injection_.files) {
    if (injection_.args)
      return RespondNow(Error("'args' may not be used with file injections."));

    // JS files don't require localization.
    constexpr bool kRequiresLocalization = false;
    std::string error;
    if (!CheckAndLoadFiles(
            std::move(*injection_.files), *extension(), kRequiresLocalization,
            base::BindOnce(&ScriptingExecuteScriptFunction::DidLoadResources,
                           this),
            &error)) {
      return RespondNow(Error(std::move(error)));
    }
    return RespondLater();
  }

  DCHECK(injection_.func);

  // TODO(devlin): This (wrapping a function to create an IIFE) is pretty hacky,
  // and along with the JSON-serialization of the arguments to curry in.
  // Add support to the ScriptExecutor to better support this case.
  std::string args_expression;
  if (injection_.args) {
    std::vector<std::string> string_args;
    string_args.reserve(injection_.args->size());
    for (const auto& arg : *injection_.args) {
      std::string json;
      if (!base::JSONWriter::Write(arg, &json))
        return RespondNow(Error("Unserializable argument passed."));
      string_args.push_back(std::move(json));
    }
    args_expression = base::JoinString(string_args, ",");
  }

  std::string code_to_execute = base::StringPrintf(
      "(%s)(%s)", injection_.func->c_str(), args_expression.c_str());

  std::vector<extensions::mojom::JSSourcePtr> sources;
  sources.push_back(
      extensions::mojom::JSSource::New(std::move(code_to_execute), GURL()));

  std::string error;
  if (!Execute(std::move(sources), &error))
    return RespondNow(Error(std::move(error)));

  return RespondLater();
}

void ScriptingExecuteScriptFunction::DidLoadResources(
    std::vector<InjectedFileSource> file_sources,
    absl::optional<std::string> load_error) {
  if (load_error) {
    Respond(Error(std::move(*load_error)));
    return;
  }

  DCHECK(!file_sources.empty());

  std::string error;
  if (!Execute(FileSourcesToJSSources(*extension(), std::move(file_sources)),
               &error)) {
    Respond(Error(std::move(error)));
  }
}

bool ScriptingExecuteScriptFunction::Execute(
    std::vector<extensions::mojom::JSSourcePtr> sources,
    std::string* error) {
  extensions::ScriptExecutor* script_executor = nullptr;
  extensions::ScriptExecutor::FrameScope frame_scope =
      extensions::ScriptExecutor::SPECIFIED_FRAMES;
  std::set<int> frame_ids;

  TabHelper* tab_helper =
      NevaExtensionsServiceFactory::GetService(browser_context())
          ->GetTabHelper();
  if (!tab_helper) {
    LOG(ERROR) << __func__ << " tab_helper is null";
    return false;
  }

  content::WebContents* web_contents =
      tab_helper->GetWebContentsFromId(injection_.target.tab_id);
  if (!web_contents) {
    LOG(ERROR) << __func__ << " web_contents is null";
    return false;
  }

  script_executor = new extensions::ScriptExecutor(web_contents);
  frame_ids.insert(web_contents->GetPrimaryMainFrame()->GetFrameTreeNodeId());

  extensions::mojom::ExecutionWorld execution_world =
      ConvertExecutionWorld(injection_.world);

  // Extensions can specify that the script should be injected "immediately".
  // In this case, we specify kDocumentStart as the injection time. Due to
  // inherent raciness between tab creation and load and this function
  // execution, there is no guarantee that it will actually happen at
  // document start, but the renderer will appropriately inject it
  // immediately if document start has already passed.
  extensions::mojom::RunLocation run_location =
      injection_.inject_immediately && *injection_.inject_immediately
          ? extensions::mojom::RunLocation::kDocumentStart
          : extensions::mojom::RunLocation::kDocumentIdle;
  script_executor->ExecuteScript(
      extensions::mojom::HostID(
          extensions::mojom::HostID::HostType::kExtensions, extension()->id()),
      extensions::mojom::CodeInjection::NewJs(
          extensions::mojom::JSInjection::New(
              std::move(sources), execution_world,
              blink::mojom::WantResultOption::kWantResult,
              user_gesture()
                  ? blink::mojom::UserActivationOption::kActivate
                  : blink::mojom::UserActivationOption::kDoNotActivate,
              blink::mojom::PromiseResultOption::kAwait)),
      frame_scope, frame_ids, extensions::ScriptExecutor::MATCH_ABOUT_BLANK,
      run_location, extensions::ScriptExecutor::DEFAULT_PROCESS,
      /* webview_src */ GURL(),
      base::BindOnce(&ScriptingExecuteScriptFunction::OnScriptExecuted, this));

  return true;
}

void ScriptingExecuteScriptFunction::OnScriptExecuted(
    std::vector<extensions::ScriptExecutor::FrameResult> frame_results) {
  // If only a single frame was included and the injection failed, respond with
  // an error.
  if (frame_results.size() == 1 && !frame_results[0].error.empty()) {
    Respond(Error(std::move(frame_results[0].error)));
    return;
  }

  // Otherwise, respond successfully. We currently just skip over individual
  // frames that failed. In the future, we can bubble up these error messages
  // to the extension.
  std::vector<extensions::api::scripting::InjectionResult> injection_results;
  for (auto& result : frame_results) {
    if (!result.error.empty())
      continue;
    extensions::api::scripting::InjectionResult injection_result;
    injection_result.result = std::move(result.value);
    injection_result.frame_id = result.frame_id;
    if (result.document_id)
      injection_result.document_id = result.document_id.ToString();

    // Put the top frame first; otherwise, any order.
    if (result.frame_id == extensions::ExtensionApiFrameIdMap::kTopFrameId) {
      injection_results.insert(injection_results.begin(),
                               std::move(injection_result));
    } else {
      injection_results.push_back(std::move(injection_result));
    }
  }

  Respond(
      ArgumentList(extensions::api::scripting::ExecuteScript::Results::Create(
          injection_results)));
}

ScriptingRegisterContentScriptsFunction::
    ScriptingRegisterContentScriptsFunction() = default;
ScriptingRegisterContentScriptsFunction::
    ~ScriptingRegisterContentScriptsFunction() = default;

ExtensionFunction::ResponseAction
ScriptingRegisterContentScriptsFunction::Run() {
  absl::optional<extensions::api::scripting::RegisterContentScripts::Params>
      params =
          extensions::api::scripting::RegisterContentScripts::Params::Create(
              args());
  EXTENSION_FUNCTION_VALIDATE(params);

  std::vector<extensions::api::scripting::RegisteredContentScript>& scripts =
      params->scripts;
  extensions::ExtensionUserScriptLoader* loader =
      extensions::ExtensionSystem::Get(browser_context())
          ->user_script_manager()
          ->GetUserScriptLoaderForExtension(extension()->id());

  // Create script ids for dynamic content scripts.
  std::string error;
  std::set<std::string> existing_script_ids = loader->GetDynamicScriptIDs(
      extensions::UserScript::Source::kDynamicContentScript);
  std::set<std::string> new_script_ids =
      extensions::scripting::CreateDynamicScriptIds(
          scripts, extensions::UserScript::Source::kDynamicContentScript,
          existing_script_ids, &error);

  if (!error.empty()) {
    CHECK(new_script_ids.empty());
    return RespondNow(Error(std::move(error)));
  }

  // Parse content scripts.
  std::u16string parse_error;
  auto parsed_scripts = std::make_unique<extensions::UserScriptList>();
  std::set<std::string> persistent_script_ids;
  const int valid_schemes = extensions::UserScript::ValidUserScriptSchemes(
      extensions::scripting::kScriptsCanExecuteEverywhere);

  parsed_scripts->reserve(scripts.size());
  for (size_t i = 0; i < scripts.size(); ++i) {
    if (!scripts[i].matches) {
      std::string error_script_id =
          extensions::UserScript::TrimPrefixFromScriptID(scripts[i].id);
      return RespondNow(
          Error(base::StringPrintf("Script with ID '%s' must specify 'matches'",
                                   error_script_id.c_str())));
    }

    std::unique_ptr<extensions::UserScript> user_script =
        ParseUserScript(browser_context(), *extension(), scripts[i], i,
                        valid_schemes, &parse_error);
    if (!user_script) {
      return RespondNow(Error(base::UTF16ToASCII(parse_error)));
    }

    // Scripts will persist across sessions by default.
    if (!scripts[i].persist_across_sessions ||
        *scripts[i].persist_across_sessions) {
      persistent_script_ids.insert(user_script->id());
    }
    parsed_scripts->push_back(std::move(user_script));
  }

  // Add new script IDs now in case another call with the same script IDs is
  // made immediately following this one.
  loader->AddPendingDynamicScriptIDs(std::move(new_script_ids));

  extensions::GetExtensionFileTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&extensions::scripting::ValidateParsedScriptsOnFileThread,
                     extensions::script_parsing::GetSymlinkPolicy(extension()),
                     std::move(parsed_scripts)),
      base::BindOnce(&ScriptingRegisterContentScriptsFunction::
                         OnContentScriptFilesValidated,
                     this, std::move(persistent_script_ids)));

  // Balanced in `OnContentScriptFilesValidated()` or
  // `OnContentScriptsRegistered()`.
  AddRef();
  return RespondLater();
}

void ScriptingRegisterContentScriptsFunction::OnContentScriptFilesValidated(
    std::set<std::string> persistent_script_ids,
    extensions::scripting::ValidateScriptsResult result) {
  // We cannot proceed if the `browser_context` is not valid as the
  // `ExtensionSystem` will not exist.
  if (!browser_context()) {
    Release();  // Matches the `AddRef()` in `Run()`.
    return;
  }

  auto error = std::move(result.second);
  auto scripts = std::move(result.first);
  extensions::ExtensionUserScriptLoader* loader =
      extensions::ExtensionSystem::Get(browser_context())
          ->user_script_manager()
          ->GetUserScriptLoaderForExtension(extension()->id());

  if (error.has_value()) {
    std::set<std::string> ids_to_remove;
    for (const auto& script : *scripts) {
      ids_to_remove.insert(script->id());
    }

    loader->RemovePendingDynamicScriptIDs(std::move(ids_to_remove));
    Respond(Error(std::move(*error)));
    Release();  // Matches the `AddRef()` in `Run()`.
    return;
  }

  loader->AddDynamicScripts(
      std::move(scripts), std::move(persistent_script_ids),
      base::BindOnce(
          &ScriptingRegisterContentScriptsFunction::OnContentScriptsRegistered,
          this));
}

void ScriptingRegisterContentScriptsFunction::OnContentScriptsRegistered(
    const absl::optional<std::string>& error) {
  if (error.has_value()) {
    Respond(Error(std::move(*error)));
  } else {
    Respond(NoArguments());
  }
  Release();  // Matches the `AddRef()` in `Run()`.
}

}  // namespace neva
