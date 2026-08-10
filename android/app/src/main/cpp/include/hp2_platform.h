#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool Platform_CreateWindow(int width, int height, bool fullscreen);
void Platform_DestroyWindow();
void Platform_SwapBuffers();
void Platform_SetWindowTitle(const char* title);
void Platform_ShowCursor(bool show);
void Platform_CaptureMouse(bool capture);

void FS_SetAssetPath(const char* path);
bool FS_FileExists(const char* filename);
size_t FS_FileSize(const char* filename);
bool FS_ReadFile(const char* filename, void* buffer, size_t size);

bool Audio_Initialize(int32_t sampleRate, int32_t channels);
void Audio_Shutdown();
void Audio_SetCallback(void (*callback)(float* buffer, int32_t numFrames));

bool Renderer_Initialize();
void Renderer_SetViewport(int x, int y, int width, int height);
void Renderer_Clear(float r, float g, float b, float a, bool clearDepth);
void Renderer_SetProjectionMatrix(const float* matrix);
void Renderer_SetModelViewMatrix(const float* matrix);
uint32_t Renderer_CreateTexture(int width, int height, const void* data, bool alpha);
void Renderer_DestroyTexture(uint32_t tex);
void Renderer_BindTexture(uint32_t tex);

#ifdef __cplusplus
}
#endif
