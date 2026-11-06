#include "cdroot_vfs.h"
#include "ps2_elf_validator.h"

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

void writeU16Le(std::vector<uint8_t> &bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value & 0xFFu);
    bytes[offset + 1u] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
}

void writeU32Le(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value & 0xFFu);
    bytes[offset + 1u] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
    bytes[offset + 2u] = static_cast<uint8_t>((value >> 16u) & 0xFFu);
    bytes[offset + 3u] = static_cast<uint8_t>((value >> 24u) & 0xFFu);
}

std::vector<uint8_t> makeSyntheticElf(bool validMagic, bool entryCovered) {
    std::vector<uint8_t> bytes(88u, 0u);
    if (validMagic) {
        bytes[0] = 0x7Fu;
        bytes[1] = 'E';
        bytes[2] = 'L';
        bytes[3] = 'F';
    }
    bytes[4] = 1u; // ELFCLASS32
    bytes[5] = 1u; // ELFDATA2LSB
    bytes[6] = 1u; // EV_CURRENT
    writeU16Le(bytes, 16u, 2u); // ET_EXEC
    writeU16Le(bytes, 18u, 8u); // EM_MIPS
    writeU32Le(bytes, 20u, 1u); // EV_CURRENT
    writeU32Le(bytes, 24u, entryCovered ? 0x00100000u : 0x00200000u);
    writeU32Le(bytes, 28u, 52u); // e_phoff
    writeU16Le(bytes, 40u, 52u); // e_ehsize
    writeU16Le(bytes, 42u, 32u); // e_phentsize
    writeU16Le(bytes, 44u, 1u);  // e_phnum

    writeU32Le(bytes, 52u, 1u); // PT_LOAD
    writeU32Le(bytes, 56u, 84u); // p_offset
    writeU32Le(bytes, 60u, 0x00100000u); // p_vaddr
    writeU32Le(bytes, 64u, 0x00100000u); // p_paddr
    writeU32Le(bytes, 68u, 4u); // p_filesz
    writeU32Le(bytes, 72u, 8u); // p_memsz
    writeU32Le(bytes, 76u, 5u); // p_flags
    writeU32Le(bytes, 80u, 16u); // p_align
    bytes[84] = 0x11u;
    bytes[85] = 0x22u;
    bytes[86] = 0x33u;
    bytes[87] = 0x44u;
    return bytes;
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

    gow::disc::CdRootFile isoStyleFile{};
    if (!vfs.openFile("data/boot.bin;1", isoStyleFile, &error) ||
        isoStyleFile.handle == 0u || isoStyleFile.size != payload.size()) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot did not resolve case-insensitive ISO versioned path: " + error);
    }
    vfs.closeFile(isoStyleFile.handle);

    std::vector<gow::disc::CdRootEntry> caseInsensitiveEntries;
    if (!vfs.listDirectory("data", caseInsensitiveEntries, &error) ||
        caseInsensitiveEntries.size() != 1u ||
        caseInsensitiveEntries[0].name != "BOOT.BIN") {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot did not resolve a case-insensitive directory path: " + error);
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

    std::vector<gow::disc::CdRootEntry> repeatedRootEntries;
    if (!vfs.listDirectory("", repeatedRootEntries, &error) ||
        repeatedRootEntries.size() != 2u ||
        repeatedRootEntries[0].name != "DATA" ||
        repeatedRootEntries[1].name != "ROOT.DAT") {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("repeated cdRoot listing did not return the same entries");
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

    std::filesystem::remove(root / "ESCAPE.BIN", filesystemError);
    if (filesystemError || !writeFile(dataDirectory / "boot.bin;1", payload)) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("could not create duplicate ISO-normalized name fixture");
    }
    if (vfs.openFile("DATA/BOOT.BIN", rejected, &error)) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("cdRoot selected one of two ambiguous ISO-normalized names");
    }

    const std::filesystem::path validElfPath = dataDirectory / "BOOT.ELF;1";
    const std::filesystem::path badMagicElfPath = dataDirectory / "BAD-MAGIC.ELF";
    const std::filesystem::path badEntryElfPath = dataDirectory / "BAD-ENTRY.ELF";
    if (!writeFile(validElfPath, makeSyntheticElf(true, true)) ||
        !writeFile(badMagicElfPath, makeSyntheticElf(false, true)) ||
        !writeFile(badEntryElfPath, makeSyntheticElf(true, false))) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("could not create synthetic PS2 ELF fixtures");
    }

    gow::disc::Ps2ElfInfo elfInfo{};
    if (!gow::disc::validatePs2Elf(vfs, "data/boot.elf", elfInfo, &error) ||
        elfInfo.fileSize != 88u ||
        elfInfo.entryPoint != 0x00100000u ||
        elfInfo.programHeaderCount != 1u ||
        elfInfo.loadableSegmentCount != 1u) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("private cdRoot did not validate a case-insensitive versioned PS2 ELF: " + error);
    }

    gow::disc::Ps2ElfInfo rejectedElfInfo{};
    if (gow::disc::validatePs2Elf(vfs, "DATA/BAD-MAGIC.ELF",
                                  rejectedElfInfo, &error) ||
        error.find("magic") == std::string::npos) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("private cdRoot accepted a boot ELF with invalid magic");
    }
    if (gow::disc::validatePs2Elf(vfs, "DATA/BAD-ENTRY.ELF",
                                  rejectedElfInfo, &error) ||
        error.find("entry point") == std::string::npos) {
        vfs.close();
        std::filesystem::remove_all(root);
        std::filesystem::remove(outsideFile);
        return fail("private cdRoot accepted an ELF entry point outside PT_LOAD");
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
