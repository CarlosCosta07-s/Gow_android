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

bool validExtent(uint32_t extent, uint32_t size, uint64_t imageSize) noexcept {
    const uint64_t start = static_cast<uint64_t>(extent) * kIsoSectorSize;
    if (start > imageSize) return false;
    return static_cast<uint64_t>(size) <= imageSize - start;
}

} // namespace

Iso9660Reader::~Iso9660Reader() {
    close();
}

Iso9660Reader::Iso9660Reader(Iso9660Reader &&other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)),
      imageSize_(std::exchange(other.imageSize_, 0u)),
      root_(std::exchange(other.root_, Directory{})) {
}

Iso9660Reader &Iso9660Reader::operator=(Iso9660Reader &&other) noexcept {
    if (this != &other) {
        close();
        descriptor_ = std::exchange(other.descriptor_, -1);
        imageSize_ = std::exchange(other.imageSize_, 0u);
        root_ = std::exchange(other.root_, Directory{});
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
    root_ = {};
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

    uint32_t rootExtent = 0u;
    uint32_t rootSize = 0u;
    uint8_t rootFlags = 0u;
    const uint8_t rootRecordLength = pvd[156u];
    if (rootRecordLength < kDirectoryRecordMinimum ||
        static_cast<size_t>(156u + rootRecordLength) > pvd.size() ||
        !decodeDirectoryRecord(pvd.data() + 156u, rootRecordLength,
                               rootExtent, rootSize, rootFlags, error) ||
        (rootFlags & kDirectoryFlag) == 0u ||
        (rootFlags & kMultiExtentFlag) != 0u ||
        rootSize == 0u || rootSize > kMaxDirectorySize ||
        !validExtent(rootExtent, rootSize, imageSize_)) {
        close();
        if (error && error->empty()) {
            *error = "The ISO9660 root directory record is invalid.";
        }
        return false;
    }

    root_.byteOffset = static_cast<uint64_t>(rootExtent) * kIsoSectorSize;
    root_.size = rootSize;
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
            if (!validExtent(extent, size, imageSize_)) {
                setError(error, "An ISO9660 entry points outside the image.");
                return false;
            }

            const std::string_view identifier(
                reinterpret_cast<const char *>(recordBuffer.data() + 33u),
                identifierLength);
            const std::string name = normalizeIdentifier(identifier);
            if (name.empty()) {
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
                entry.file.byteOffset = static_cast<uint64_t>(extent) * kIsoSectorSize;
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
    if (descriptor_ < 0) {
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

    Directory current = root_;
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

        const bool last =
            path.find_first_not_of("/\\", cursor) == std::string_view::npos;
        std::vector<IsoDirectoryEntry> entries;
        if (!readDirectoryEntries(current, entries, error)) return false;

        const std::string wanted = normalizeIdentifier(component);
        const auto found = std::find_if(entries.begin(), entries.end(),
            [&wanted](const IsoDirectoryEntry &entry) { return entry.name == wanted; });
        if (found == entries.end()) {
            setError(error, "The requested file was not found in the ISO image.");
            return false;
        }
        if (found->multiExtent) {
            setError(error, "Multi-extent ISO9660 files are not supported yet.");
            return false;
        }

        if (last) {
            if (found->isDirectory) {
                setError(error, "The requested ISO path identifies a directory, not a file.");
                return false;
            }
            file = found->file;
            return true;
        }
        if (!found->isDirectory) {
            setError(error, "An ISO path component is not a directory.");
            return false;
        }

        current.byteOffset = found->file.byteOffset;
        current.size = found->file.size;
    }

    setError(error, "The ISO path does not identify a file.");
    return false;
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
