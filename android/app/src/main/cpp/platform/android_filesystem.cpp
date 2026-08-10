#include <sys/stat.h>
#include <fstream>
#include <string>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HP2FS", __VA_ARGS__)

static std::string g_assetBasePath;

void FS_SetAssetPath(const char* path) {
    g_assetBasePath = path;
    LOGI("Asset path: %s", path);
}

bool FS_FileExists(const char* filename) {
    struct stat st;
    return stat((g_assetBasePath + "/" + filename).c_str(), &st) == 0;
}

size_t FS_FileSize(const char* filename) {
    struct stat st;
    return (stat((g_assetBasePath + "/" + filename).c_str(), &st) == 0) ? st.st_size : 0;
}

bool FS_ReadFile(const char* filename, void* buffer, size_t size) {
    std::ifstream file(g_assetBasePath + "/" + filename, std::ios::binary);
    if (!file) return false;
    file.read(static_cast<char*>(buffer), size);
    return file.gcount() == size;
}
