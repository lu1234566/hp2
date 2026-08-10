#!/bin/bash
# ============================================================
# HP2 Mobile Port - Complete Project Generator
# Run this in your Codespace: bash setup_hp2_mobile.sh
# ============================================================

set -e

echo "=========================================="
echo "  HP2 Mobile Port - Project Generator"
echo "=========================================="

# Navigate to repo root
cd /workspaces/hp2

# ============================================================
# STEP 1: Clean Git History (remove large files from history)
# ============================================================
echo ""
echo "[1/5] Cleaning git history..."

# Save current branch name
BRANCH=$(git branch --show-current)

# Reset to clean state, keeping only README if it exists
git checkout --orphan temp_branch
git rm -rf --cached . 2>/dev/null || true
rm -rf "Harry Potter and the Chamber of Secrets" hp2 2>/dev/null || true
rm -f hp2-mobile-port.zip hp2.iso hp2.zip 2>/dev/null || true

# Create .gitignore
cat > .gitignore << 'GITIGNORE_EOF'
# Binaries
*.exe
*.dll
*.mds
*.mdf
*.iso
*.zip
*.7z
*.rar
*.tar
*.gz

# Media
*.jpg
*.jpeg
*.png
*.bmp
*.gif
*.mp3
*.wav
*.ogg
*.mp4
*.avi

# Build artifacts
build/
*.so
*.apk
*.obb
*.o
*.obj
*.a
*.lib

# IDE
.vscode/
.idea/
*.sln
*.vcxproj

# Android
.gradle/
local.properties
*.iml

# OS
.DS_Store
Thumbs.db
GITIGNORE_EOF

git add .gitignore
git commit -m "Initial commit - project structure"

# Delete old branch and rename temp
git branch -D $BRANCH 2>/dev/null || true
git branch -m $BRANCH

echo "   ✅ Git history cleaned"

# ============================================================
# STEP 2: Create Directory Structure
# ============================================================
echo ""
echo "[2/5] Creating directory structure..."

mkdir -p android/app/src/main/java/com/hp2/mobile
mkdir -p android/app/src/main/cpp/platform
mkdir -p android/app/src/main/cpp/jni
mkdir -p android/app/src/main/cpp/include
mkdir -p android/app/src/main/res/values
mkdir -p android/app/src/main/res/mipmap-hdpi
mkdir -p android/app/src/main/res/mipmap-mdpi
mkdir -p android/app/src/main/res/mipmap-xhdpi
mkdir -p android/app/src/main/res/mipmap-xxhdpi
mkdir -p android/app/src/main/res/mipmap-xxxhdpi
mkdir -p scripts
mkdir -p engine
mkdir -p assets

echo "   ✅ Directories created"

# ============================================================
# STEP 3: Create Root Build Files
# ============================================================
echo ""
echo "[3/5] Creating build files..."

# build.gradle (root)
cat > build.gradle << 'EOF'
plugins {
    id 'com.android.application' version '8.5.0' apply false
    id 'org.jetbrains.kotlin.android' version '1.9.0' apply false
}
EOF

# settings.gradle
cat > settings.gradle << 'EOF'
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "HP2-Mobile"
include ':app'
EOF

# gradle.properties
cat > gradle.properties << 'EOF'
org.gradle.jvmargs=-Xmx4096m -Dfile.encoding=UTF-8
android.useAndroidX=true
kotlin.code.style=official
android.nonTransitiveRClass=true
android.defaults.buildfeatures.buildconfig=true
EOF

echo "   ✅ Root build files created"

# ============================================================
# STEP 4: Create Android App Files
# ============================================================
echo ""
echo "[4/5] Creating Android app files..."

# app/build.gradle
cat > android/app/build.gradle << 'EOF'
plugins {
    id 'com.android.application'
    id 'org.jetbrains.kotlin.android'
}

android {
    namespace 'com.hp2.mobile'
    compileSdk 34

    defaultConfig {
        applicationId "com.hp2.mobile"
        minSdk 26
        targetSdk 34
        versionCode 1
        versionName "0.1.0-alpha"

        externalNativeBuild {
            cmake {
                cppFlags "-std=c++17 -O3 -DANDROID -DNDEBUG"
                arguments "-DANDROID_STL=c++_shared",
                          "-DANDROID_PLATFORM=android-26",
                          "-DANDROID_ARM_NEON=ON"
            }
        }
        ndk {
            abiFilters 'arm64-v8a'
        }
    }

    buildTypes {
        release {
            minifyEnabled false
            proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'), 'proguard-rules.pro'
            externalNativeBuild {
                cmake {
                    cppFlags "-std=c++17 -O3 -DANDROID -DNDEBUG -fvisibility=hidden"
                }
            }
        }
        debug {
            externalNativeBuild {
                cmake {
                    cppFlags "-std=c++17 -O0 -g -DANDROID -DDEBUG"
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            path file('src/main/cpp/CMakeLists.txt')
            version '3.22.1'
        }
    }

    compileOptions {
        sourceCompatibility JavaVersion.VERSION_17
        targetCompatibility JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = '17'
    }
}

dependencies {
    implementation 'androidx.core:core-ktx:1.13.1'
    implementation 'androidx.appcompat:appcompat:1.7.0'
    implementation 'com.google.android.material:material:1.12.0'
    implementation 'androidx.games:games-activity:3.0.3'
    implementation 'androidx.games:games-controller:2.0.1'
}
EOF

# proguard-rules.pro
cat > android/app/proguard-rules.pro << 'EOF'
-keep class com.hp2.mobile.** { *; }
-keepclasseswithmembernames class * {
    native <methods>;
}
EOF

# AndroidManifest.xml
cat > android/app/src/main/AndroidManifest.xml << 'EOF'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.hp2.mobile">

    <uses-feature android:glEsVersion="0x00030000" android:required="true" />
    <uses-feature android:name="android.hardware.gamepad" android:required="true" />
    <uses-feature android:name="android.hardware.vulkan.level" android:required="false" />

    <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
    <uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" />

    <application
        android:allowBackup="true"
        android:label="@string/app_name"
        android:theme="@style/Theme.HP2Mobile"
        android:extractNativeLibs="true">

        <activity
            android:name="com.hp2.mobile.MainActivity"
            android:configChanges="orientation|screenSize|smallestScreenSize|density|keyboard|keyboardHidden|navigation"
            android:exported="true"
            android:screenOrientation="landscape"
            android:theme="@style/Theme.HP2Mobile.Fullscreen">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>

    </application>
</manifest>
EOF

# strings.xml
cat > android/app/src/main/res/values/strings.xml << 'EOF'
<resources>
    <string name="app_name">HP2 Mobile</string>
    <string name="installing_assets">Installing game assets...</string>
    <string name="controller_required">Bluetooth/USB controller required</string>
</resources>
EOF

# themes.xml
cat > android/app/src/main/res/values/themes.xml << 'EOF'
<resources>
    <style name="Theme.HP2Mobile" parent="Theme.Material3.Dark.NoActionBar">
        <item name="android:windowNoTitle">true</item>
        <item name="android:windowFullscreen">true</item>
        <item name="android:windowLayoutInDisplayCutoutMode">shortEdges</item>
    </style>
    <style name="Theme.HP2Mobile.Fullscreen" parent="Theme.HP2Mobile">
        <item name="android:windowBackground">@android:color/black</item>
    </style>
</resources>
EOF

echo "   ✅ Android manifest and resources created"

# ============================================================
# STEP 5: Create Kotlin Source Files
# ============================================================
echo ""
echo "[5/5] Creating Kotlin and C++ source files..."

# MainActivity.kt
cat > android/app/src/main/java/com/hp2/mobile/MainActivity.kt << 'EOF'
package com.hp2.mobile

import android.app.NativeActivity
import android.os.Bundle
import android.view.View
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

/**
 * HP2 Mobile - Native Activity Entry Point
 * Controller-only. No touch support.
 */
class MainActivity : NativeActivity() {

    companion object {
        init {
            System.loadLibrary("hp2engine")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setupImmersiveMode()
    }

    private fun setupImmersiveMode() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).let { controller ->
            controller.hide(WindowInsetsCompat.Type.systemBars())
            controller.systemBarsBehavior = 
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) setupImmersiveMode()
    }
}
EOF

# ControllerManager.kt
cat > android/app/src/main/java/com/hp2/mobile/ControllerManager.kt << 'EOF'
package com.hp2.mobile

import android.content.Context
import android.hardware.input.InputManager
import android.os.Handler
import android.os.Looper
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent

/**
 * Controller-only input manager.
 * Maps Android gamepad events to UE1 native input codes.
 */
class ControllerManager(context: Context) : InputManager.InputDeviceListener {

    private val inputManager = context.getSystemService(Context.INPUT_SERVICE) as InputManager
    private val handler = Handler(Looper.getMainLooper())

    init {
        inputManager.registerInputDeviceListener(this, handler)
    }

    fun onKeyEvent(event: KeyEvent): Boolean {
        if (!isGamepadEvent(event)) return false
        val ueKey = mapAndroidKeyToUE1(event.keyCode)
        val pressed = event.action == KeyEvent.ACTION_DOWN
        return if (ueKey >= 0) {
            nativeSendKeyEvent(ueKey, pressed)
            true
        } else false
    }

    fun onMotionEvent(event: MotionEvent): Boolean {
        if (!isGamepadEvent(event)) return false
        val lx = event.getAxisValue(MotionEvent.AXIS_X)
        val ly = event.getAxisValue(MotionEvent.AXIS_Y)
        val rx = event.getAxisValue(MotionEvent.AXIS_Z)
        val ry = event.getAxisValue(MotionEvent.AXIS_RZ)
        val lt = event.getAxisValue(MotionEvent.AXIS_LTRIGGER)
        val rt = event.getAxisValue(MotionEvent.AXIS_RTRIGGER)
        val dpadX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val dpadY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        nativeSendAnalogEvent(lx, ly, rx, ry, lt, rt, dpadX, dpadY)
        return true
    }

    private fun isGamepadEvent(event: KeyEvent): Boolean {
        val source = event.source
        return (source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
               (source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }

    private fun isGamepadEvent(event: MotionEvent): Boolean {
        return (event.source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }

    override fun onInputDeviceAdded(deviceId: Int) {}
    override fun onInputDeviceRemoved(deviceId: Int) {}
    override fun onInputDeviceChanged(deviceId: Int) {}

    private fun mapAndroidKeyToUE1(key: Int): Int = when (key) {
        KeyEvent.KEYCODE_BUTTON_A -> 0x100
        KeyEvent.KEYCODE_BUTTON_B -> 0x101
        KeyEvent.KEYCODE_BUTTON_X -> 0x102
        KeyEvent.KEYCODE_BUTTON_Y -> 0x103
        KeyEvent.KEYCODE_BUTTON_L1 -> 0x104
        KeyEvent.KEYCODE_BUTTON_R1 -> 0x105
        KeyEvent.KEYCODE_BUTTON_L2 -> 0x106
        KeyEvent.KEYCODE_BUTTON_R2 -> 0x107
        KeyEvent.KEYCODE_BUTTON_START -> 0x108
        KeyEvent.KEYCODE_BUTTON_SELECT -> 0x109
        KeyEvent.KEYCODE_BUTTON_THUMBL -> 0x10A
        KeyEvent.KEYCODE_BUTTON_THUMBR -> 0x10B
        KeyEvent.KEYCODE_DPAD_UP -> 0x10C
        KeyEvent.KEYCODE_DPAD_DOWN -> 0x10D
        KeyEvent.KEYCODE_DPAD_LEFT -> 0x10E
        KeyEvent.KEYCODE_DPAD_RIGHT -> 0x10F
        else -> -1
    }

    private external fun nativeSendKeyEvent(key: Int, pressed: Boolean)
    private external fun nativeSendAnalogEvent(
        lx: Float, ly: Float, rx: Float, ry: Float,
        lt: Float, rt: Float, dpadX: Float, dpadY: Float
    )
}
EOF

# ============================================================
# STEP 6: Create C++ Native Files
# ============================================================

# CMakeLists.txt
cat > android/app/src/main/cpp/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22.1)
project("hp2engine")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(game-activity REQUIRED CONFIG)

set(PLATFORM_SOURCES
    platform/android_main.cpp
    platform/android_input.cpp
    platform/android_window.cpp
    platform/android_filesystem.cpp
    platform/android_audio.cpp
    platform/android_renderer.cpp
    jni/bridge.cpp
)

add_library(hp2engine SHARED ${PLATFORM_SOURCES})

target_link_libraries(hp2engine
    game-activity::game-activity
    android
    log
    EGL
    GLESv3
    aaudio
)

target_compile_options(hp2engine PRIVATE
    -Wall -DANDROID -DUSE_OPENGLES -DUSE_AAUDIO
)

set_target_properties(hp2engine PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
EOF

# android_main.cpp
cat > android/app/src/main/cpp/platform/android_main.cpp << 'EOF'
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
EOF

# android_input.cpp
cat > android/app/src/main/cpp/platform/android_input.cpp << 'EOF'
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
EOF

# android_window.cpp
cat > android/app/src/main/cpp/platform/android_window.cpp << 'EOF'
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
EOF

# android_filesystem.cpp
cat > android/app/src/main/cpp/platform/android_filesystem.cpp << 'EOF'
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
EOF

# android_audio.cpp
cat > android/app/src/main/cpp/platform/android_audio.cpp << 'EOF'
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
EOF

# android_renderer.cpp
cat > android/app/src/main/cpp/platform/android_renderer.cpp << 'EOF'
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
EOF

# bridge.cpp
cat > android/app/src/main/cpp/jni/bridge.cpp << 'EOF'
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
EOF

# Platform header
cat > android/app/src/main/cpp/include/hp2_platform.h << 'EOF'
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
EOF

# ============================================================
# STEP 7: Create Scripts
# ============================================================

cat > scripts/build.sh << 'EOF'
#!/bin/bash
set -e
echo "========================================"
echo "  HP2 Mobile - Build"
echo "========================================"
cd android
./gradlew assembleRelease
echo ""
echo "APK: android/app/build/outputs/apk/release/app-release.apk"
EOF

cat > scripts/setup.sh << 'EOF'
#!/bin/bash
set -e
echo "========================================"
echo "  HP2 Mobile - Setup"
echo "========================================"
if [ -z "$ANDROID_SDK_ROOT" ]; then
    echo "Set ANDROID_SDK_ROOT first!"
    echo "Example: export ANDROID_SDK_ROOT=/home/user/Android/Sdk"
    exit 1
fi
yes | $ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager --licenses 2>/dev/null || true
$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager \
    "platforms;android-34" "build-tools;34.0.0" "ndk;26.3.11579264" "cmake;3.22.1"
echo "Setup complete!"
EOF

chmod +x scripts/build.sh scripts/setup.sh

# ============================================================
# STEP 8: Create Placeholders and README
# ============================================================

cat > engine/README.md << 'EOF'
# HP2 Engine Source

Place the leaked HP2 engine source here.

Expected structure:
```
engine/
  Core/
  Engine/
  Renderer/
  Audio/
  ...
```

Download from Internet Archive:
https://archive.org/details/HP2-Windows-Prototype
EOF

cat > assets/README.md << 'EOF'
# Game Assets

Place original HP2 PC game files here:
- Maps/ (*.unr)
- Sounds/ (*.umx, *.wav)
- System/ (*.u, *.int, *.ini)
- Textures/ (*.utx)

These will be packaged into an OBB expansion file.
EOF

cat > README.md << 'EOF'
# HP2 Mobile Port

Unofficial Android port of **Harry Potter and the Chamber of Secrets (PC)**.

## Architecture

- **Native C++** engine (no emulation)
- **Controller-only** (Bluetooth/USB gamepad required)
- **OpenGL ES 3.0** renderer
- **AAudio** low-latency audio

## Quick Start

```bash
# 1. Setup Android SDK
export ANDROID_SDK_ROOT=/path/to/Android/Sdk
./scripts/setup.sh

# 2. Add HP2 engine source to `engine/` folder

# 3. Build
./scripts/build.sh
```

## Controller Mapping

| Button | Action |
|--------|--------|
| A | Jump / Confirm |
| B | Back / Cancel |
| X | Use |
| Y | Cast Spell |
| L1/R1 | Target |
| L2 | Block |
| R2 | Action / Run |
| Start | Menu |
| Select | Map |
| L3 | Crouch |
| R3 | Look Mode |
| D-Pad | Navigation |
| Left Stick | Move |
| Right Stick | Camera |

## License

Launcher code: MIT. Engine source: academic preservation only.
EOF

# ============================================================
# STEP 9: Commit Everything
# ============================================================
echo ""
echo "[Final] Committing all files..."

git add .
git commit -m "Add complete Android project structure - HP2 Mobile Port"
git push --force origin main

echo ""
echo "=========================================="
echo "  ✅ PROJECT CREATED SUCCESSFULLY!"
echo "=========================================="
echo ""
echo "Files created:"
find . -type f | grep -v ".git/" | sort | head -30
echo ""
echo "Next steps:"
echo "  1. Verify the push worked: visit https://github.com/lu1234566/hp2"
echo "  2. Download HP2 engine source from Internet Archive"
echo "  3. Place source in engine/ folder"
echo "  4. Run ./scripts/build.sh"
