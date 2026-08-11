#include "hp2/runtime.h"
#include "hp2/ue_model.h"
#include "hp2/ue_texture.h"

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
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

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

std::optional<std::filesystem::path> FindMap(
    const std::filesystem::path& root,
    const std::string& preferred_name
) {
    std::error_code error_code;
    if (!std::filesystem::is_directory(root, error_code)) {
        return std::nullopt;
    }
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator(root, options, error_code);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (error_code) {
            error_code.clear();
            iterator.increment(error_code);
            continue;
        }
        if (iterator->is_regular_file(error_code) && !error_code) {
            std::string filename = iterator->path().filename().string();
            std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (filename == preferred_name) {
                return iterator->path();
            }
        }
        iterator.increment(error_code);
    }
    return std::nullopt;
}

class BootstrapRenderer {
public:
    void SetScene(const hp2::ModelGeometry& geometry, const hp2::DecodedTexture& texture) {
        mesh_vertices_.clear();
        texture_pixels_.clear();
        texture_width_ = 0;
        texture_height_ = 0;
        if (texture.valid && texture.width > 0 && texture.height > 0
            && texture.rgba_pixels.size() == static_cast<std::size_t>(texture.width)
                * static_cast<std::size_t>(texture.height) * 4u) {
            texture_pixels_ = texture.rgba_pixels;
            texture_width_ = texture.width;
            texture_height_ = texture.height;
        }
        if (!geometry.valid || geometry.triangles.empty()) {
            if (ready()) {
                UploadMesh();
                UploadTexture();
            }
            return;
        }

        const hp2::Vec3 center = {
            (geometry.bounds_min.x + geometry.bounds_max.x) * 0.5f,
            (geometry.bounds_min.y + geometry.bounds_max.y) * 0.5f,
            (geometry.bounds_min.z + geometry.bounds_max.z) * 0.5f
        };
        const float extent_x = geometry.bounds_max.x - geometry.bounds_min.x;
        const float extent_y = geometry.bounds_max.y - geometry.bounds_min.y;
        const float extent_z = geometry.bounds_max.z - geometry.bounds_min.z;
        const float largest_extent = std::max({extent_x, extent_y, extent_z});
        if (!std::isfinite(largest_extent) || largest_extent <= 0.0f) {
            return;
        }
        const float scale = 1.75f / largest_extent;
        mesh_vertices_.reserve(geometry.triangles.size() * 18u);
        for (const hp2::BspTriangle& triangle : geometry.triangles) {
            const std::array<std::uint32_t, 3> indices = {triangle.a, triangle.b, triangle.c};
            std::array<hp2::TextureCoordinate, 3> coordinates{};
            bool textured = texture.valid && triangle.surface_index >= 0
                && static_cast<std::size_t>(triangle.surface_index) < geometry.surfaces.size()
                && geometry.surfaces[static_cast<std::size_t>(triangle.surface_index)].material_index
                    == texture.map_material_index;
            if (textured) {
                for (std::size_t corner = 0; corner < indices.size(); ++corner) {
                    textured = hp2::ComputeSurfaceTextureCoordinate(
                        geometry, triangle.surface_index, indices[corner],
                        texture.width, texture.height, coordinates[corner]
                    );
                    if (!textured) {
                        break;
                    }
                }
            }
            for (std::size_t corner = 0; corner < indices.size(); ++corner) {
                const std::uint32_t point_index = indices[corner];
                if (point_index >= geometry.points.size()) {
                    mesh_vertices_.clear();
                    return;
                }
                const hp2::Vec3& point = geometry.points[point_index];
                mesh_vertices_.push_back((point.x - center.x) * scale);
                mesh_vertices_.push_back((point.y - center.y) * scale);
                mesh_vertices_.push_back((point.z - center.z) * scale);
                mesh_vertices_.push_back(coordinates[corner].u);
                mesh_vertices_.push_back(coordinates[corner].v);
                mesh_vertices_.push_back(textured ? 1.0f : 0.0f);
            }
        }
        if (ready()) {
            UploadMesh();
            UploadTexture();
        }
    }

    bool has_geometry() const {
        return mesh_vertex_count_ > 0;
    }

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
        if (!InitializePipeline()) {
            LOGE("G3 mesh pipeline initialization failed");
            Shutdown();
            return false;
        }
        UploadMesh();
        UploadTexture();
        LOGI("G3 renderer ready: %dx%d, %s, mesh_vertices=%d, texture=%dx%d", width_, height_,
             reinterpret_cast<const char*>(glGetString(GL_VERSION)), mesh_vertex_count_,
             texture_width_, texture_height_);
        return true;
    }

    void Shutdown() {
        if (display_ != EGL_NO_DISPLAY) {
            if (context_ != EGL_NO_CONTEXT && surface_ != EGL_NO_SURFACE) {
                eglMakeCurrent(display_, surface_, surface_, context_);
                DestroyPipeline();
            }
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
        mesh_vertex_count_ = 0;
    }

    bool ready() const {
        return display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE && context_ != EGL_NO_CONTEXT;
    }

    void Render(const hp2::Runtime& runtime, float seconds) {
        if (!ready()) {
            return;
        }

        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, width_, height_);
        if (has_geometry()) {
            glClearColor(0.015f, 0.025f, 0.060f, 1.0f);
        } else {
            switch (runtime.status()) {
                case hp2::BootStatus::WaitingForController: glClearColor(0.025f, 0.055f, 0.12f, 1.0f); break;
                case hp2::BootStatus::MissingGameData: glClearColor(0.12f, 0.045f, 0.025f, 1.0f); break;
                case hp2::BootStatus::IncompatibleGameData: glClearColor(0.10f, 0.025f, 0.12f, 1.0f); break;
                case hp2::BootStatus::PackageProbeReady: glClearColor(0.018f, 0.10f, 0.075f, 1.0f); break;
            }
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (has_geometry()) {
            DrawGeometry(seconds, runtime.analog().right_x);
        }
        glDisable(GL_DEPTH_TEST);

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

        if (!has_geometry()) {
            const int marker_size = std::max(24, height_ / 14);
            const int center_x = width_ / 2 + static_cast<int>(runtime.analog().left_x * width_ * 0.16f);
            const int center_y = height_ / 2 - static_cast<int>(runtime.analog().left_y * height_ * 0.16f);
            DrawRect(center_x - marker_size / 2, center_y - marker_size / 2,
                     marker_size, marker_size, {0.86f, 0.68f, 0.28f, 1.0f});
        }

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

    static GLuint CompileShader(GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_TRUE) {
            return shader;
        }
        std::array<char, 1024> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        LOGE("Shader compilation failed: %s", log.data());
        glDeleteShader(shader);
        return 0;
    }

    bool InitializePipeline() {
        static constexpr char kVertexShader[] = R"glsl(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in float aTextured;
uniform float uYaw;
uniform float uAspect;
out float vHeight;
out highp vec2 vTexCoord;
out float vTextured;
void main() {
    float c = cos(uYaw);
    float s = sin(uYaw);
    vec3 rotated = vec3(
        c * aPosition.x - s * aPosition.y,
        s * aPosition.x + c * aPosition.y,
        aPosition.z
    );
    float x = rotated.x / max(uAspect, 1.0);
    float y = rotated.z * 0.82 - rotated.y * 0.28;
    gl_Position = vec4(x * 0.92, y * 0.92, rotated.y * 0.35, 1.0);
    vHeight = clamp(aPosition.z * 0.55 + 0.5, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vTextured = aTextured;
}
)glsl";
        static constexpr char kFragmentShader[] = R"glsl(#version 300 es
precision mediump float;
in float vHeight;
in highp vec2 vTexCoord;
in float vTextured;
uniform sampler2D uTexture;
out vec4 outColor;
void main() {
    vec3 low = vec3(0.10, 0.42, 0.34);
    vec3 high = vec3(0.86, 0.69, 0.29);
    vec3 diagnostic = mix(low, high, vHeight);
    vec3 original = texture(uTexture, vTexCoord).rgb;
    outColor = vec4(vTextured > 0.5 ? original : diagnostic, 1.0);
}
)glsl";

        const GLuint vertex_shader = CompileShader(GL_VERTEX_SHADER, kVertexShader);
        const GLuint fragment_shader = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
        if (vertex_shader == 0 || fragment_shader == 0) {
            if (vertex_shader != 0) {
                glDeleteShader(vertex_shader);
            }
            if (fragment_shader != 0) {
                glDeleteShader(fragment_shader);
            }
            return false;
        }

        program_ = glCreateProgram();
        glAttachShader(program_, vertex_shader);
        glAttachShader(program_, fragment_shader);
        glLinkProgram(program_);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);

        GLint linked = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            std::array<char, 1024> log{};
            glGetProgramInfoLog(program_, static_cast<GLsizei>(log.size()), nullptr, log.data());
            LOGE("Shader program link failed: %s", log.data());
            glDeleteProgram(program_);
            program_ = 0;
            return false;
        }
        yaw_uniform_ = glGetUniformLocation(program_, "uYaw");
        aspect_uniform_ = glGetUniformLocation(program_, "uAspect");
        texture_uniform_ = glGetUniformLocation(program_, "uTexture");
        return yaw_uniform_ >= 0 && aspect_uniform_ >= 0 && texture_uniform_ >= 0;
    }

    void DestroyPipeline() {
        if (vertex_buffer_ != 0) {
            glDeleteBuffers(1, &vertex_buffer_);
            vertex_buffer_ = 0;
        }
        if (vertex_array_ != 0) {
            glDeleteVertexArrays(1, &vertex_array_);
            vertex_array_ = 0;
        }
        if (texture_id_ != 0) {
            glDeleteTextures(1, &texture_id_);
            texture_id_ = 0;
        }
        if (program_ != 0) {
            glDeleteProgram(program_);
            program_ = 0;
        }
        mesh_vertex_count_ = 0;
        yaw_uniform_ = -1;
        aspect_uniform_ = -1;
        texture_uniform_ = -1;
    }

    void UploadTexture() {
        if (texture_id_ != 0) {
            glDeleteTextures(1, &texture_id_);
            texture_id_ = 0;
        }
        if (program_ == 0 || texture_pixels_.empty() || texture_width_ <= 0 || texture_height_ <= 0) {
            return;
        }
        glGenTextures(1, &texture_id_);
        glBindTexture(GL_TEXTURE_2D, texture_id_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_width_, texture_height_, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, texture_pixels_.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        LOGI("G3 original texture uploaded: %dx%d, rgba_bytes=%zu",
             texture_width_, texture_height_, texture_pixels_.size());
    }

    void UploadMesh() {
        if (vertex_buffer_ != 0) {
            glDeleteBuffers(1, &vertex_buffer_);
            vertex_buffer_ = 0;
        }
        if (vertex_array_ != 0) {
            glDeleteVertexArrays(1, &vertex_array_);
            vertex_array_ = 0;
        }
        mesh_vertex_count_ = 0;
        if (program_ == 0 || mesh_vertices_.empty()) {
            return;
        }

        const std::size_t count = mesh_vertices_.size() / 6u;
        if (count > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
            LOGE("G3 mesh has too many vertices for OpenGL ES");
            return;
        }
        glGenVertexArrays(1, &vertex_array_);
        glGenBuffers(1, &vertex_buffer_);
        glBindVertexArray(vertex_array_);
        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh_vertices_.size() * sizeof(float)),
                     mesh_vertices_.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(5 * sizeof(float)));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        mesh_vertex_count_ = static_cast<GLsizei>(count);
        LOGI("G3 BSP mesh uploaded: vertices=%d triangles=%d",
             mesh_vertex_count_, mesh_vertex_count_ / 3);
    }

    void DrawGeometry(float seconds, float controller_yaw) {
        if (program_ == 0 || vertex_array_ == 0 || mesh_vertex_count_ <= 0) {
            return;
        }
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_CULL_FACE);
        glUseProgram(program_);
        glUniform1f(yaw_uniform_, seconds * 0.075f + controller_yaw * 0.85f);
        glUniform1f(aspect_uniform_, height_ > 0
            ? static_cast<float>(width_) / static_cast<float>(height_)
            : 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_id_);
        glUniform1i(texture_uniform_, 0);
        glBindVertexArray(vertex_array_);
        glDrawArrays(GL_TRIANGLES, 0, mesh_vertex_count_);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    }

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
    GLuint program_ = 0;
    GLuint vertex_array_ = 0;
    GLuint vertex_buffer_ = 0;
    GLuint texture_id_ = 0;
    GLint yaw_uniform_ = -1;
    GLint aspect_uniform_ = -1;
    GLint texture_uniform_ = -1;
    GLsizei mesh_vertex_count_ = 0;
    std::vector<float> mesh_vertices_;
    std::vector<std::uint8_t> texture_pixels_;
    GLsizei texture_width_ = 0;
    GLsizei texture_height_ = 0;
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
        LoadGeometry();
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
                LoadGeometry();
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

    void LoadGeometry() {
        const auto map_path = FindMap(game_root_, "duel10.unr");
        if (!map_path.has_value()) {
            geometry_ = {};
            texture_ = {};
            renderer_.SetScene(geometry_, texture_);
            LOGI("G3 Duel10.unr is not installed; keeping diagnostic renderer");
            return;
        }
        const hp2::PackageIndex map_package = hp2::LoadPackageIndex(*map_path);
        const std::size_t model_export = hp2::FindLargestModelExport(map_package);
        if (model_export < map_package.exports.size()) {
            geometry_ = hp2::LoadModelGeometry(map_package, model_export);
        } else {
            geometry_ = {};
            geometry_.error = map_package.valid
                ? "package contains no serialized Model export"
                : map_package.error;
        }
        texture_ = hp2::LoadFirstSurfaceTexture(game_root_, map_package, geometry_);
        renderer_.SetScene(geometry_, texture_);
        if (geometry_.valid) {
            LOGI("G3 BSP ready: map=%s model=%s points=%zu nodes=%zu triangles=%zu",
                 map_path->c_str(), geometry_.object_name.c_str(), geometry_.points.size(),
                 geometry_.nodes.size(), geometry_.triangles.size());
            if (texture_.valid) {
                LOGI("G3 texture ready: package=%s object=%s palette=%s size=%dx%d triangles=%zu",
                     texture_.package_name.c_str(), texture_.object_name.c_str(),
                     texture_.palette_name.c_str(), texture_.width, texture_.height,
                     texture_.triangle_count);
            } else {
                LOGE("G3 texture decode failed: %s", texture_.error.c_str());
            }
        } else {
            LOGE("G3 BSP decode failed: map=%s error=%s",
                 map_path->c_str(), geometry_.error.c_str());
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
    hp2::ModelGeometry geometry_;
    hp2::DecodedTexture texture_;
    BootstrapRenderer renderer_;
};

}  // namespace

extern "C" __attribute__((visibility("default"))) void android_main(android_app* app) {
    AndroidShell shell(app);
    shell.Run();
}
