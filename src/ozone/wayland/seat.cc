// Copyright 2013 The Chromium Authors. All rights reserved.
// Copyright 2013 Intel Corporation. All rights reserved.
// Copyright 2016-2018 LG Electronics, Inc.
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

#include "ozone/wayland/seat.h"

#include "base/logging.h"
#include "ozone/wayland/display.h"
#include "ozone/wayland/input/cursor.h"
#include "ozone/wayland/input/keyboard.h"
#include "ozone/wayland/input/pointer.h"
#include "ozone/wayland/input/touchscreen.h"
#include "ozone/wayland/window.h"

#if defined(OS_WEBOS)
#include "ozone/wayland/input/webos_text_input.h"
#else
#include "ozone/wayland/input/text_input.h"
#endif

#if defined(USE_DATA_DEVICE_MANAGER)
#include "ozone/wayland/data_device.h"
#endif

namespace {
const char kKeyboardSuffix[] = "_keyboard";
const char kPointerSuffix[] = "_pointer";
const char kTouchscreenSuffix[] = "_touch";
}  // namespace

namespace ozonewayland {

WaylandSeat::WaylandSeat(WaylandDisplay* display, uint32_t id)
    : seat_id_(id),
      active_input_window_handle_(0),
      seat_(NULL),
#if defined(USE_DATA_DEVICE_MANAGER)
      data_device_(NULL),
#endif
      input_keyboard_(NULL),
      input_pointer_(NULL),
      input_touch_(NULL),
      text_input_(NULL) {
  static const struct wl_seat_listener kInputSeatListener = {
      WaylandSeat::OnSeatCapabilities,
      WaylandSeat::OnName,
  };

  seat_ = static_cast<wl_seat*>(
      wl_registry_bind(display->registry(), id, &wl_seat_interface, 1));
  DCHECK(seat_);
  wl_seat_add_listener(seat_, &kInputSeatListener, this);
  wl_seat_set_user_data(seat_, this);

#if defined(USE_DATA_DEVICE_MANAGER)
  if (display->GetDataDeviceManager())
    data_device_ = new WaylandDataDevice(display, seat_);
#endif
  text_input_ = new WaylandTextInput(this);
}

WaylandSeat::~WaylandSeat() {
#if defined(USE_DATA_DEVICE_MANAGER)
  if (data_device_ != NULL)
    delete data_device_;
#endif
  delete input_keyboard_;
  delete input_pointer_;
  delete text_input_;
  if (input_touch_ != NULL) {
    delete input_touch_;
  }
  wl_seat_destroy(seat_);
}

void WaylandSeat::OnSeatCapabilities(void* data, wl_seat* seat, uint32_t caps) {
  WaylandSeat* device = static_cast<WaylandSeat*>(data);
  WaylandDisplay* display = WaylandDisplay::GetInstance();
  VLOG(3) << __func__ << " name=" << device->name_ << " caps=" << caps;

  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !device->input_keyboard_) {
    device->input_keyboard_ = new WaylandKeyboard();
    device->input_keyboard_->SetName(device->name_ + kKeyboardSuffix);
    device->input_keyboard_->OnSeatCapabilities(seat, caps);
    display->OnKeyboardAdded(device->input_keyboard_->GetId(),
                             device->input_keyboard_->GetName());
  } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && device->input_keyboard_) {
    display->OnKeyboardRemoved(device->input_keyboard_->GetId());
    delete device->input_keyboard_;
    device->input_keyboard_ = NULL;
  }

  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !device->input_pointer_) {
    device->input_pointer_ = new WaylandPointer();
    device->input_pointer_->OnSeatCapabilities(seat, caps);
    device->input_pointer_->SetName(device->name_ + kPointerSuffix);
    display->OnPointerAdded(device->input_pointer_->GetId(),
                            device->input_pointer_->GetName());
  } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && device->input_pointer_) {
    display->OnPointerRemoved(device->input_pointer_->GetId());
    delete device->input_pointer_;
    device->input_pointer_ = NULL;
  }

  if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !device->input_touch_) {
    device->input_touch_ = new WaylandTouchscreen();
    device->input_touch_->OnSeatCapabilities(seat, caps);
    device->input_touch_->SetName(device->name_ + kTouchscreenSuffix);
    display->OnTouchscreenAdded(device->input_touch_->GetId(),
                                device->input_touch_->GetName());
  } else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && device->input_touch_) {
    display->OnTouchscreenRemoved(device->input_touch_->GetId());
    delete device->input_touch_;
    device->input_touch_ = NULL;
  }
}

void WaylandSeat::OnName(void* data, wl_seat* seat, const char* name) {
  WaylandSeat* device = static_cast<WaylandSeat*>(data);
  device->name_ = name;

  VLOG(3) << __func__ << " name=" << name;

  // A name change implies the need to update all the devices reported
  // to hotplug
  WaylandDisplay* display = WaylandDisplay::GetInstance();
  if (device->input_keyboard_) {
    device->input_keyboard_->SetName(device->name_ + kKeyboardSuffix);
    display->OnKeyboardRemoved(device->input_keyboard_->GetId());
    display->OnKeyboardAdded(device->input_keyboard_->GetId(),
                             device->input_keyboard_->GetName());
  }
  if (device->input_pointer_) {
    device->input_pointer_->SetName(device->name_ + kPointerSuffix);
    display->OnPointerRemoved(device->input_pointer_->GetId());
    display->OnPointerAdded(device->input_pointer_->GetId(),
                            device->input_pointer_->GetName());
  }
  if (device->input_touch_) {
    device->input_touch_->SetName(device->name_ + kTouchscreenSuffix);
    display->OnTouchscreenRemoved(device->input_touch_->GetId());
    display->OnTouchscreenAdded(device->input_touch_->GetId(),
                                device->input_touch_->GetName());
  }
}

void WaylandSeat::SetEnteredWindowHandle(uint32_t device_id,
                                         unsigned windowhandle) {
  entered_window_handle_map_[device_id] = windowhandle;
}

unsigned WaylandSeat::GetEnteredWindowHandle(uint32_t device_id) const {
  if (entered_window_handle_map_.find(device_id) !=
      entered_window_handle_map_.end()) {
    return entered_window_handle_map_.at(device_id);
  }
  return 0;
}

void WaylandSeat::SetActiveInputWindow(unsigned windowhandle) {
  active_input_window_handle_ = windowhandle;
  WaylandWindow* window = nullptr;
  if (windowhandle) {
    window = WaylandDisplay::GetInstance()->GetWindow(windowhandle);
    if (!window)
      return;
  }
  text_input_->SetActiveWindow(window);
}

void WaylandSeat::ResetEnteredWindowHandle(unsigned window_handle) {
  for (auto& entered_window_handle_item : entered_window_handle_map_) {
    if (entered_window_handle_item.second == window_handle) {
      entered_window_handle_item.second = 0;
    }
  }
}

void WaylandSeat::SetGrabWindow(uint32_t device_id,
                                const GrabWindowInfo& grab_window) {
  grab_window_map_[device_id] = grab_window;
}

unsigned WaylandSeat::GetGrabWindowHandle(uint32_t device_id) const {
  if (grab_window_map_.find(device_id) != grab_window_map_.end()) {
    return grab_window_map_.at(device_id).grab_window_handle;
  }
  return 0;
}

uint32_t WaylandSeat::GetGrabButton(uint32_t device_id) const {
  if (grab_window_map_.find(device_id) != grab_window_map_.end()) {
    return grab_window_map_.at(device_id).grab_button;
  }
  return 0;
}

void WaylandSeat::ResetGrabWindow(unsigned window_handle) {
  for (auto& grab_window_item : grab_window_map_) {
    if (grab_window_item.second.grab_window_handle == window_handle) {
      grab_window_item.second.grab_window_handle = 0;
      grab_window_item.second.grab_button = 0;
    }
  }
}

void WaylandSeat::SetCursorBitmap(const std::vector<SkBitmap>& bitmaps,
                                  const gfx::Point& location) {
  if (!input_pointer_) {
    LOG(WARNING) << "Tried to change cursor without input configured";
    return;
  }
  input_pointer_->Cursor()->UpdateBitmap(
      bitmaps, location, WaylandDisplay::GetInstance()->GetSerial());
}

void WaylandSeat::MoveCursor(const gfx::Point& location) {
  if (!input_pointer_) {
    LOG(WARNING) << "Tried to move cursor without input configured";
    return;
  }

  input_pointer_->Cursor()->MoveCursor(
      location, WaylandDisplay::GetInstance()->GetSerial());
}

void WaylandSeat::ResetIme(unsigned handle) {
#if defined(OS_WEBOS)
  text_input_->ResetIme(handle);
#else
  text_input_->ResetIme();
#endif
}

void WaylandSeat::ImeCaretBoundsChanged(gfx::Rect rect) {
  NOTIMPLEMENTED();
}

void WaylandSeat::ShowInputPanel(unsigned handle) {
  text_input_->ShowInputPanel(seat_, handle);
}

void WaylandSeat::HideInputPanel(unsigned handle,
                                 ui::ImeHiddenType hidden_type) {
  text_input_->HideInputPanel(seat_, handle, hidden_type);
}

void WaylandSeat::SetTextInputInfo(const ui::TextInputInfo& text_input_info,
                                   unsigned handle) {
#if defined(OS_WEBOS)
  text_input_->SetTextInputInfo(text_input_info, handle);
#endif
}

void WaylandSeat::SetSurroundingText(unsigned handle,
                                     const std::string& text,
                                     size_t cursor_position,
                                     size_t anchor_position) {
#if defined(OS_WEBOS)
  text_input_->SetSurroundingText(handle, text, cursor_position,
                                  anchor_position);
#endif
}

}  // namespace ozonewayland
