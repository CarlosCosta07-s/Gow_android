#include "iso9660_reader.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace gow::disc {
namespace {

constexpr uint64_t kPrimaryVolumeDescriptorSector = 16u;
constexpr uint64_t kMaxDirectorySize = 64u * 1024u * 1024u;
constexpr size_t kDirectoryRecordMinimum = 34u;
constexpr uint8_t kDirectoryFlag = 0x02u;
constexpr uint8_t kMultiExtentFlag = 0x80u;

uint32_t readU32Le(const uint8_t *data) noexcept {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8u) |
           (static_cast<uint32_t>(data[2]) << 16u) |
           (static_cast<uint32_t>(data[3]) << 24u);
}

uint32_t readU32Be(const uint8_t *data) noexcept {
    return (static_cast<uint32_t>(data[0]) << 24u) |
           (static_cast<uint32_t>(data[1]) << 16u) |
           (static_cast<uint32_t>(data[2]) << 8u) |
           static_cast<uint32_t>(data[3]);
}

bool decodeDirectoryRecord(const uint8_t *record, size_t recordLength,
                           uint32_t &extent, uint32_t &size,
                           uint8_t &flags, std::string *error) {
    if (recordLength < kDirectoryRecordMinimum ||
        record[0] != recordLength) {
        if (error) *error = "ISO9660 directory record is too short or inconsistent.";
        return false;
    }

    const uint8_t identifierLength = record[32];
    if (identifierLength == 0u ||
        static_cast<size_t>(33u + identifierLength) > recordLength) {
        if (error) *error = "ISO9660 directory record has an invalid identifier length.";
        return false;
    }

    const uint32_t extentLe = readU32Le(record + 2u);
    const uint32_t extentBe = readU32Be(record + 6u);
    const uint32_t sizeLe = readU32Le(record + 10u);
    const uint32_t sizeBe = readU32Be(record + 14u);
    if (extentLe != extentBe || sizeLe != sizeBe) {
        if (error) *error = "ISO9660 both-endian extent or size fields disagree.";
        return false;
    }

    extent = extentLe;
    size = sizeLe;
    flags = record[25];
    return true;
}

bool validExtent(uint32_t baseLsn, uint32_t extent, uint32_t size,
                 uint32_t volumeSectors, uint64_t imageSize) noexcept {
    const uint64_t relativeStart = extent;
    const uint64_t sectors =
        (static_cast<uint64_t>(size) + kIsoSectorSize - 1u) / kIsoSectorSize;
    if (relativeStart > volumeSectors ||
        sectors > static_cast<uint64_t>(volumeSectors) - relativeStart) {
        return false;
    }
    const uint64_t absoluteLsn = static_cast<uint64_t>(baseLsn) + relativeStart;
    if (absoluteLsn > std::numeric_limits<uint32_t>::max() ||
        absoluteLsn > std::numeric_limits<uint64_t>::max() / kIsoSectorSize) {
        return false;
    }
    const uint64_t start = absoluteLsn * kIsoSectorSize;
    if (start > imageSize) return false;
    return static_cast<uint64_t>(size) <= imageSize - start;
}

bool safeIsoIdentifier(std::string_view value) noexcept {
    if (value.empty()) return false;
    for (const unsigned char ch : value) {
        if (ch < 0x20u || ch == 0x7fu || ch == '/' || ch == '\\' || ch == ':') {
            return false;
        }
    }
    return true;
}

} // namespace

Iso9660Reader::~Iso9660Reader() {
    close();
}

Iso9660Reader::Iso9660Reader(Iso9660Reader &&other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)),
      imageSize_(std::exchange(other.imageSize_, 0u)),
      sectorCount_(std::exchange(other.sectorCount_, 0u)),
      layer1Start_(std::exchange(other.layer1Start_, 0u)),
      root_(std::exchange(other.root_, Directory{})),
      volumes_(std::move(other.volumes_)) {
    other.volumes_.clear();
}

Iso9660Reader &Iso9660Reader::operator=(Iso9660Reader &&other) noexcept {
    if (this != &other) {
        close();
        descriptor_ = std::exchange(other.descriptor_, -1);
        imageSize_ = std::exchange(other.imageSize_, 0u);
        sectorCount_ = std::exchange(other.sectorCount_, 0u);
        layer1Start_ = std::exchange(other.layer1Start_, 0u);
        root_ = std::exchange(other.root_, Directory{});
        volumes_ = std::move(other.volumes_);
        other.volumes_.clear();
    }
    return *this;
}

void Iso9660Reader::setError(std::string *error, std::string_view message) {
    if (error) *error = std::string(message);
}

void Iso9660Reader::close() noexcept {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
    imageSize_ = 0u;
    sectorCount_ = 0u;
    layer1Start_ = 0u;
    root_ = {};
    volumes_.clear();
}

bool Iso9660Reader::openDescriptor(int descriptor, std::string *error) {
    close();
    if (descriptor < 0) {
        setError(error, "The ISO descriptor is invalid.");
        return false;
    }

    const int copy = ::dup(descriptor);
    if (copy < 0) {
        setError(error, "Could not duplicate the ISO descriptor.");
        return false;
    }

    struct stat status {};
    if (::fstat(copy, &status) != 0 || status.st_size < 0) {
        ::close(copy);
        setError(error, "Could not query the ISO image size.");
        return false;
    }

    const uint64_t size = static_cast<uint64_t>(status.st_size);
    constexpr uint64_t minimumSize =
        (kPrimaryVolumeDescriptorSector + 1u) * kIsoSectorSize;
    if (size < minimumSize) {
        ::close(copy);
        setError(error, "The image is too small to contain an ISO9660 volume descriptor.");
        return false;
    }

    descriptor_ = copy;
    imageSize_ = size;
    const uint64_t sectors64 = size / kIsoSectorSize;
    if (sectors64 > std::numeric_limits<uint32_t>::max()) {
        close();
        setError(error, "The ISO image exceeds the supported 32-bit PS2 LSN range.");
        return false;
    }
    sectorCount_ = static_cast<uint32_t>(sectors64);

    std::array<uint8_t, kIsoSectorSize> pvd{};
    if (!readAt(kPrimaryVolumeDescriptorSector * kIsoSectorSize,
                pvd.data(), pvd.size())) {
        close();
        setError(error, "Could not read the ISO9660 primary volume descriptor.");
        return false;
    }

    if (pvd[0] != 1u ||
        std::memcmp(pvd.data() + 1u, "CD001", 5u) != 0 ||
        pvd[6] != 1u) {
        close();
        setError(error, "Sector 16 is not a supported ISO9660 primary volume descriptor.");
        return false;
    }

    const uint32_t layer0Le = readU32Le(pvd.data() + 80u);
    const uint32_t layer0Be = readU32Be(pvd.data() + 84u);
    if (layer0Le != layer0Be ||
        layer0Le <= kPrimaryVolumeDescriptorSector ||
        layer0Le > sectorCount_) {
        close();
        setError(error, "The ISO9660 volume-space size is invalid or inconsistent.");
        return false;
    }

    auto decodeRoot = [&](const std::array<uint8_t, kIsoSectorSize> &descriptor,
                          uint32_t baseLsn, uint32_t volumeSectors,
                          Directory &root) -> bool {
        uint32_t rootExtent = 0u;
        uint32_t rootSize = 0u;
        uint8_t rootFlags = 0u;
        const uint8_t rootRecordLength = descriptor[156u];
        if (rootRecordLength < kDirectoryRecordMinimum ||
            static_cast<size_t>(156u + rootRecordLength) > descriptor.size() ||
            !decodeDirectoryRecord(descriptor.data() + 156u, rootRecordLength,
                                   rootExtent, rootSize, rootFlags, error) ||
            (rootFlags & kDirectoryFlag) == 0u ||
            (rootFlags & kMultiExtentFlag) != 0u ||
            rootSize == 0u || rootSize > kMaxDirectorySize ||
            !validExtent(baseLsn, rootExtent, rootSize,
                         volumeSectors, imageSize_)) {
            if (error && error->empty()) {
                *error = "The ISO9660 root directory record is invalid.";
            }
            return false;
        }
        const uint64_t absoluteLsn = static_cast<uint64_t>(baseLsn) + rootExtent;
        root.byteOffset = absoluteLsn * kIsoSectorSize;
        root.size = rootSize;
        root.baseLsn = baseLsn;
        root.volumeSectors = volumeSectors;
        return true;
    };

    Volume layer0{};
    layer0.baseLsn = 0u;
    layer0.sectorCount = layer0Le;
    if (!decodeRoot(pvd, layer0.baseLsn, layer0.sectorCount, layer0.root)) {
        close();
        return false;
    }
    volumes_.push_back(layer0);
    root_ = layer0.root;

    // The runtime's DVD model checks for a second PVD at physical LSN=layer0
    // sectors. Layer-1 extents are relative to layer0Sectors - 16.
    std::array<uint8_t, kIsoSectorSize> layer1Descriptor{};
    const uint64_t layer1PvdOffset =
        static_cast<uint64_t>(layer0Le) * kIsoSectorSize;
    if (layer0Le < sectorCount_ &&
        readAt(layer1PvdOffset, layer1Descriptor.data(), layer1Descriptor.size()) &&
        layer1Descriptor[0] == 1u &&
        std::memcmp(layer1Descriptor.data() + 1u, "CD001", 5u) == 0 &&
        layer1Descriptor[6] == 1u) {
        if (layer0Le < kPrimaryVolumeDescriptorSector) {
            close();
            setError(error, "The dual-layer ISO reports an invalid layer boundary.");
            return false;
        }
        const uint32_t layer1Le = readU32Le(layer1Descriptor.data() + 80u);
        const uint32_t layer1Be = readU32Be(layer1Descriptor.data() + 84u);
        const uint32_t layer1Base =
            layer0Le - static_cast<uint32_t>(kPrimaryVolumeDescriptorSector);
        if (layer1Le != layer1Be ||
            layer1Le <= kPrimaryVolumeDescriptorSector ||
            static_cast<uint64_t>(layer1Base) + layer1Le > sectorCount_) {
            close();
            setError(error, "The second ISO9660 volume has an invalid volume-space size.");
            return false;
        }
        Volume layer1{};
        layer1.baseLsn = layer1Base;
        layer1.sectorCount = layer1Le;
        if (!decodeRoot(layer1Descriptor, layer1.baseLsn,
                        layer1.sectorCount, layer1.root)) {
            close();
            return false;
        }
        layer1Start_ = layer1Base;
        volumes_.push_back(layer1);
    }

    return true;
}

bool Iso9660Reader::readAt(uint64_t offset, void *destination, size_t size) const {
    if (descriptor_ < 0 || (!destination && size != 0u) ||
        offset > imageSize_ || static_cast<uint64_t>(size) > imageSize_ - offset ||
        offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
        return false;
    }

    auto *bytes = static_cast<uint8_t *>(destination);
    size_t total = 0u;
    while (total < size) {
        const uint64_t position = offset + static_cast<uint64_t>(total);
        if (position > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
            return false;
        }
        const ssize_t count = ::pread(
            descriptor_, bytes + total, size - total, static_cast<off_t>(position));
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (count == 0) return false;
        total += static_cast<size_t>(count);
    }
    return true;
}

std::string Iso9660Reader::normalizeIdentifier(std::string_view value) {
    const size_t version = value.find(';');
    if (version != std::string_view::npos) value = value.substr(0u, version);
    while (!value.empty() && value.back() == '.') value.remove_suffix(1u);

    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        result.push_back(static_cast<char>(std::toupper(ch)));
    }
    return result;
}

bool Iso9660Reader::readDirectoryEntries(
    const Directory &directory, std::vector<IsoDirectoryEntry> &entries,
    std::string *error) const {
    entries.clear();
    if (descriptor_ < 0) {
        setError(error, "No ISO image is open.");
        return false;
    }
    if (directory.size == 0u || directory.byteOffset > imageSize_ ||
        directory.size > imageSize_ - directory.byteOffset ||
        directory.size > kMaxDirectorySize) {
        setError(error, "The ISO9660 directory size or extent is invalid.");
        return false;
    }

    constexpr size_t kMaxDirectoryEntries = 65536u;
    uint64_t cursor = 0u;
    std::array<uint8_t, kIsoSectorSize> recordBuffer{};
    std::vector<IsoDirectoryEntry> parsed;
    std::unordered_map<std::string, size_t> indicesByName;

    while (cursor < directory.size) {
        const uint64_t sectorRemaining =
            kIsoSectorSize - (cursor % kIsoSectorSize);
        const uint64_t availableInDirectory = directory.size - cursor;
        uint8_t recordLength = 0u;
        if (!readAt(directory.byteOffset + cursor, &recordLength, 1u)) {
            setError(error, "Could not read an ISO9660 directory record length.");
            return false;
        }

        if (recordLength == 0u) {
            cursor += std::min(sectorRemaining, availableInDirectory);
            continue;
        }
        if (recordLength < kDirectoryRecordMinimum ||
            recordLength > sectorRemaining ||
            recordLength > availableInDirectory) {
            setError(error, "An ISO9660 directory record crosses a sector or directory boundary.");
            return false;
        }
        if (!readAt(directory.byteOffset + cursor, recordBuffer.data(), recordLength)) {
            setError(error, "Could not read an ISO9660 directory record.");
            return false;
        }

        uint32_t extent = 0u;
        uint32_t size = 0u;
        uint8_t flags = 0u;
        if (!decodeDirectoryRecord(recordBuffer.data(), recordLength,
                                   extent, size, flags, error)) {
            return false;
        }

        const uint8_t identifierLength = recordBuffer[32u];
        const bool special = identifierLength == 1u &&
                             (recordBuffer[33u] == 0u || recordBuffer[33u] == 1u);
        if (!special) {
            if (!validExtent(directory.baseLsn, extent, size,
                             directory.volumeSectors, imageSize_)) {
                setError(error, "An ISO9660 entry points outside its volume or image.");
                return false;
            }

            const std::string_view identifier(
                reinterpret_cast<const char *>(recordBuffer.data() + 33u),
                identifierLength);
            if (!safeIsoIdentifier(identifier)) {
                setError(error, "An ISO9660 entry contains an unsafe identifier.");
                return false;
            }
            const std::string name = normalizeIdentifier(identifier);
            if (name.empty() || name == "." || name == "..") {
                setError(error, "An ISO9660 entry has an empty normalized name.");
                return false;
            }

            const bool isDirectory = (flags & kDirectoryFlag) != 0u;
            const bool multiExtent = (flags & kMultiExtentFlag) != 0u;
            if (isDirectory && (size == 0u || size > kMaxDirectorySize)) {
                setError(error, "An ISO9660 subdirectory has an unsupported size.");
                return false;
            }

            const auto existing = indicesByName.find(name);
            if (existing != indicesByName.end()) {
                const size_t previousIndex = existing->second;
                if (previousIndex + 1u != parsed.size() ||
                    !parsed[previousIndex].multiExtent ||
                    parsed[previousIndex].isDirectory || isDirectory) {
                    setError(error, "The ISO9660 directory contains ambiguous duplicate names.");
                    return false;
                }

                // Multi-extent file records are consecutive. Preserve the first
                // extent for diagnostics, but mark the logical file unreadable.
                parsed[previousIndex].multiExtent = true;
                parsed[previousIndex].file.size += static_cast<uint64_t>(size);
            } else {
                if (parsed.size() >= kMaxDirectoryEntries) {
                    setError(error, "The ISO9660 directory exceeds the entry-count safety limit.");
                    return false;
                }

                IsoDirectoryEntry entry;
                entry.name = name;
                const uint64_t absoluteLsn =
                    static_cast<uint64_t>(directory.baseLsn) + extent;
                if (absoluteLsn > std::numeric_limits<uint32_t>::max()) {
                    setError(error, "An ISO9660 entry exceeds the supported 32-bit LSN range.");
                    return false;
                }
                entry.file.lsn = static_cast<uint32_t>(absoluteLsn);
                entry.file.byteOffset = absoluteLsn * kIsoSectorSize;
                entry.file.size = size;
                entry.isDirectory = isDirectory;
                entry.multiExtent = multiExtent;
                indicesByName.emplace(entry.name, parsed.size());
                parsed.push_back(std::move(entry));
            }
        }

        cursor += recordLength;
    }

    entries.swap(parsed);
    return true;
}

bool Iso9660Reader::resolveDirectory(std::string_view path, Directory &directory,
                                     std::string *error) const {
    if (descriptor_ < 0) {
        setError(error, "No ISO image is open.");
        return false;
    }

    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.remove_prefix(1u);
    }

    directory = root_;
    if (path.empty()) return true;

    size_t cursor = 0u;
    unsigned int depth = 0u;
    while (cursor < path.size()) {
        while (cursor < path.size() &&
               (path[cursor] == '/' || path[cursor] == '\\')) {
            ++cursor;
        }
        if (cursor >= path.size()) break;

        const size_t end = path.find_first_of("/\\", cursor);
        const size_t componentEnd = end == std::string_view::npos ? path.size() : end;
        const std::string_view component = path.substr(cursor, componentEnd - cursor);
        cursor = componentEnd;

        if (component.empty() || component == "." || component == "..") {
            setError(error, "The ISO path contains an empty or traversal component.");
            return false;
        }
        if (component.size() > 255u || ++depth > 32u) {
            setError(error, "The ISO path exceeds a supported component or depth limit.");
            return false;
        }

        std::vector<IsoDirectoryEntry> entries;
        if (!readDirectoryEntries(directory, entries, error)) return false;

        const std::string wanted = normalizeIdentifier(component);
        const auto found = std::find_if(entries.begin(), entries.end(),
            [&wanted](const IsoDirectoryEntry &entry) { return entry.name == wanted; });
        if (found == entries.end()) {
            setError(error, "The requested ISO directory was not found.");
            return false;
        }
        if (!found->isDirectory || found->multiExtent) {
            setError(error, "An ISO path component is not a supported directory.");
            return false;
        }

        directory.byteOffset = found->file.byteOffset;
        directory.size = found->file.size;
    }

    return true;
}

bool Iso9660Reader::listDirectory(std::string_view path,
                                  std::vector<IsoDirectoryEntry> &entries,
                                  std::string *error) const {
    entries.clear();
    Directory directory{};
    if (!resolveDirectory(path, directory, error)) return false;

    std::vector<IsoDirectoryEntry> parsed;
    if (!readDirectoryEntries(directory, parsed, error)) return false;
    entries.swap(parsed);
    return true;
}

bool Iso9660Reader::lookupFile(std::string_view path, IsoFile &file,
                               std::string *error) const {
    file = {};
    if (descriptor_ < 0 || volumes_.empty()) {
        setError(error, "No ISO image is open.");
        return false;
    }

    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.remove_prefix(1u);
    }
    if (path.empty()) {
        setError(error, "The ISO path is empty.");
        return false;
    }

    std::vector<std::string> components;
    size_t cursor = 0u;
    while (cursor < path.size()) {
        while (cursor < path.size() &&
               (path[cursor] == '/' || path[cursor] == '\\')) {
            ++cursor;
        }
        if (cursor >= path.size()) break;
        const size_t end = path.find_first_of("/\\", cursor);
        const size_t componentEnd =
            end == std::string_view::npos ? path.size() : end;
        const std::string_view component =
            path.substr(cursor, componentEnd - cursor);
        if (component.empty() || component == "." || component == ".." ||
            component.size() > 255u || components.size() >= 32u) {
            setError(error, "The ISO path contains an unsafe component or exceeds the depth limit.");
            return false;
        }
        const std::string normalized = normalizeIdentifier(component);
        if (!safeIsoIdentifier(normalized) || normalized == "." || normalized == "..") {
            setError(error, "The ISO path contains an unsafe normalized component.");
            return false;
        }
        components.push_back(normalized);
        cursor = componentEnd;
    }
    if (components.empty()) {
        setError(error, "The ISO path does not identify a file.");
        return false;
    }

    for (const Volume &volume : volumes_) {
        Directory current = volume.root;
        bool missingInThisVolume = false;
        for (size_t index = 0u; index < components.size(); ++index) {
            std::vector<IsoDirectoryEntry> entries;
            if (!readDirectoryEntries(current, entries, error)) return false;

            const auto found = std::find_if(
                entries.begin(), entries.end(),
                [&components, index](const IsoDirectoryEntry &entry) {
                    return entry.name == components[index];
                });
            if (found == entries.end()) {
                missingInThisVolume = true;
                break;
            }
            if (found->multiExtent) {
                setError(error, "Multi-extent ISO9660 files are not supported yet.");
                return false;
            }

            const bool last = index + 1u == components.size();
            if (last) {
                if (found->isDirectory) {
                    missingInThisVolume = true;
                    break;
                }
                file = found->file;
                return true;
            }
            if (!found->isDirectory) {
                missingInThisVolume = true;
                break;
            }

            current.byteOffset = found->file.byteOffset;
            current.size = found->file.size;
            // The child remains in the same ISO volume/layer.
            current.baseLsn = volume.baseLsn;
            current.volumeSectors = volume.sectorCount;
        }
        if (!missingInThisVolume) break;
    }

    setError(error, "The requested file was not found in any ISO9660 volume.");
    return false;
}

bool Iso9660Reader::scanDiscLayout(IsoDiscLayout &layout,
                                   std::string *error) const {
    layout = {};
    if (descriptor_ < 0 || volumes_.empty() || sectorCount_ == 0u) {
        setError(error, "No supported ISO9660 volume is open.");
        return false;
    }

    constexpr size_t kMaxFiles = 65536u;
    constexpr unsigned int kMaxDepth = 32u;
    std::unordered_set<std::string> paths;
    std::unordered_set<std::string> visitedDirectories;
    std::vector<IsoDiscFile> files;

    auto walkDirectory = [&](auto &&self, const Directory &directory,
                             const std::string &parent,
                             unsigned int depth) -> bool {
        if (depth > kMaxDepth) {
            setError(error, "The ISO9660 directory tree exceeds the supported depth.");
            return false;
        }
        const std::string directoryKey =
            std::to_string(directory.baseLsn) + ":" +
            std::to_string(directory.byteOffset) + ":" +
            std::to_string(directory.size);
        if (!visitedDirectories.emplace(directoryKey).second) {
            setError(error, "The ISO9660 directory tree contains a cycle or duplicate directory extent.");
            return false;
        }

        std::vector<IsoDirectoryEntry> entries;
        if (!readDirectoryEntries(directory, entries, error)) return false;
        for (const auto &entry : entries) {
            if (entry.multiExtent) {
                setError(error, "Multi-extent ISO9660 files/directories are not supported by the disc-layout installer.");
                return false;
            }
            if (!safeIsoIdentifier(entry.name)) {
                setError(error, "The ISO9660 directory contains an unsafe normalized name.");
                return false;
            }
            const std::string path = parent.empty()
                ? entry.name
                : parent + "/" + entry.name;
            if (path.size() > 1024u) {
                setError(error, "An ISO9660 path exceeds the supported path length.");
                return false;
            }

            if (entry.isDirectory) {
                Directory child{};
                child.byteOffset = entry.file.byteOffset;
                child.size = entry.file.size;
                child.baseLsn = directory.baseLsn;
                child.volumeSectors = directory.volumeSectors;
                if (!self(self, child, path, depth + 1u)) return false;
                continue;
            }

            if (files.size() >= kMaxFiles ||
                entry.file.size > std::numeric_limits<uint32_t>::max()) {
                setError(error, "The ISO9660 file count or individual file size exceeds the supported limit.");
                return false;
            }
            std::string key = path;
            for (char &ch : key) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            if (!paths.emplace(key).second) {
                setError(error, "The ISO9660 contains duplicate paths across directories or layers.");
                return false;
            }
            files.push_back({path, entry.file.lsn,
                             static_cast<uint32_t>(entry.file.size)});
        }
        return true;
    };

    for (const Volume &volume : volumes_) {
        if (!walkDirectory(walkDirectory, volume.root, {}, 0u)) return false;
    }

    std::sort(files.begin(), files.end(),
        [](const IsoDiscFile &left, const IsoDiscFile &right) {
            return left.lsn < right.lsn;
        });
    uint64_t previousEnd = 0u;
    for (const IsoDiscFile &file : files) {
        const uint64_t sectors =
            (static_cast<uint64_t>(file.size) + kIsoSectorSize - 1u) / kIsoSectorSize;
        if (sectors != 0u && file.lsn < previousEnd) {
            setError(error, "The ISO9660 file extents overlap; a deterministic disc layout cannot be generated.");
            return false;
        }
        previousEnd = std::max(previousEnd, static_cast<uint64_t>(file.lsn) + sectors);
    }

    layout.sectorCount = sectorCount_;
    layout.layer1Start = layer1Start_;
    layout.files.swap(files);
    return true;
}

bool Iso9660Reader::readSectors(uint32_t lsn, uint32_t count,
                                void *destination) const {
    if (count == 0u) return true;
    if (destination == nullptr ||
        static_cast<uint64_t>(lsn) + count > sectorCount_) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(lsn) * kIsoSectorSize;
    const uint64_t byteCount = static_cast<uint64_t>(count) * kIsoSectorSize;
    if (byteCount > std::numeric_limits<size_t>::max()) return false;
    return readAt(offset, destination, static_cast<size_t>(byteCount));
}

bool Iso9660Reader::readFile(const IsoFile &file, uint64_t offset,
                             void *destination, size_t requested,
                             size_t &bytesRead) const {
    bytesRead = 0u;
    if (file.byteOffset > imageSize_ || file.size > imageSize_ - file.byteOffset ||
        offset > file.size || (!destination && requested != 0u)) {
        return false;
    }
    const uint64_t available = file.size - offset;
    const size_t count = static_cast<size_t>(
        std::min<uint64_t>(available, static_cast<uint64_t>(requested)));
    if (count == 0u) return true;
    if (!readAt(file.byteOffset + offset, destination, count)) return false;
    bytesRead = count;
    return true;
}

} // namespace gow::disc
