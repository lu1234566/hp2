cmake_minimum_required(VERSION 3.22.1)
project(hp2engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(game-activity REQUIRED CONFIG)

set(PORTCORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/portcore")

add_library(hp2engine SHARED
    platform/android_main.cpp
    "${PORTCORE_ROOT}/runtime.cpp"
    "${PORTCORE_ROOT}/ue_model.cpp"
    "${PORTCORE_ROOT}/ue_package.cpp"
)

target_include_directories(hp2engine PRIVATE
    "${PORTCORE_ROOT}/include"
)

target_link_libraries(hp2engine
    game-activity::game-activity_static
    android
    log
    EGL
    GLESv3
)

target_compile_options(hp2engine PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -fvisibility=hidden
)

target_link_options(hp2engine PRIVATE
    "-Wl,-z,max-page-size=16384"
    "-Wl,-u,Java_com_google_androidgamesdk_GameActivity_initializeNativeCode"
)

set_target_properties(hp2engine PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
