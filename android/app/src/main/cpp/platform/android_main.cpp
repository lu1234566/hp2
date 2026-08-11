#include "hp2/runtime.h"

#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>

#define LOG_TAG "HP2Engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

bool HasSource(int source, int expected) {
    return (source & expected) == expected;
}

bool IsGamepadSource(int source) {
    return HasSource(source, AINPUT_SOURCE_GAMEPAD)
        || HasSource(source, AINPUT_SOURCE_JOYSTICK)
        || HasSource(source, AINPUT_SOURCE_DPAD);
}

bool FilterGamepadKey(const GameActivityKeyEvent* event) {
    return event != nullptr && IsGamepadSource(event->source);
}

bool FilterJoystickMotion(const GameActivityMotionEvent* event) {
    return event != nullptr && HasSource(event->source, AINPUT_SOURCE_JOYSTICK);
}

float Deadzone(float value, float deadzone = 0.15f) {
    if (!std::isfinite(value) || std::abs(value) <= deadzone) {
        return 0.0f;
    }
    const float sign = value < 0.0f ? -1.0f : 1.0f;
    return sign * std::clamp((std::abs(value) - deadzone) / (1.0f - deadzone), 0.0f, 1.0f);
}

float TriggerValue(float primary, float fallback) {
    return std::clamp(std::max(primary, fallback), 0.0f, 1.0f);
}

std::optional<hp2::Button> MapButton(int key_code) {
    switch (key_code) {
        case AKEYCODE_BUTTON_A: return hp2::Button::A;
        case AKEYCODE_BUTTON_B: return hp2::Button::B;
        case AKEYCODE_BUTTON_X: return hp2::Button::X;
        case AKEYCODE_BUTTON_Y: return hp2::Button::Y;
        case AKEYCODE_BUTTON_L1: return hp2::Button::L1;
        case AKEYCODE_BUTTON_R1: return hp2::Button::R1;
        case AKEYCODE_BUTTON_L2: return hp2::Button::L2;
        case AKEYCODE_BUTTON_R2: return hp2::Button::R2;
        case AKEYCODE_BUTTON_START: return hp2::Button::Start;
        case AKEYCODE_BUTTON_SELECT: return hp2::Button::Select;
        case AKEYCODE_BUTTON_THUMBL: return hp2::Button::LeftStick;
        case AKEYCODE_BUTTON_THUMBR: return hp2::Button::RightStick;
        case AKEYCODE_DPAD_UP: return hp2::Button::DpadUp;
        case AKEYCODE_DPAD_DOWN: return hp2::Button::DpadDown;
        case AKEYCODE_DPAD_LEFT: return hp2::Button::DpadLeft;
        case AKEYCODE_DPAD_RIGHT: return hp2::Button::DpadRight;
        default: return std::nullopt;
    }
}

class BootstrapRenderer {
public:
    bool Initialize(ANativeWindow* window) {
        if (window == nullptr) {
            return false;
        }

        const EGLint config_attributes[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
        };
        const EGLint context_attributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };

        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) == EGL_FALSE) {
            LOGE("EGL display initialization failed");
            Shutdown();
            return false;
        }

        EGLConfig config = nullptr;
        EGLint config_count = 0;
        if (eglChooseConfig(display_, config_attributes, &config, 1, &config_count) == EGL_FALSE
            || config_count < 1) {
            LOGE("No compatible OpenGL ES 3 EGL config");
            Shutdown();
            return false;
        }

        EGLint native_format = 0;
        eglGetConfigAttrib(display_, config, EGL_NATIVE_VISUAL_ID, &native_format);
        ANativeWindow_setBuffersGeometry(window, 0, 0, native_format);

        surface_ = eglCreateWindowSurface(display_, config, window, nullptr);
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attributes);
        if (surface_ == EGL_NO_SURFACE || context_ == EGL_NO_CONTEXT
            || eglMakeCurrent(display_, surface_, surface_, context_) == EGL_FALSE) {
            LOGE("EGL surface/context initialization failed");
            Shutdown();
            return false;
        }

        eglQuerySurface(display_, surface_, EGL_WIDTH, &width_);
        eglQuerySurface(display_, surface_, EGL_HEIGHT, &height_);
        eglSwapInterval(display_, 1);
        LOGI("G0 renderer ready: %dx%d, %s", width_, height_,
             reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        return true;
    }

    void Shutdown() {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT) {
                eglDestroyContext(display_, context_);
            }
            if (surface_ != EGL_NO_SURFACE) {
                eglDestroySurface(display_, surface_);
            }
            eglTerminate(display_);
        }
        display_ = EGL_NO_DISPLAY;
        surface_ = EGL_NO_SURFACE;
        context_ = EGL_NO_CONTEXT;
        width_ = 0;
        height_ = 0;
    }

    bool ready() const {
        return display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE && context_ != EGL_NO_CONTEXT;
    }

    void Render(const hp2::Runtime& runtime, float seconds) {
        if (!ready()) {
            return;
        }

        glDisable(GL_SCISSOR_TEST);
        switch (runtime.status()) {
            case hp2::BootStatus::WaitingForController: glClearColor(0.025f, 0.055f, 0.12f, 1.0f); break;
            case hp2::BootStatus::MissingGameData: glClearColor(0.12f, 0.045f, 0.025f, 1.0f); break;
            case hp2::BootStatus::IncompatibleGameData: glClearColor(0.10f, 0.025f, 0.12f, 1.0f); break;
            case hp2::BootStatus::PackageProbeReady: glClearColor(0.018f, 0.10f, 0.075f, 1.0f); break;
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const int margin = std::max(16, width_ / 32);
        const int gap = std::max(8, width_ / 100);
        const int bar_height = std::max(18, height_ / 18);
        const int usable_width = width_ - (margin * 2);
        const int segment_width = (usable_width - (gap * 3)) / 4;
        const float pulse = 0.58f + 0.22f * std::sin(seconds * 3.0f);

        DrawRect(margin, margin, segment_width, bar_height, {0.18f, 0.70f, 0.45f, 1.0f});
        DrawRect(margin + (segment_width + gap), margin, segment_width, bar_height,
                 runtime.controller_present()
                     ? Color{0.18f, 0.70f, 0.45f, 1.0f}
                     : Color{pulse, pulse * 0.78f, 0.18f, 1.0f});
        DrawRect(margin + 2 * (segment_width + gap), margin, segment_width, bar_height,
                 !runtime.packages().empty()
                     ? Color{0.18f, 0.70f, 0.45f, 1.0f}
                     : Color{0.70f, 0.18f, 0.13f, 1.0f});
        DrawRect(margin + 3 * (segment_width + gap), margin, segment_width, bar_height,
                 runtime.valid_package_count() > 0
                     ? Color{0.18f, 0.70f, 0.45f, 1.0f}
                     : Color{0.28f, 0.28f, 0.32f, 1.0f});

        const int marker_size = std::max(24, height_ / 14);
        const int center_x = width_ / 2 + static_cast<int>(runtime.analog().left_x * width_ * 0.16f);
        const int center_y = height_ / 2 - static_cast<int>(runtime.analog().left_y * height_ * 0.16f);
        DrawRect(center_x - marker_size / 2, center_y - marker_size / 2,
                 marker_size, marker_size, {0.86f, 0.68f, 0.28f, 1.0f});

        glDisable(GL_SCISSOR_TEST);
        eglSwapBuffers(display_, surface_);
    }

private:
    struct Color {
        float red;
        float green;
        float blue;
        float alpha;
    };

    void DrawRect(int x, int y, int width, int height, const Color& color) {
        if (width <= 0 || height <= 0) {
            return;
        }
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, y, width, height);
        glClearColor(color.red, color.green, color.blue, color.alpha);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLint width_ = 0;
    EGLint height_ = 0;
};

class AndroidShell {
public:
    explicit AndroidShell(android_app* app) : app_(app) {
        const char* base = app_->activity->externalDataPath;
        if (base == nullptr || base[0] == '\0') {
            base = app_->activity->internalDataPath;
        }
        game_root_ = std::filesystem::path(base == nullptr ? "." : base) / "game";
        runtime_.Initialize(game_root_);
        LogCatalog();
    }

    ~AndroidShell() {
        renderer_.Shutdown();
    }

    void Run() {
        app_->userData = this;
        app_->onAppCmd = [](android_app* app, int32_t command) {
            static_cast<AndroidShell*>(app->userData)->OnCommand(command);
        };

        android_app_set_key_event_filter(app_, FilterGamepadKey);
        android_app_set_motion_event_filter(app_, FilterJoystickMotion);
        EnableControllerAxes();

        const auto start = std::chrono::steady_clock::now();
        while (!app_->destroyRequested) {
            int events = 0;
            android_poll_source* source = nullptr;
            while (ALooper_pollOnce(renderer_.ready() ? 0 : -1, nullptr, &events,
                                    reinterpret_cast<void**>(&source)) >= 0) {
                if (source != nullptr) {
                    source->process(source->app, source);
                }
                if (app_->destroyRequested) {
                    break;
                }
            }
            ProcessInput();
            if (renderer_.ready()) {
                const float seconds = std::chrono::duration<float>(
                    std::chrono::steady_clock::now() - start
                ).count();
                renderer_.Render(runtime_, seconds);
            }
        }
    }

private:
    void OnCommand(int32_t command) {
        switch (command) {
            case APP_CMD_INIT_WINDOW:
                renderer_.Shutdown();
                renderer_.Initialize(app_->window);
                break;
            case APP_CMD_TERM_WINDOW:
                renderer_.Shutdown();
                break;
            default:
                break;
        }
    }

    void ProcessInput() {
        android_input_buffer* buffer = android_app_swap_input_buffers(app_);
        if (buffer == nullptr) {
            return;
        }

        for (std::uint64_t index = 0; index < buffer->keyEventsCount; ++index) {
            const GameActivityKeyEvent& event = buffer->keyEvents[index];
            const auto button = MapButton(event.keyCode);
            if (!button.has_value()) {
                continue;
            }
            const bool pressed = event.action == AKEY_EVENT_ACTION_DOWN;
            runtime_.SetButton(*button, pressed);
            if (*button == hp2::Button::Start && pressed) {
                runtime_.Initialize(game_root_);
                LogCatalog();
            }
        }

        for (std::uint64_t index = 0; index < buffer->motionEventsCount; ++index) {
            const GameActivityMotionEvent& event = buffer->motionEvents[index];
            if (!IsGamepadSource(event.source) || event.pointerCount == 0) {
                continue;
            }
            const GameActivityPointerAxes& axes = event.pointers[0];
            hp2::AnalogState analog;
            analog.left_x = Deadzone(GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_X));
            analog.left_y = Deadzone(GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_Y));
            const float z = GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_Z);
            const float rz = GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_RZ);
            const float rx = GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_RX);
            const float ry = GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_RY);
            analog.right_x = Deadzone(std::abs(z) >= std::abs(rx) ? z : rx);
            analog.right_y = Deadzone(std::abs(rz) >= std::abs(ry) ? rz : ry);
            analog.left_trigger = TriggerValue(
                GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_LTRIGGER),
                GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_BRAKE)
            );
            analog.right_trigger = TriggerValue(
                GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_RTRIGGER),
                GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_GAS)
            );
            analog.dpad_x = GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_HAT_X);
            analog.dpad_y = GameActivityPointerAxes_getAxisValue(&axes, AMOTION_EVENT_AXIS_HAT_Y);
            runtime_.SetAnalog(analog);
        }

        android_app_clear_key_events(buffer);
        android_app_clear_motion_events(buffer);
    }

    static void EnableControllerAxes() {
        constexpr std::array<int32_t, 12> axes = {
            AMOTION_EVENT_AXIS_X,
            AMOTION_EVENT_AXIS_Y,
            AMOTION_EVENT_AXIS_Z,
            AMOTION_EVENT_AXIS_RZ,
            AMOTION_EVENT_AXIS_RX,
            AMOTION_EVENT_AXIS_RY,
            AMOTION_EVENT_AXIS_LTRIGGER,
            AMOTION_EVENT_AXIS_RTRIGGER,
            AMOTION_EVENT_AXIS_BRAKE,
            AMOTION_EVENT_AXIS_GAS,
            AMOTION_EVENT_AXIS_HAT_X,
            AMOTION_EVENT_AXIS_HAT_Y
        };
        for (const int32_t axis : axes) {
            GameActivityPointerAxes_enableAxis(axis);
        }
    }

    void LogCatalog() const {
        const auto status = hp2::ToString(runtime_.status());
        LOGI("G0 root=%s candidates=%zu valid=%zu status=%.*s",
             game_root_.c_str(), runtime_.packages().size(), runtime_.valid_package_count(),
             static_cast<int>(status.size()), status.data());
    }

    android_app* app_ = nullptr;
    std::filesystem::path game_root_;
    hp2::Runtime runtime_;
    BootstrapRenderer renderer_;
};

}  // namespace

extern "C" __attribute__((visibility("default"))) void android_main(android_app* app) {
    AndroidShell shell(app);
    shell.Run();
}
