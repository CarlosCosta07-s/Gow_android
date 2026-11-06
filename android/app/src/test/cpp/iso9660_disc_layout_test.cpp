// Host test for .disc_layout / .disc_meta generation. Uses only a synthetic
// dual-layer ISO built in this file; no game data is involved.
#include "iso9660_disc_layout.h"
#include "iso9660_installer.h"
#include "iso9660_reader.h"
#include <ps2x/iop/disc_source.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using gow::disc::IsoDiscLayout;
using gow::disc::IsoDiscMetadataInfo;
using gow::disc::Iso9660Reader;
using gow::disc::installIsoDisc;
using gow::disc::measureIsoDiscMetadata;
using gow::disc::writeIsoDiscMetadata;

namespace {

constexpr uint64_t kSector = gow::disc::kIsoSectorSize;
constexpr size_t kRecord = 4u + gow::disc::kIsoSectorSize;

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

void addRecord(std::array<uint8_t, kSector> &sector, size_t &cursor,
               const std::vector<uint8_t> &record) {
    if (cursor + record.size() > sector.size()) return;
    std::copy(record.begin(), record.end(),
              sector.begin() + static_cast<std::ptrdiff_t>(cursor));
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

const std::vector<uint8_t> &lowPayload() {
    static const std::vector<uint8_t> payload = [] {
        std::vector<uint8_t> bytes(2050u);
        for (size_t i = 0u; i < bytes.size(); ++i)
            bytes[i] = static_cast<uint8_t>((i * 13u + 7u) & 0xFFu);
        return bytes;
    }();
    return payload;
}

// 64 sectors, layer 1 starts at LSN 16 (layer0 = 32 sectors).
// Files:  LOW.BIN@22 (2050 B, 2 sectors), LOWDIR/LOWNEST.BIN@25 (1 B),
//         HIGH.BIN@38 (3 B).
// Non-file sectors with data: 16, 20, 24, 32, 36.
bool buildDualLayerFixture(int fd) {
    constexpr uint32_t layer0Sectors = 32u;
    constexpr uint32_t totalSectors = 64u;
    constexpr uint64_t layer1Base = layer0Sectors - 16u;
    if (::ftruncate(fd, static_cast<off_t>(totalSectors * kSector)) != 0) return false;

    const std::string_view self("\0", 1u);
    const std::string_view parent("\1", 1u);

    std::array<uint8_t, kSector> pvd0{};
    pvd0[0] = 1u;
    std::memcpy(pvd0.data() + 1u, "CD001", 5u);
    pvd0[6] = 1u;
    putU32Both(pvd0.data() + 80u, layer0Sectors);
    const auto root0 = makeRecord(self, 20u, 2048u, 0x02u);
    std::copy(root0.begin(), root0.end(), pvd0.begin() + 156);
    if (!writeAt(fd, 16u * kSector, pvd0.data(), pvd0.size())) return false;

    std::array<uint8_t, kSector> dir0{};
    size_t c0 = 0u;
    addRecord(dir0, c0, makeRecord(self, 20u, 2048u, 0x02u));
    addRecord(dir0, c0, makeRecord(parent, 20u, 2048u, 0x02u));
    addRecord(dir0, c0, makeRecord("LOW.BIN;1", 22u, 2050u, 0x00u));
    addRecord(dir0, c0, makeRecord("LOWDIR", 24u, 2048u, 0x02u));
    if (!writeAt(fd, 20u * kSector, dir0.data(), dir0.size())) return false;

    std::array<uint8_t, kSector> nested{};
    size_t cn = 0u;
    addRecord(nested, cn, makeRecord(self, 24u, 2048u, 0x02u));
    addRecord(nested, cn, makeRecord(parent, 20u, 2048u, 0x02u));
    addRecord(nested, cn, makeRecord("LOWNEST.BIN;1", 25u, 1u, 0x00u));
    if (!writeAt(fd, 24u * kSector, nested.data(), nested.size())) return false;

    if (!writeAt(fd, 22u * kSector, lowPayload().data(), lowPayload().size())) return false;
    const uint8_t nestedByte = 0xA5u;
    if (!writeAt(fd, 25u * kSector, &nestedByte, 1u)) return false;

    std::array<uint8_t, kSector> pvd1{};
    pvd1[0] = 1u;
    std::memcpy(pvd1.data() + 1u, "CD001", 5u);
    pvd1[6] = 1u;
    putU32Both(pvd1.data() + 80u, 32u);
    const auto root1 = makeRecord(self, 20u, 2048u, 0x02u);
    std::copy(root1.begin(), root1.end(), pvd1.begin() + 156);
    if (!writeAt(fd, static_cast<uint64_t>(layer0Sectors) * kSector,
                 pvd1.data(), pvd1.size())) return false;

    std::array<uint8_t, kSector> dir1{};
    size_t c1 = 0u;
    addRecord(dir1, c1, makeRecord(self, 20u, 2048u, 0x02u));
    addRecord(dir1, c1, makeRecord(parent, 20u, 2048u, 0x02u));
    addRecord(dir1, c1, makeRecord("HIGH.BIN;1", 22u, 3u, 0x00u));
    if (!writeAt(fd, (layer1Base + 20u) * kSector, dir1.data(), dir1.size())) return false;
    const std::array<uint8_t, 3> high{0xD1u, 0xD2u, 0xD3u};
    return writeAt(fd, (layer1Base + 22u) * kSector, high.data(), high.size());
}

bool writeFile(const fs::path &path, const std::vector<uint8_t> &bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::vector<uint8_t> readFile(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Extracted tree matching the fixture. Optional per-test tampering is applied
// by the caller afterwards.
bool populateRoot(const fs::path &root) {
    return writeFile(root / "LOW.BIN", lowPayload()) &&
           writeFile(root / "LOWDIR" / "LOWNEST.BIN", {0xA5u}) &&
           writeFile(root / "HIGH.BIN", {0xD1u, 0xD2u, 0xD3u});
}

struct Fixture {
    FILE *file = nullptr;
    Iso9660Reader reader;
    IsoDiscLayout layout;
    fs::path root;

    ~Fixture() {
        reader.close();
        if (file) std::fclose(file);
        std::error_code ec;
        if (!root.empty()) fs::remove_all(root, ec);
    }
};

bool makeFixture(Fixture &f, std::string &why) {
    f.file = std::tmpfile();
    if (!f.file) { why = "tmpfile"; return false; }
    const int fd = ::fileno(f.file);
    if (fd < 0 || !buildDualLayerFixture(fd)) { why = "build ISO"; return false; }
    std::string error;
    if (!f.reader.openDescriptor(fd, &error)) { why = "open: " + error; return false; }
    if (!f.reader.scanDiscLayout(f.layout, &error)) { why = "scan: " + error; return false; }

    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    std::string pattern = (base / "gow-disc-layout-XXXXXX").string();
    if (!::mkdtemp(pattern.data())) { why = "mkdtemp"; return false; }
    f.root = pattern;
    if (!populateRoot(f.root)) { why = "populate root"; return false; }
    return true;
}

int fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

bool present(const fs::path &path) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(path, ec));
}

int testMeasureAndWrite() {
    Fixture f;
    std::string why;
    if (!makeFixture(f, why)) return fail("fixture: " + why);

    IsoDiscMetadataInfo measured{};
    std::string error;
    if (!measureIsoDiscMetadata(f.reader, f.layout, measured, &error))
        return fail("measure failed: " + error);
    if (measured.metadataSectorCount != 5u ||
        measured.metadataFileBytes != 5u * kRecord)
        return fail("measured metadata sector count/bytes mismatch");

    IsoDiscMetadataInfo written{};
    if (!writeIsoDiscMetadata(f.reader, f.layout, f.root.string(), &written, &error))
        return fail("write failed: " + error);
    if (written.metadataSectorCount != 5u) return fail("written info mismatch");

    const std::string expectedLayout =
        "sectors 64\n"
        "layer1 16\n"
        "file 22 2050 LOW.BIN\n"
        "file 25 1 LOWDIR/LOWNEST.BIN\n"
        "file 38 3 HIGH.BIN\n";
    const auto layoutBytes = readFile(f.root / ".disc_layout");
    if (std::string(layoutBytes.begin(), layoutBytes.end()) != expectedLayout)
        return fail(".disc_layout text differs from the expected contract");

    const auto meta = readFile(f.root / ".disc_meta");
    if (meta.size() != 5u * kRecord) return fail(".disc_meta size mismatch");

    const uint32_t expectedLsns[5] = {16u, 20u, 24u, 32u, 36u};
    for (size_t i = 0u; i < 5u; ++i) {
        const uint8_t *rec = meta.data() + i * kRecord;
        const uint32_t lsn = static_cast<uint32_t>(rec[0]) |
                             (static_cast<uint32_t>(rec[1]) << 8u) |
                             (static_cast<uint32_t>(rec[2]) << 16u) |
                             (static_cast<uint32_t>(rec[3]) << 24u);
        if (lsn != expectedLsns[i]) return fail(".disc_meta LSN order/value mismatch");
        std::array<uint8_t, kSector> original{};
        if (!f.reader.readSectors(lsn, 1u, original.data()))
            return fail("could not re-read an original sector");
        if (std::memcmp(rec + 4u, original.data(), kSector) != 0)
            return fail(".disc_meta sector content differs from the ISO");
    }

    if (present(f.root / ".disc_layout.tmp") || present(f.root / ".disc_meta.tmp"))
        return fail("temporary files were left behind");
    return 0;
}

int testInstalledDiscMatchesWaffleDiscSource() {
    Fixture f;
    std::string why;
    if (!makeFixture(f, why)) return fail("fixture: " + why);

    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) return fail("temporary directory lookup");
    std::string pattern = (base / "gow-disc-install-XXXXXX").string();
    if (!::mkdtemp(pattern.data())) return fail("could not create installation root");
    const fs::path privateRoot(pattern);

    const auto installed = installIsoDisc(
        f.reader, privateRoot.string(), "synthetic-game");
    if (!installed.success) {
        fs::remove_all(privateRoot, ec);
        return fail("full ISO installer failed: " + installed.error);
    }

    const fs::path discRoot = privateRoot / "synthetic-game" / "disc";
    if (!present(discRoot / ".disc_layout") || !present(discRoot / ".disc_meta") ||
        !present(discRoot / "LOW.BIN") ||
        !present(discRoot / "LOWDIR" / "LOWNEST.BIN") ||
        !present(discRoot / "HIGH.BIN")) {
        fs::remove_all(privateRoot, ec);
        return fail("full installation did not publish files and metadata together");
    }

    auto disc = ps2x::iop::DiscSource::openExtracted(discRoot);
    if (!disc) {
        fs::remove_all(privateRoot, ec);
        return fail("Waffle DiscSource::openExtracted rejected generated metadata");
    }
    if (disc->layer1Start() != 16u) {
        fs::remove_all(privateRoot, ec);
        return fail("Waffle DiscSource returned the wrong layer-1 start");
    }

    std::array<uint8_t, kSector> pvd0{};
    std::array<uint8_t, kSector> pvd1{};
    if (!disc->readSectors(16u, 1u, pvd0.data()) ||
        !disc->readSectors(32u, 1u, pvd1.data()) ||
        std::memcmp(pvd0.data() + 1u, "CD001", 5u) != 0 ||
        std::memcmp(pvd1.data() + 1u, "CD001", 5u) != 0) {
        fs::remove_all(privateRoot, ec);
        return fail("Waffle DiscSource did not reconstruct both volume descriptors");
    }

    std::array<uint8_t, 2u * kSector> lowSectors{};
    if (!disc->readSectors(22u, 2u, lowSectors.data()) ||
        std::memcmp(lowSectors.data(), lowPayload().data(), lowPayload().size()) != 0 ||
        !std::all_of(lowSectors.begin() + static_cast<std::ptrdiff_t>(lowPayload().size()),
                     lowSectors.end(), [](uint8_t value) { return value == 0u; })) {
        fs::remove_all(privateRoot, ec);
        return fail("Waffle DiscSource did not read the original layer-0 file extent");
    }

    const std::array<uint8_t, 3> expectedHigh{0xD1u, 0xD2u, 0xD3u};
    std::array<uint8_t, kSector> highSector{};
    if (!disc->readSectors(38u, 1u, highSector.data()) ||
        !std::equal(expectedHigh.begin(), expectedHigh.end(), highSector.begin())) {
        fs::remove_all(privateRoot, ec);
        return fail("Waffle DiscSource did not read the original layer-1 file extent");
    }

    const auto lowFile = disc->findFile("cdrom0:\\LOW.BIN;1");
    const auto nestedFile = disc->findFile("cdrom0:\\LOWDIR\\LOWNEST.BIN;1");
    const auto highFile = disc->findFile("cdrom0:\\HIGH.BIN;1");
    if (!lowFile || lowFile->lsn != 22u || lowFile->size != 2050u ||
        !nestedFile || nestedFile->lsn != 25u ||
        !highFile || highFile->lsn != 38u) {
        fs::remove_all(privateRoot, ec);
        return fail("Waffle DiscSource findFile did not preserve paths and original LSNs");
    }

    std::array<uint8_t, 2u * kSector> beyondDisc{};
    if (disc->readSectors(63u, 2u, beyondDisc.data())) {
        fs::remove_all(privateRoot, ec);
        return fail("Waffle DiscSource accepted a read that crosses the disc end");
    }

    fs::remove_all(privateRoot, ec);
    if (ec) return fail("could not clean temporary installation");
    return 0;
}

struct CancelMetadataProgress {
    bool cancelDuringMeasure = false;
    bool cancelDuringWrite = false;
};

bool cancelMetadataProgress(const gow::disc::IsoInstallProgress &progress,
                            void *opaque) {
    auto *context = static_cast<CancelMetadataProgress *>(opaque);
    if (context == nullptr) return true;
    if (context->cancelDuringMeasure && progress.totalFiles == 0u &&
        progress.totalBytes == progress.copiedBytes) {
        return false;
    }
    if (context->cancelDuringWrite && progress.totalFiles > 0u &&
        progress.completedFiles == progress.totalFiles &&
        progress.copiedBytes == progress.totalBytes) {
        return false;
    }
    return true;
}

int testFullInstallCancellationBeforePublication() {
    for (const bool duringWrite : {false, true}) {
        Fixture f;
        std::string why;
        if (!makeFixture(f, why)) return fail("fixture: " + why);

        std::error_code ec;
        const fs::path base = fs::temp_directory_path(ec);
        if (ec) return fail("temporary directory lookup");
        std::string pattern = (base / "gow-disc-cancel-XXXXXX").string();
        if (!::mkdtemp(pattern.data())) return fail("could not create installation root");
        const fs::path privateRoot(pattern);

        CancelMetadataProgress context{};
        context.cancelDuringMeasure = !duringWrite;
        context.cancelDuringWrite = duringWrite;
        const auto result = installIsoDisc(
            f.reader, privateRoot.string(), "cancelled-game",
            cancelMetadataProgress, &context);
        if (result.success || !result.cancelled) {
            fs::remove_all(privateRoot, ec);
            return fail(duringWrite
                ? "metadata write cancellation did not stop the install"
                : "metadata analysis cancellation did not stop the install");
        }
        if (fs::exists(privateRoot / "cancelled-game") ||
            fs::exists(privateRoot / ".cancelled-game.staging")) {
            fs::remove_all(privateRoot, ec);
            return fail("cancelled full install left a visible or staging directory");
        }
        fs::remove_all(privateRoot, ec);
        if (ec) return fail("could not clean cancelled installation root");
    }
    return 0;
}

int testRejectsMissingFile() {
    Fixture f;
    std::string why;
    if (!makeFixture(f, why)) return fail("fixture: " + why);
    fs::remove(f.root / "HIGH.BIN");
    std::string error;
    if (writeIsoDiscMetadata(f.reader, f.layout, f.root.string(), nullptr, &error))
        return fail("missing extracted file was accepted");
    if (present(f.root / ".disc_layout") || present(f.root / ".disc_meta") ||
        present(f.root / ".disc_layout.tmp") || present(f.root / ".disc_meta.tmp"))
        return fail("failed run left metadata files behind");
    return 0;
}

int testRejectsWrongSize() {
    Fixture f;
    std::string why;
    if (!makeFixture(f, why)) return fail("fixture: " + why);
    if (!writeFile(f.root / "HIGH.BIN", {0xD1u, 0xD2u})) return fail("tamper");
    std::string error;
    if (writeIsoDiscMetadata(f.reader, f.layout, f.root.string(), nullptr, &error))
        return fail("wrong-size extracted file was accepted");
    return 0;
}

int testRejectsSymlink() {
    Fixture f;
    std::string why;
    if (!makeFixture(f, why)) return fail("fixture: " + why);
    fs::remove(f.root / "HIGH.BIN");
    if (!writeFile(f.root / "elsewhere.bin", {0xD1u, 0xD2u, 0xD3u}))
        return fail("tamper");
    std::error_code ec;
    fs::create_symlink(f.root / "elsewhere.bin", f.root / "HIGH.BIN", ec);
    if (ec) return fail("could not create symlink");
    std::string error;
    if (writeIsoDiscMetadata(f.reader, f.layout, f.root.string(), nullptr, &error))
        return fail("symlinked extracted file was accepted");
    return 0;
}

// Regression: a refused run must not delete metadata that was already there.
int testKeepsExistingMetadata() {
    Fixture f;
    std::string why;
    if (!makeFixture(f, why)) return fail("fixture: " + why);
    const std::vector<uint8_t> sentinel{'k', 'e', 'e', 'p'};
    if (!writeFile(f.root / ".disc_layout", sentinel)) return fail("tamper");
    std::string error;
    if (writeIsoDiscMetadata(f.reader, f.layout, f.root.string(), nullptr, &error))
        return fail("existing metadata was overwritten");
    if (readFile(f.root / ".disc_layout") != sentinel)
        return fail("pre-existing .disc_layout was modified or deleted");
    if (present(f.root / ".disc_meta"))
        return fail("a partial .disc_meta was created next to existing metadata");
    return 0;
}

// Regression: stale temporaries from an interrupted run must not block a retry.
int testRecoversFromStaleTemporaries() {
    Fixture f;
    std::string why;
    if (!makeFixture(f, why)) return fail("fixture: " + why);
    if (!writeFile(f.root / ".disc_layout.tmp", {1u}) ||
        !writeFile(f.root / ".disc_meta.tmp", {2u}))
        return fail("tamper");
    std::string error;
    if (!writeIsoDiscMetadata(f.reader, f.layout, f.root.string(), nullptr, &error))
        return fail("stale temporaries blocked the run: " + error);
    if (!present(f.root / ".disc_layout") || !present(f.root / ".disc_meta"))
        return fail("metadata missing after recovery");
    return 0;
}

int testRejectsLayoutMismatch() {
    Fixture f;
    std::string why;
    if (!makeFixture(f, why)) return fail("fixture: " + why);

    IsoDiscLayout wrongLsn = f.layout;
    wrongLsn.files[0].lsn += 1u;
    std::string error;
    if (writeIsoDiscMetadata(f.reader, wrongLsn, f.root.string(), nullptr, &error))
        return fail("layout with a moved extent was accepted");

    IsoDiscLayout wrongLayer = f.layout;
    wrongLayer.layer1Start = 0u;
    if (writeIsoDiscMetadata(f.reader, wrongLayer, f.root.string(), nullptr, &error))
        return fail("layout with a wrong layer1 start was accepted");

    IsoDiscLayout unsafe = f.layout;
    unsafe.files[0].path = "../escape.bin";
    if (writeIsoDiscMetadata(f.reader, unsafe, f.root.string(), nullptr, &error))
        return fail("layout with a traversal path was accepted");

    if (present(f.root / ".disc_layout") || present(f.root / ".disc_meta"))
        return fail("rejected layouts left metadata behind");
    return 0;
}

int testRejectsBadRoot() {
    Fixture f;
    std::string why;
    if (!makeFixture(f, why)) return fail("fixture: " + why);
    std::string error;
    if (writeIsoDiscMetadata(f.reader, f.layout, "", nullptr, &error))
        return fail("empty root was accepted");
    if (writeIsoDiscMetadata(f.reader, f.layout,
                             (f.root / "does-not-exist").string(), nullptr, &error))
        return fail("missing root was accepted");
    return 0;
}

} // namespace

int main() {
    struct Case { const char *name; int (*run)(); };
    const Case cases[] = {
        {"measure and write", testMeasureAndWrite},
        {"full install and Waffle DiscSource contract", testInstalledDiscMatchesWaffleDiscSource},
        {"full install cancellation before publication", testFullInstallCancellationBeforePublication},
        {"rejects missing extracted file", testRejectsMissingFile},
        {"rejects wrong extracted size", testRejectsWrongSize},
        {"rejects symlinked extracted file", testRejectsSymlink},
        {"keeps pre-existing metadata", testKeepsExistingMetadata},
        {"recovers from stale temporaries", testRecoversFromStaleTemporaries},
        {"rejects mismatched layout", testRejectsLayoutMismatch},
        {"rejects bad root", testRejectsBadRoot},
    };
    for (const Case &c : cases) {
        if (c.run() != 0) {
            std::cerr << "  in case: " << c.name << '\n';
            return 1;
        }
        std::cout << "ok - " << c.name << '\n';
    }
    std::cout << "iso9660 disc layout tests passed\n";
    return 0;
}
