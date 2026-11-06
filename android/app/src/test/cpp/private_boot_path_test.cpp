#include "private_boot_path.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void writeFile(const std::filesystem::path &path, const std::string &value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    if (!stream || !(stream << value)) {
        throw std::runtime_error("Could not create a private boot-path fixture.");
    }
}

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("gow-private-boot-path-" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

} // namespace

int main() {
    try {
        TempDirectory temporary;
        const auto root = temporary.path / "install" / "disc";
        const auto outside = temporary.path / "outside";
        writeFile(root / "PS2" / "BOOT" / "SCUS_973.99", "ELF");
        writeFile(outside / "SECRET.ELF", "outside");

        std::filesystem::path resolved;
        std::string error;
        require(
            gow::disc::resolvePrivateBootElfPath(
                root.string(), "ps2/boot/scus_973.99;1", resolved, error),
            "Expected the normalized private BOOT2 path to resolve.");
        require(
            resolved == std::filesystem::weakly_canonical(
                            root / "PS2" / "BOOT" / "SCUS_973.99"),
            "Resolved BOOT2 path differs from the expected private file.");

        require(
            !gow::disc::resolvePrivateBootElfPath(
                root.string(), "../outside/SECRET.ELF", resolved, error),
            "Traversal path was accepted.");
        require(
            !gow::disc::resolvePrivateBootElfPath(
                root.string(), "/PS2/BOOT/SCUS_973.99", resolved, error),
            "Absolute BOOT2 path was accepted.");
        require(
            !gow::disc::resolvePrivateBootElfPath(
                root.string(), "PS2//BOOT/SCUS_973.99", resolved, error),
            "Empty path component was accepted.");
        require(
            !gow::disc::resolvePrivateBootElfPath(
                root.string(), "PS2/BOOT", resolved, error),
            "A directory was accepted as the BOOT2 executable.");
        require(
            !gow::disc::resolvePrivateBootElfPath(
                root.string(), "cdrom0:PS2/BOOT/SCUS_973.99", resolved, error),
            "A device-prefixed path was accepted.");

        const auto escapeLink = root / "PS2" / "BOOT" / "ESCAPE.ELF";
        std::filesystem::create_symlink(
            outside / "SECRET.ELF", escapeLink);
        require(
            !gow::disc::resolvePrivateBootElfPath(
                root.string(), "PS2/BOOT/ESCAPE.ELF", resolved, error),
            "A symlink escaping the private installation was accepted.");

        std::cout << "Private BOOT2 path resolver tests passed.\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "Private BOOT2 path resolver test failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
