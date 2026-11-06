cmake_minimum_required(VERSION 3.22.1)

project(gow_android_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(game-activity REQUIRED CONFIG)

find_library(ANDROID_LOG_LIBRARY log REQUIRED)
find_library(ANDROID_LIBRARY android REQUIRED)
find_library(VULKAN_LIBRARY vulkan REQUIRED)

add_library(android-game SHARED
    main.cpp
    vu1_neon_compat_smoke.cpp
)

# The Python preparation step exports the pinned and privately patched Waffle
# source tree here. Keep it under app/build (ignored by Git), never as a submodule.
set(GOW_WAFFLE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../build/gow-runtime-source")
if(EXISTS "${GOW_WAFFLE_SOURCE_DIR}/CMakeLists.txt")
    set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
    set(PS2X_BUILD_RUNTIME ON CACHE BOOL "" FORCE)
    set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
    set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
    set(PS2X_IOP_NATIVE_ONLY ON CACHE BOOL "" FORCE)
    set(PS2X_VU1_NATIVE_ONLY ON CACHE BOOL "" FORCE)
    set(PS2X_ENABLE_FFMPEG OFF CACHE BOOL "" FORCE)
    set(PS2X_ENABLE_DEBUG_UI OFF CACHE BOOL "" FORCE)
    set(PS2X_ENABLE_RUNNER_UNITY_BUILD OFF CACHE BOOL "" FORCE)
    set(PS2X_ENABLE_RUNNER_PCH OFF CACHE BOOL "" FORCE)

    # EXCLUDE_FROM_ALL omits the upstream NativeActivity/Raylib runner. The
    # ps2_runtime archive is still built because android-game links to it.
    add_subdirectory(
        "${GOW_WAFFLE_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/gow-waffle"
        EXCLUDE_FROM_ALL
    )

    if(NOT TARGET ps2_runtime)
        message(FATAL_ERROR "Prepared Waffle tree did not define ps2_runtime.")
    endif()

    target_sources(android-game PRIVATE runtime_link_bridge.cpp)
    target_include_directories(android-game PRIVATE
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/include"
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/src/lib"
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel"
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xIOP/include"
        "${CMAKE_CURRENT_BINARY_DIR}/gow-waffle/_deps/sse2neon-src"
    )
    target_compile_definitions(android-game PRIVATE USE_SSE2NEON)
    target_link_libraries(android-game PRIVATE ps2_runtime)
    message(STATUS "FORGE Android target links the strict Waffle PS2 runtime.")
else()
    message(WARNING
        "Pinned Waffle source is not prepared. Run tools/build_recompiler_host.py "
        "--prepare-android-runtime before building the integrated Android target."
    )
endif()


target_compile_definitions(android-game PRIVATE
    VK_USE_PLATFORM_ANDROID_KHR
)

target_compile_options(android-game PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(android-game PRIVATE
    game-activity::game-activity_static
    ${ANDROID_LOG_LIBRARY}
    ${ANDROID_LIBRARY}
    ${VULKAN_LIBRARY}
)
