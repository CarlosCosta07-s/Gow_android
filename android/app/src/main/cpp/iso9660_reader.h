#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gow::disc {

inline constexpr uint64_t kIsoSectorSize = 2048u;

struct IsoFile {
    uint64_t byteOffset = 0u;
    uint64_t size = 0u;
    uint32_t lsn = 0u;
};

struct IsoDiscFile {
    std::string path;
    uint32_t lsn = 0u;
    uint32_t size = 0u;
};

struct IsoDiscLayout {
    uint32_t sectorCount = 0u;
    uint32_t layer1Start = 0u;
    std::vector<IsoDiscFile> files;
};

struct IsoDirectoryEntry {
    std::string name;
    IsoFile file{};
    bool isDirectory = false;
    bool multiExtent = false;
};

class Iso9660Reader {
public:
    Iso9660Reader() = default;
    ~Iso9660Reader();

    Iso9660Reader(const Iso9660Reader &) = delete;
    Iso9660Reader &operator=(const Iso9660Reader &) = delete;

    Iso9660Reader(Iso9660Reader &&other) noexcept;
    Iso9660Reader &operator=(Iso9660Reader &&other) noexcept;

    // Duplicates descriptor; the caller retains ownership of its descriptor.
    bool openDescriptor(int descriptor, std::string *error = nullptr);
    void close() noexcept;
    bool isOpen() const noexcept { return descriptor_ >= 0; }
    uint64_t imageSize() const noexcept { return imageSize_; }
    uint32_t sectorCount() const noexcept { return sectorCount_; }
    uint32_t layer1Start() const noexcept { return layer1Start_; }

    // Scans every volume/layer and returns all regular files with physical
    // disc LSNs. Paths are normalized ISO9660 relative paths using '/'.
    // Multi-extent files and ambiguous paths are rejected explicitly.
    bool scanDiscLayout(IsoDiscLayout &layout,
                        std::string *error = nullptr) const;

    // Reads whole 2048-byte sectors from the original image.
    bool readSectors(uint32_t lsn, uint32_t count,
                     void *destination) const;

    // ISO paths are case-insensitive and may include an optional ;version suffix.
    // Directories and multi-extent files are not returned as ordinary files.
    bool lookupFile(std::string_view path, IsoFile &file,
                    std::string *error = nullptr) const;

    // Lists immediate children. Names are normalized to uppercase without
    // ISO version suffixes; entry order follows the ISO directory record order.
    // Multi-extent entries are reported but cannot yet be read as one file.
    bool listDirectory(std::string_view path,
                       std::vector<IsoDirectoryEntry> &entries,
                       std::string *error = nullptr) const;

    // Reads from a file returned by lookupFile. EOF is a successful short read.
    bool readFile(const IsoFile &file, uint64_t offset, void *destination,
                  size_t requested, size_t &bytesRead) const;

private:
    struct Directory {
        uint64_t byteOffset = 0u;
        uint64_t size = 0u;
        uint32_t baseLsn = 0u;
        uint32_t volumeSectors = 0u;
    };

    struct Volume {
        uint32_t baseLsn = 0u;
        uint32_t sectorCount = 0u;
        Directory root{};
    };

    bool readAt(uint64_t offset, void *destination, size_t size) const;
    bool resolveDirectory(std::string_view path, Directory &directory,
                          std::string *error) const;
    bool readDirectoryEntries(const Directory &directory,
                              std::vector<IsoDirectoryEntry> &entries,
                              std::string *error) const;
    static std::string normalizeIdentifier(std::string_view value);
    static void setError(std::string *error, std::string_view message);

    int descriptor_ = -1;
    uint64_t imageSize_ = 0u;
    uint32_t sectorCount_ = 0u;
    uint32_t layer1Start_ = 0u;
    Directory root_{};
    std::vector<Volume> volumes_;
};

} // namespace gow::disc
