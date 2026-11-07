cmake_minimum_required(VERSION 3.22.1)

project(gow_android_native LANGUAGES C CXX)

# The old surface clear/present was only a bring-up diagnostic, not a GS
# renderer. Keep it disabled by default until the Android GS backend exists.
option(GOW_ANDROID_ENABLE_VULKAN_SMOKE
    "Enable the isolated Vulkan clear/present diagnostic (not the PS2 GS renderer)" OFF)
option(GOW_ANDROID_ENABLE_GS_VULKAN
    "Enable the experimental, untextured Android GS primitive pipeline" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The game recompilation output contains many large C++ translation units.
# Default to one native compiler job for modest-memory build hosts; maintainers
# can explicitly raise this with -DGOW_ANDROID_NATIVE_COMPILE_JOBS=<n>.
set(GOW_ANDROID_NATIVE_COMPILE_JOBS "1" CACHE STRING
    "Maximum concurrent Ninja compile jobs for the Android runtime and generated game code")
if(NOT GOW_ANDROID_NATIVE_COMPILE_JOBS MATCHES "^[1-8]$")
    message(FATAL_ERROR "GOW_ANDROID_NATIVE_COMPILE_JOBS must be an integer from 1 to 8.")
endif()

find_package(game-activity REQUIRED CONFIG)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Pin the SSE-to-NEON intrinsic translation layer used by the PS2 runtime.
# Populate only the header repository; it does not need to be built as a target.
include(FetchContent)
FetchContent_Declare(
    sse2neon
    GIT_REPOSITORY https://github.com/DLTcollab/sse2neon.git
    GIT_TAG v1.9.1
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(sse2neon)
if(NOT sse2neon_POPULATED)
    FetchContent_Populate(sse2neon)
endif()

find_program(GOW_GLSLANG_VALIDATOR NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED)

find_library(ANDROID_LOG_LIBRARY log REQUIRED)
find_library(ANDROID_LIBRARY android REQUIRED)
find_library(VULKAN_LIBRARY vulkan REQUIRED)

set(GOW_GS_SHADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GOW_GS_SHADER_HEADER "${GOW_GS_SHADER_GENERATED_DIR}/gs_vulkan_primitive_shaders.h")
add_custom_command(
    OUTPUT "${GOW_GS_SHADER_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GOW_GS_SHADER_GENERATED_DIR}"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_LIST_DIR}/../../../../../tools/compile_android_gs_shaders.py"
        --compiler "${GOW_GLSLANG_VALIDATOR}"
        --vertex "${CMAKE_CURRENT_LIST_DIR}/../shaders/gs_primitive.vert"
        --fragment "${CMAKE_CURRENT_LIST_DIR}/../shaders/gs_primitive.frag"
        --output "${GOW_GS_SHADER_HEADER}"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../../../../../tools/compile_android_gs_shaders.py"
        "${CMAKE_CURRENT_LIST_DIR}/../shaders/gs_primitive.vert"
        "${CMAKE_CURRENT_LIST_DIR}/../shaders/gs_primitive.frag"
    VERBATIM
)
add_custom_target(gow_android_gs_shaders DEPENDS "${GOW_GS_SHADER_HEADER}")

add_library(android-game SHARED
    main.cpp
    android_runtime_log.cpp
    android_pad_bridge.cpp
    iso9660_reader.cpp
    iso9660_installer.cpp
    sha256.cpp
    iso9660_disc_layout.cpp
    cdroot_vfs.cpp
    private_boot_path.cpp
    android_frame_scaler.cpp
    android_gs_geometry.cpp
    android_gs_cpu_rasterizer.cpp
    android_gs_scene_capture.cpp
    android_gs_vulkan_backend.cpp
    ps2_elf_validator.cpp
    android_disc_bridge.cpp
    vu1_neon_compat_smoke.cpp
    # Satisfies Raylib's renamed, dormant NativeActivity wrapper. GameActivity
    # remains the only Android lifecycle entry point used by FORGE.
    raylib_android_main_stub.cpp
)

# The Python preparation step exports the pinned and privately patched Waffle
# source tree here. Keep it under app/build (ignored by Git), never as a submodule.
set(GOW_WAFFLE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../build/gow-runtime-source")
set(GOW_ANDROID_GENERATED_IOP_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../build/gow-generated-iop")
set(GOW_ANDROID_GENERATED_VU1_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../build/gow-generated-vu1")
set(PS2X_IOP_NATIVE_SOURCE_DIR "${GOW_ANDROID_GENERATED_IOP_DIR}" CACHE PATH
    "Private generated IRX AOT C++ directory" FORCE)
file(GLOB GOW_ANDROID_GENERATED_VU1_SOURCES CONFIGURE_DEPENDS
    "${GOW_ANDROID_GENERATED_VU1_DIR}/vu1n_part*.cpp")
set(GOW_ANDROID_GENERATED_GAME_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../build/gow-generated-game"
    CACHE PATH "Private PS2Recomp-generated C++ for the user's own PS2 ELF")
file(GLOB_RECURSE GOW_ANDROID_GENERATED_GAME_SOURCES
    CONFIGURE_DEPENDS "${GOW_ANDROID_GENERATED_GAME_DIR}/*.cpp")
set(GOW_ANDROID_CAPTURE_BOOTSTRAP OFF)
if("$ENV{GOW_ANDROID_CAPTURE_BOOTSTRAP}" STREQUAL "1")
    set(GOW_ANDROID_CAPTURE_BOOTSTRAP ON)
endif()

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

    # The runtime is a separate CMake target: android-game's USE_SSE2NEON
    # definition and include path do not propagate backwards through linking.
    # Compile the PS2 core itself against sse2neon on Android ARM64.
    target_compile_definitions(ps2_runtime PRIVATE USE_SSE2NEON)
    target_include_directories(ps2_runtime PRIVATE
        "${sse2neon_SOURCE_DIR}")

    # Private VU1 blocks are generated from the user's captured 16 KiB micro-memory
    # images. They are compiled into the Android runtime; no interpreter fallback
    # is enabled in the shipped native-only profile.
    if(GOW_ANDROID_GENERATED_VU1_SOURCES)
        target_sources(ps2_runtime PRIVATE ${GOW_ANDROID_GENERATED_VU1_SOURCES})
        set_source_files_properties(${GOW_ANDROID_GENERATED_VU1_SOURCES} PROPERTIES
            SKIP_UNITY_BUILD_INCLUSION ON
            SKIP_PRECOMPILE_HEADERS ON)
        message(STATUS "FORGE Android target includes private native VU1 blocks.")
    elseif(GOW_ANDROID_GENERATED_GAME_SOURCES AND NOT GOW_ANDROID_CAPTURE_BOOTSTRAP)
        message(FATAL_ERROR
            "The ELF AOT is present but generated VU1 blocks are missing. "
            "Supply the private VU1 captures or use the explicit capture-bootstrap build.")
    endif()

    if(GOW_ANDROID_GENERATED_GAME_SOURCES
            AND NOT EXISTS "${GOW_ANDROID_GENERATED_IOP_DIR}/iopn_registry.cpp"
            AND NOT GOW_ANDROID_CAPTURE_BOOTSTRAP)
        message(FATAL_ERROR
            "The ELF AOT is present but the generated IOP registry is missing. "
            "Supply private IRX captures or use the explicit capture-bootstrap build.")
    endif()

    # GameActivity owns the Android lifecycle. Raylib device teardown is gated
    # in the prepared runtime until a native Android audio adapter is available.
    target_compile_definitions(ps2_runtime PRIVATE PS2X_ANDROID_LIFECYCLE_MANAGED=1)

    set_property(GLOBAL APPEND PROPERTY JOB_POOLS
        gow_android_native_compile=${GOW_ANDROID_NATIVE_COMPILE_JOBS})
    set_property(TARGET ps2_runtime PROPERTY JOB_POOL_COMPILE gow_android_native_compile)
    set_property(TARGET android-game PROPERTY JOB_POOL_COMPILE gow_android_native_compile)

    target_sources(android-game PRIVATE runtime_link_bridge.cpp)

    # PS2Recomp outputs remain private and are staged under android/app/build,
    # which is ignored by Git. Its individual-function output may be nested.
    if(GOW_ANDROID_GENERATED_GAME_SOURCES)
        foreach(required_file
            register_functions.cpp
            ps2_recompiled_functions.h
            ps2_recompiled_stubs.h)
            if(NOT EXISTS "${GOW_ANDROID_GENERATED_GAME_DIR}/${required_file}")
                message(FATAL_ERROR
                    "Generated PS2 game-code staging is incomplete: missing ${required_file}. "
                    "Re-run the private PS2Recomp export.")
            endif()
        endforeach()

        # A real generated table replaces the zero-slot shell-only table.
        set(GOW_ANDROID_EMPTY_RECOMPILED_TABLE OFF CACHE BOOL
            "Link an empty function table until private game code is generated" FORCE)
        target_sources(android-game PRIVATE ${GOW_ANDROID_GENERATED_GAME_SOURCES})
        target_include_directories(android-game PRIVATE "${GOW_ANDROID_GENERATED_GAME_DIR}")

        # Register the game-specific runtime fixes with the Android shared library.
        # The override source must be linked into android-game (not only ps2_runtime):
        # its static AutoRegister descriptor is what PS2Runtime::loadElf matches.
        set(GOW_ANDROID_GAME_OVERRIDES_DIR
            "${CMAKE_CURRENT_LIST_DIR}/../../../../../port/kiexter")
        set(GOW_ANDROID_GAME_OVERRIDES_SOURCE
            "${GOW_ANDROID_GAME_OVERRIDES_DIR}/gow_overrides.cpp")
        set(GOW_ANDROID_GS_DISPLAY_HEADER
            "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/include/runtime/gs/gs_display.h")
        set(GOW_ANDROID_NEON_COMPAT_HEADER
            "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/include/runtime/ps2_vu1_neon_compat.h")
        if(NOT EXISTS "${GOW_ANDROID_GAME_OVERRIDES_SOURCE}")
            message(FATAL_ERROR
                "God of War Android override source is missing: ${GOW_ANDROID_GAME_OVERRIDES_SOURCE}")
        endif()
        if(NOT EXISTS "${GOW_ANDROID_GS_DISPLAY_HEADER}")
            message(FATAL_ERROR
                "Prepared runtime is missing the GS display hooks required by gow_overrides.cpp: "
                "${GOW_ANDROID_GS_DISPLAY_HEADER}. Re-run --prepare-android-runtime with the pinned KIexster patch set.")
        endif()
        if(NOT EXISTS "${GOW_ANDROID_NEON_COMPAT_HEADER}")
            message(FATAL_ERROR
                "Prepared runtime is missing the ARM64 VU1 compatibility header: "
                "${GOW_ANDROID_NEON_COMPAT_HEADER}. Re-run --prepare-android-runtime.")
        endif()
        target_sources(android-game PRIVATE "${GOW_ANDROID_GAME_OVERRIDES_SOURCE}")
        target_include_directories(android-game PRIVATE "${GOW_ANDROID_GAME_OVERRIDES_DIR}")
        message(STATUS "FORGE Android target includes private PS2Recomp game C++ and God of War game overrides.")
    else()
        option(GOW_ANDROID_EMPTY_RECOMPILED_TABLE
            "Link an empty function table until private game code is generated" ON)
        if(GOW_ANDROID_EMPTY_RECOMPILED_TABLE)
            set(GOW_EMPTY_TABLE_SOURCE
                "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/src/lib/android_recompiled_table_placeholder.cpp")
            if(NOT EXISTS "${GOW_EMPTY_TABLE_SOURCE}")
                message(FATAL_ERROR
                    "Shell function-table stub is missing. Re-run --prepare-android-runtime.")
            endif()
            target_sources(android-game PRIVATE "${GOW_EMPTY_TABLE_SOURCE}")
        else()
            message(FATAL_ERROR
                "GOW_ANDROID_EMPTY_RECOMPILED_TABLE is OFF, but no private generated game C++ "
                "was found in ${GOW_ANDROID_GENERATED_GAME_DIR}.")
        endif()
    endif()

    target_include_directories(android-game PRIVATE
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/include"
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/src/lib"
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/src/lib/gs"
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/src/lib/gs/gpu"
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel"
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xIOP/include"
        "${GOW_WAFFLE_SOURCE_DIR}/ps2xIOP/src"
        "${sse2neon_SOURCE_DIR}"
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


target_sources(android-game PRIVATE "${GOW_GS_SHADER_HEADER}")
target_include_directories(android-game PRIVATE "${GOW_GS_SHADER_GENERATED_DIR}")
add_dependencies(android-game gow_android_gs_shaders)

target_compile_definitions(android-game PRIVATE
    VK_USE_PLATFORM_ANDROID_KHR
)

if(GOW_ANDROID_ENABLE_VULKAN_SMOKE)
    target_compile_definitions(android-game PRIVATE GOW_ANDROID_ENABLE_VULKAN_SMOKE=1)
endif()
if(GOW_ANDROID_ENABLE_GS_VULKAN)
    target_compile_definitions(android-game PRIVATE GOW_ANDROID_ENABLE_GS_VULKAN=1)
endif()

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
