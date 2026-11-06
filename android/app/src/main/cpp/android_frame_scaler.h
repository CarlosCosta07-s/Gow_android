#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gow::android {

// Validates and aspect-fits a tightly packed RGBA8 frame into the target
// dimensions. The output is opaque black letterboxing plus nearest-neighbor
// sampling; no Vulkan or Android types are required.
bool scaleRgbaToFit(
    const uint8_t *source,
    size_t sourceBytes,
    uint32_t sourceWidth,
    uint32_t sourceHeight,
    uint32_t targetWidth,
    uint32_t targetHeight,
    std::vector<uint8_t> &destination,
    std::string &error);

void convertRgbaToBgraInPlace(std::vector<uint8_t> &pixels) noexcept;

} // namespace gow::android
