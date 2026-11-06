#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gow::disc {

inline constexpr uint64_t kIsoSectorSize = 2048u;

struct IsoFile {
    uint64_t byteOffset = 0u;
    uint64_t size = 0u;
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

    // ISO paths are case-insensitive and may include an optional ;version suffix.
    // Directories and multi-extent files are not returned as ordinary files.
    bool lookupFile(std::string_view path, IsoFile &file,
                    std::string *error = nullptr) const;

    // Reads from a file returned by lookupFile. EOF is a successful short read.
    bool readFile(const IsoFile &file, uint64_t offset, void *destination,
                  size_t requested, size_t &bytesRead) const;

private:
    struct Directory {
        uint64_t byteOffset = 0u;
        uint64_t size = 0u;
    };

    bool readAt(uint64_t offset, void *destination, size_t size) const;
    bool findInDirectory(const Directory &directory, std::string_view name,
                         uint32_t &extent, uint32_t &size, uint8_t &flags,
                         std::string *error) const;
    static std::string normalizeIdentifier(std::string_view value);
    static void setError(std::string *error, std::string_view message);

    int descriptor_ = -1;
    uint64_t imageSize_ = 0u;
    Directory root_{};
};

} // namespace gow::disc
