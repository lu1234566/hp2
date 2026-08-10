#include <game-activity/GameActivity.cpp>
#include <game-text-input/gametextinput.cpp>
#include <android/log.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <memory>
#include <thread>
#include <chrono>

#define LOG_TAG "HP2Engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern bool HP2_Initialize(const char* assetPath);
extern void HP2_Shutdown();
extern void HP2_Tick(float deltaTime);
extern void HP2_Render();
extern void HP2_Resize(int width, int height);

struct EngineState {
    ANativeWindow* window = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0;
    int height = 0;
    bool running = false;
    std::thread gameThread;
};

static std::unique_ptr<EngineState> g_state;

static bool InitializeEGL(EngineState* state) {
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
    eglInitialize(state->display, nullptr, nullptr);

    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(state->display, attribs, &config, 1, &numConfigs);

    EGLint format;
    eglGetConfigAttrib(state->display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(state->window, 0, 0, format);

    state->surface = eglCreateWindowSurface(state->display, config, state->window, nullptr);

    const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, contextAttribs);

    if (eglMakeCurrent(state->display, state->surface, state->surface, state->context) == EGL_FALSE) {
        LOGE("Unable to eglMakeCurrent");
        return false;
    }

    eglQuerySurface(state->display, state->surface, EGL_WIDTH, &state->width);
    eglQuerySurface(state->display, state->surface, EGL_HEIGHT, &state->height);

    LOGI("EGL initialized: %dx%d", state->width, state->height);
    return true;
}

static void ShutdownEGL(EngineState* state) {
    if (state->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (state->context != EGL_NO_CONTEXT) eglDestroyContext(state->display, state->context);
        if (state->surface != EGL_NO_SURFACE) eglDestroySurface(state->display, state->surface);
        eglTerminate(state->display);
    }
    state->display = EGL_NO_DISPLAY;
    state->context = EGL_NO_CONTEXT;
    state->surface = EGL_NO_SURFACE;
}

static void GameLoop(EngineState* state) {
    const char* assetPath = "/sdcard/Android/data/com.hp2.mobile/files/hp2_assets";
    if (!HP2_Initialize(assetPath)) {
        LOGE("Failed to initialize HP2 engine");
        return;
    }

    HP2_Resize(state->width, state->height);
    auto lastTime = std::chrono::steady_clock::now();

    while (state->running) {
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        HP2_Tick(deltaTime);
        HP2_Render();
        eglSwapBuffers(state->display, state->surface);
    }

    HP2_Shutdown();
}

extern "C" {

void GameActivity_onCreate(GameActivity* activity, void* savedState, size_t savedStateSize) {
    LOGI("GameActivity_onCreate");
    g_state = std::make_unique<EngineState>();
    GameActivity_onCreate_C(activity, savedState, savedStateSize);
}

void GameActivity_onDestroy(GameActivity* activity) {
    LOGI("GameActivity_onDestroy");
    if (g_state) {
        g_state->running = false;
        if (g_state->gameThread.joinable()) g_state->gameThread.join();
        ShutdownEGL(g_state.get());
        g_state.reset();
    }
    GameActivity_onDestroy_C(activity);
}

void GameActivity_onStart(GameActivity* activity) { GameActivity_onStart_C(activity); }
void GameActivity_onResume(GameActivity* activity) { GameActivity_onResume_C(activity); }
void GameActivity_onPause(GameActivity* activity) { GameActivity_onPause_C(activity); }
void GameActivity_onStop(GameActivity* activity) { GameActivity_onStop_C(activity); }
void GameActivity_onWindowFocusChanged(GameActivity* activity, int focused) {
    GameActivity_onWindowFocusChanged_C(activity, focused);
}

void GameActivity_onNativeWindowCreated(GameActivity* activity, ANativeWindow* window) {
    LOGI("Native window created");
    g_state->window = window;
    if (InitializeEGL(g_state.get())) {
        g_state->running = true;
        g_state->gameThread = std::thread(GameLoop, g_state.get());
    }
}

void GameActivity_onNativeWindowDestroyed(GameActivity* activity, ANativeWindow* window) {
    LOGI("Native window destroyed");
    g_state->running = false;
    if (g_state->gameThread.joinable()) g_state->gameThread.join();
    ShutdownEGL(g_state.get());
    g_state->window = nullptr;
}

void GameActivity_onNativeWindowResized(GameActivity* activity, ANativeWindow* window) {
    if (g_state && g_state->display != EGL_NO_DISPLAY) {
        eglQuerySurface(g_state->display, g_state->surface, EGL_WIDTH, &g_state->width);
        eglQuerySurface(g_state->display, g_state->surface, EGL_HEIGHT, &g_state->height);
        HP2_Resize(g_state->width, g_state->height);
    }
}

} // extern "C"

// Engine stubs - replace with real engine source
bool HP2_Initialize(const char* assetPath) {
    LOGI("HP2_Initialize stub: %s", assetPath);
    return true;
}

void HP2_Shutdown() { LOGI("HP2_Shutdown stub"); }
void HP2_Tick(float deltaTime) {}

void HP2_Render() {
    glClearColor(0.0f, 0.0f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void HP2_Resize(int width, int height) {
    glViewport(0, 0, width, height);
    LOGI("HP2_Resize: %dx%d", width, height);
}
