#include "android_aot_identity.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main() {
    const fs::path temporary = fs::temp_directory_path() / "gow-android-aot-identity-test";
    std::error_code cleanupError;
    fs::remove_all(temporary, cleanupError);
    const fs::path installRoot = temporary / "files/installations/gow_scus_973_99_ntsc_u";
    const fs::path discRoot = installRoot / "disc";
    const fs::path captureRoot = temporary / "files/gow-aot-captures";
    fs::create_directories(discRoot);
    fs::create_directories(captureRoot);
    {
        std::ofstream elf(discRoot / "SCUS_973.99", std::ios::binary);
        elf << "synthetic-private-ps2-elf";
    }

    std::string error;
    if (!gow::android::writePrivateAotGameIdentity(
            discRoot.string(), "SCUS_973.99", captureRoot.string(), &error)) {
        std::cerr << "valid identity write failed: " << error << '\n';
        fs::remove_all(temporary, cleanupError);
        return 1;
    }

    const fs::path identity = captureRoot / "game_identity.txt";
    std::ifstream input(identity);
    const std::string text(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (text.find("format=gow-android-game-identity-v1\n") == std::string::npos ||
        text.find("boot_path=SCUS_973.99\n") == std::string::npos ||
        text.find("elf_size=25\n") == std::string::npos ||
        text.find("elf_sha256=") == std::string::npos) {
        std::cerr << "identity manifest fields are incomplete\n";
        fs::remove_all(temporary, cleanupError);
        return 1;
    }

    {
        std::ofstream staleVu1(captureRoot / "vu1/stale.bin", std::ios::binary);
        staleVu1 << "old vu1 capture";
        std::ofstream staleIop(captureRoot / "iop/stale.bin", std::ios::binary);
        staleIop << "old iop capture";
    }
    if (!gow::android::writePrivateAotGameIdentity(
            discRoot.string(), "SCUS_973.99", captureRoot.string(), &error) ||
        !fs::exists(captureRoot / "vu1/stale.bin") ||
        !fs::exists(captureRoot / "iop/stale.bin")) {
        std::cerr << "same ELF identity should preserve existing captures\\n";
        fs::remove_all(temporary, cleanupError);
        return 1;
    }
    {
        std::ofstream elf(discRoot / "SCUS_973.99", std::ios::binary | std::ios::trunc);
        elf << "a different synthetic private ELF revision";
    }
    if (!gow::android::writePrivateAotGameIdentity(
            discRoot.string(), "SCUS_973.99", captureRoot.string(), &error) ||
        fs::exists(captureRoot / "vu1/stale.bin") ||
        fs::exists(captureRoot / "iop/stale.bin")) {
        std::cerr << "changed ELF identity must clear stale VU1/IOP captures\\n";
        fs::remove_all(temporary, cleanupError);
        return 1;
    }

    if (gow::android::writePrivateAotGameIdentity(
            discRoot.string(), "../SCUS_973.99", captureRoot.string(), &error)) {
        std::cerr << "path traversal was accepted\n";
        fs::remove_all(temporary, cleanupError);
        return 1;
    }

    const fs::path linkPath = discRoot / "linked.elf";
    fs::create_symlink(discRoot / "SCUS_973.99", linkPath, cleanupError);
    if (!cleanupError && gow::android::writePrivateAotGameIdentity(
            discRoot.string(), "linked.elf", captureRoot.string(), &error)) {
        std::cerr << "symlinked BOOT2 was accepted\n";
        fs::remove_all(temporary, cleanupError);
        return 1;
    }

    fs::remove_all(temporary, cleanupError);
    std::cout << "Android private AOT game identity tests passed\n";
    return 0;
}
