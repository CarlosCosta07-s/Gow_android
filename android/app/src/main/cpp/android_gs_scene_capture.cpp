#include "android_gs_scene_capture.h"

#include <cstdio>
#include <limits>
#include <string>

namespace gow::android {
namespace {

constexpr uint64_t kMaxCapturePixels = 16ull * 1024ull * 1024ull;

} // namespace

bool writeRgbaPpmCapture(
    const std::string &path,
    const std::vector<uint8_t> &rgbaPixels,
    uint32_t width,
    uint32_t height,
    std::string &error) {
    error.clear();
    if (path.empty()) {
        error = "Capture output path is empty.";
        return false;
    }
    if (width == 0u || height == 0u) {
        error = "Capture dimensions must be positive.";
        return false;
    }

    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (pixels > kMaxCapturePixels ||
        pixels > static_cast<uint64_t>(
            std::numeric_limits<size_t>::max() / 4u)) {
        error = "Capture dimensions exceed the 16-megapixel safety limit.";
        return false;
    }
    const size_t required = static_cast<size_t>(pixels * 4u);
    if (rgbaPixels.size() != required) {
        error = "RGBA capture buffer size does not match its dimensions.";
        return false;
    }

    const std::string temporaryPath = path + ".tmp";
    FILE *file = std::fopen(temporaryPath.c_str(), "wb");
    if (file == nullptr) {
        error = "Could not open the temporary PPM capture file.";
        return false;
    }

    bool success = std::fprintf(
        file, "P6\n%u %u\n255\n", width, height) > 0;
    for (size_t pixel = 0u; success && pixel < static_cast<size_t>(pixels); ++pixel) {
        const size_t offset = pixel * 4u;
        const uint8_t rgb[3] = {
            rgbaPixels[offset + 0u],
            rgbaPixels[offset + 1u],
            rgbaPixels[offset + 2u]};
        success = std::fwrite(rgb, sizeof(rgb), 1u, file) == 1u;
    }

    if (std::fflush(file) != 0) success = false;
    if (std::fclose(file) != 0) success = false;
    if (!success) {
        std::remove(temporaryPath.c_str());
        error = "Writing the PPM capture failed.";
        return false;
    }

    if (std::rename(temporaryPath.c_str(), path.c_str()) != 0) {
        std::remove(temporaryPath.c_str());
        error = "Could not publish the completed PPM capture.";
        return false;
    }
    return true;
}

} // namespace gow::android
