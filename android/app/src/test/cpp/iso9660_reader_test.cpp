#include "iso9660_reader.h"
#include "iso9660_installer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr uint64_t kSector = gow::disc::kIsoSectorSize;

void putU32Both(uint8_t *out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8u);
    out[2] = static_cast<uint8_t>(value >> 16u);
    out[3] = static_cast<uint8_t>(value >> 24u);
    out[4] = static_cast<uint8_t>(value >> 24u);
    out[5] = static_cast<uint8_t>(value >> 16u);
    out[6] = static_cast<uint8_t>(value >> 8u);
    out[7] = static_cast<uint8_t>(value);
}

std::vector<uint8_t> makeRecord(std::string_view name, uint32_t extent,
                                uint32_t size, uint8_t flags) {
    const size_t idSize = name.size();
    const size_t length = 33u + idSize + ((idSize & 1u) == 0u ? 1u : 0u);
    std::vector<uint8_t> rec(length, 0u);
    rec[0] = static_cast<uint8_t>(length);
    putU32Both(rec.data() + 2u, extent);
    putU32Both(rec.data() + 10u, size);
    rec[25] = flags;
    rec[28] = 1u;
    rec[30] = 0u;
    rec[31] = 1u;
    rec[32] = static_cast<uint8_t>(idSize);
    std::memcpy(rec.data() + 33u, name.data(), idSize);
    return rec;
}

template <size_t N>
void addRecord(std::array<uint8_t, N> &sector, size_t &cursor,
               const std::vector<uint8_t> &record) {
    if (cursor + record.size() > sector.size()) return;
    std::copy(record.begin(), record.end(), sector.begin() + static_cast<std::ptrdiff_t>(cursor));
    cursor += record.size();
}

bool writeAt(int fd, uint64_t offset, const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    size_t done = 0u;
    while (done < size) {
        const ssize_t count = ::pwrite(fd, bytes + done, size - done,
                                       static_cast<off_t>(offset + done));
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

int fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

bool buildFixture(int fd) {
    constexpr uint64_t sectors = 25u;
    if (::ftruncate(fd, static_cast<off_t>(sectors * kSector)) != 0) return false;

    std::array<uint8_t, kSector> pvd{};
    pvd[0] = 1u;
    std::memcpy(pvd.data() + 1u, "CD001", 5u);
    pvd[6] = 1u;
    const auto root = makeRecord(std::string_view("\0", 1u), 20u, 2048u, 0x02u);
    std::copy(root.begin(), root.end(), pvd.begin() + 156);
    if (!writeAt(fd, 16u * kSector, pvd.data(), pvd.size())) return false;

    std::array<uint8_t, kSector> rootDirectory{};
    size_t rootCursor = 0u;
    addRecord(rootDirectory, rootCursor, makeRecord(std::string_view("\0", 1u), 20u, 2048u, 0x02u));
    addRecord(rootDirectory, rootCursor, makeRecord(std::string_view("\1", 1u), 20u, 2048u, 0x02u));
    addRecord(rootDirectory, rootCursor, makeRecord("DATA", 21u, 2048u, 0x02u));
    addRecord(rootDirectory, rootCursor, makeRecord("SYSTEM.CNF;1", 24u, 32u, 0x00u));
    if (!writeAt(fd, 20u * kSector, rootDirectory.data(), rootDirectory.size())) return false;

    std::array<uint8_t, kSector> dataDirectory{};
    size_t dataCursor = 0u;
    addRecord(dataDirectory, dataCursor, makeRecord(std::string_view("\0", 1u), 21u, 2048u, 0x02u));
    addRecord(dataDirectory, dataCursor, makeRecord(std::string_view("\1", 1u), 20u, 2048u, 0x02u));
    addRecord(dataDirectory, dataCursor, makeRecord("TEST.BIN;1", 22u, 2050u, 0x00u));
    if (!writeAt(fd, 21u * kSector, dataDirectory.data(), dataDirectory.size())) return false;

    std::vector<uint8_t> content(2050u);
    for (size_t i = 0u; i < content.size(); ++i) {
        content[i] = static_cast<uint8_t>((i * 17u) & 0xFFu);
    }
    if (!writeAt(fd, 22u * kSector, content.data(), content.size())) return false;
    const std::array<uint8_t, 32> systemCnf{
        'B','O','O','T','2','=','c',':','\\','S','C','U','S','_','9','7','3','.','9','9',';','1'
    };
    return writeAt(fd, 24u * kSector, systemCnf.data(), systemCnf.size());
}
}

int main() {
    using gow::disc::Iso9660Reader;
    using gow::disc::IsoFile;

    FILE *file = std::tmpfile();
    if (!file) return fail("could not create temporary ISO fixture");
    const int fd = ::fileno(file);
    if (fd < 0 || !buildFixture(fd)) {
        std::fclose(file);
        return fail("could not build temporary ISO fixture");
    }

    Iso9660Reader reader;
    std::string error;
    if (!reader.openDescriptor(fd, &error)) {
        std::fclose(file);
        return fail(error);
    }
    if (!reader.isOpen() || reader.imageSize() != 25u * kSector) {
        std::fclose(file);
        return fail("ISO descriptor size/open state mismatch");
    }

    std::vector<gow::disc::IsoDirectoryEntry> rootEntries;
    if (!reader.listDirectory("/", rootEntries, &error)) {
        std::fclose(file);
        return fail("root ISO directory enumeration failed: " + error);
    }
    if (rootEntries.size() != 2u ||
        rootEntries[0].name != "DATA" || !rootEntries[0].isDirectory ||
        rootEntries[1].name != "SYSTEM.CNF" || rootEntries[1].isDirectory) {
        std::fclose(file);
        return fail("root ISO directory entries or ordering mismatch");
    }

    std::vector<gow::disc::IsoDirectoryEntry> dataEntries;
    if (!reader.listDirectory("data/", dataEntries, &error)) {
        std::fclose(file);
        return fail("nested ISO directory enumeration failed: " + error);
    }
    if (dataEntries.size() != 1u ||
        dataEntries[0].name != "TEST.BIN" ||
        dataEntries[0].isDirectory ||
        dataEntries[0].multiExtent ||
        dataEntries[0].file.byteOffset != 22u * kSector ||
        dataEntries[0].file.size != 2050u) {
        std::fclose(file);
        return fail("nested ISO directory entry metadata mismatch");
    }

    std::vector<gow::disc::IsoDirectoryEntry> invalidEntries;
    if (reader.listDirectory("../DATA", invalidEntries, &error) ||
        !invalidEntries.empty()) {
        std::fclose(file);
        return fail("directory enumeration accepted a traversal path");
    }
    if (reader.listDirectory("DATA/TEST.BIN", invalidEntries, &error) ||
        !invalidEntries.empty()) {
        std::fclose(file);
        return fail("directory enumeration accepted a regular file path");
    }

    IsoFile found{};
    if (!reader.lookupFile("/data/test.bin", found, &error)) {
        std::fclose(file);
        return fail(error);
    }
    if (found.byteOffset != 22u * kSector || found.size != 2050u) {
        std::fclose(file);
        return fail("nested ISO file extent or size mismatch");
    }

    std::array<uint8_t, 10> bytes{};
    size_t bytesRead = 0u;
    if (!reader.readFile(found, 2040u, bytes.data(), bytes.size(), bytesRead) ||
        bytesRead != bytes.size()) {
        std::fclose(file);
        return fail("cross-sector random access read failed");
    }
    for (size_t i = 0u; i < bytes.size(); ++i) {
        if (bytes[i] != static_cast<uint8_t>(((2040u + i) * 17u) & 0xFFu)) {
            std::fclose(file);
            return fail("cross-sector file bytes do not match source image");
        }
    }

    IsoFile cnf{};
    if (!reader.lookupFile("SYSTEM.CNF", cnf, &error) || cnf.size != 32u) {
        std::fclose(file);
        return fail("root lookup or optional ISO version suffix failed");
    }
    if (reader.lookupFile("../DATA/TEST.BIN", found, &error)) {
        std::fclose(file);
        return fail("path traversal was accepted");
    }
    if (reader.lookupFile("DATA", found, &error)) {
        std::fclose(file);
        return fail("directory was returned as a regular file");
    }

    char temporaryRoot[] = "/tmp/gow-iso-install-XXXXXX";
    char *privateRoot = ::mkdtemp(temporaryRoot);
    if (!privateRoot) {
        std::fclose(file);
        return fail("could not create private installation test directory");
    }

    const std::vector<gow::disc::IsoInstallFile> manifest{
        {"SYSTEM.CNF", "boot/SYSTEM.CNF", 32u},
        {"DATA/TEST.BIN", "assets/test.bin", 2050u},
    };
    const auto installResult = gow::disc::installIsoManifest(
        reader, manifest, privateRoot, "gow-test-install");
    if (!installResult.success ||
        installResult.totalBytes != 2082u ||
        installResult.copiedBytes != 2082u ||
        installResult.completedFiles != 2u) {
        std::filesystem::remove_all(privateRoot);
        std::fclose(file);
        return fail("manifest extraction did not publish a complete installation: " +
                    installResult.error);
    }

    const std::filesystem::path installedRoot =
        std::filesystem::path(privateRoot) / "gow-test-install";
    std::ifstream extractedFile(installedRoot / "assets/test.bin",
                                std::ios::binary);
    const std::vector<uint8_t> extractedBytes(
        (std::istreambuf_iterator<char>(extractedFile)),
        std::istreambuf_iterator<char>());
    if (!extractedFile.is_open() || extractedFile.bad() || extractedBytes.size() != 2050u) {
        std::filesystem::remove_all(privateRoot);
        std::fclose(file);
        return fail("extracted file has an unexpected size or could not be read");
    }
    for (size_t i = 0u; i < extractedBytes.size(); ++i) {
        if (extractedBytes[i] != static_cast<uint8_t>((i * 17u) & 0xFFu)) {
            std::filesystem::remove_all(privateRoot);
            std::fclose(file);
            return fail("extracted bytes do not match the ISO source file");
        }
    }

    const std::vector<gow::disc::IsoInstallFile> wrongSizeManifest{
        {"SYSTEM.CNF", "SYSTEM.CNF", 31u},
    };
    const auto wrongSizeResult = gow::disc::installIsoManifest(
        reader, wrongSizeManifest, privateRoot, "wrong-size");
    if (wrongSizeResult.success ||
        std::filesystem::exists(std::filesystem::path(privateRoot) / "wrong-size")) {
        std::filesystem::remove_all(privateRoot);
        std::fclose(file);
        return fail("manifest size mismatch was not rejected before extraction");
    }

    const std::vector<gow::disc::IsoInstallFile> traversalManifest{
        {"SYSTEM.CNF", "../outside.txt", 32u},
    };
    const auto traversalResult = gow::disc::installIsoManifest(
        reader, traversalManifest, privateRoot, "traversal");
    if (traversalResult.success ||
        std::filesystem::exists(std::filesystem::path(privateRoot) / "outside.txt")) {
        std::filesystem::remove_all(privateRoot);
        std::fclose(file);
        return fail("manifest destination traversal was not rejected");
    }

    const std::vector<gow::disc::IsoInstallFile> duplicateManifest{
        {"SYSTEM.CNF", "same.bin", 32u},
        {"DATA/TEST.BIN", "SAME.BIN", 2050u},
    };
    const auto duplicateResult = gow::disc::installIsoManifest(
        reader, duplicateManifest, privateRoot, "duplicate");
    if (duplicateResult.success) {
        std::filesystem::remove_all(privateRoot);
        std::fclose(file);
        return fail("case-insensitive duplicate destinations were accepted");
    }

    const std::vector<gow::disc::IsoInstallFile> prefixCollisionManifest{
        {"SYSTEM.CNF", "boot", 32u},
        {"DATA/TEST.BIN", "boot/file.bin", 2050u},
    };
    const auto prefixCollisionResult = gow::disc::installIsoManifest(
        reader, prefixCollisionManifest, privateRoot, "prefix-collision");
    if (prefixCollisionResult.success) {
        std::filesystem::remove_all(privateRoot);
        std::fclose(file);
        return fail("manifest file/directory prefix collision was accepted");
    }

    const std::vector<gow::disc::IsoInstallFile> cancelManifest{
        {"DATA/TEST.BIN", "cancel/test.bin", 2050u},
    };
    const auto cancelResult = gow::disc::installIsoManifest(
        reader, cancelManifest, privateRoot, "cancelled-install",
        [](const gow::disc::IsoInstallProgress &progress, void *) {
            return progress.copiedBytes == 0u;
        });
    if (cancelResult.success || !cancelResult.cancelled ||
        std::filesystem::exists(std::filesystem::path(privateRoot) / "cancelled-install") ||
        std::filesystem::exists(std::filesystem::path(privateRoot) / ".cancelled-install.staging")) {
        std::filesystem::remove_all(privateRoot);
        std::fclose(file);
        return fail("cancelled installation was published or staging was not cleaned");
    }

    std::filesystem::remove_all(privateRoot);
    std::fclose(file);
    reader.close();
    if (reader.isOpen()) return fail("reader did not close its duplicated descriptor");
    std::cout << "ISO9660 reader tests passed\n";
    return 0;
}
