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

#ifndef MEDIA_CAPTURE_VIDEO_WEBOS_WEBOS_CAMERA_SERVICE_H_
#define MEDIA_CAPTURE_VIDEO_WEBOS_WEBOS_CAMERA_SERVICE_H_

#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/neva/webos/luna_service_client.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "media/capture/capture_export.h"
#include "media/capture/video/webos/webos_camera_constants.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace camera {
class CameraBuffer;
}

namespace media {

using WebOSCameraDeviceId = std::string;
using WebOSCameraDeviceIdList = std::vector<WebOSCameraDeviceId>;

class CAPTURE_EXPORT WebOSCameraService
    : public base::RefCountedThreadSafe<WebOSCameraService> {
 public:
  using ResponseCB = base::RepeatingCallback<void(const std::string&)>;

  explicit WebOSCameraService();
  virtual ~WebOSCameraService();

  absl::optional<int> Open(base::PlatformThreadId pid,
                           const WebOSCameraDeviceId& device_id,
                           const std::string& mode);
  void Close(base::PlatformThreadId pid, int handle);

  bool GetDeviceIds(WebOSCameraDeviceIdList& device_ids);
  bool GetDeviceInfo(const WebOSCameraDeviceId& device_id, base::Value* info);
  absl::optional<base::Value> GetProperties(const WebOSCameraDeviceId& id);
  bool SetProperties(int handle, base::Value::Dict& properties);

  bool SetFormat(int handle,
                 int width,
                 int height,
                 const std::string& format,
                 int fps);
  absl::optional<int> StartCamera(int handle);
  void StopCamera(int handle);

  void SubscribeCameraChange(ResponseCB cb);
  void SubscribeFaultEvent(ResponseCB cb);

  bool OpenCameraBuffer(int shmem_key);
  base::span<uint8_t> ReadCameraBuffer();
  bool CloseCameraBuffer();

  absl::optional<base::Value::Dict> GetRootDictionary(const std::string& payload);

 private:
  struct LunaCbHandle {
    LunaCbHandle(const std::string& uri,
                 const std::string& param,
                 std::string* response)
        : uri(uri), param(param), response(response) {}
    std::string uri;
    std::string param;
    std::string* response = nullptr;
    base::WaitableEvent async_done;
  };

  friend class base::RefCountedThreadSafe<WebOSCameraService>;

  WebOSCameraService(const WebOSCameraService&) = delete;
  WebOSCameraService& operator=(const WebOSCameraService&) = delete;

  void EnsureLunaServiceCreated();
  bool LunaCallInternal(const std::string& uri,
                        const std::string& param,
                        std::string* response);
  void LunaCallAsyncInternal(LunaCbHandle* handle);

  void OnLunaCallResponse(LunaCbHandle* handle, const std::string& response);

  std::unique_ptr<base::LunaServiceClient> luna_service_client_;

  LSMessageToken fault_event_subscribe_key_ = LSMESSAGE_TOKEN_INVALID;
  LSMessageToken camera_list_subscribe_key_ = LSMESSAGE_TOKEN_INVALID;

  base::Thread luna_call_thread_;
  scoped_refptr<base::SingleThreadTaskRunner> luna_response_runner_ = nullptr;

  base::Lock camera_service_lock_;

  std::unique_ptr<camera::CameraBuffer> camera_buffer_;

  base::WeakPtr<WebOSCameraService> weak_this_;
  base::WeakPtrFactory<WebOSCameraService> weak_factory_{this};
};

}  // namespace media

#endif  // MEDIA_CAPTURE_VIDEO_WEBOS_WEBOS_CAMERA_SERVICE_H_
