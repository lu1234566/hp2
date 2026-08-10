#include <jni.h>
#include <android/log.h>
#include <cmath>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HP2Input", __VA_ARGS__)

extern void HP2_OnKeyEvent(int key, bool pressed);
extern void HP2_OnAnalogEvent(float lx, float ly, float rx, float ry,
                               float lt, float rt, float dpadX, float dpadY);

extern "C" {

JNIEXPORT void JNICALL
Java_com_hp2_mobile_ControllerManager_nativeSendKeyEvent(
    JNIEnv* env, jobject thiz, jint key, jboolean pressed) {
    HP2_OnKeyEvent(key, pressed);
}

JNIEXPORT void JNICALL
Java_com_hp2_mobile_ControllerManager_nativeSendAnalogEvent(
    JNIEnv* env, jobject thiz, jfloat lx, jfloat ly, jfloat rx, jfloat ry,
    jfloat lt, jfloat rt, jfloat dpadX, jfloat dpadY) {
    auto dz = [](float v) {
        const float d = 0.15f;
        return (std::abs(v) < d) ? 0.0f : (v - (v > 0 ? d : -d)) / (1.0f - d);
    };
    HP2_OnAnalogEvent(dz(lx), dz(ly), dz(rx), dz(ry), lt, rt, dpadX, dpadY);
}

} // extern "C"
