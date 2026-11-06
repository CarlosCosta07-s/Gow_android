#include "cdroot_vfs.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

namespace {
int fail(const std::string &message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

bool writeFile(const std::filesystem::path &path,
               const std::vector<uint8_t> &bytes) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return stream.good();
}
} // namespace

int main() {
    char temporaryRoot[] = "/tmp/gow-cdroot-test-XXXXXX";
    char *rootText = ::mkdtemp(temporaryRoot);
    if (rootText == nullptr) return fail("could not create test root");

    const std::filesystem::path root(rootText);
    const std::filesystem::path dataDirectory = root / "DATA";
    const std::filesystem::path outsideFile = root.parent_path() / "gow-cdroot-outside.bin";
    const std::filesystem::path rootSymlink = root.parent_path() / "gow-cdroot-link";

    std::error_code filesystemError;
    std::filesystem::create_directories(dataDirectory, filesystemError);
    if (filesystemError) {
        std::filesystem::remove_all(root);
        return fail("could not create DATA directory");
    }

    const std::vector<uint8_t> payload{0x10u, 0x21u, 0x32u, 0x43u, 0x54u, 0x65u, 0x76u};
    const std::vector<uint8_t> outsidePayload{0xAAu, 0xBBu, 0xCCu};
    if (!writeFile(dataDirectory / "BOOT.BIN", payload) ||
        !writeFile(root / "ROOT.DAT", outsidePayload) ||
        !writeFile(outsideFile, outsidePayload)) {
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("could not create VFS fixture files");
    }

    gow::disc::CdRootVfs vfs;
    std::string error;
    if (!vfs.openRoot(root.string(), &error) || !vfs.isOpen()) {
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("could not open private cdRoot: " + error);
    }

    std::vector<gow::disc::CdRootEntry> rootEntries;
    if (!vfs.listDirectory("", rootEntries, &error) ||
        rootEntries.size() != 2u ||
        rootEntries[0].name != "DATA" || !rootEntries[0].isDirectory ||
        rootEntries[1].name != "ROOT.DAT" || rootEntries[1].isDirectory ||
        rootEntries[1].size != outsidePayload.size()) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot root listing or metadata mismatch: " + error);
    }

    std::vector<gow::disc::CdRootEntry> dataEntries;
    if (!vfs.listDirectory("DATA", dataEntries, &error) ||
        dataEntries.size() != 1u ||
        dataEntries[0].name != "BOOT.BIN" ||
        dataEntries[0].isDirectory ||
        dataEntries[0].size != payload.size()) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("nested cdRoot listing mismatch: " + error);
    }

    gow::disc::CdRootFile file{};
    if (!vfs.openFile("DATA/BOOT.BIN", file, &error) ||
        file.handle == 0u || file.size != payload.size()) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("could not open a nested cdRoot file: " + error);
    }

    std::array<uint8_t, 6> readBuffer{};
    size_t bytesRead = 0u;
    if (!vfs.readFile(file.handle, 2u, readBuffer.data(),
                      readBuffer.size(), bytesRead) ||
        bytesRead != 5u ||
        readBuffer[0] != payload[2] ||
        readBuffer[4] != payload[6]) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("random-access cdRoot read or EOF handling failed");
    }

    bytesRead = 99u;
    if (!vfs.readFile(file.handle, file.size, readBuffer.data(),
                      readBuffer.size(), bytesRead) ||
        bytesRead != 0u) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("reading at EOF did not return a successful empty read");
    }

    if (vfs.readFile(file.handle, file.size + 1u, readBuffer.data(),
                     readBuffer.size(), bytesRead)) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot read accepted an offset beyond EOF");
    }

    gow::disc::CdRootFile rejected{};
    if (vfs.openFile("../gow-cdroot-outside.bin", rejected, &error) ||
        vfs.openFile("/DATA/BOOT.BIN", rejected, &error) ||
        vfs.openFile("DATA//BOOT.BIN", rejected, &error) ||
        vfs.openFile("DATA/../ROOT.DAT", rejected, &error) ||
        vfs.openFile("DATA/BOOT.BIN:stream", rejected, &error) ||
        vfs.openFile("DATA", rejected, &error)) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot accepted an unsafe path or a directory as a file");
    }

    std::filesystem::create_symlink(outsideFile, root / "ESCAPE.BIN", filesystemError);
    if (filesystemError) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("could not create symlink attack fixture");
    }
    if (vfs.openFile("ESCAPE.BIN", rejected, &error)) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot followed a symbolic link");
    }
    if (vfs.listDirectory("", rootEntries, &error)) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot directory listing accepted a symbolic link entry");
    }

    vfs.closeFile(file.handle);
    if (vfs.readFile(file.handle, 0u, readBuffer.data(),
                     readBuffer.size(), bytesRead)) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot read accepted a closed file handle");
    }

    vfs.close();
    if (vfs.isOpen()) {
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot remained open after close");
    }

    std::filesystem::create_directory_symlink(root, rootSymlink, filesystemError);
    if (filesystemError) {
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("could not create root symlink fixture");
    }
    if (vfs.openRoot(rootSymlink.string(), &error)) {
        vfs.close();
        std::filesystem::remove(rootSymlink);
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot accepted a symlink as its root");
    }

    std::filesystem::remove(rootSymlink, filesystemError);
    std::filesystem::remove_all(root);
    std::filesystem::remove(outsideFile);
    std::cout << "cdRoot VFS tests passed\n";
    return 0;
}
