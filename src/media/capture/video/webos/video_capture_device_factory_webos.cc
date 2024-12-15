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

#include "media/capture/video/webos/video_capture_device_factory_webos.h"

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/bind_post_task.h"
#include "media/capture/video/webos/video_capture_device_webos.h"
#include "media/capture/video/webos/webos_camera_constants.h"
#include "media/capture/video/webos/webos_camera_service.h"

namespace media {

namespace {

bool IsControlSupported(base::Value::Dict* property) {
  if (!property)
    return false;

  absl::optional<double> max, min, current, step;
  max = property->FindDoubleByDottedPath(kMax);
  min = property->FindDoubleByDottedPath(kMin);
  current = property->FindDoubleByDottedPath(kValue);
  step = property->FindDoubleByDottedPath(kStep);

  return (max && min && current && step);
}

}  // namespace

VideoCaptureDeviceFactoryWebOS::VideoCaptureDeviceFactoryWebOS()
    : camera_service_(base::MakeRefCounted<WebOSCameraService>()) {
  VLOG(1) << __func__ << " this=" << this;
}

VideoCaptureDeviceFactoryWebOS::~VideoCaptureDeviceFactoryWebOS() {
  VLOG(1) << __func__ << " this=" << this;
}

VideoCaptureErrorOrDevice VideoCaptureDeviceFactoryWebOS::CreateDevice(
    const VideoCaptureDeviceDescriptor& device_descriptor) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto video_capture_device = std::make_unique<VideoCaptureDeviceWebOS>(
      camera_service_, device_descriptor);

  return VideoCaptureErrorOrDevice(std::move(video_capture_device));
}

void VideoCaptureDeviceFactoryWebOS::GetDevicesInfo(
    GetDevicesInfoCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  std::vector<VideoCaptureDeviceInfo> devices_info;

  WebOSCameraDeviceIdList device_ids;
  camera_service_->GetDeviceIds(device_ids);
  if (device_ids.empty()) {
    LOG(ERROR) << __func__ << " Empty list of device ids";
    std::move(callback).Run(std::move(devices_info));
    return;
  }

  for (const auto& device_id : device_ids) {
    base::Value info;
    camera_service_->GetDeviceInfo(device_id, &info);
    std::string* device_name = info.GetDict().FindStringByDottedPath(kName);

    if (!device_name) {
      LOG(WARNING) << __func__
                   << " Can not find device name, device id=" << device_id;
      continue;
    }

    VLOG(1) << __func__ << " id=" << device_id << " name=" << *device_name;

    auto supported_formats = GetSupportedFormats(device_id, &info);
    auto supported_control = GetSupportedControl(device_id);

    if (!supported_formats.has_value() || supported_formats->empty() ||
        !supported_control.has_value()) {
      LOG(WARNING) << __func__ << " No supported formats for: " << device_id;
      continue;
    }

    devices_info.emplace_back(VideoCaptureDeviceDescriptor(
        *device_name, device_id, device_id, VideoCaptureApi::UNKNOWN,
        supported_control.value()));

    devices_info.back().supported_formats =
        std::move(supported_formats.value());
  }

  // Remove old entries from |supported_formats_cache_| if necessary.
  if (supported_formats_cache_.size() > devices_info.size()) {
    base::EraseIf(supported_formats_cache_, [&devices_info](const auto& entry) {
      return base::ranges::none_of(
          devices_info, [&entry](const VideoCaptureDeviceInfo& info) {
            return entry.first == info.descriptor.device_id;
          });
    });
  }

  // Remove old entries from |controls_cache_| if necessary.
  if (controls_cache_.size() > devices_info.size()) {
    base::EraseIf(controls_cache_, [&devices_info](const auto& entry) {
      return base::ranges::none_of(
          devices_info, [&entry](const VideoCaptureDeviceInfo& info) {
            return entry.first == info.descriptor.device_id;
          });
    });
  }

  VLOG(1) << __func__ << " device info size=" << devices_info.size();
  std::move(callback).Run(std::move(devices_info));
}

absl::optional<VideoCaptureControlSupport>
VideoCaptureDeviceFactoryWebOS::GetSupportedControl(
    const std::string& device_id) {
  auto control_it = controls_cache_.find(device_id);
  if (control_it != controls_cache_.end()) {
    return control_it->second;
  }

  absl::optional<int> camera_handle =
      camera_service_->Open(device_id, "secondary");
  if (!camera_handle) {
    LOG(ERROR) << __func__ << " Failed opening device: " << device_id;
    return absl::nullopt;
  }

  absl::optional<base::Value> properties =
      camera_service_->GetProperties(device_id);
  if (!properties || !properties->is_dict()) {
    LOG(ERROR) << __func__ << " Failed getting properties for: " << device_id;
    return absl::nullopt;
  }

  VideoCaptureControlSupport control_support;
  base::Value::Dict& properties_dict = properties->GetDict();
  control_support.pan =
      IsControlSupported(properties_dict.FindDictByDottedPath(kPan));
  control_support.tilt =
      IsControlSupported(properties_dict.FindDictByDottedPath(kTilt));
  control_support.zoom =
      IsControlSupported(properties_dict.FindDictByDottedPath(kZoom));
  controls_cache_.emplace(device_id, control_support);

  camera_service_->Close(*camera_handle);

  VLOG(1) << __func__ << " pan=" << control_support.pan
          << ", tilt=" << control_support.tilt
          << ", zoom=" << control_support.zoom;

  return control_support;
}

absl::optional<VideoCaptureFormats>
VideoCaptureDeviceFactoryWebOS::GetSupportedFormats(
    const std::string& device_id,
    base::Value* info) {
  auto it = supported_formats_cache_.find(device_id);
  if (it != supported_formats_cache_.end()) {
    return it->second;
  }

  base::Value::Dict* formats =
      info->GetDict().FindDictByDottedPath(kResolution);
  VideoCaptureFormats capture_formats;
  if (formats) {
    SetSupportedFormat(capture_formats, PIXEL_FORMAT_YUY2,
                       formats->FindByDottedPath(kYUV));
    SetSupportedFormat(capture_formats, PIXEL_FORMAT_MJPEG,
                       formats->FindByDottedPath(kJPEG));
    SetSupportedFormat(capture_formats, PIXEL_FORMAT_NV12,
                       formats->FindByDottedPath(kNV12));
    SetSupportedFormat(capture_formats, PIXEL_FORMAT_NV21,
                       formats->FindByDottedPath(kNV21));

    supported_formats_cache_.emplace(device_id, capture_formats);
  }

  VLOG(1) << __func__ << " supported formats size=" << capture_formats.size();

  return capture_formats;
}

void VideoCaptureDeviceFactoryWebOS::SetSupportedFormat(
    VideoCaptureFormats& capture_formats,
    VideoPixelFormat pixel_format,
    base::Value* supported_resolutions) {
  if (!supported_resolutions) {
    VLOG(1) << __func__ << " Empty resolution for: " << pixel_format;
    return;
  }

  for (const auto& item : supported_resolutions->GetList()) {
    const auto& resolution = item.GetString();
    VLOG(2) << __func__ << " " << pixel_format << " [" << resolution << "]";
    if (!resolution.empty()) {
      VideoCaptureFormat capture_format;
      std::string width, height, freq;
      std::stringstream string_stream(resolution);
      std::getline(string_stream, width, ',');
      std::getline(string_stream, height, ',');
      std::getline(string_stream, freq);
      capture_format.pixel_format = pixel_format;

      int num_width = 0;
      int num_height = 0;
      int num_freq = 0;
      int result;
      if (base::StringToInt(width, &result))
        num_width = result;
      if (base::StringToInt(height, &result))
        num_height = result;
      if (base::StringToInt(freq, &result))
        num_freq = result;

      capture_format.frame_size.SetSize(num_width, num_height);
      capture_format.frame_rate = num_freq;
      capture_formats.push_back(capture_format);
    }
  }
}

}  // namespace media
