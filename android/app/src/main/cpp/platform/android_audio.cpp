#include <aaudio/AAudio.h>
#include <android/log.h>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HP2Audio", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "HP2Audio", __VA_ARGS__)

struct AAudioBackend {
    AAudioStream* stream = nullptr;
    void (*callback)(float* buffer, int32_t numFrames) = nullptr;
};

static AAudioBackend g_audio;

static aaudio_data_callback_result_t AudioCallback(
    AAudioStream* stream, void* userData, void* audioData, int32_t numFrames) {
    if (g_audio.callback) {
        g_audio.callback(static_cast<float*>(audioData), numFrames);
    } else {
        memset(audioData, 0, numFrames * 2 * sizeof(float));
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

bool Audio_Initialize(int32_t sampleRate, int32_t channels) {
    AAudioStreamBuilder* builder;
    AAudio_createStreamBuilder(&builder);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setDataCallback(builder, AudioCallback, nullptr);

    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &g_audio.stream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK) {
        LOGE("AAudio open failed: %s", AAudio_convertResultToText(result));
        return false;
    }
    AAudioStream_requestStart(g_audio.stream);
    LOGI("AAudio: %dHz %dch", sampleRate, channels);
    return true;
}

void Audio_Shutdown() {
    if (g_audio.stream) {
        AAudioStream_requestStop(g_audio.stream);
        AAudioStream_close(g_audio.stream);
        g_audio.stream = nullptr;
    }
}

void Audio_SetCallback(void (*callback)(float* buffer, int32_t numFrames)) {
    g_audio.callback = callback;
}
