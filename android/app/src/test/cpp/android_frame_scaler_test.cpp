#include "android_frame_scaler.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        // 2x1 source: red, green. A 4x4 target should preserve aspect ratio
        // and center the image in black letterboxing.
        const std::vector<uint8_t> source{
            255, 0, 0, 255,
            0, 255, 0, 255
        };
        std::vector<uint8_t> destination;
        std::string error;
        require(gow::android::scaleRgbaToFit(
                    source.data(), source.size(), 2, 1, 4, 4,
                    destination, error),
                "Expected a valid RGBA source to scale.");
        require(destination.size() == 4u * 4u * 4u,
                "Scaled target has an incorrect byte count.");

        // Image occupies rows 1 and 2 and all four columns.
        for (uint32_t y = 0; y < 4; ++y) {
            for (uint32_t x = 0; x < 4; ++x) {
                const size_t offset = (static_cast<size_t>(y) * 4u + x) * 4u;
                if (y == 0u || y == 3u) {
                    require(destination[offset + 0] == 0u &&
                            destination[offset + 1] == 0u &&
                            destination[offset + 2] == 0u &&
                            destination[offset + 3] == 0u,
                            "Letterbox pixels must remain transparent black.");
                } else if (x < 2u) {
                    require(destination[offset + 0] == 255u &&
                            destination[offset + 1] == 0u,
                            "Left image half must retain the red source pixel.");
                } else {
                    require(destination[offset + 0] == 0u &&
                            destination[offset + 1] == 255u,
                            "Right image half must retain the green source pixel.");
                }
            }
        }

        gow::android::convertRgbaToBgraInPlace(destination);
        require(destination[4u * 5u + 2u] == 255u &&
                destination[4u * 5u + 0u] == 0u,
                "RGBA-to-BGRA conversion did not swap red and blue.");

        require(!gow::android::scaleRgbaToFit(
                    source.data(), 4u, 2, 1, 4, 4,
                    destination, error),
                "Truncated source frame was accepted.");
        require(!error.empty(), "Invalid source did not provide a diagnostic.");

        require(!gow::android::scaleRgbaToFit(
                    source.data(), source.size(), 2, 1, 0, 4,
                    destination, error),
                "Zero-sized target was accepted.");

        std::cout << "Android RGBA frame scaler tests passed.\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "Android RGBA frame scaler test failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
