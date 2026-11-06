#include "android_frame_scaler.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace gow::android {
namespace {

constexpr uint64_t kMaxPixelCount = 16ull * 1024ull * 1024ull;

bool checkedRgbaSize(uint32_t width, uint32_t height, size_t &bytes) {
    if (width == 0u || height == 0u) return false;
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (pixels > kMaxPixelCount ||
        pixels > static_cast<uint64_t>(std::numeric_limits<size_t>::max() / 4u)) {
        return false;
    }
    bytes = static_cast<size_t>(pixels * 4ull);
    return true;
}

} // namespace

bool scaleRgbaToFit(
    const uint8_t *source,
    size_t sourceBytes,
    uint32_t sourceWidth,
    uint32_t sourceHeight,
    uint32_t targetWidth,
    uint32_t targetHeight,
    std::vector<uint8_t> &destination,
    std::string &error) {
    destination.clear();
    error.clear();

    size_t sourceRequired = 0u;
    size_t targetRequired = 0u;
    if (source == nullptr ||
        !checkedRgbaSize(sourceWidth, sourceHeight, sourceRequired) ||
        sourceBytes < sourceRequired) {
        error = "Source frame is null, truncated, empty, or exceeds the size limit.";
        return false;
    }
    if (!checkedRgbaSize(targetWidth, targetHeight, targetRequired)) {
        error = "Target frame dimensions are empty or exceed the size limit.";
        return false;
    }

    try {
        destination.assign(targetRequired, 0u);
        for (size_t offset = 3u; offset < destination.size(); offset += 4u) {
            destination[offset] = 255u;
        }
    } catch (...) {
        error = "Could not allocate the Vulkan upload frame.";
        destination.clear();
        return false;
    }

    const uint64_t widthLimited =
        static_cast<uint64_t>(targetWidth) * sourceHeight;
    const uint64_t heightLimited =
        static_cast<uint64_t>(targetHeight) * sourceWidth;
    uint32_t scaledWidth = 0u;
    uint32_t scaledHeight = 0u;

    if (widthLimited <= heightLimited) {
        scaledWidth = targetWidth;
        scaledHeight = static_cast<uint32_t>(
            std::max<uint64_t>(1u,
                static_cast<uint64_t>(sourceHeight) * targetWidth / sourceWidth));
    } else {
        scaledHeight = targetHeight;
        scaledWidth = static_cast<uint32_t>(
            std::max<uint64_t>(1u,
                static_cast<uint64_t>(sourceWidth) * targetHeight / sourceHeight));
    }
    scaledWidth = std::min(scaledWidth, targetWidth);
    scaledHeight = std::min(scaledHeight, targetHeight);

    const uint32_t left = (targetWidth - scaledWidth) / 2u;
    const uint32_t top = (targetHeight - scaledHeight) / 2u;

    for (uint32_t y = 0u; y < scaledHeight; ++y) {
        const uint32_t sourceY = static_cast<uint32_t>(
            static_cast<uint64_t>(y) * sourceHeight / scaledHeight);
        for (uint32_t x = 0u; x < scaledWidth; ++x) {
            const uint32_t sourceX = static_cast<uint32_t>(
                static_cast<uint64_t>(x) * sourceWidth / scaledWidth);
            const size_t sourceOffset =
                (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 4u;
            const size_t targetOffset =
                (static_cast<size_t>(top + y) * targetWidth + left + x) * 4u;
            destination[targetOffset + 0u] = source[sourceOffset + 0u];
            destination[targetOffset + 1u] = source[sourceOffset + 1u];
            destination[targetOffset + 2u] = source[sourceOffset + 2u];
            destination[targetOffset + 3u] = source[sourceOffset + 3u];
        }
    }
    return true;
}

void convertRgbaToBgraInPlace(std::vector<uint8_t> &pixels) noexcept {
    for (size_t offset = 0u; offset + 3u < pixels.size(); offset += 4u) {
        std::swap(pixels[offset], pixels[offset + 2u]);
    }
}

} // namespace gow::android
