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
    game-activity::game-activity_static
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
