#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gow::disc {

struct CdRootEntry {
    std::string name;
    uint64_t size = 0u;
    bool isDirectory = false;
};

struct CdRootFile {
    uint64_t handle = 0u;
    uint64_t size = 0u;
};

// Read-only VFS rooted at an application-private installed disc directory.
// Guest path matching follows ISO9660 conventions: ASCII case-insensitive and
// optional ;version suffixes are ignored. Ambiguous normalized names are rejected.
// All paths are relative to the root and are resolved with openat/O_NOFOLLOW.
class CdRootVfs {
public:
    CdRootVfs() = default;
    ~CdRootVfs();

    CdRootVfs(const CdRootVfs &) = delete;
    CdRootVfs &operator=(const CdRootVfs &) = delete;

    bool openRoot(std::string_view path, std::string *error = nullptr);
    void close() noexcept;
    bool isOpen() const noexcept;

    bool listDirectory(std::string_view path,
                       std::vector<CdRootEntry> &entries,
                       std::string *error = nullptr);

    bool openFile(std::string_view path, CdRootFile &file,
                  std::string *error = nullptr);
    bool readFile(uint64_t handle, uint64_t offset, void *destination,
                  size_t requested, size_t &bytesRead) const;
    void closeFile(uint64_t handle) noexcept;

private:
    static bool splitRelativePath(std::string_view path,
                                  std::vector<std::string> &parts,
                                  bool allowRoot);
    static void setError(std::string *error, std::string_view message);
    int openDirectory(const std::vector<std::string> &parts) const;

    mutable std::mutex mutex_;
    int rootFd_ = -1;
    uint64_t nextHandle_ = 1u;
    std::unordered_map<uint64_t, int> openFiles_;
    std::unordered_map<uint64_t, uint64_t> fileSizes_;
};

} // namespace gow::disc
