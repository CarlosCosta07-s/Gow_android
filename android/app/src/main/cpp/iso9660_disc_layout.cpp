#include "iso9660_disc_layout.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace gow::disc {
namespace {

constexpr uint64_t kSectorSize = kIsoSectorSize;
constexpr uint32_t kSectorBatch = 64u;
constexpr size_t kRelativePathLimit = 1024u;
constexpr size_t kComponentLimit = 255u;
constexpr size_t kMetadataRecordSize = 4u + kIsoSectorSize;

struct FileInterval {
    uint64_t begin = 0u;
    uint64_t end = 0u;
};

void setError(std::string *error, const std::string &message) {
    if (error) *error = message;
}

bool splitSafePath(const std::string &path, std::vector<std::string> &parts) {
    parts.clear();
    if (path.empty() || path.size() > kRelativePathLimit ||
        path.front() == '/' || path.back() == '/') {
        return false;
    }

    size_t cursor = 0u;
    while (cursor < path.size()) {
        const size_t end = path.find('/', cursor);
        const size_t componentEnd = end == std::string::npos ? path.size() : end;
        const std::string component = path.substr(cursor, componentEnd - cursor);
        if (component.empty() || component == "." || component == ".." ||
            component.size() > kComponentLimit) {
            return false;
        }
        for (const unsigned char ch : component) {
            if (ch == 0u || ch < 0x20u || ch == 0x7fu ||
                ch == ':' || ch == '\\' || ch == '/') {
                return false;
            }
        }
        parts.push_back(component);
        if (parts.size() > 32u) return false;
        if (end == std::string::npos) break;
        cursor = end + 1u;
    }
    return !parts.empty();
}

bool buildIntervals(const IsoDiscLayout &layout,
                    std::vector<FileInterval> &intervals,
                    std::string *error) {
    intervals.clear();
    intervals.reserve(layout.files.size());
    for (const IsoDiscFile &file : layout.files) {
        std::vector<std::string> parts;
        if (!splitSafePath(file.path, parts)) {
            setError(error, "The disc layout contains an unsafe relative file path.");
            return false;
        }
        const uint64_t sectors =
            (static_cast<uint64_t>(file.size) + kSectorSize - 1u) / kSectorSize;
        const uint64_t begin = file.lsn;
        const uint64_t end = begin + sectors;
        if (end < begin || end > layout.sectorCount) {
            setError(error, "A disc layout file extent exceeds the image sector count.");
            return false;
        }
        if (sectors != 0u) {
            if (!intervals.empty() && begin < intervals.back().end) {
                setError(error, "The disc layout contains overlapping file extents.");
                return false;
            }
            intervals.push_back({begin, end});
        }
    }
    return true;
}

bool layoutsMatch(const IsoDiscLayout &left, const IsoDiscLayout &right) {
    if (left.sectorCount != right.sectorCount ||
        left.layer1Start != right.layer1Start ||
        left.files.size() != right.files.size()) {
        return false;
    }
    for (size_t i = 0u; i < left.files.size(); ++i) {
        const auto &a = left.files[i];
        const auto &b = right.files[i];
        if (a.path != b.path || a.lsn != b.lsn || a.size != b.size) return false;
    }
    return true;
}

bool validateLayoutAgainstReader(const Iso9660Reader &reader,
                                 const IsoDiscLayout &layout,
                                 std::vector<FileInterval> &intervals,
                                 std::string *error) {
    if (!reader.isOpen() || layout.sectorCount == 0u ||
        layout.sectorCount != reader.sectorCount() ||
        layout.layer1Start != reader.layer1Start() ||
        layout.files.empty()) {
        setError(error, "The disc layout does not match the currently open ISO image.");
        return false;
    }

    IsoDiscLayout actual{};
    std::string scanError;
    if (!reader.scanDiscLayout(actual, &scanError)) {
        setError(error, "Could not rescan the ISO image: " + scanError);
        return false;
    }
    if (!layoutsMatch(layout, actual)) {
        setError(error, "The supplied disc layout differs from the ISO image.");
        return false;
    }
    return buildIntervals(layout, intervals, error);
}

bool sectorIsCovered(uint64_t lsn, const std::vector<FileInterval> &intervals,
                     size_t &intervalIndex) {
    while (intervalIndex < intervals.size() &&
           lsn >= intervals[intervalIndex].end) {
        ++intervalIndex;
    }
    return intervalIndex < intervals.size() &&
           lsn >= intervals[intervalIndex].begin &&
           lsn < intervals[intervalIndex].end;
}

bool sectorHasData(const uint8_t *sector) {
    return std::any_of(sector, sector + kIsoSectorSize,
                       [](uint8_t value) { return value != 0u; });
}

bool readNonFileSectorCount(const Iso9660Reader &reader,
                            const IsoDiscLayout &layout,
                            const std::vector<FileInterval> &intervals,
                            uint64_t &count,
                            std::string *error) {
    count = 0u;
    std::vector<uint8_t> buffer(static_cast<size_t>(kSectorBatch) * kIsoSectorSize);
    size_t intervalIndex = 0u;

    for (uint64_t first = 0u; first < layout.sectorCount;) {
        const uint32_t batch = static_cast<uint32_t>(
            std::min<uint64_t>(kSectorBatch, layout.sectorCount - first));
        if (!reader.readSectors(static_cast<uint32_t>(first), batch, buffer.data())) {
            setError(error, "Could not scan ISO sectors while measuring disc metadata.");
            return false;
        }

        for (uint32_t index = 0u; index < batch; ++index) {
            const uint64_t lsn = first + index;
            if (sectorIsCovered(lsn, intervals, intervalIndex)) continue;
            const uint8_t *sector =
                buffer.data() + static_cast<size_t>(index) * kIsoSectorSize;
            if (sectorHasData(sector)) {
                if (count == std::numeric_limits<uint64_t>::max()) {
                    setError(error, "The metadata sector count overflowed.");
                    return false;
                }
                ++count;
            }
        }
        first += batch;
    }
    return true;
}

std::string buildLayoutText(const IsoDiscLayout &layout) {
    std::ostringstream text;
    text << "sectors " << layout.sectorCount << '\n';
    text << "layer1 " << layout.layer1Start << '\n';
    for (const IsoDiscFile &file : layout.files) {
        text << "file " << file.lsn << ' ' << file.size << ' ' << file.path << '\n';
    }
    return text.str();
}

bool writeAll(int fd, const uint8_t *data, size_t size) {
    size_t written = 0u;
    while (written < size) {
        const ssize_t count = ::write(fd, data + written, size - written);
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (count == 0) return false;
        written += static_cast<size_t>(count);
    }
    return true;
}

bool validateExtractedFiles(int rootFd, const IsoDiscLayout &layout,
                            std::string *error) {
    for (const IsoDiscFile &file : layout.files) {
        std::vector<std::string> parts;
        if (!splitSafePath(file.path, parts)) {
            setError(error, "The extracted disc contains an unsafe file path.");
            return false;
        }

        int parentFd = ::openat(rootFd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (parentFd < 0) {
            setError(error, "Could not open the private disc root.");
            return false;
        }
        bool parentOk = true;
        for (size_t i = 0u; i + 1u < parts.size(); ++i) {
            const int childFd = ::openat(parentFd, parts[i].c_str(),
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            ::close(parentFd);
            parentFd = childFd;
            if (parentFd < 0) {
                parentOk = false;
                break;
            }
        }
        if (!parentOk) {
            setError(error, "An extracted disc directory is missing or is a symlink.");
            return false;
        }

        const int fileFd = ::openat(parentFd, parts.back().c_str(),
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        ::close(parentFd);
        if (fileFd < 0) {
            setError(error, "An extracted ISO file is missing or is a symlink.");
            return false;
        }

        struct stat status {};
        const bool valid =
            ::fstat(fileFd, &status) == 0 &&
            S_ISREG(status.st_mode) &&
            status.st_size >= 0 &&
            static_cast<uint64_t>(status.st_size) == file.size;
        ::close(fileFd);
        if (!valid) {
            setError(error, "An extracted ISO file has an unexpected type or size.");
            return false;
        }
    }
    return true;
}

void writeU32Le(uint8_t *destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8u);
    destination[2] = static_cast<uint8_t>(value >> 16u);
    destination[3] = static_cast<uint8_t>(value >> 24u);
}

} // namespace

bool measureIsoDiscMetadata(const Iso9660Reader &reader,
                            const IsoDiscLayout &layout,
                            IsoDiscMetadataInfo &info,
                            std::string *error) {
    info = {};
    std::vector<FileInterval> intervals;
    if (!validateLayoutAgainstReader(reader, layout, intervals, error)) return false;

    std::string text = buildLayoutText(layout);
    info.layoutFileBytes = static_cast<uint64_t>(text.size());
    if (!readNonFileSectorCount(reader, layout, intervals,
                                info.metadataSectorCount, error)) {
        info = {};
        return false;
    }
    if (info.metadataSectorCount >
        (std::numeric_limits<uint64_t>::max() / kMetadataRecordSize)) {
        info = {};
        setError(error, "The disc metadata file would exceed the supported size.");
        return false;
    }
    info.metadataFileBytes = info.metadataSectorCount * kMetadataRecordSize;
    return true;
}

bool writeIsoDiscMetadata(const Iso9660Reader &reader,
                          const IsoDiscLayout &layout,
                          const std::string &extractedRoot,
                          IsoDiscMetadataInfo *info,
                          std::string *error) {
    IsoDiscMetadataInfo measured{};
    if (!measureIsoDiscMetadata(reader, layout, measured, error)) return false;
    if (extractedRoot.empty()) {
        setError(error, "The extracted disc root path is empty.");
        return false;
    }

    const int rootFd = ::open(extractedRoot.c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (rootFd < 0) {
        setError(error, "Could not open the extracted private disc root.");
        return false;
    }

    // Removes only this call's temporary files. Published files are removed
    // separately, and only if this call published them.
    auto removeTemporaries = [&]() {
        ::unlinkat(rootFd, ".disc_layout.tmp", 0);
        ::unlinkat(rootFd, ".disc_meta.tmp", 0);
    };
    auto fail = [&](const std::string &message) {
        removeTemporaries();
        ::close(rootFd);
        setError(error, message);
        return false;
    };

    // Never overwrite, and never delete, metadata that was already there.
    for (const char *name : {".disc_layout", ".disc_meta"}) {
        struct stat existing {};
        if (::fstatat(rootFd, name, &existing, AT_SYMLINK_NOFOLLOW) == 0) {
            ::close(rootFd);
            setError(error, "Disc metadata files already exist in the extracted root.");
            return false;
        }
        if (errno != ENOENT) {
            ::close(rootFd);
            setError(error, "Could not inspect existing disc metadata files.");
            return false;
        }
    }

    if (!validateExtractedFiles(rootFd, layout, error)) {
        ::close(rootFd);
        return false;
    }

    std::vector<FileInterval> intervals;
    if (!buildIntervals(layout, intervals, error)) {
        ::close(rootFd);
        return false;
    }

    // A previous interrupted run may have left temporaries; they are ours
    // (dot-prefixed, never part of the disc), so clear them before O_EXCL.
    removeTemporaries();

    struct statvfs storageInfo {};
    if (::fstatvfs(rootFd, &storageInfo) != 0 || storageInfo.f_frsize == 0u) {
        return fail("Could not query free space for disc metadata.");
    }
    const uint64_t blockSize = static_cast<uint64_t>(storageInfo.f_frsize);
    const uint64_t blocks = static_cast<uint64_t>(storageInfo.f_bavail);
    const uint64_t available =
        blocks > std::numeric_limits<uint64_t>::max() / blockSize
            ? std::numeric_limits<uint64_t>::max()
            : blocks * blockSize;
    if (measured.metadataFileBytes > available ||
        measured.layoutFileBytes > available - measured.metadataFileBytes) {
        return fail("Insufficient private storage for the PS2 disc metadata files.");
    }

    const std::string layoutText = buildLayoutText(layout);
    const int layoutFd = ::openat(rootFd, ".disc_layout.tmp",
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (layoutFd < 0) {
        return fail("Could not create the temporary .disc_layout file.");
    }
    bool layoutOk = writeAll(layoutFd,
        reinterpret_cast<const uint8_t *>(layoutText.data()), layoutText.size()) &&
        ::fsync(layoutFd) == 0;
    if (::close(layoutFd) != 0) layoutOk = false;
    if (!layoutOk) {
        return fail("Could not write and synchronize .disc_layout.");
    }

    const int metaFd = ::openat(rootFd, ".disc_meta.tmp",
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (metaFd < 0) {
        return fail("Could not create the temporary .disc_meta file.");
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(kSectorBatch) * kIsoSectorSize);
    std::array<uint8_t, kMetadataRecordSize> record{};
    size_t intervalIndex = 0u;
    uint64_t writtenSectors = 0u;
    bool metaOk = true;
    for (uint64_t first = 0u; first < layout.sectorCount && metaOk;) {
        const uint32_t batch = static_cast<uint32_t>(
            std::min<uint64_t>(kSectorBatch, layout.sectorCount - first));
        if (!reader.readSectors(static_cast<uint32_t>(first), batch, buffer.data())) {
            metaOk = false;
            break;
        }
        for (uint32_t index = 0u; index < batch; ++index) {
            const uint64_t lsn = first + index;
            if (sectorIsCovered(lsn, intervals, intervalIndex)) continue;
            const uint8_t *sector =
                buffer.data() + static_cast<size_t>(index) * kIsoSectorSize;
            if (!sectorHasData(sector)) continue;
            if (lsn > std::numeric_limits<uint32_t>::max()) {
                metaOk = false;
                break;
            }
            writeU32Le(record.data(), static_cast<uint32_t>(lsn));
            std::memcpy(record.data() + 4u, sector, kIsoSectorSize);
            if (!writeAll(metaFd, record.data(), record.size())) {
                metaOk = false;
                break;
            }
            ++writtenSectors;
        }
        first += batch;
    }

    if (metaOk && writtenSectors != measured.metadataSectorCount) {
        metaOk = false;
    }
    if (metaOk && ::fsync(metaFd) != 0) metaOk = false;
    if (::close(metaFd) != 0) metaOk = false;
    if (!metaOk) {
        return fail("Could not generate a complete .disc_meta sector index.");
    }

    // .disc_layout is published last: its presence marks a complete pair.
    if (::renameat(rootFd, ".disc_meta.tmp", rootFd, ".disc_meta") != 0) {
        return fail("Could not publish .disc_meta.");
    }
    if (::renameat(rootFd, ".disc_layout.tmp", rootFd, ".disc_layout") != 0) {
        ::unlinkat(rootFd, ".disc_meta", 0);
        return fail("Could not publish .disc_layout.");
    }
    if (::fsync(rootFd) != 0) {
        ::unlinkat(rootFd, ".disc_layout", 0);
        ::unlinkat(rootFd, ".disc_meta", 0);
        return fail("Could not synchronize the extracted disc metadata directory.");
    }

    ::close(rootFd);
    if (info) *info = measured;
    return true;
}

} // namespace gow::disc
