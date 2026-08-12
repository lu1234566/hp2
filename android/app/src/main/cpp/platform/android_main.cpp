#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <android/log.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <string>

#define LOG_TAG "HP2Engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern bool HP2_Initialize(const char* assetPath);
extern void HP2_Shutdown();
extern void HP2_Tick(float deltaTime);
extern void HP2_Render();
extern void HP2_Resize(int width, int height);

struct EngineState {
    android_app* app = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0;
    int height = 0;
    bool hasWindow = false;
    bool engineInitialized = false;
};

static bool InitializeEGL(EngineState* state) {
    if (!state || !state->app || !state->app->window) {
        return false;
    }

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };

    state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state->display == EGL_NO_DISPLAY || !eglInitialize(state->display, nullptr, nullptr)) {
        LOGE("Unable to initialize EGL display");
        return false;
    }

    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(state->display, attribs, &config, 1, &numConfigs) || numConfigs < 1) {
        LOGE("Unable to choose EGL config");
        return false;
    }

    EGLint format = 0;
    eglGetConfigAttrib(state->display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(state->app->window, 0, 0, format);

    state->surface = eglCreateWindowSurface(state->display, config, state->app->window, nullptr);
    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, contextAttribs);

    if (state->surface == EGL_NO_SURFACE || state->context == EGL_NO_CONTEXT ||
        eglMakeCurrent(state->display, state->surface, state->surface, state->context) == EGL_FALSE) {
        LOGE("Unable to create/make current EGL context");
        return false;
    }

    eglQuerySurface(state->display, state->surface, EGL_WIDTH, &state->width);
    eglQuerySurface(state->display, state->surface, EGL_HEIGHT, &state->height);
    state->hasWindow = true;

    LOGI("EGL initialized: %dx%d", state->width, state->height);
    return true;
}

static void ShutdownEGL(EngineState* state) {
    if (!state) return;

    if (state->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (state->context != EGL_NO_CONTEXT) {
            eglDestroyContext(state->display, state->context);
        }
        if (state->surface != EGL_NO_SURFACE) {
            eglDestroySurface(state->display, state->surface);
        }
        eglTerminate(state->display);
    }

    state->display = EGL_NO_DISPLAY;
    state->surface = EGL_NO_SURFACE;
    state->context = EGL_NO_CONTEXT;
    state->hasWindow = false;
    state->width = 0;
    state->height = 0;
}

static std::string AssetPathFor(const android_app* app) {
    if (app && app->activity && app->activity->externalDataPath) {
        return std::string(app->activity->externalDataPath) + "/hp2_assets";
    }
    if (app && app->activity && app->activity->internalDataPath) {
        return std::string(app->activity->internalDataPath) + "/hp2_assets";
    }
    return "hp2_assets";
}

static void HandleAppCommand(android_app* app, int32_t command) {
    auto* state = static_cast<EngineState*>(app->userData);
    if (!state) return;

    switch (command) {
        case APP_CMD_INIT_WINDOW: {
            if (!app->window || state->hasWindow) break;

            if (!InitializeEGL(state)) {
                LOGE("EGL initialization failed");
                break;
            }

            if (!state->engineInitialized) {
                const std::string assetPath = AssetPathFor(app);
                state->engineInitialized = HP2_Initialize(assetPath.c_str());
                if (!state->engineInitialized) {
                    LOGE("Failed to initialize HP2 engine");
                    ShutdownEGL(state);
                    break;
                }
            }

            HP2_Resize(state->width, state->height);
            break;
        }

        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED: {
            if (state->hasWindow && state->display != EGL_NO_DISPLAY) {
                eglQuerySurface(state->display, state->surface, EGL_WIDTH, &state->width);
                eglQuerySurface(state->display, state->surface, EGL_HEIGHT, &state->height);
                HP2_Resize(state->width, state->height);
            }
            break;
        }

        case APP_CMD_TERM_WINDOW:
            ShutdownEGL(state);
            break;

        default:
            break;
    }
}

extern "C" void android_main(struct android_app* app) {
    LOGI("android_main started");

    EngineState state;
    state.app = app;
    app->userData = &state;
    app->onAppCmd = HandleAppCommand;

    auto lastTime = std::chrono::steady_clock::now();

    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;

        while (ALooper_pollOnce(state.hasWindow ? 0 : -1, nullptr, &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }
            if (app->destroyRequested) {
                break;
            }
        }

        if (!state.hasWindow || !state.engineInitialized) {
            lastTime = std::chrono::steady_clock::now();
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        HP2_Tick(deltaTime);
        HP2_Render();
        eglSwapBuffers(state.display, state.surface);
    }

    if (state.engineInitialized) {
        HP2_Shutdown();
    }
    ShutdownEGL(&state);
    LOGI("android_main stopped");
}

// Engine scaffolding. These are intentionally small until the real HP2 engine
// modules are connected to the Android platform layer.
bool HP2_Initialize(const char* assetPath) {
    LOGI("HP2_Initialize scaffold: %s", assetPath ? assetPath : "(null)");
    return true;
}

void HP2_Shutdown() {
    LOGI("HP2_Shutdown scaffold");
}

void HP2_Tick(float deltaTime) {
    (void)deltaTime;
}

void HP2_Render() {
    glClearColor(0.0f, 0.0f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void HP2_Resize(int width, int height) {
    glViewport(0, 0, width, height);
    LOGI("HP2_Resize: %dx%d", width, height);
}

// Input scaffolding used by ControllerManager JNI until the UE1 input bridge is
// connected. Keeping it here makes controller plumbing testable without leaving
// unresolved native symbols in the APK.
void HP2_OnKeyEvent(int key, bool pressed) {
    LOGI("Controller key: %d %s", key, pressed ? "down" : "up");
}

void HP2_OnAnalogEvent(float lx, float ly, float rx, float ry,
                       float lt, float rt, float dpadX, float dpadY) {
    (void)lx;
    (void)ly;
    (void)rx;
    (void)ry;
    (void)lt;
    (void)rt;
    (void)dpadX;
    (void)dpadY;
}
