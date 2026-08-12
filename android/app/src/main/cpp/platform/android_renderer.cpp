#include <GLES3/gl3.h>
#include <android/log.h>
#include <algorithm>
#include <array>
#include <cstdint>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HP2Renderer", __VA_ARGS__)

namespace {
std::array<float, 16> g_projection{};
std::array<float, 16> g_modelView{};

void SetIdentity(std::array<float, 16>& matrix) {
    matrix.fill(0.0f);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}
}  // namespace

bool Renderer_Initialize() {
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    LOGI("OpenGL ES: %s", version ? version : "unknown");

    SetIdentity(g_projection);
    SetIdentity(g_modelView);

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
    if (m) {
        std::copy_n(m, g_projection.size(), g_projection.begin());
    }
}

void Renderer_SetModelViewMatrix(const float* m) {
    if (m) {
        std::copy_n(m, g_modelView.size(), g_modelView.begin());
    }
}

uint32_t Renderer_CreateTexture(int width, int height, const void* data, bool alpha) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    const GLenum fmt = alpha ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}

void Renderer_DestroyTexture(uint32_t tex) {
    const GLuint texture = tex;
    glDeleteTextures(1, &texture);
}

void Renderer_BindTexture(uint32_t tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
}
