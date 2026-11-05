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
)

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
