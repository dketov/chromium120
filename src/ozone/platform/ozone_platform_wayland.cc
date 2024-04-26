// Copyright 2014 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ozone/platform/ozone_platform_wayland.h"

#include "base/at_exit.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "ozone/platform/ozone_gpu_platform_support_host.h"
#include "ozone/platform/ozone_wayland_window.h"
#include "ozone/platform/window_manager_wayland.h"
#include "ozone/wayland/display.h"
#include "ozone/wayland/ozone_wayland_screen.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/input_method_minimal.h"
#include "ui/display/types/native_display_delegate.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/events/ozone/layout/xkb/xkb_evdev_codes.h"
#include "ui/events/ozone/layout/xkb/xkb_keyboard_layout_engine.h"
#include "ui/ozone/common/bitmap_cursor_factory.h"
#include "ui/ozone/common/stub_overlay_manager.h"
#include "ui/ozone/public/platform_screen.h"
#include "ui/ozone/public/system_input_injector.h"
#include "ui/platform_window/platform_window_delegate.h"
#include "ui/platform_window/platform_window_init_properties.h"

#if defined(USE_NEVA_MEDIA)
#include "ui/platform_window/neva/mojom/video_window.mojom.h"
#endif  // defined(USE_NEVA_MEDIA)

namespace ui {

namespace {

// OzonePlatform for Wayland
//
// This platform is Linux with the Wayland display server.
class OzonePlatformWayland : public OzonePlatform {
 public:
  OzonePlatformWayland() {
    base::AtExitManager::RegisterTask(
        base::BindOnce(&base::DeletePointer<OzonePlatformWayland>, this));
  }
  OzonePlatformWayland(const OzonePlatformWayland&) = delete;
  OzonePlatformWayland& operator=(const OzonePlatformWayland&) = delete;

  ~OzonePlatformWayland() override {}

  // OzonePlatform:
  ui::SurfaceFactoryOzone* GetSurfaceFactoryOzone() override {
    return wayland_display_.get();
  }

  ui::CursorFactory* GetCursorFactory() override {
    return cursor_factory_.get();
  }

  ui::OverlayManagerOzone* GetOverlayManager() override {
    return overlay_manager_.get();
  }

  InputController* GetInputController() override { return NULL; }

  std::unique_ptr<SystemInputInjector> CreateSystemInputInjector() override {
    return std::unique_ptr<SystemInputInjector>();
  }

  GpuPlatformSupportHost* GetGpuPlatformSupportHost() override {
    return gpu_platform_host_.get();
  }

  GpuPlatformSupport* GetGpuPlatformSupport() override {
    return wayland_display_.get();
  }

#if defined(USE_NEVA_MEDIA)
  void AddInterfaces(mojo::BinderMap* binders) override {
    // This is called from gpu main thread and we want to get callback with gpu
    // main thread.
    binders->Add<ui::mojom::VideoWindowConnector>(
        base::BindRepeating(
            &OzonePlatformWayland::GetVideoWindowControllerConnection,
            base::Unretained(this)),
        base::SingleThreadTaskRunner::GetCurrentDefault());
    GetVideoWindowController()->Initialize(
        base::SingleThreadTaskRunner::GetCurrentDefault());
  }

  void GetVideoWindowControllerConnection(
      mojo::PendingReceiver<ui::mojom::VideoWindowConnector> receiver) {
    wayland_display_->BindVideoWindowController(std::move(receiver));
  }

  ui::VideoWindowGeometryManager* GetVideoWindowGeometryManager() override {
    return GetVideoWindowController();
  }

  ui::VideoWindowControllerImpl* GetVideoWindowController() {
    return wayland_display_->GetVideoWindowControllerImpl();
  }
#endif

  std::unique_ptr<PlatformWindow> CreatePlatformWindow(
      PlatformWindowDelegate* delegate,
      PlatformWindowInitProperties properties) override {
    return std::unique_ptr<PlatformWindow>(new OzoneWaylandWindow(
        delegate, gpu_platform_host_.get(), window_manager_.get(),
        properties.bounds, properties.opacity));
  }

  std::unique_ptr<InputMethod> CreateInputMethod(
      ImeKeyEventDispatcher* ime_key_event_dispatcher,
      gfx::AcceleratedWidget widget) override {
    return std::make_unique<InputMethodMinimal>(ime_key_event_dispatcher);
  }

  std::unique_ptr<DesktopPlatformScreen> CreatePlatformScreen(
      DesktopPlatformScreenDelegate* delegate) {
    return std::unique_ptr<DesktopPlatformScreen>(
        new ozonewayland::OzoneWaylandScreen(delegate, window_manager_.get()));
  }

  std::unique_ptr<display::NativeDisplayDelegate> CreateNativeDisplayDelegate()
      override {
    return nullptr;
  }

  std::unique_ptr<PlatformScreen> CreateScreen() override { return nullptr; }

  void InitScreen(PlatformScreen* screen) override {}

  bool InitializeUI(const InitParams& args) override {
    // For tests.
    if (wayland_display_.get()) {
      return true;
    }

    gpu_platform_host_.reset(new ui::OzoneGpuPlatformSupportHost());
    // Needed as Browser creates accelerated widgets through SFO.
    wayland_display_.reset(new ozonewayland::WaylandDisplay());
    cursor_factory_.reset(new ui::BitmapCursorFactory());
    overlay_manager_.reset(new StubOverlayManager());
    KeyboardLayoutEngineManager::SetKeyboardLayoutEngine(
        new XkbKeyboardLayoutEngine(xkb_evdev_code_converter_));
    window_manager_.reset(
        new ui::WindowManagerWayland(gpu_platform_host_.get()));

    return true;
  }

  void InitializeGPU(const InitParams& args) override {
    if (!wayland_display_) {
      wayland_display_.reset(new ozonewayland::WaylandDisplay());
    }

    if (!wayland_display_->InitializeHardware()) {
      LOG(FATAL) << "failed to initialize display hardware";
    }
  }

  void PreEarlyInitialize() override { setenv("EGL_PLATFORM", "wayland", 0); }

  const PlatformProperties& GetPlatformProperties() override {
    static base::NoDestructor<OzonePlatform::PlatformProperties> properties;
    static bool initialised = false;
    if (!initialised) {
      properties->custom_frame_pref_default = false;
      properties->message_pump_type_for_gpu = base::MessagePumpType::DEFAULT;
      properties->supports_vulkan_swap_chain = false;
      properties->supports_global_screen_coordinates = false;

      initialised = true;
    }

    return *properties;
  }

 private:
  std::unique_ptr<ui::BitmapCursorFactory> cursor_factory_;
  std::unique_ptr<ozonewayland::WaylandDisplay> wayland_display_;
  std::unique_ptr<StubOverlayManager> overlay_manager_;
  std::unique_ptr<ui::WindowManagerWayland> window_manager_;
  XkbEvdevCodes xkb_evdev_code_converter_;
  std::unique_ptr<ui::OzoneGpuPlatformSupportHost> gpu_platform_host_;
};

}  // namespace

OzonePlatform* CreateOzonePlatformWayland_external() {
  return new OzonePlatformWayland;
}

std::unique_ptr<DesktopPlatformScreen> CreatePlatformScreen(
    DesktopPlatformScreenDelegate* delegate) {
  OzonePlatformWayland* platform =
      static_cast<OzonePlatformWayland*>(ui::OzonePlatform::GetInstance());
  return platform->CreatePlatformScreen(delegate);
}

}  // namespace ui
