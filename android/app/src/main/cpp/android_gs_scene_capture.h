#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gow::android {

// Writes a compact RGBA frame as an RGB PPM P6 file.
// Capture files are written only to the caller's local app-data path.
bool writeRgbaPpmCapture(
    const std::string &path,
    const std::vector<uint8_t> &rgbaPixels,
    uint32_t width,
    uint32_t height,
    std::string &error);

} // namespace gow::android
