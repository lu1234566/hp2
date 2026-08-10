#include <jni.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HP2JNI", __VA_ARGS__)

extern bool HP2_Initialize(const char* assetPath);
extern void HP2_Shutdown();

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_hp2_mobile_MainActivity_nativeInitializeEngine(JNIEnv* env, jobject thiz, jstring assetPath) {
    const char* path = env->GetStringUTFChars(assetPath, nullptr);
    bool result = HP2_Initialize(path);
    env->ReleaseStringUTFChars(assetPath, path);
    return result;
}

JNIEXPORT void JNICALL
Java_com_hp2_mobile_MainActivity_nativeShutdownEngine(JNIEnv* env, jobject thiz) {
    HP2_Shutdown();
}

} // extern "C"
