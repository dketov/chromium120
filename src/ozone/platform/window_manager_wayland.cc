// Copyright 2014 Intel Corporation. All rights reserved.
// Copyright 2016-2020 LG Electronics, Inc.
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

#include "ozone/platform/window_manager_wayland.h"

#include <string>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/unsafe_shared_memory_region.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "ozone/platform/desktop_platform_screen_delegate.h"
#include "ozone/platform/messages.h"
#include "ozone/platform/ozone_gpu_platform_support_host.h"
#include "ozone/platform/ozone_wayland_window.h"
#include "ozone/wayland/ozone_wayland_screen.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/events/event_switches.h"
#include "ui/events/event_utils.h"
#include "ui/events/ozone/evdev/touch_evdev_types.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/platform_window/platform_window_delegate.h"

namespace ui {

namespace {
const int kDefaultMaxTouchPoints = 1;
}

// TODO(M120): Replace base::DoNothing() with an actual callback.
WindowManagerWayland::WindowManagerWayland(OzoneGpuPlatformSupportHost* proxy)
    : proxy_(proxy),
      keyboard_(KeyboardEvdevNeva::Create(
          &modifiers_,
          KeyboardLayoutEngineManager::GetKeyboardLayoutEngine(),
          base::BindRepeating(&WindowManagerWayland::PostUiEvent,
                              base::Unretained(this)),
          base::DoNothing())),
      platform_screen_(NULL),
      dragging_(false),
      touch_slot_generator_(0),
      weak_ptr_factory_(this) {
  proxy_->RegisterHandler(this);
}

WindowManagerWayland::~WindowManagerWayland() {
}

ui::DeviceHotplugEventObserver*
WindowManagerWayland::GetHotplugEventObserver() {
  return ui::DeviceDataManager::GetInstance();
}

void WindowManagerWayland::OnRootWindowCreated(
    OzoneWaylandWindow* window) {
  open_windows(window->GetDisplayId()).push_back(window);
}

void WindowManagerWayland::OnRootWindowClosed(
    OzoneWaylandWindow* window) {
  const std::string& display_id = window->GetDisplayId();
  std::list<OzoneWaylandWindow*>& windows = open_windows(display_id);
  windows.remove(window);
  if (window && GetActiveWindow(display_id) == window) {
    active_window_map_[display_id] = nullptr;
    if (!windows.empty())
      OnActivationChanged(windows.front()->GetHandle(), true);
  }

  if (event_grabber_ == gfx::AcceleratedWidget(window->GetHandle()))
    event_grabber_ = gfx::kNullAcceleratedWidget;

  if (current_capture_ == gfx::AcceleratedWidget(window->GetHandle())) {
    GetWindow(current_capture_)->GetDelegate()->OnLostCapture();
    current_capture_ = gfx::kNullAcceleratedWidget;
  }

  if (windows.empty()) {
    clear_open_windows(display_id);
    return;
  }
}

void WindowManagerWayland::OnRootWindowDisplayChanged(
    const std::string& prev_display_id,
    const std::string& new_display_id,
    OzoneWaylandWindow* window) {
  // Remove the window from previous display window list
  open_windows(prev_display_id).remove(window);
  // Add the window to his new diplay window list
  open_windows(new_display_id).push_back(window);
}

void WindowManagerWayland::Restore(OzoneWaylandWindow* window) {
  if (window) {
    active_window_map_[window->GetDisplayId()] = window;
    event_grabber_ = window->GetHandle();
  }
}

void WindowManagerWayland::OnPlatformScreenCreated(
      ozonewayland::OzoneWaylandScreen* screen) {
  DCHECK(!platform_screen_);
  platform_screen_ = screen;
}

scoped_refptr<PlatformCursor> WindowManagerWayland::GetPlatformCursor() {
  return platform_cursor_;
}

void WindowManagerWayland::SetPlatformCursor(
    scoped_refptr<PlatformCursor> cursor) {
  platform_cursor_ = cursor;
}

bool WindowManagerWayland::HasWindowsOpen() const {
  return !open_windows_map_.empty();
}

OzoneWaylandWindow* WindowManagerWayland::GetActiveWindow(
    const std::string& display_id) const {
  if (active_window_map_.find(display_id) != active_window_map_.end())
    return active_window_map_.at(display_id);
  return nullptr;
}

void WindowManagerWayland::GrabEvents(gfx::AcceleratedWidget widget) {
  if (current_capture_ == widget)
    return;

  if (current_capture_) {
    OzoneWaylandWindow* window = GetWindow(current_capture_);
    window->GetDelegate()->OnLostCapture();
  }

  current_capture_ = widget;
  event_grabber_ = widget;
}

void WindowManagerWayland::UngrabEvents(gfx::AcceleratedWidget widget) {
  if (current_capture_ != widget)
    return;

  if (current_capture_) {
    OzoneWaylandWindow* window = GetWindow(current_capture_);
    if (window) {
      OzoneWaylandWindow* active_window =
          GetActiveWindow(window->GetDisplayId());
      current_capture_ = gfx::kNullAcceleratedWidget;
      event_grabber_ = active_window ? active_window->GetHandle() : 0;
    }
  }
}

OzoneWaylandWindow*
WindowManagerWayland::GetWindow(unsigned handle) {
  for (const auto& kv : open_windows_map_) {
    for (auto* const window : *(kv.second)) {
      if (window->GetHandle() == handle)
        return window;
    }
  }
  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// WindowManagerWayland, Private implementation:
void WindowManagerWayland::OnActivationChanged(unsigned windowhandle,
                                               bool active) {
  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Invalid window handle " << windowhandle;
    return;
  }

  OzoneWaylandWindow* active_window = GetActiveWindow(window->GetDisplayId());

  if (active) {
    event_grabber_ = windowhandle;
    if (current_capture_)
      return;

    if (active_window && active_window == window)
      return;

    if (active_window)
      active_window->GetDelegate()->OnActivationChanged(false);

    active_window_map_[window->GetDisplayId()] = window;
    window->GetDelegate()->OnActivationChanged(active);
  } else if (active_window == window) {
    active_window->GetDelegate()->OnActivationChanged(active);
    if (event_grabber_ == gfx::AcceleratedWidget(active_window->GetHandle()))
      event_grabber_ = gfx::kNullAcceleratedWidget;

    active_window_map_[window->GetDisplayId()] = nullptr;
  }
}

std::list<OzoneWaylandWindow*>& WindowManagerWayland::open_windows(
    const std::string& display_id) {
  if (open_windows_map_.find(display_id) == open_windows_map_.end())
    open_windows_map_[display_id] = new std::list<OzoneWaylandWindow*>();
  return *open_windows_map_[display_id];
}
void WindowManagerWayland::clear_open_windows(const std::string& display_id) {
  if (open_windows_map_.find(display_id) != open_windows_map_.end()) {
    delete open_windows_map_[display_id];
    open_windows_map_.erase(display_id);
  }
}

void WindowManagerWayland::OnWindowFocused(unsigned handle) {
  OnActivationChanged(handle, true);
}

void WindowManagerWayland::OnWindowEnter(unsigned handle) {
  OnWindowFocused(handle);
}

void WindowManagerWayland::OnWindowLeave(unsigned handle) {
}

void WindowManagerWayland::OnWindowClose(unsigned handle) {
  OzoneWaylandWindow* window = GetWindow(handle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << handle
               << " from GPU process";
    return;
  }

  window->GetDelegate()->OnCloseRequest();
}

void WindowManagerWayland::OnWindowResized(unsigned handle,
                                           unsigned width,
                                           unsigned height) {
  OzoneWaylandWindow* window = GetWindow(handle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << handle
               << " from GPU process";
    return;
  }

  if (!window->GetResizeEnabled())
    return;

  const gfx::Rect& current_bounds = window->GetBoundsInPixels();
  window->SetBoundsInPixels(
      gfx::Rect(current_bounds.x(), current_bounds.y(), width, height));
}

void WindowManagerWayland::OnWindowUnminimized(unsigned handle) {
  OzoneWaylandWindow* window = GetWindow(handle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << handle
               << " from GPU process";
    return;
  }

  window->GetDelegate()->OnWindowStateChanged(PlatformWindowState::kMinimized,
                                              PlatformWindowState::kMaximized);
}

void WindowManagerWayland::OnWindowDeActivated(unsigned windowhandle) {
  OnActivationChanged(windowhandle, false);
}

void WindowManagerWayland::OnWindowActivated(unsigned windowhandle) {
  OnWindowFocused(windowhandle);
}

////////////////////////////////////////////////////////////////////////////////
// GpuPlatformSupportHost implementation:
void WindowManagerWayland::OnGpuProcessLaunched(
    int host_id,
    base::RepeatingCallback<void(IPC::Message*)> sender) {}

void WindowManagerWayland::OnChannelDestroyed(int host_id) {
}

void WindowManagerWayland::OnGpuServiceLaunched(
    int host_id,
    GpuHostBindInterfaceCallback binder,
    GpuHostTerminateCallback terminate_callback) {}

void WindowManagerWayland::OnMessageReceived(const IPC::Message& message) {
  IPC_BEGIN_MESSAGE_MAP(WindowManagerWayland, message)
  IPC_MESSAGE_HANDLER(WaylandInput_CloseWidget, CloseWidget)
  IPC_MESSAGE_HANDLER(WaylandWindow_Resized, WindowResized)
  IPC_MESSAGE_HANDLER(WaylandWindow_Activated, WindowActivated)
  IPC_MESSAGE_HANDLER(WaylandWindow_DeActivated, WindowDeActivated)
  IPC_MESSAGE_HANDLER(WaylandWindow_Unminimized, WindowUnminimized)
  IPC_MESSAGE_HANDLER(WaylandInput_MotionNotify, MotionNotify)
  IPC_MESSAGE_HANDLER(WaylandInput_ButtonNotify, ButtonNotify)
  IPC_MESSAGE_HANDLER(WaylandInput_TouchNotify, TouchNotify)
  IPC_MESSAGE_HANDLER(WaylandInput_AxisNotify, AxisNotify)
  IPC_MESSAGE_HANDLER(WaylandInput_PointerEnter, PointerEnter)
  IPC_MESSAGE_HANDLER(WaylandInput_PointerLeave, PointerLeave)
  IPC_MESSAGE_HANDLER(WaylandInput_InputPanelEnter, InputPanelEnter)
  IPC_MESSAGE_HANDLER(WaylandInput_InputPanelLeave, InputPanelLeave)
  IPC_MESSAGE_HANDLER(WaylandInput_KeyboardEnter, KeyboardEnter)
  IPC_MESSAGE_HANDLER(WaylandInput_KeyboardLeave, KeyboardLeave)
  IPC_MESSAGE_HANDLER(WaylandInput_KeyNotify, KeyNotify)
  IPC_MESSAGE_HANDLER(WaylandInput_VirtualKeyNotify, VirtualKeyNotify)
  IPC_MESSAGE_HANDLER(WaylandOutput_ScreenChanged, ScreenChanged)
  IPC_MESSAGE_HANDLER(WaylandInput_InitializeXKB, InitializeXKB)
  IPC_MESSAGE_HANDLER(WaylandInput_DragEnter, DragEnter)
  IPC_MESSAGE_HANDLER(WaylandInput_DragData, DragData)
  IPC_MESSAGE_HANDLER(WaylandInput_DragLeave, DragLeave)
  IPC_MESSAGE_HANDLER(WaylandInput_DragMotion, DragMotion)
  IPC_MESSAGE_HANDLER(WaylandInput_DragDrop, DragDrop)
  IPC_MESSAGE_HANDLER(WaylandInput_InputPanelVisibilityChanged, InputPanelVisibilityChanged)
  IPC_MESSAGE_HANDLER(WaylandInput_InputPanelRectChanged, InputPanelRectChanged)
  IPC_MESSAGE_HANDLER(WaylandWindow_Close, WindowClose)
  IPC_MESSAGE_HANDLER(WaylandWindow_Exposed, NativeWindowExposed)
  IPC_MESSAGE_HANDLER(WaylandWindow_StateChanged, NativeWindowStateChanged)
  IPC_MESSAGE_HANDLER(WaylandWindow_StateAboutToChange, NativeWindowStateAboutToChange)
  IPC_MESSAGE_HANDLER(WaylandInput_CursorVisibilityChanged, CursorVisibilityChanged)
  IPC_MESSAGE_HANDLER(WaylandInput_KeyboardAdded, KeyboardAdded)
  IPC_MESSAGE_HANDLER(WaylandInput_KeyboardRemoved, KeyboardRemoved)
  IPC_MESSAGE_HANDLER(WaylandInput_PointerAdded, PointerAdded)
  IPC_MESSAGE_HANDLER(WaylandInput_PointerRemoved, PointerRemoved)
  IPC_MESSAGE_HANDLER(WaylandInput_TouchscreenAdded, TouchscreenAdded)
  IPC_MESSAGE_HANDLER(WaylandInput_TouchscreenRemoved, TouchscreenRemoved)
  IPC_END_MESSAGE_MAP()
}

void WindowManagerWayland::MotionNotify(float x, float y) {
  if (dragging_) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyDragging,
                                weak_ptr_factory_.GetWeakPtr(), x, y));
  } else {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyMotion,
                                weak_ptr_factory_.GetWeakPtr(), x, y));
  }
}

void WindowManagerWayland::ButtonNotify(unsigned handle,
                                        EventType type,
                                        EventFlags flags,
                                        float x,
                                        float y) {
  dragging_ =
      ((type == ui::ET_MOUSE_PRESSED) && (flags == ui::EF_LEFT_MOUSE_BUTTON));

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyButtonPress,
                                weak_ptr_factory_.GetWeakPtr(), handle, type,
                                flags, x, y));
}

void WindowManagerWayland::AxisNotify(float x,
                                      float y,
                                      int xoffset,
                                      int yoffset) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyAxis,
                     weak_ptr_factory_.GetWeakPtr(), x, y, xoffset, yoffset));
}

void WindowManagerWayland::PointerEnter(uint32_t device_id,
                                        unsigned handle,
                                        float x,
                                        float y) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyPointerEnter,
                     weak_ptr_factory_.GetWeakPtr(), device_id, handle, x, y));
}

void WindowManagerWayland::PointerLeave(uint32_t device_id,
                                        unsigned handle,
                                        float x,
                                        float y) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyPointerLeave,
                     weak_ptr_factory_.GetWeakPtr(), device_id, handle, x, y));
}

void WindowManagerWayland::InputPanelEnter(uint32_t device_id,
                                           unsigned handle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyInputPanelEnter,
                     weak_ptr_factory_.GetWeakPtr(), device_id, handle));
}

void WindowManagerWayland::InputPanelLeave(uint32_t device_id) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyInputPanelLeave,
                                weak_ptr_factory_.GetWeakPtr(), device_id));
}

void WindowManagerWayland::KeyNotify(EventType type,
                                     unsigned code,
                                     int device_id) {
  VirtualKeyNotify(type, code, device_id);
}

void WindowManagerWayland::VirtualKeyNotify(EventType type,
                                            uint32_t key,
                                            int device_id) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::OnVirtualKeyNotify,
                     weak_ptr_factory_.GetWeakPtr(), type, key, device_id));
}

void WindowManagerWayland::OnVirtualKeyNotify(EventType type,
                                              uint32_t key,
                                              int device_id) {
  keyboard_->OnKeyChange(key, 0, type != ET_KEY_RELEASED, false,
                         EventTimeForNow(), device_id, ui::EF_NONE);
}

void WindowManagerWayland::TouchNotify(uint32_t device_id,
                                       unsigned handle,
                                       ui::EventType type,
                                       const ui::TouchEventInfo& event_info) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyTouchEvent,
                                weak_ptr_factory_.GetWeakPtr(), device_id,
                                handle, type, event_info));
}

void WindowManagerWayland::CloseWidget(unsigned handle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::OnWindowClose,
                                weak_ptr_factory_.GetWeakPtr(), handle));
}

void WindowManagerWayland::ScreenChanged(const std::string& display_id,
                                         const std::string& display_name,
                                         unsigned width,
                                         unsigned height,
                                         int rotation) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyScreenChanged,
                                weak_ptr_factory_.GetWeakPtr(), display_id,
                                display_name, width, height, rotation));
}

void WindowManagerWayland::WindowResized(unsigned handle,
                                         unsigned width,
                                         unsigned height) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::OnWindowResized,
                     weak_ptr_factory_.GetWeakPtr(), handle, width, height));
}

void WindowManagerWayland::WindowUnminimized(unsigned handle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::OnWindowUnminimized,
                                weak_ptr_factory_.GetWeakPtr(), handle));
}

void WindowManagerWayland::WindowDeActivated(unsigned windowhandle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::OnWindowDeActivated,
                                weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::WindowActivated(unsigned windowhandle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::OnWindowActivated,
                                weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::DragEnter(
    unsigned windowhandle,
    float x,
    float y,
    const std::vector<std::string>& mime_types,
    uint32_t serial) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyDragEnter,
                                weak_ptr_factory_.GetWeakPtr(), windowhandle, x,
                                y, mime_types, serial));
}

void WindowManagerWayland::DragData(unsigned windowhandle,
                                    base::FileDescriptor pipefd) {
  // TODO(mcatanzaro): pipefd will be leaked if the WindowManagerWayland is
  // destroyed before NotifyDragData is called.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyDragData,
                     weak_ptr_factory_.GetWeakPtr(), windowhandle, pipefd));
}

void WindowManagerWayland::DragLeave(unsigned windowhandle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyDragLeave,
                                weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::DragMotion(unsigned windowhandle,
                                      float x,
                                      float y,
                                      uint32_t time) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyDragMotion,
                     weak_ptr_factory_.GetWeakPtr(), windowhandle, x, y, time));
}

void WindowManagerWayland::DragDrop(unsigned windowhandle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyDragDrop,
                                weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::InitializeXKB(
    base::UnsafeSharedMemoryRegion region) {
  auto mapped_memory = region.Map();
  const char* keymap = mapped_memory.GetMemoryAs<char>();
  if (!keymap)
    return;
  bool success = KeyboardLayoutEngineManager::GetKeyboardLayoutEngine()
                     ->SetCurrentLayoutFromBuffer(keymap, mapped_memory.size());
  DCHECK(success) << "Failed to set the XKB keyboard mapping.";
}

////////////////////////////////////////////////////////////////////////////////
// PlatformEventSource implementation:
void WindowManagerWayland::PostUiEvent(Event* event) {
  DispatchEvent(event);
}

void WindowManagerWayland::DispatchUiEventTask(std::unique_ptr<Event> event) {
  DispatchEvent(event.get());
}

void WindowManagerWayland::OnDispatcherListChanged() {
}

////////////////////////////////////////////////////////////////////////////////
void WindowManagerWayland::NotifyMotion(float x,
                                        float y) {
  gfx::Point position(x, y);
  MouseEvent mouseev(ET_MOUSE_MOVED,
                         position,
                         position,
                         EventTimeForNow(),
                         0,
                         0);
  DispatchEvent(&mouseev);
}

void WindowManagerWayland::NotifyDragging(float x, float y) {
  gfx::Point position(x, y);
  MouseEvent mouseev(ET_MOUSE_DRAGGED, position, position, EventTimeForNow(),
                     ui::EF_LEFT_MOUSE_BUTTON, 0);
  DispatchEvent(&mouseev);
}

void WindowManagerWayland::NotifyButtonPress(unsigned handle,
                                             EventType type,
                                             EventFlags flags,
                                             float x,
                                             float y) {
  gfx::Point position(x, y);
  MouseEvent mouseev(type,
                         position,
                         position,
                         EventTimeForNow(),
                         flags,
                         flags);

  DispatchEvent(&mouseev);

  if (type == ET_MOUSE_RELEASED)
    OnWindowFocused(handle);
}

void WindowManagerWayland::NotifyAxis(float x,
                                      float y,
                                      int xoffset,
                                      int yoffset) {
  gfx::Point position(x, y);
  MouseEvent mouseev(ET_MOUSEWHEEL, position, position, EventTimeForNow(), 0,
                     0);

  MouseWheelEvent wheelev(mouseev, xoffset, yoffset);

  DispatchEvent(&wheelev);
}

void WindowManagerWayland::NotifyPointerEnter(uint32_t device_id,
                                              unsigned handle,
                                              float x,
                                              float y) {
  OnWindowEnter(handle);

  gfx::Point position(x, y);
  MouseEvent mouseev(ET_MOUSE_ENTERED, position, position, EventTimeForNow(), 0,
                     0);

  DispatchEvent(&mouseev);
}

void WindowManagerWayland::NotifyPointerLeave(uint32_t device_id,
                                              unsigned handle,
                                              float x,
                                              float y) {
  OnWindowLeave(handle);
#if !defined(OS_WEBOS)
  // LSM sends a pointer leave event to a window
  // if we touch on another window of the second display.
  // The first window after that can be unfocused.
  // We can't right handle pointer leave event
  // on the first window on client side.
  // Disable dispatching ET_MOUSE_EXITED event.
  gfx::Point position(x, y);
  MouseEvent mouseev(ET_MOUSE_EXITED, position, position, EventTimeForNow(), 0,
                     0);

  DispatchEvent(&mouseev);
#endif
}

void WindowManagerWayland::NotifyInputPanelEnter(uint32_t device_id,
                                                 unsigned handle) {
  GrabDeviceEvents(device_id, handle);
}

void WindowManagerWayland::NotifyInputPanelLeave(uint32_t device_id) {
  UnGrabDeviceEvents(device_id);
}

void WindowManagerWayland::NotifyTouchEvent(
    uint32_t device_id,
    unsigned handle,
    ui::EventType type,
    const ui::TouchEventInfo& event_info) {
  gfx::PointF position(event_info.x_, event_info.y_);
  base::TimeTicks timestamp = EventTimeForNow();

  // We need to make the slot ID unique to system, not only to device.
  // Otherwise gesture recognizer touch lock will fail. To achieve this we copy
  // the algorithm used in EventFactoryEvdev.
  int input_id = device_id * ui::kNumTouchEvdevSlots + event_info.touch_id_;
  uint32_t touch_slot = touch_slot_generator_.GetGeneratedID(input_id);

  if (type == ET_TOUCH_PRESSED)
    GrabTouchButton(device_id, handle);

  TouchEvent touchev(
      type, position, position, timestamp,
      ui::PointerDetails(ui::EventPointerType::kTouch, touch_slot));
  touchev.set_source_device_id(device_id);

  DispatchEvent(&touchev);

  if (type == ET_TOUCH_RELEASED || type == ET_TOUCH_CANCELLED) {
    if (type == ET_TOUCH_CANCELLED)
      UnGrabTouchButton(device_id);
    touch_slot_generator_.ReleaseNumber(input_id);
  }
}

void WindowManagerWayland::NotifyScreenChanged(const std::string& display_id,
                                               const std::string& display_name,
                                               unsigned width,
                                               unsigned height,
                                               int rotation) {
  if (platform_screen_)
    platform_screen_->GetDelegate()->OnScreenChanged(display_id, display_name,
                                                     width, height, rotation);
}

void WindowManagerWayland::NotifyDragEnter(
    unsigned windowhandle,
    float x,
    float y,
    const std::vector<std::string>& mime_types,
    uint32_t serial) {
  ui::OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnDragEnter(windowhandle, x, y, mime_types, serial);
}

void WindowManagerWayland::NotifyDragData(unsigned windowhandle,
                                          base::FileDescriptor pipefd) {
  ui::OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    close(pipefd.fd);
    return;
  }
  window->GetDelegate()->OnDragDataReceived(pipefd.fd);
}

void WindowManagerWayland::NotifyDragLeave(unsigned windowhandle) {
  ui::OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnDragLeave();
}

void WindowManagerWayland::NotifyDragMotion(unsigned windowhandle,
                                            float x,
                                            float y,
                                            uint32_t time) {
  ui::OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnDragMotion(x, y, time);
}

void WindowManagerWayland::NotifyDragDrop(unsigned windowhandle) {
  ui::OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnDragDrop();
}

// Additional notification for app-runtime
void WindowManagerWayland::InputPanelVisibilityChanged(unsigned windowhandle,
                                                       bool visibility) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyInputPanelVisibilityChanged,
                     weak_ptr_factory_.GetWeakPtr(), windowhandle, visibility));
}

void WindowManagerWayland::InputPanelRectChanged(unsigned windowhandle,
                                                 int32_t x,
                                                 int32_t y,
                                                 uint32_t width,
                                                 uint32_t height) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyInputPanelRectChanged,
                     weak_ptr_factory_.GetWeakPtr(), windowhandle, x, y, width,
                     height));
}

void WindowManagerWayland::NativeWindowExposed(unsigned windowhandle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyNativeWindowExposed,
                     weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::WindowClose(unsigned windowhandle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyWindowClose,
                                weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::KeyboardEnter(unsigned windowhandle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyKeyboardEnter,
                                weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::KeyboardLeave(unsigned windowhandle) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyKeyboardLeave,
                                weak_ptr_factory_.GetWeakPtr(), windowhandle));
}

void WindowManagerWayland::CursorVisibilityChanged(bool visible) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyCursorVisibilityChanged,
                     weak_ptr_factory_.GetWeakPtr(), visible));
}

void WindowManagerWayland::KeyboardAdded(const int device_id,
                                         const std::string& name) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyKeyboardAdded,
                     weak_ptr_factory_.GetWeakPtr(), device_id, name));
}

void WindowManagerWayland::KeyboardRemoved(const int device_id) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyKeyboardRemoved,
                                weak_ptr_factory_.GetWeakPtr(), device_id));
}

void WindowManagerWayland::PointerAdded(const int device_id,
                                        const std::string& name) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyPointerAdded,
                     weak_ptr_factory_.GetWeakPtr(), device_id, name));
}

void WindowManagerWayland::PointerRemoved(const int device_id) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyPointerRemoved,
                                weak_ptr_factory_.GetWeakPtr(), device_id));
}

void WindowManagerWayland::TouchscreenAdded(const int device_id,
                                            const std::string& name) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyTouchscreenAdded,
                     weak_ptr_factory_.GetWeakPtr(), device_id, name));
}

void WindowManagerWayland::TouchscreenRemoved(const int device_id) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WindowManagerWayland::NotifyTouchscreenRemoved,
                                weak_ptr_factory_.GetWeakPtr(), device_id));
}

void WindowManagerWayland::NotifyInputPanelVisibilityChanged(
    unsigned windowhandle,
    bool visibility) {
  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }

  if (!visibility) {
    for (auto* const open_window : open_windows(window->GetDisplayId())) {
      if (open_window->GetHandle() != windowhandle)
        open_window->GetDelegate()->OnInputPanelVisibilityChanged(visibility);
    }
  }

  window->GetDelegate()->OnInputPanelVisibilityChanged(visibility);
}

void WindowManagerWayland::NotifyInputPanelRectChanged(unsigned windowhandle,
                                                       int32_t x,
                                                       int32_t y,
                                                       uint32_t width,
                                                       uint32_t height) {
  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }

  for (auto* const window : open_windows(window->GetDisplayId()))
    window->GetDelegate()->OnInputPanelRectChanged(x, y, width, height);
}

void WindowManagerWayland::NativeWindowStateChanged(unsigned handle,
                                                    ui::WidgetState new_state) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WindowManagerWayland::NotifyNativeWindowStateChanged,
                     weak_ptr_factory_.GetWeakPtr(), handle, new_state));
}

void WindowManagerWayland::NativeWindowStateAboutToChange(unsigned handle,
                                                          ui::WidgetState state) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &WindowManagerWayland::NotifyNativeWindowStateAboutToChange,
          weak_ptr_factory_.GetWeakPtr(), handle, state));
}

void WindowManagerWayland::NotifyNativeWindowExposed(unsigned windowhandle) {
  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnWindowHostExposed();
}

void WindowManagerWayland::NotifyWindowClose(unsigned windowhandle) {
  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnWindowHostClose();
}

void WindowManagerWayland::NotifyKeyboardEnter(unsigned windowhandle) {
  OnWindowEnter(windowhandle);

  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnKeyboardEnter();
}

void WindowManagerWayland::NotifyKeyboardLeave(unsigned windowhandle) {
  OnWindowLeave(windowhandle);

  OzoneWaylandWindow* window = GetWindow(windowhandle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << windowhandle
               << " from GPU process";
    return;
  }
  window->GetDelegate()->OnKeyboardLeave();
}

void WindowManagerWayland::NotifyCursorVisibilityChanged(bool visible) {
  // Notify all open windows with cursor visibility state change
  for (const auto& kv : open_windows_map_) {
    for (const OzoneWaylandWindow* window : *(kv.second)) {
      window->GetDelegate()->OnCursorVisibilityChanged(visible);
    }
  }
}

void WindowManagerWayland::NotifyKeyboardAdded(const int device_id,
                                               const std::string& name) {
  keyboard_devices_.push_back(
      ui::KeyboardDevice(device_id, ui::INPUT_DEVICE_UNKNOWN, name));
  GetHotplugEventObserver()->OnKeyboardDevicesUpdated(keyboard_devices_);
}

void WindowManagerWayland::NotifyKeyboardRemoved(const int device_id) {
  base::EraseIf(keyboard_devices_, [device_id](const auto& device) {
    return device.id == device_id;
  });
  GetHotplugEventObserver()->OnKeyboardDevicesUpdated(keyboard_devices_);
}

void WindowManagerWayland::NotifyPointerAdded(const int device_id,
                                              const std::string& name) {
  pointer_devices_.push_back(
      ui::InputDevice(device_id, ui::INPUT_DEVICE_UNKNOWN, name));
  GetHotplugEventObserver()->OnMouseDevicesUpdated(pointer_devices_);
}

void WindowManagerWayland::NotifyPointerRemoved(const int device_id) {
  base::EraseIf(pointer_devices_, [device_id](const auto& device) {
    return device.id == device_id;
  });
  GetHotplugEventObserver()->OnMouseDevicesUpdated(pointer_devices_);
}

void WindowManagerWayland::NotifyTouchscreenAdded(const int device_id,
                                                  const std::string& name) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kIgnoreTouchDevices))
    return;

  int max_touch_points = 1;
  std::string override_max_touch_points =
      command_line->GetSwitchValueASCII(switches::kForceMaxTouchPoints);
  if (!override_max_touch_points.empty()) {
    int temp;
    if (base::StringToInt(override_max_touch_points, &temp))
      max_touch_points = temp;
  }
  touchscreen_devices_.push_back(
      ui::TouchscreenDevice(device_id, ui::INPUT_DEVICE_UNKNOWN, name,
                            gfx::Size(), max_touch_points));
  GetHotplugEventObserver()->OnTouchscreenDevicesUpdated(touchscreen_devices_);
}

int WindowManagerWayland::GetMaxTouchPoints() {
  auto* command_line = base::CommandLine::ForCurrentProcess();
  std::string str =
      command_line->GetSwitchValueASCII(switches::kForceMaxTouchPoints);

  if (!str.empty()) {
    int max_touch_points;
    if (base::StringToInt(str, &max_touch_points))
      return max_touch_points;
    else
      LOG(ERROR) << "Failed to get force max touch points. Use default value.";
  }

  return kDefaultMaxTouchPoints;
}

void WindowManagerWayland::NotifyTouchscreenRemoved(const int device_id) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kIgnoreTouchDevices))
    return;

  base::EraseIf(touchscreen_devices_, [device_id](const auto& device) {
    return device.id == device_id;
  });
  ui::DeviceHotplugEventObserver* device_data_manager =
      ui::DeviceDataManager::GetInstance();
  device_data_manager->OnTouchscreenDevicesUpdated(touchscreen_devices_);
}

void WindowManagerWayland::NotifyNativeWindowStateChanged(unsigned handle,
                                                          ui::WidgetState new_state) {
  OzoneWaylandWindow* window = GetWindow(handle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << handle
               << " from GPU process";
    return;
  }
  VLOG(1) << __PRETTY_FUNCTION__;
  window->GetDelegate()->OnWindowHostStateChanged(new_state);
}

void WindowManagerWayland::NotifyNativeWindowStateAboutToChange(unsigned handle,
                                                                ui::WidgetState state) {
  OzoneWaylandWindow* window = GetWindow(handle);
  if (!window) {
    LOG(ERROR) << "Received invalid window handle " << handle
               << " from GPU process";
    return;
  }
  VLOG(1) << __PRETTY_FUNCTION__;
  window->GetDelegate()->OnWindowHostStateAboutToChange(state);
}

void WindowManagerWayland::GrabDeviceEvents(uint32_t device_id,
                                            unsigned widget) {
  OzoneWaylandWindow* window = GetWindow(widget);
  if (window) {
    OzoneWaylandWindow* active_window = GetActiveWindow(window->GetDisplayId());
    if (active_window && widget == active_window->GetHandle())
      device_event_grabber_map_[device_id] = widget;
  }
}

void WindowManagerWayland::UnGrabDeviceEvents(uint32_t device_id) {
  if (device_event_grabber_map_.find(device_id) !=
      device_event_grabber_map_.end())
    device_event_grabber_map_[device_id] = 0;
}

unsigned WindowManagerWayland::DeviceEventGrabber(uint32_t device_id) const {
  if (device_event_grabber_map_.find(device_id) !=
      device_event_grabber_map_.end())
    return device_event_grabber_map_.at(device_id);
  return 0;
}

void WindowManagerWayland::GrabTouchButton(uint32_t touch_button_id,
                                           unsigned widget) {
  OzoneWaylandWindow* window = GetWindow(widget);
  if (window) {
    OzoneWaylandWindow* active_window = GetActiveWindow(window->GetDisplayId());
    if (active_window && widget == active_window->GetHandle())
      touch_button_grabber_map_[touch_button_id] = widget;
  }
}

void WindowManagerWayland::UnGrabTouchButton(uint32_t touch_button_id) {
  if (device_event_grabber_map_.find(touch_button_id) !=
      device_event_grabber_map_.end())
    device_event_grabber_map_.erase(touch_button_id);
}

unsigned WindowManagerWayland::TouchButtonGrabber(
    uint32_t touch_button_id) const {
  if (touch_button_grabber_map_.find(touch_button_id) !=
      touch_button_grabber_map_.end())
    return touch_button_grabber_map_.at(touch_button_id);
  return 0;
}

}  // namespace ui
