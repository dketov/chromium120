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

#ifndef OZONE_PLATFORM_WINDOW_MANAGER_WAYLAND_H_
#define OZONE_PLATFORM_WINDOW_MANAGER_WAYLAND_H_

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/file_descriptor_posix.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "ozone/platform/event_param_traits.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/platform_cursor.h"
#include "ui/events/devices/device_hotplug_event_observer.h"
#include "ui/events/devices/input_device.h"
#include "ui/events/devices/keyboard_device.h"
#include "ui/events/devices/touchscreen_device.h"
#include "ui/events/event.h"
#include "ui/events/event_modifiers.h"
#include "ui/events/event_source.h"
#include "ui/events/ozone/evdev/neva/keyboard_evdev_neva.h"
#include "ui/events/platform/platform_event_dispatcher.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/sequential_id_generator.h"
#include "ui/ozone/public/gpu_platform_support_host.h"
#include "ui/views/widget/desktop_aura/neva/ui_constants.h"

namespace base {
class UnsafeSharedMemoryRegion;
}

namespace ozonewayland {
class OzoneWaylandScreen;
}

namespace ui {

class OzoneGpuPlatformSupportHost;
class OzoneWaylandWindow;

// A static class used by OzoneWaylandWindow for basic window management.
class WindowManagerWayland : public PlatformEventSource,
                             public GpuPlatformSupportHost {
 public:
  explicit WindowManagerWayland(OzoneGpuPlatformSupportHost* proxy);
  WindowManagerWayland(const WindowManagerWayland&) = delete;
  WindowManagerWayland& operator=(const WindowManagerWayland&) = delete;
  ~WindowManagerWayland() override;

  void OnRootWindowCreated(OzoneWaylandWindow* window);
  void OnRootWindowClosed(OzoneWaylandWindow* window);
  void OnRootWindowDisplayChanged(const std::string& prev_display_id,
                                  const std::string& new_display_id,
                                  OzoneWaylandWindow* window);
  void Restore(OzoneWaylandWindow* window);

  void OnPlatformScreenCreated(ozonewayland::OzoneWaylandScreen* screen);

  scoped_refptr<PlatformCursor> GetPlatformCursor();
  void SetPlatformCursor(scoped_refptr<PlatformCursor> cursor);

  OzoneWaylandWindow* GetWindow(unsigned handle);
  bool HasWindowsOpen() const;

  OzoneWaylandWindow* GetActiveWindow(const std::string& display_id) const;

  // Tries to set a given widget as the recipient for events. It will
  // fail if there is already another widget as recipient.
  void GrabEvents(gfx::AcceleratedWidget widget);

  // Unsets a given widget as the recipient for events.
  void UngrabEvents(gfx::AcceleratedWidget widget);

  // Gets the current widget recipient of mouse events.
  gfx::AcceleratedWidget event_grabber() const { return event_grabber_; }

  unsigned DeviceEventGrabber(uint32_t device_id) const;
  unsigned TouchButtonGrabber(uint32_t touch_button_id) const;

 private:
  ui::DeviceHotplugEventObserver* GetHotplugEventObserver();
  void OnActivationChanged(unsigned windowhandle, bool active);
  std::list<OzoneWaylandWindow*>& open_windows(const std::string& display_id);
  void clear_open_windows(const std::string& display_id);
  void OnWindowFocused(unsigned handle);
  void OnWindowEnter(unsigned handle);
  void OnWindowLeave(unsigned handle);
  void OnWindowClose(unsigned handle);
  void OnWindowResized(unsigned windowhandle,
                       unsigned width,
                       unsigned height);
  void OnWindowUnminimized(unsigned windowhandle);
  void OnWindowDeActivated(unsigned windowhandle);
  void OnWindowActivated(unsigned windowhandle);
  // GpuPlatformSupportHost
  void OnGpuProcessLaunched(
      int host_id,
      base::RepeatingCallback<void(IPC::Message*)> sender) override;
  void OnChannelDestroyed(int host_id) override;
  void OnMessageReceived(const IPC::Message&) override;
  void OnGpuServiceLaunched(
      int host_id,
      GpuHostBindInterfaceCallback binder,
      GpuHostTerminateCallback terminate_callback) override;
  void MotionNotify(float x, float y);
  void ButtonNotify(unsigned handle,
                    EventType type,
                    EventFlags flags,
                    float x,
                    float y);
  void AxisNotify(float x,
                  float y,
                  int xoffset,
                  int yoffset);
  void PointerEnter(uint32_t device_id, unsigned handle, float x, float y);
  void PointerLeave(uint32_t device_id, unsigned handle, float x, float y);
  void InputPanelEnter(uint32_t device_id, unsigned handle);
  void InputPanelLeave(uint32_t device_id);
  void KeyNotify(EventType type, unsigned code, int device_id);
  void VirtualKeyNotify(EventType type,
                        uint32_t key,
                        int device_id);
  void TouchNotify(uint32_t device_id,
                   unsigned handle,
                   ui::EventType type,
                   const ui::TouchEventInfo& event_info);
  void CloseWidget(unsigned handle);

  void ScreenChanged(const std::string& display_id,
                     const std::string& display_name,
                     unsigned width,
                     unsigned height,
                     int rotation);
  void WindowResized(unsigned windowhandle,
                     unsigned width,
                     unsigned height);
  void WindowUnminimized(unsigned windowhandle);
  void WindowDeActivated(unsigned windowhandle);
  void WindowActivated(unsigned windowhandle);

  void DragEnter(unsigned windowhandle,
                 float x,
                 float y,
                 const std::vector<std::string>& mime_types,
                 uint32_t serial);
  void DragData(unsigned windowhandle, base::FileDescriptor pipefd);
  void DragLeave(unsigned windowhandle);
  void DragMotion(unsigned windowhandle, float x, float y, uint32_t time);
  void DragDrop(unsigned windowhandle);

  void InitializeXKB(base::UnsafeSharedMemoryRegion region);
  // PlatformEventSource:
  void OnDispatcherListChanged() override;

  // Dispatch event via PlatformEventSource.
  void DispatchUiEventTask(std::unique_ptr<Event> event);
  // Post a task to dispatch an event.
  void PostUiEvent(Event* event);

  void NotifyMotion(float x,
                    float y);
  void NotifyDragging(float x, float y);
  void NotifyButtonPress(unsigned handle,
                         EventType type,
                         EventFlags flags,
                         float x,
                         float y);
  void NotifyAxis(float x,
                  float y,
                  int xoffset,
                  int yoffset);
  void NotifyPointerEnter(uint32_t device_id,
                          unsigned handle,
                          float x,
                          float y);
  void NotifyPointerLeave(uint32_t device_id,
                          unsigned handle,
                          float x,
                          float y);
  void NotifyInputPanelEnter(uint32_t device_id, unsigned handle);
  void NotifyInputPanelLeave(uint32_t device_id);
  void NotifyTouchEvent(uint32_t device_id,
                        unsigned handle,
                        ui::EventType type,
                        const ui::TouchEventInfo& event_info);
  void NotifyScreenChanged(const std::string& display_id,
                           const std::string& display_name,
                           unsigned width,
                           unsigned height,
                           int rotation);

  void NotifyDragEnter(unsigned windowhandle,
                       float x,
                       float y,
                       const std::vector<std::string>& mime_types,
                       uint32_t serial);
  void NotifyDragData(unsigned windowhandle, base::FileDescriptor pipefd);
  void NotifyDragLeave(unsigned windowhandle);
  void NotifyDragMotion(unsigned windowhandle, float x, float y, uint32_t time);
  void NotifyDragDrop(unsigned windowhandle);
  void OnVirtualKeyNotify(EventType type, uint32_t key, int device_id);
  ///@name USE_NEVA_APPRUNTIME
  ///@{
  void InputPanelVisibilityChanged(unsigned windowhandle, bool visibility);
  void InputPanelRectChanged(unsigned windowhandle,
                             int32_t x,
                             int32_t y,
                             uint32_t width,
                             uint32_t height);
  void NativeWindowExposed(unsigned windowhandle);
  void NativeWindowStateChanged(unsigned handle, ui::WidgetState new_state);
  void NativeWindowStateAboutToChange(unsigned handle, ui::WidgetState state);
  void WindowClose(unsigned windowhandle);
  void KeyboardEnter(unsigned windowhandle);
  void KeyboardLeave(unsigned windowhandle);
  void CursorVisibilityChanged(bool visible);
  void KeyboardAdded(const int device_id, const std::string& name);
  void KeyboardRemoved(const int device_id);
  void PointerAdded(const int device_id, const std::string& name);
  void PointerRemoved(const int device_id);
  void TouchscreenAdded(const int device_id, const std::string& name);
  void TouchscreenRemoved(const int device_id);
  void NotifyInputPanelVisibilityChanged(unsigned windowhandle, bool visibility);
  void NotifyInputPanelRectChanged(unsigned windowhandle,
                                   int32_t x,
                                   int32_t y,
                                   uint32_t width,
                                   uint32_t height);
  void NotifyNativeWindowExposed(unsigned windowhandle);
  void NotifyNativeWindowStateChanged(unsigned handle, ui::WidgetState new_state);
  void NotifyNativeWindowStateAboutToChange(unsigned handle, ui::WidgetState state);
  void NotifyWindowClose(unsigned windowhandle);
  void NotifyKeyboardEnter(unsigned windowhandle);
  void NotifyKeyboardLeave(unsigned windowhandle);
  void NotifyCursorVisibilityChanged(bool visible);
  void NotifyKeyboardAdded(const int device_id, const std::string& name);
  void NotifyKeyboardRemoved(const int device_id);
  void NotifyPointerAdded(const int device_id, const std::string& name);
  void NotifyPointerRemoved(const int device_id);
  void NotifyTouchscreenAdded(const int device_id, const std::string& name);
  int GetMaxTouchPoints();
  void NotifyTouchscreenRemoved(const int device_id);
  ///@}

  void GrabDeviceEvents(uint32_t device_id, unsigned widget);
  void UnGrabDeviceEvents(uint32_t device_id);

  void GrabTouchButton(uint32_t touch_button_id, unsigned widget);
  void UnGrabTouchButton(uint32_t touch_button_id);

  // List of all open aura::Window.
  std::map<std::string, std::list<OzoneWaylandWindow*>*> open_windows_map_;
  std::list<OzoneWaylandWindow*>* open_windows_;
  gfx::AcceleratedWidget event_grabber_ = gfx::kNullAcceleratedWidget;
  std::map<uint32_t, unsigned> device_event_grabber_map_;
  std::map<uint32_t, unsigned> touch_button_grabber_map_;
  std::map<std::string, OzoneWaylandWindow*> active_window_map_;
  gfx::AcceleratedWidget current_capture_ = gfx::kNullAcceleratedWidget;
  OzoneGpuPlatformSupportHost* proxy_;
  // Modifier key state (shift, ctrl, etc).
  EventModifiers modifiers_;
  // Keyboard state.
  std::unique_ptr<KeyboardEvdevNeva> keyboard_;
  ozonewayland::OzoneWaylandScreen* platform_screen_;
  scoped_refptr<PlatformCursor> platform_cursor_;
  bool dragging_;

  SequentialIDGenerator touch_slot_generator_;
  std::vector<ui::KeyboardDevice> keyboard_devices_;
  std::vector<ui::InputDevice> pointer_devices_;
  std::vector<ui::TouchscreenDevice> touchscreen_devices_;

  // Support weak pointers for attach & detach callbacks.
  base::WeakPtrFactory<WindowManagerWayland> weak_ptr_factory_;
};

}  // namespace ui

#endif  // OZONE_PLATFORM_WINDOW_MANAGER_WAYLAND_H_
