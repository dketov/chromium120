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

#include "media/capture/video/webos/webos_camera_service.h"
#include "media/capture/video/webos/webos_camera_constants.h"

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

#include <CameraBuffer.h>

namespace media {

namespace {

const char kWebOSChromiumCamera[] = "com.webos.chromium.camera";

void SigHandlerForCameraService(int signum) {
  VLOG(1) << __func__ << ", signal handler : signum(" << signum << ")";
}

}  // namespace

#define BIND_TO_LUNA_THREAD(function, data)                           \
  base::BindPostTask(luna_response_runner_,                           \
                     base::BindRepeating(function, weak_this_, data), \
                     FROM_HERE)

WebOSCameraService::WebOSCameraService()
    : weak_factory_(this),
      luna_call_thread_("WebOSCameraLunaCallThread"),
      luna_response_runner_(base::ThreadPool::CreateSingleThreadTaskRunner(
          {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  VLOG(1) << __func__ << " this[" << this << "]";

  luna_call_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::UI, 0));

  weak_this_ = weak_factory_.GetWeakPtr();

  camera_buffer_.reset(
      new camera::CameraBuffer(camera::CameraBuffer::SHMEM_SYSTEMV));

  luna_call_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&WebOSCameraService::EnsureLunaServiceCreated,
                                weak_this_));
}

WebOSCameraService::~WebOSCameraService() {
  VLOG(1) << __func__ << " this[" << this << "]";

  if (fault_event_subscribe_key_)
    luna_service_client_->Unsubscribe(fault_event_subscribe_key_);

  if (camera_list_subscribe_key_)
    luna_service_client_->Unsubscribe(camera_list_subscribe_key_);

  if (luna_call_thread_.IsRunning())
    luna_call_thread_.Stop();
}

absl::optional<int> WebOSCameraService::Open(
    base::PlatformThreadId pid,
    const WebOSCameraDeviceId& device_id,
    const std::string& mode) {
  VLOG(1) << __func__ << " pid=" << pid << ", device_id=" << device_id
          << ", mode=" << mode;

  signal(SIGUSR1, SigHandlerForCameraService);

  base::Value::Dict register_root;
  register_root.Set(kPid, pid);
  register_root.Set(kId, device_id);
  register_root.Set(kMode, mode);

  std::string open_payload;
  if (!base::JSONWriter::Write(register_root, &open_payload)) {
    LOG(ERROR) << __func__ << " Failed to write open payload";
    return absl::nullopt;
  }

  std::string response;
  if (!LunaCallInternal(base::LunaServiceClient::GetServiceURI(
                            base::LunaServiceClient::URIType::CAMERA, kOpen),
                        open_payload, &response)) {
    LOG(ERROR) << __func__ << " Failed luna service call";
    return absl::nullopt;
  }

  absl::optional<base::Value::Dict> root_value = GetRootDictionary(response);
  if (!root_value)
    return absl::nullopt;

  absl::optional<int> device_handle = root_value->FindIntByDottedPath(kHandle);
  if (!device_handle) {
    LOG(ERROR) << __func__ << " Did not receive device handle: " << response;
  }

  return device_handle;
}

void WebOSCameraService::Close(base::PlatformThreadId pid, int handle) {
  VLOG(1) << __func__ << " pid=" << pid << ", handle=" << handle;

  base::Value::Dict register_root;
  register_root.Set(kPid, pid);
  register_root.Set(kHandle, handle);

  std::string close_payload;
  if (!base::JSONWriter::Write(register_root, &close_payload)) {
    LOG(ERROR) << __func__ << " Failed to write close payload";
    return;
  }

  if (!LunaCallInternal(base::LunaServiceClient::GetServiceURI(
                            base::LunaServiceClient::URIType::CAMERA, kClose),
                        close_payload, nullptr)) {
    LOG(ERROR) << __func__ << " Failed luna service call";
  }
}

bool WebOSCameraService::GetDeviceIds(WebOSCameraDeviceIdList& device_ids) {
  VLOG(1) << __func__;

  base::Value::Dict register_root;
  register_root.Set(kSubscribe, false);

  std::string list_payload;
  if (!base::JSONWriter::Write(register_root, &list_payload)) {
    LOG(ERROR) << __func__ << " Failed to write getCameraList payload";
    return false;
  }

  std::string response;
  if (!LunaCallInternal(
          base::LunaServiceClient::GetServiceURI(
              base::LunaServiceClient::URIType::CAMERA, kGetCameraList),
          list_payload, &response)) {
    LOG(ERROR) << __func__ << " Failed luna service call";
    return false;
  }

  absl::optional<base::Value::Dict> root_value = GetRootDictionary(response);
  if (!root_value)
    return false;

  base::Value* device_list = root_value->FindByDottedPath(kDeviceList);
  if (!device_list) {
    LOG(ERROR) << __func__ << " Did not receive camera list: " << response;
    return false;
  }

  for (auto& device_entry : device_list->GetList()) {
    if (!device_entry.is_dict()) {
      continue;
    }
    std::string* device_id = device_entry.GetDict().FindStringByDottedPath(kId);
    if (device_id && !device_id->empty())
      device_ids.push_back(*device_id);
  }

  return true;
}

bool WebOSCameraService::GetDeviceInfo(const WebOSCameraDeviceId& camera_id,
    base::Value* info) {
  DCHECK(luna_call_thread_.task_runner()->BelongsToCurrentThread());

  VLOG(1) << __func__;

  if (camera_id.empty()) {
    LOG(ERROR) << __func__ << " camera_id is empty";
    return false;
  }

  base::Value::Dict register_root;
  register_root.Set(kId, camera_id);

  std::string info_payload;
  if (!base::JSONWriter::Write(register_root, &info_payload)) {
    LOG(ERROR) << __func__ << " Failed to write info payload";
    return false;
  }

  std::string response;
  if (!LunaCallInternal(base::LunaServiceClient::GetServiceURI(
                            base::LunaServiceClient::URIType::CAMERA, kGetInfo),
                        info_payload, &response)) {
    LOG(ERROR) << __func__ << " Failed luna service call";
    return false;
  }

  absl::optional<base::Value::Dict> root_value = GetRootDictionary(response);
  if (!root_value)
    return false;

  auto* info_value = root_value->FindByDottedPath(kInfo);
  if (info && info_value)
    *info = info_value->Clone();

  return true;
}

absl::optional<base::Value> WebOSCameraService::GetProperties(
    const WebOSCameraDeviceId& device_id) {
  DCHECK(luna_call_thread_.task_runner()->BelongsToCurrentThread());

  VLOG(1) << __func__ << "device_id=" << device_id;

  if (device_id.empty() < 0) {
    LOG(ERROR) << __func__ << " Invalid camera id";
    return absl::nullopt;
  }

  base::Value::Dict register_root;
  register_root.Set(kId, base::Value(device_id));

  std::string properties_payload;
  if (!base::JSONWriter::Write(register_root, &properties_payload)) {
    LOG(ERROR) << __func__ << " Failed to write getProperties payload";
    return absl::nullopt;
  }

  std::string response;
  if (!LunaCallInternal(
          base::LunaServiceClient::GetServiceURI(
              base::LunaServiceClient::URIType::CAMERA, kGetProperties),
          properties_payload, &response)) {
    LOG(ERROR) << __func__ << " Failed luna service call";
    return absl::nullopt;
  }

  absl::optional<base::Value::Dict> root_value = GetRootDictionary(response);
  if (!root_value)
    return absl::nullopt;

  auto* param_value = root_value->FindByDottedPath(kParams);
  if (param_value)
    return param_value->Clone();

  return absl::nullopt;
}

bool WebOSCameraService::SetProperties(int handle,
                                       base::Value::Dict& properties) {
  VLOG(1) << __func__;

  if (handle < 0) {
    LOG(ERROR) << __func__ << " Invalid camera handle";
    return false;
  }

  base::Value::Dict register_root;
  register_root.Set(kHandle, handle);
  register_root.Set(kParams, properties.Clone());

  std::string properties_payload;
  if (!base::JSONWriter::Write(register_root, &properties_payload)) {
    LOG(ERROR) << __func__ << " Failed to write setProperties payload";
    return false;
  }

  std::string response;
  if (!LunaCallInternal(
          base::LunaServiceClient::GetServiceURI(
              base::LunaServiceClient::URIType::CAMERA, kSetProperties),
          properties_payload, &response)) {
    LOG(ERROR) << __func__ << " Failed luna service call";
    return false;
  }

  return GetRootDictionary(response).has_value();
}

bool WebOSCameraService::SetFormat(int handle,
                                   int width,
                                   int height,
                                   const std::string& format,
                                   int fps) {
  VLOG(1) << __func__ << " handle=" << handle << ", width=" << width
          << ", height=" << height << " fps=" << fps;

  if (handle < 0) {
    LOG(ERROR) << __func__ << " Invalid camera handle";
    return false;
  }

  base::Value::Dict format_value;
  format_value.Set(kWidth, width);
  format_value.Set(kHeight, height);
  format_value.Set(kFormat, format);
  format_value.Set(kFps, fps);

  base::Value::Dict format_root;
  format_root.Set(kHandle, handle);
  format_root.Set(kParams, format_value.Clone());

  std::string format_payload;
  if (!base::JSONWriter::Write(format_root, &format_payload)) {
    LOG(ERROR) << __func__ << " Failed to write setFormat payload";
    return false;
  }

  std::string response;
  if (!LunaCallInternal(
          base::LunaServiceClient::GetServiceURI(
              base::LunaServiceClient::URIType::CAMERA, kSetFormat),
          format_payload, &response)) {
    LOG(ERROR) << __func__ << " Failed luna service call";
    return false;
  }

  return GetRootDictionary(response).has_value();
}

absl::optional<int> WebOSCameraService::StartCamera(int handle) {
  VLOG(1) << __func__ << " handle=" << handle;

  if (handle < 0) {
    LOG(ERROR) << __func__ << " Invalid camera handle";
    return absl::nullopt;
  }

  base::Value::Dict param_value;
  param_value.Set(kType, kSharedMemory);
  param_value.Set(kSource, "0");

  base::Value::Dict preview_root;
  preview_root.Set(kHandle, handle);
  preview_root.Set(kParams, param_value.Clone());

  std::string camera_payload;
  if (!base::JSONWriter::Write(preview_root, &camera_payload)) {
    LOG(ERROR) << __func__ << " Failed to write startCamera payload";
    return absl::nullopt;
  }

  std::string response;
  if (!LunaCallInternal(
          base::LunaServiceClient::GetServiceURI(
              base::LunaServiceClient::URIType::CAMERA, kStartCamera),
          camera_payload, &response)) {
    LOG(ERROR) << __func__ << " Failed luna service call";
    return absl::nullopt;
  }

  absl::optional<base::Value::Dict> root_value = GetRootDictionary(response);
  if (!root_value)
    return absl::nullopt;

  absl::optional<int> shmem_key = root_value->FindIntByDottedPath(kKey);
  if (!shmem_key) {
    LOG(ERROR) << __func__ << " Did not receive key value: " << response;
  }

  VLOG(1) << __func__ << " shmem_key=" << *shmem_key;
  return shmem_key;
}

void WebOSCameraService::StopCamera(int handle) {
  VLOG(1) << __func__ << " handle=" << handle;

  if (handle < 0) {
    LOG(ERROR) << __func__ << " Invalid camera handle";
    return;
  }

  base::Value::Dict preview_root;
  preview_root.Set(kHandle, handle);

  std::string camera_payload;
  if (!base::JSONWriter::Write(preview_root, &camera_payload)) {
    LOG(ERROR) << __func__ << " Failed to write stopCamera payload";
    return;
  }

  if (!LunaCallInternal(
          base::LunaServiceClient::GetServiceURI(
              base::LunaServiceClient::URIType::CAMERA, kStopCamera),
          camera_payload, nullptr)) {
    LOG(ERROR) << __func__ << " Failed luna service call";
  }
}

void WebOSCameraService::SubscribeCameraChange(ResponseCB camera_cb) {
  base::Value::Dict camera_list_root;
  camera_list_root.Set(kSubscribe, true);

  std::string camera_list_payload;
  if (!base::JSONWriter::Write(camera_list_root, &camera_list_payload)) {
    LOG(ERROR) << __func__ << " Failed to write getCameraList payload";
    return;
  }

  base::AutoLock auto_lock(camera_service_lock_);
  luna_service_client_->Subscribe(
      base::LunaServiceClient::GetServiceURI(
          base::LunaServiceClient::URIType::CAMERA, kGetCameraList),
      camera_list_payload, &camera_list_subscribe_key_, camera_cb);
}

void WebOSCameraService::SubscribeFaultEvent(ResponseCB event_cb) {
  base::Value::Dict get_event_root;
  get_event_root.Set(kSubscribe, true);

  std::string get_event_payload;
  if (!base::JSONWriter::Write(get_event_root, &get_event_payload)) {
    LOG(ERROR) << __func__ << " Failed to write getEventNotification payload";
    return;
  }

  base::AutoLock auto_lock(camera_service_lock_);
  luna_service_client_->Subscribe(
      base::LunaServiceClient::GetServiceURI(
          base::LunaServiceClient::URIType::CAMERA, kGetEventNotification),
      get_event_payload, &fault_event_subscribe_key_, event_cb);
}

bool WebOSCameraService::OpenCameraBuffer(int shmem_key) {
  base::AutoLock auto_lock(camera_service_lock_);
  if (camera_buffer_)
    return camera_buffer_->Open(shmem_key);
  return false;
}

base::span<uint8_t> WebOSCameraService::ReadCameraBuffer() {
  base::AutoLock auto_lock(camera_service_lock_);
  uint8_t* buffer = nullptr;
  int size;
  if (camera_buffer_ && camera_buffer_->ReadData(&buffer, &size))
    return base::make_span(buffer, static_cast<size_t>(size));
  return {};
}

bool WebOSCameraService::CloseCameraBuffer() {
  base::AutoLock auto_lock(camera_service_lock_);
  if (camera_buffer_)
    return camera_buffer_->Close();
  return false;
}

absl::optional<base::Value::Dict> WebOSCameraService::GetRootDictionary(
    const std::string& payload) {
  VLOG(1) << __func__ << " payload: " << payload;
  if (payload.empty())
    return absl::nullopt;

  absl::optional<base::Value> root = base::JSONReader::Read(payload);
  if (!root || !root->is_dict()) {
    return absl::nullopt;
  }

  base::Value::Dict& root_value = root->GetDict();
  auto return_value = root_value.FindBoolByDottedPath(kReturnValue);
  if (!return_value || !*return_value) {
    LOG(WARNING) << __func__ << " return value false: " << payload;
    return absl::nullopt;
  }

  return std::move(*root).TakeDict();
}

void WebOSCameraService::EnsureLunaServiceCreated() {
  if (luna_service_client_)
    return;

  luna_service_client_.reset(new base::LunaServiceClient(kWebOSChromiumCamera));
  VLOG(1) << __func__ << " luna_service_client_=" << luna_service_client_.get();
}

bool WebOSCameraService::LunaCallInternal(const std::string& uri,
                                          const std::string& param,
                                          std::string* response) {
  VLOG(1) << __func__ << " " << uri << " " << param;

  base::AutoLock auto_lock(camera_service_lock_);

  std::unique_ptr<LunaCbHandle> handle =
      std::make_unique<LunaCbHandle>(uri, param, response);

  luna_call_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&WebOSCameraService::LunaCallAsyncInternal,
                                weak_this_, handle.get()));
  handle->async_done.Wait();
  return true;
}

void WebOSCameraService::LunaCallAsyncInternal(LunaCbHandle* handle) {
  VLOG(1) << __func__ << " " << handle->uri << " " << handle->param;

  EnsureLunaServiceCreated();

  if (!handle->response) {
    luna_service_client_->CallAsync(handle->uri, handle->param);
    handle->async_done.Signal();
    return;
  }

  luna_service_client_->CallAsync(
      handle->uri, handle->param,
      BIND_TO_LUNA_THREAD(&WebOSCameraService::OnLunaCallResponse,
                          handle));
}

void WebOSCameraService::OnLunaCallResponse(LunaCbHandle* handle,
                                            const std::string& response) {
  VLOG(1) << __func__ << " response=[" << response << "]";

  if (handle && handle->response)
    handle->response->assign(response);

  handle->async_done.Signal();
}

}  // namespace media
