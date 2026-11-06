#include "android_gs_scene_capture.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<uint8_t> readFile(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not read PPM output.");
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    const std::string path = "gow_android_scene_capture_test.ppm";
    const std::string temporaryPath = path + ".tmp";
    std::remove(path.c_str());
    std::remove(temporaryPath.c_str());

    try {
        std::vector<uint8_t> rgba = {
            255u, 0u, 0u, 0u,
            0u, 255u, 0u, 128u};
        std::string error;
        require(gow::android::writeRgbaPpmCapture(
                    path, rgba, 2u, 1u, error),
                "Valid RGBA PPM capture was rejected.");
        const std::vector<uint8_t> output = readFile(path);
        const std::string header = "P6\n2 1\n255\n";
        require(output.size() == header.size() + 6u,
                "PPM output has an unexpected byte count.");
        require(std::equal(header.begin(), header.end(), output.begin()),
                "PPM output header is incorrect.");
        const std::vector<uint8_t> expected = {
            255u, 0u, 0u, 0u, 255u, 0u};
        require(std::equal(
                    expected.begin(), expected.end(),
                    output.begin() + static_cast<std::ptrdiff_t>(header.size())),
                "PPM capture did not preserve RGB channels or discard alpha.");

        std::vector<uint8_t> invalid = {1u, 2u, 3u};
        require(!gow::android::writeRgbaPpmCapture(
                    path, invalid, 1u, 1u, error),
                "Truncated RGBA frame was accepted.");
        require(!error.empty(), "Invalid capture did not return a diagnostic.");

        require(!gow::android::writeRgbaPpmCapture(
                    path, rgba, 0u, 1u, error),
                "Zero-width capture was accepted.");
        require(!gow::android::writeRgbaPpmCapture(
                    path, rgba, 4097u, 4097u, error),
                "Oversized capture was accepted.");

        std::remove(path.c_str());
        std::remove(temporaryPath.c_str());
        std::cout << "Android GS scene capture tests passed.\n";
        return 0;
    } catch (const std::exception &exception) {
        std::remove(path.c_str());
        std::remove(temporaryPath.c_str());
        std::cerr << "Android GS scene capture test failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
