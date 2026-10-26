cmake_minimum_required(VERSION 3.24)
project(Eon LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(EON_BUILD_APPS "Build eon_sim and eon_editor (needs raylib)" ON)
option(EON_BUILD_TESTS "Build core tests" ON)

include(FetchContent)

FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

# Jolt Physics (library only)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.6.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build)
FetchContent_MakeAvailable(JoltPhysics)

# ---- Core: no graphics, fully testable ----
add_library(eon_core STATIC
    engine/core/Circuit.cpp
    engine/core/Zone.cpp
    engine/core/World.cpp
    engine/core/Physics.cpp)
target_include_directories(eon_core PUBLIC engine)
target_link_libraries(eon_core PUBLIC nlohmann_json::nlohmann_json PRIVATE Jolt)
target_compile_definitions(eon_core PUBLIC EON_CONTENT_DIR="${CMAKE_SOURCE_DIR}/content")
if(MSVC)
    target_compile_options(eon_core PRIVATE /W4)
else()
    target_compile_options(eon_core PRIVATE -Wall -Wextra)
endif()

# ---- Apps ----
if(EON_BUILD_APPS)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    set(OPENGL_VERSION "4.3" CACHE STRING "" FORCE)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)

    add_library(eon_render STATIC engine/render/ZoneRenderer.cpp)
    target_link_libraries(eon_render PUBLIC eon_core raylib)

    add_executable(eon_sim apps/sim/main.cpp)
    target_link_libraries(eon_sim PRIVATE eon_render)

    add_executable(eon_editor apps/editor/main.cpp)
    target_link_libraries(eon_editor PRIVATE eon_render)
endif()

# ---- Tests ----
if(EON_BUILD_TESTS)
    enable_testing()
    add_executable(eon_tests tests/test_main.cpp)
    target_link_libraries(eon_tests PRIVATE eon_core)
    add_test(NAME eon_tests COMMAND eon_tests ${CMAKE_SOURCE_DIR}/content)
endif()
