cmake_minimum_required(VERSION 3.22)
project(hp2_portcore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hp2_portcore STATIC
    src/portcore/runtime.cpp
    src/portcore/ue_actor.cpp
    src/portcore/ue_lightmap.cpp
    src/portcore/ue_mesh.cpp
    src/portcore/ue_model.cpp
    src/portcore/ue_package.cpp
    src/portcore/ue_skeletal.cpp
    src/portcore/ue_texture.cpp
)
target_include_directories(hp2_portcore PUBLIC src/portcore/include)
target_compile_options(hp2_portcore PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hp2_probe tools/hp2_probe.cpp)
target_link_libraries(hp2_probe PRIVATE hp2_portcore)

add_executable(hp2_map_probe tools/hp2_map_probe.cpp)
target_link_libraries(hp2_map_probe PRIVATE hp2_portcore)

add_executable(hp2_skeletal_probe tools/hp2_skeletal_probe.cpp)
target_link_libraries(hp2_skeletal_probe PRIVATE hp2_portcore)

include(CTest)
if(BUILD_TESTING)
    add_executable(portcore_tests tests/portcore_tests.cpp)
    target_link_libraries(portcore_tests PRIVATE hp2_portcore)
    add_test(NAME portcore_tests COMMAND portcore_tests)

    add_executable(skeletal_probe_tests tests/skeletal_probe_tests.cpp)
    target_link_libraries(skeletal_probe_tests PRIVATE hp2_portcore)
    add_test(NAME skeletal_probe_tests COMMAND skeletal_probe_tests)
endif()
