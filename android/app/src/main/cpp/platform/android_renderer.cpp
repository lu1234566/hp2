#include <GLES3/gl3.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HP2Renderer", __VA_ARGS__)

bool Renderer_Initialize() {
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    LOGI("OpenGL ES: %s", version);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    return true;
}

void Renderer_SetViewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

void Renderer_Clear(float r, float g, float b, float a, bool clearDepth) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | (clearDepth ? GL_DEPTH_BUFFER_BIT : 0));
}

void Renderer_SetProjectionMatrix(const float* m) {
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m);
}

void Renderer_SetModelViewMatrix(const float* m) {
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
}

uint32_t Renderer_CreateTexture(int width, int height, const void* data, bool alpha) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GLenum fmt = alpha ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}

void Renderer_DestroyTexture(uint32_t tex) {
    GLuint t = tex;
    glDeleteTextures(1, &t);
}

void Renderer_BindTexture(uint32_t tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
}
