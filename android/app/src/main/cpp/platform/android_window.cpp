#include <android/native_window.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HP2Window", __VA_ARGS__)

bool Platform_CreateWindow(int width, int height, bool fullscreen) {
    LOGI("Window: %dx%d", width, height);
    return true;
}

void Platform_DestroyWindow() {}
void Platform_SwapBuffers() {}
void Platform_SetWindowTitle(const char* title) {}
void Platform_ShowCursor(bool show) {}
void Platform_CaptureMouse(bool capture) {}
