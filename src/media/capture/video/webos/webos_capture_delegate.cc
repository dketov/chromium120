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

#include "media/capture/video/webos/webos_capture_delegate.h"

#include "base/task/bind_post_task.h"
#include "media/capture/mojom/image_capture_types.h"
#include "media/capture/video/blob_utils.h"
#include "media/capture/video/webos/webos_camera_constants.h"
#include "media/capture/video/webos/webos_camera_service.h"

namespace media {

namespace {

std::string PixelFormatToString(VideoPixelFormat format) {
  switch (format) {
    case PIXEL_FORMAT_MJPEG:
      return std::string("JPEG");
    case PIXEL_FORMAT_YUY2:
      return std::string("YUV");
    case PIXEL_FORMAT_NV12:
      return std::string("NV12");
    case PIXEL_FORMAT_NV21:
      return std::string("NV21");
    default:
      LOG(WARNING) << __func__
                   << " Unsupported: " << VideoPixelFormatToString(format);
  }
  return std::string();
}

mojom::RangePtr MojoRangeFromValue(base::Value::Dict* property) {
  if (!property)
    return mojom::Range::New();

  absl::optional<double> max, min, current, step;
  max = property->FindDoubleByDottedPath(kMax);
  min = property->FindDoubleByDottedPath(kMin);
  current = property->FindDoubleByDottedPath(kValue);
  step = property->FindDoubleByDottedPath(kStep);

  if (max && min && current && step) {
    mojom::RangePtr capability = mojom::Range::New();
    capability->max = *max;
    capability->min = *min;
    capability->current = *current;
    capability->step = *step;
    return capability;
  }

  return mojom::Range::New();
}

}  // namespace

#define BIND_POST_TASK_TO_CURRENT_LOOP(function) \
  (base::BindPostTaskToCurrentDefault(           \
      base::BindRepeating(function, weak_this_)))

WebOSCaptureDelegate::WebOSCaptureDelegate(
    scoped_refptr<WebOSCameraService> camera_service,
    const VideoCaptureDeviceDescriptor& device_descriptor,
    const scoped_refptr<base::SingleThreadTaskRunner>& capture_task_runner,
    int power_line_frequency,
    int rotation)
    : camera_service_(camera_service),
      capture_task_runner_(capture_task_runner),
      device_descriptor_(device_descriptor),
      power_line_frequency_(power_line_frequency) {
  VLOG(1) << __func__ << " this[" << this << "]";

  weak_this_ = weak_factory_.GetWeakPtr();

  camera_service_->SubscribeFaultEvent(BIND_POST_TASK_TO_CURRENT_LOOP(
      &WebOSCaptureDelegate::OnFaultEventOccured));

  camera_service_->SubscribeCameraChange(BIND_POST_TASK_TO_CURRENT_LOOP(
      &WebOSCaptureDelegate::OnCameraListUpdated));
}

WebOSCaptureDelegate::~WebOSCaptureDelegate() {
  VLOG(1) << __func__ << " this[" << this << "]";
}

void WebOSCaptureDelegate::AllocateAndStart(
    base::PlatformThreadId pid,
    gfx::Size frame_size,
    float frame_rate,
    std::unique_ptr<VideoCaptureDevice::Client> client) {
  VLOG(1) << __func__ << " width=" << frame_size.width()
          << ", height=" << frame_size.height() << ", frame_rate=" << frame_rate
          << ", pid=" << pid;

  DCHECK(capture_task_runner_->BelongsToCurrentThread());
  DCHECK(client);

  client_ = std::move(client);

  const auto& device_id = device_descriptor_.device_id;
  camera_handle_ = camera_service_->Open(pid, device_id, "primary");
  if (!camera_handle_) {
    std::string reason = std::string(" Failed opening device:") + device_id;
    SetErrorState(VideoCaptureError::kV4L2FailedToOpenV4L2DeviceDriverFile,
                  FROM_HERE, reason);
    return;
  }

  VideoPixelFormat pixel_format = GetPixelFormat(frame_size, frame_rate);
  if (pixel_format == PIXEL_FORMAT_UNKNOWN) {
    SetErrorState(VideoCaptureError::kV4L2UnsupportedPixelFormat, FROM_HERE,
                  "Unsupported pixel format");
    return;
  }

  capture_format_.frame_size = frame_size;
  capture_format_.frame_rate = frame_rate;
  capture_format_.pixel_format = pixel_format;

  base::Value::Dict propert_value;
  propert_value.Set(kFrequency, power_line_frequency_);

  if (!camera_service_->SetProperties(*camera_handle_, propert_value)) {
    SetErrorState(VideoCaptureError::kV4L2FailedToSetVideoCaptureFormat,
                  FROM_HERE, "Failed setting power line frequency");
    return;
  }

  std::string format = PixelFormatToString(pixel_format);
  if (!camera_service_->SetFormat(
          *camera_handle_, capture_format_.frame_size.width(),
          capture_format_.frame_size.height(), format,
          base::saturated_cast<int>(capture_format_.frame_rate))) {
    SetErrorState(VideoCaptureError::kV4L2FailedToSetVideoCaptureFormat,
                  FROM_HERE, "Failed setting format and size");
    return;
  }

  shmem_key_ = camera_service_->StartCamera(*camera_handle_);
  if (!shmem_key_) {
    SetErrorState(VideoCaptureError::kV4L2ThisIsNotAV4L2VideoCaptureDevice,
                  FROM_HERE, "Failed to start camera preview");
    return;
  }

  if (!camera_service_->OpenCameraBuffer(*shmem_key_)) {
    SetErrorState(VideoCaptureError::kV4L2ThisIsNotAV4L2VideoCaptureDevice,
                  FROM_HERE, "Failed to open camera buffer capture");
    return;
  }

  is_capturing_ = true;

  if (client_)
    client_->OnStarted();

  first_ref_time_ = base::TimeTicks();

  // Post task to start fetching frames from camera service.
  capture_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&WebOSCaptureDelegate::DoCapture, weak_this_));
}

void WebOSCaptureDelegate::StopAndDeAllocate(base::PlatformThreadId pid) {
  VLOG(1) << __func__;
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  camera_service_->CloseCameraBuffer();

  is_capturing_ = false;
  if (camera_handle_) {
    camera_service_->StopCamera(*camera_handle_);
    camera_service_->Close(pid, *camera_handle_);
    camera_handle_.reset();
  }
}

void WebOSCaptureDelegate::TakePhoto(
    VideoCaptureDevice::TakePhotoCallback callback) {
  VLOG(1) << __func__;
  DCHECK(capture_task_runner_->BelongsToCurrentThread());
  take_photo_callbacks_.push(std::move(callback));
}

void WebOSCaptureDelegate::GetPhotoState(
    VideoCaptureDevice::GetPhotoStateCallback callback) {
  VLOG(1) << __func__;
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  mojom::PhotoStatePtr photo_capabilities = mojo::CreateEmptyPhotoState();
  const auto& device_id = device_descriptor_.device_id;
  absl::optional<base::Value> properties =
      camera_service_->GetProperties(device_id);
  if (!properties || !properties->is_dict()) {
    LOG(ERROR) << __func__ << " Failed to get properties.";
    std::move(callback).Run(std::move(photo_capabilities));
    return;
  }

  base::Value::Dict& properties_dict = properties->GetDict();
  photo_capabilities->pan =
      MojoRangeFromValue(properties_dict.FindDictByDottedPath(kPan));
  photo_capabilities->tilt =
      MojoRangeFromValue(properties_dict.FindDictByDottedPath(kTilt));
  photo_capabilities->zoom =
      MojoRangeFromValue(properties_dict.FindDictByDottedPath(kZoom));
  photo_capabilities->color_temperature = MojoRangeFromValue(
      properties_dict.FindDictByDottedPath(kWhiteBalanceTemperature));
  photo_capabilities->brightness =
      MojoRangeFromValue(properties_dict.FindDictByDottedPath(kBrightness));
  photo_capabilities->contrast =
      MojoRangeFromValue(properties_dict.FindDictByDottedPath(kContrast));
  photo_capabilities->saturation =
      MojoRangeFromValue(properties_dict.FindDictByDottedPath(kSaturation));
  photo_capabilities->sharpness =
      MojoRangeFromValue(properties_dict.FindDictByDottedPath(kSharpness));

  photo_capabilities->current_focus_mode = media::mojom::MeteringMode::NONE;
  photo_capabilities->current_exposure_mode = media::mojom::MeteringMode::NONE;
  photo_capabilities->current_white_balance_mode =
      media::mojom::MeteringMode::NONE;
  if (photo_capabilities->color_temperature) {
    photo_capabilities->supported_white_balance_modes.push_back(
        media::mojom::MeteringMode::MANUAL);
    photo_capabilities->supported_white_balance_modes.push_back(
        media::mojom::MeteringMode::CONTINUOUS);
  }

  photo_capabilities->height = mojom::Range::New(
      capture_format_.frame_size.height(), capture_format_.frame_size.height(),
      capture_format_.frame_size.height(), 0 /* step */);
  photo_capabilities->width = mojom::Range::New(
      capture_format_.frame_size.width(), capture_format_.frame_size.width(),
      capture_format_.frame_size.width(), 0 /* step */);
  photo_capabilities->red_eye_reduction = mojom::RedEyeReduction::NEVER;
  photo_capabilities->torch = false;

  std::move(callback).Run(std::move(photo_capabilities));
}

void WebOSCaptureDelegate::SetPhotoOptions(
    mojom::PhotoSettingsPtr settings,
    VideoCaptureDevice::SetPhotoOptionsCallback callback) {
  VLOG(1) << __func__;
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  base::Value::Dict propert_value;
  if (settings->has_pan) {
    propert_value.Set(kPan,
                      base::Value(base::saturated_cast<int>(settings->pan)));
  }
  if (settings->has_tilt) {
    propert_value.Set(kTilt,
                      base::Value(base::saturated_cast<int>(settings->tilt)));
  }
  if (settings->has_zoom) {
    propert_value.Set(kZoom,
                      base::Value(base::saturated_cast<int>(settings->zoom)));
  }
  if (settings->has_color_temperature) {
    propert_value.Set(
        kWhiteBalanceTemperature,
        base::Value(base::saturated_cast<int>(settings->color_temperature)));
  }
  if (settings->has_brightness) {
    propert_value.Set(
        kBrightness,
        base::Value(base::saturated_cast<int>(settings->brightness)));
  }
  if (settings->has_contrast) {
    propert_value.Set(
        kContrast, base::Value(base::saturated_cast<int>(settings->contrast)));
  }
  if (settings->has_saturation) {
    propert_value.Set(
        kSaturation,
        base::Value(base::saturated_cast<int>(settings->saturation)));
  }
  if (settings->has_sharpness) {
    propert_value.Set(
        kSharpness,
        base::Value(base::saturated_cast<int>(settings->sharpness)));
  }

  if (settings->has_white_balance_mode &&
      (settings->white_balance_mode == mojom::MeteringMode::CONTINUOUS ||
       settings->white_balance_mode == mojom::MeteringMode::MANUAL)) {
    propert_value.Set(kAutoWhiteBalance,
                      base::Value((settings->white_balance_mode ==
                                   mojom::MeteringMode::CONTINUOUS)));
  }

  if (camera_handle_)
    camera_service_->SetProperties(*camera_handle_, propert_value);

  std::move(callback).Run(true);
}

void WebOSCaptureDelegate::SetRotation(int rotation) {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());
  DCHECK_GE(rotation, 0);
  DCHECK_LT(rotation, 360);
  DCHECK_EQ(rotation % 90, 0);
  rotation_ = rotation;
}

base::WeakPtr<WebOSCaptureDelegate> WebOSCaptureDelegate::GetWeakPtr() {
  return weak_this_;
}

void WebOSCaptureDelegate::DoCapture() {
  VLOG(2) << __func__;
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  if (!is_capturing_)
    return;

  if (!shmem_key_) {
    LOG(ERROR) << __func__ << ", return, fail to get shmem_key_";
    return;
  }

  base::span<uint8_t> cameraBuffer = camera_service_->ReadCameraBuffer();
  if (cameraBuffer.empty()) {
    SetErrorState(VideoCaptureError::kV4L2FailedToDequeueCaptureBuffer,
                  FROM_HERE, "Failed to read buffer from camera service");
    return;
  }

  uint8_t* buffer = cameraBuffer.data();
  int buffer_size = cameraBuffer.size();

  VLOG(2) << __func__ << " buffer_size[" << buffer_size << "]";
  if (buffer_size > 0) {
    const base::TimeTicks now = base::TimeTicks::Now();
    if (first_ref_time_.is_null())
      first_ref_time_ = now;
    const base::TimeDelta timestamp = now - first_ref_time_;

    if (client_)
      client_->OnIncomingCapturedData(buffer, buffer_size, capture_format_,
                                      gfx::ColorSpace(), rotation_, false, now,
                                      timestamp);

    frames_per_sec_++;
    base::TimeDelta time_past = now - start_time_;
    if (time_past >= base::Seconds(1)) {
      VLOG(3) << __func__ << " camera_frames_per_sec: " << frames_per_sec_;
      start_time_ = now;
      frames_per_sec_ = 0;
    }

    while (!take_photo_callbacks_.empty()) {
      VideoCaptureDevice::TakePhotoCallback cb =
          std::move(take_photo_callbacks_.front());
      take_photo_callbacks_.pop();

      mojom::BlobPtr blob =
          RotateAndBlobify(buffer, buffer_size, capture_format_, rotation_);
      if (blob)
        std::move(cb).Run(std::move(blob));
    }
  }

  capture_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&WebOSCaptureDelegate::DoCapture, weak_this_));
}

void WebOSCaptureDelegate::SetErrorState(VideoCaptureError error,
                                         const base::Location& from_here,
                                         const std::string& reason) {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  LOG(ERROR) << __func__ << " reason: " << reason;
  if (!client_)
    return;

  is_capturing_ = false;
  client_->OnError(error, from_here, reason);
}

VideoPixelFormat WebOSCaptureDelegate::GetPixelFormat(gfx::Size frame_size,
                                                      int frame_rate) {
  VLOG(1) << __func__ << " width=" << frame_size.width()
          << ", height=" << frame_size.height()
          << ", frame_rate=" << frame_rate;

  bool support_yuv = false;
  bool support_jpeg = false;
  bool support_nv12 = false;
  bool support_nv21 = false;

  std::ostringstream resolution;
  resolution << frame_size.width() << "," << frame_size.height() << ","
             << frame_rate;

  base::Value info;
  const std::string device_id = device_descriptor_.device_id;
  if (!camera_service_->GetDeviceInfo(device_id, &info) || !info.is_dict()) {
    LOG(ERROR) << __func__ << " Failed to get info.";
    return PIXEL_FORMAT_UNKNOWN;
  }

  base::Value::Dict& info_dict = info.GetDict();
  if (base::Value::Dict* formats =
          info_dict.FindDictByDottedPath(kResolution)) {
    support_yuv = IsResolutionSupported(resolution.str(),
                                        formats->FindByDottedPath(kYUV));
    support_jpeg = IsResolutionSupported(resolution.str(),
                                         formats->FindByDottedPath(kJPEG));
    support_nv12 = IsResolutionSupported(resolution.str(),
                                         formats->FindByDottedPath(kNV12));
    support_nv21 = IsResolutionSupported(resolution.str(),
                                         formats->FindByDottedPath(kNV21));
  }

  VideoPixelFormat format = PIXEL_FORMAT_UNKNOWN;
  if (support_yuv && support_jpeg && !support_nv12) {
    format = (frame_size.width() * frame_size.height() > 640 * 480)
                 ? PIXEL_FORMAT_MJPEG
                 : PIXEL_FORMAT_YUY2;
  } else if (support_nv12) {
    format = PIXEL_FORMAT_NV12;
  } else if (support_jpeg) {
    format = PIXEL_FORMAT_MJPEG;
  } else if (support_yuv) {
    format = PIXEL_FORMAT_YUY2;
  } else if (support_nv21) {
    format = PIXEL_FORMAT_NV21;
  } else {
    LOG(WARNING) << __func__ << ", supported format not found";
  }

  VLOG(1) << __func__ << ", format: " << VideoPixelFormatToString(format);
  return format;
}

bool WebOSCaptureDelegate::IsResolutionSupported(const std::string& resolution,
                                                 base::Value* resolution_list) {
  if (!resolution_list)
    return false;

  for (const auto& item : resolution_list->GetList()) {
    const auto& format_res = item.GetString();
    VLOG(2) << __func__ << " [" << resolution << "]";
    if (!format_res.empty() && format_res == resolution) {
      return true;
    }
  }

  return false;
}

void WebOSCaptureDelegate::OnCameraListUpdated(const std::string& response) {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  VLOG(1) << __func__ << " response=" << response;
  if (device_descriptor_.device_id.empty())
    return;

  absl::optional<base::Value::Dict> root_value =
      camera_service_->GetRootDictionary(response);
  if (!root_value)
    return;

  base::Value* device_list = root_value->FindByDottedPath(kDeviceList);
  if (!device_list) {
    LOG(ERROR) << __func__ << " Did not receive device list: " << response;
    return;
  }

  WebOSCameraDeviceIdList device_ids;
  for (auto& device_entry : device_list->GetList()) {
    if (!device_entry.is_dict()){
      continue;
    }
    std::string* device_id = device_entry.GetDict().FindStringByDottedPath(kId);
    if (device_id && !device_id->empty())
      device_ids.push_back(*device_id);
  }

  auto it = std::find(device_ids.begin(), device_ids.end(),
                      device_descriptor_.device_id);
  if (it == device_ids.end()) {
    SetErrorState(VideoCaptureError::kV4L2PollFailed, FROM_HERE,
                  "getCameraList Camera Service Error");
  }
}

void WebOSCaptureDelegate::OnFaultEventOccured(const std::string& response) {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  VLOG(1) << __func__ << " response=" << response;
  if (device_descriptor_.device_id.empty())
    return;

  absl::optional<base::Value::Dict> root_value =
      camera_service_->GetRootDictionary(response);
  if (!root_value)
    return;

  std::string* event_type = root_value->FindStringByDottedPath(kEventType);
  if (event_type && event_type->find("preview_fault") != std::string::npos) {
    std::string* device_id = root_value->FindStringByDottedPath(kId);
    if (device_id && !device_id->compare(device_descriptor_.device_id)) {
      SetErrorState(
          VideoCaptureError::kV4L2PollFailed, FROM_HERE,
          std::string("getEventNotification Camera Service Error:") + response);
    }
  }
}

}  // namespace media
