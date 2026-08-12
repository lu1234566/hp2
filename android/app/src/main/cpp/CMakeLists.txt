cmake_minimum_required(VERSION 3.22.1)
project("hp2engine")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(game-activity REQUIRED CONFIG)

# GameActivity's native_app_glue must come from the same AAR/Prefab package as
# GameActivity itself. Do not use the NDK NativeActivity glue.
get_target_property(GAME_ACTIVITY_INCLUDE_DIRS
    game-activity::game-activity_static
    INTERFACE_INCLUDE_DIRECTORIES)
list(GET GAME_ACTIVITY_INCLUDE_DIRS 0 GAME_ACTIVITY_INCLUDE_DIR)
set(GAME_ACTIVITY_GLUE
    "${GAME_ACTIVITY_INCLUDE_DIR}/game-activity/native_app_glue/android_native_app_glue.c")

set(PLATFORM_SOURCES
    platform/android_main.cpp
    platform/android_input.cpp
    platform/android_window.cpp
    platform/android_filesystem.cpp
    platform/android_audio.cpp
    platform/android_renderer.cpp
    jni/bridge.cpp
    ${GAME_ACTIVITY_GLUE}
)

add_library(hp2engine SHARED ${PLATFORM_SOURCES})

target_link_libraries(hp2engine
    game-activity::game-activity_static
    android
    log
    EGL
    GLESv3
    aaudio
)

# The GameActivity JNI entry point lives in a static archive. Force the linker
# to retain it so GameActivity can initialize the native side at runtime.
target_link_options(hp2engine PRIVATE
    "-Wl,-u,Java_com_google_androidgamesdk_GameActivity_initializeNativeCode")

target_compile_options(hp2engine PRIVATE
    -Wall -DANDROID -DUSE_OPENGLES -DUSE_AAUDIO
)

set_target_properties(hp2engine PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
