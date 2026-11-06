#include "cdroot_vfs.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_rom_device.h"
#include "runtime/ps2_vfs.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {
gow::disc::CdRootVfs g_cdRoot;

int fail(const char *message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

bool writeFixture(const std::filesystem::path &path,
                  const std::vector<uint8_t> &bytes) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return stream.good();
}
} // namespace

extern "C" uint64_t gowAndroidCdRootOpenFile(
    const char *path, uint64_t *fileSize) noexcept {
    if (fileSize != nullptr) *fileSize = 0u;
    if (path == nullptr || path[0] == '\0' || fileSize == nullptr) return 0u;

    gow::disc::CdRootFile file{};
    if (!g_cdRoot.openFile(path, file, nullptr)) return 0u;
    *fileSize = file.size;
    return file.handle;
}

extern "C" bool gowAndroidCdRootReadFile(
    uint64_t handle, uint64_t offset, void *destination,
    size_t requested, size_t *bytesRead) noexcept {
    if (bytesRead == nullptr) return false;
    *bytesRead = 0u;
    return g_cdRoot.readFile(handle, offset, destination, requested, *bytesRead);
}

extern "C" void gowAndroidCdRootCloseFile(uint64_t handle) noexcept {
    g_cdRoot.closeFile(handle);
}

int main() {
    char temporaryRoot[] = "/tmp/gow-ps2-vfs-smoke-XXXXXX";
    char *rootText = ::mkdtemp(temporaryRoot);
    if (rootText == nullptr) return fail("could not create synthetic cdRoot directory");

    const std::filesystem::path root(rootText);
    const std::filesystem::path data = root / "DATA";
    std::error_code filesystemError;
    std::filesystem::create_directories(data, filesystemError);
    if (filesystemError) {
        std::filesystem::remove_all(root);
        return fail("could not create synthetic DATA directory");
    }

    const std::vector<uint8_t> payload{
        0x10u, 0x21u, 0x32u, 0x43u, 0x54u, 0x65u, 0x76u, 0x87u
    };
    if (!writeFixture(data / "BOOT.BIN;1", payload)) {
        std::filesystem::remove_all(root);
        return fail("could not write synthetic BOOT.BIN fixture");
    }

    std::string error;
    if (!g_cdRoot.openRoot(root.string(), &error)) {
        std::filesystem::remove_all(root);
        return fail(("could not mount synthetic private cdRoot: " + error).c_str());
    }

    PS2Vfs vfs;
    PS2VfsMounts mounts;
    mounts.cdRoot = std::filesystem::path("gowcdroot:/");
    PS2RomDevice rom;

    const int32_t descriptor = vfs.open(
        "cdrom0:/data/boot.bin;1", PS2_FIO_O_RDONLY, mounts, rom);
    if (descriptor < 0) {
        g_cdRoot.close();
        std::filesystem::remove_all(root);
        return fail("PS2Vfs could not open cdrom0 path through gowcdroot provider");
    }

    std::array<uint8_t, 4> firstRead{};
    if (vfs.read(descriptor, firstRead.data(), firstRead.size()) !=
            static_cast<int64_t>(firstRead.size()) ||
        firstRead[0] != payload[0] || firstRead[1] != payload[1] ||
        firstRead[2] != payload[2] || firstRead[3] != payload[3]) {
        vfs.close(descriptor);
        g_cdRoot.close();
        std::filesystem::remove_all(root);
        return fail("PS2Vfs sequential read returned incorrect bytes");
    }

    if (vfs.seek(descriptor, 5, SEEK_SET) != 5) {
        vfs.close(descriptor);
        g_cdRoot.close();
        std::filesystem::remove_all(root);
        return fail("PS2Vfs seek did not move to the requested file offset");
    }

    std::array<uint8_t, 2> secondRead{};
    if (vfs.read(descriptor, secondRead.data(), secondRead.size()) !=
            static_cast<int64_t>(secondRead.size()) ||
        secondRead[0] != payload[5] || secondRead[1] != payload[6]) {
        vfs.close(descriptor);
        g_cdRoot.close();
        std::filesystem::remove_all(root);
        return fail("PS2Vfs random-access read returned incorrect bytes");
    }

    PS2VfsStat stat{};
    if (!vfs.stat("cdrom0:/DATA/BOOT.BIN;1", mounts, rom, stat) ||
        stat.directory || !stat.readOnly || stat.size != payload.size()) {
        vfs.close(descriptor);
        g_cdRoot.close();
        std::filesystem::remove_all(root);
        return fail("PS2Vfs stat did not resolve the cdRoot file metadata");
    }

    if (vfs.open("cdrom0:/DATA/BOOT.BIN;1", PS2_FIO_O_WRONLY, mounts, rom) >= 0) {
        vfs.close(descriptor);
        g_cdRoot.close();
        std::filesystem::remove_all(root);
        return fail("PS2Vfs allowed write access to the read-only cdRoot provider");
    }

    if (vfs.open("cdrom0:/../outside.bin", PS2_FIO_O_RDONLY, mounts, rom) >= 0) {
        vfs.close(descriptor);
        g_cdRoot.close();
        std::filesystem::remove_all(root);
        return fail("PS2Vfs accepted a traversal path through cdRoot");
    }

    if (vfs.close(descriptor) != 0 || vfs.close(descriptor) == 0 ||
        vfs.read(descriptor, firstRead.data(), firstRead.size()) >= 0) {
        g_cdRoot.close();
        std::filesystem::remove_all(root);
        return fail("PS2Vfs descriptor close or closed-handle behavior is incorrect");
    }

    g_cdRoot.close();
    std::filesystem::remove_all(root);
    std::cout << "PS2Vfs gowcdroot synthetic open/read/seek/stat/close smoke passed\n";
    return 0;
}
