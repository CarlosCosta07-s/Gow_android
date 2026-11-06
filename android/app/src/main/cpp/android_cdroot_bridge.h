#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

bool gowAndroidCdRootIsReady() noexcept;

uint64_t gowAndroidCdRootOpenFile(
    const char *path, uint64_t *fileSize) noexcept;

bool gowAndroidCdRootReadFile(
    uint64_t handle, uint64_t offset, void *destination,
    size_t requested, size_t *bytesRead) noexcept;

void gowAndroidCdRootCloseFile(uint64_t handle) noexcept;
void gowAndroidCdRootClose() noexcept;

}
