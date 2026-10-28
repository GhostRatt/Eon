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

FetchContent_Declare(ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG v0.23.1
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(ufbx)
if(NOT ufbx_POPULATED)
    FetchContent_Populate(ufbx)
endif()

# Lua 5.4: object scripts (world machinery: vending machines, thermostats, timers ...)
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.6
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
endif()
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua|ltests)\\.c$")
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${lua_SOURCE_DIR})
if(UNIX)
    target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
    target_link_libraries(lua PRIVATE m)
endif()
add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx PUBLIC ${ufbx_SOURCE_DIR})

# ---- Core: no graphics, fully testable ----
add_library(eon_core STATIC
    engine/core/Circuit.cpp
    engine/core/Zone.cpp
    engine/core/World.cpp
    engine/core/World_Matter.cpp
    engine/core/World_Doors.cpp
    engine/core/Jobs.cpp
    engine/core/Terrain.cpp
    engine/core/World_Outdoor.cpp
    engine/core/World_Scripts.cpp
    engine/core/Physics.cpp)
target_include_directories(eon_core PUBLIC engine)
target_link_libraries(eon_core PUBLIC nlohmann_json::nlohmann_json PRIVATE Jolt lua)
target_compile_definitions(eon_core PUBLIC EON_CONTENT_DIR="${CMAKE_SOURCE_DIR}/content")
if(MSVC)
    target_compile_options(eon_core PRIVATE /W4)
else()
    target_compile_options(eon_core PRIVATE -Wall -Wextra)
endif()

# ---- Character: FBX import, skeleton, IK, soft tissue, limb collision, skinning ----
add_library(eon_character STATIC
    engine/character/FbxImport.cpp
    engine/character/Character.cpp
    engine/character/Posture.cpp
    engine/character/Face.cpp
    engine/character/Dangles.cpp
    engine/character/BodyState.cpp
    engine/character/Wardrobe.cpp
    engine/character/Motor.cpp
    engine/character/LipSync.cpp
    engine/character/BodyMatter.cpp)
target_include_directories(eon_character PUBLIC engine)
target_link_libraries(eon_character PUBLIC eon_core PRIVATE ufbx)
find_package(Threads REQUIRED)
target_link_libraries(eon_character PUBLIC Threads::Threads)

# ---- Mind: sensor firewall, memory graph, learner (no rendering; the only door into Jenny's head) ----
add_library(eon_mind STATIC
    engine/mind/MemoryGraph.cpp
    engine/mind/Perception.cpp
    engine/mind/Learner.cpp)
target_link_libraries(eon_mind PUBLIC eon_character)

# ---- Apps ----
if(EON_BUILD_APPS)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    set(OPENGL_VERSION "4.3" CACHE STRING "" FORCE)
    set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
    set(SUPPORT_FILEFORMAT_TGA ON CACHE BOOL "" FORCE)   # Primrose textures are .tga
    set(SUPPORT_FILEFORMAT_JPG ON CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)

    add_library(eon_render STATIC engine/render/ZoneRenderer.cpp engine/render/CharacterRenderer.cpp)
    target_link_libraries(eon_render PUBLIC eon_core eon_character raylib)

    add_executable(eon_sim apps/sim/main.cpp)
    target_link_libraries(eon_sim PRIVATE eon_render eon_mind)

    add_executable(eon_editor apps/editor/main.cpp)
    target_link_libraries(eon_editor PRIVATE eon_render)
endif()

# ---- Tests ----
if(EON_BUILD_TESTS)
    enable_testing()
    add_executable(eon_tests tests/test_main.cpp)
    target_link_libraries(eon_tests PRIVATE eon_core eon_character eon_mind)
    target_compile_definitions(eon_tests PRIVATE EON_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
    add_test(NAME eon_tests COMMAND eon_tests ${CMAKE_SOURCE_DIR}/content)
    # headless frame-cost benchmark (CPU only): eon_bench [zone] [--frames N] [--rig]
    add_executable(eon_bench apps/bench/main.cpp)
    target_link_libraries(eon_bench PRIVATE eon_core eon_character eon_mind)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(NAME eongraph COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/codegraph/test_eongraph.py)
    endif()
endif()
