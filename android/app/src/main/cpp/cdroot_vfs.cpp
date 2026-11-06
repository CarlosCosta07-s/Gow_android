#include "cdroot_vfs.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <string>
#include <utility>

#include <sys/stat.h>
#include <unistd.h>

namespace gow::disc {
namespace {

constexpr size_t kMaxPathLength = 1024u;
constexpr size_t kMaxPathComponentLength = 255u;
constexpr size_t kMaxPathDepth = 32u;
constexpr size_t kMaxDirectoryEntries = 65536u;

bool isSafeComponent(std::string_view component) {
    if (component.empty() || component == "." || component == ".." ||
        component.size() > kMaxPathComponentLength) {
        return false;
    }
    for (const unsigned char ch : component) {
        if (ch < 0x20u || ch == 0x7Fu || ch == ':' ||
            ch == '/' || ch == '\\' || ch == 0u) {
            return false;
        }
    }
    return true;
}

} // namespace

CdRootVfs::~CdRootVfs() {
    close();
}

void CdRootVfs::setError(std::string *error, std::string_view message) {
    if (error != nullptr) {
        *error = std::string(message);
    }
}

bool CdRootVfs::splitRelativePath(
    std::string_view path, std::vector<std::string> &parts, bool allowRoot) {
    parts.clear();
    if (path.empty()) {
        return allowRoot;
    }
    if (path.size() > kMaxPathLength || path.front() == '/' ||
        path.back() == '/') {
        return false;
    }

    size_t cursor = 0u;
    while (cursor < path.size()) {
        const size_t end = path.find('/', cursor);
        const size_t componentEnd =
            end == std::string_view::npos ? path.size() : end;
        const std::string_view component =
            path.substr(cursor, componentEnd - cursor);
        if (!isSafeComponent(component) || parts.size() >= kMaxPathDepth) {
            parts.clear();
            return false;
        }
        parts.emplace_back(component);
        if (end == std::string_view::npos) {
            break;
        }
        cursor = end + 1u;
    }
    return !parts.empty();
}

bool CdRootVfs::openRoot(std::string_view path, std::string *error) {
    if (path.empty() || path.size() > kMaxPathLength ||
        path.find('\0') != std::string_view::npos) {
        setError(error, "The private cdRoot path is empty or invalid.");
        return false;
    }

    const std::string nativePath(path);
    const int candidate = ::open(
        nativePath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (candidate < 0) {
        setError(error, "Could not open the private cdRoot directory.");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &file : openFiles_) {
        ::close(file.second);
    }
    openFiles_.clear();
    fileSizes_.clear();
    if (rootFd_ >= 0) {
        ::close(rootFd_);
    }
    rootFd_ = candidate;
    return true;
}

void CdRootVfs::close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &file : openFiles_) {
        ::close(file.second);
    }
    openFiles_.clear();
    fileSizes_.clear();
    if (rootFd_ >= 0) {
        ::close(rootFd_);
        rootFd_ = -1;
    }
}

bool CdRootVfs::isOpen() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return rootFd_ >= 0;
}

int CdRootVfs::openDirectory(const std::vector<std::string> &parts) const {
    if (rootFd_ < 0) {
        return -1;
    }
    // dup(rootFd_) would share the directory stream offset with the
    // persistent root descriptor. Open "." instead to obtain an independent
    // open-file description for every traversal/listing operation.
    int current = ::openat(rootFd_, ".",
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current < 0) {
        return -1;
    }

    for (const auto &component : parts) {
        const int next = ::openat(
            current, component.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        ::close(current);
        if (next < 0) {
            return -1;
        }
        current = next;
    }
    return current;
}

bool CdRootVfs::listDirectory(
    std::string_view path, std::vector<CdRootEntry> &entries,
    std::string *error) {
    entries.clear();
    std::vector<std::string> parts;
    if (!splitRelativePath(path, parts, true)) {
        setError(error, "The cdRoot directory path is unsafe.");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (rootFd_ < 0) {
        setError(error, "No private cdRoot directory is open.");
        return false;
    }

    const int directoryFd = openDirectory(parts);
    if (directoryFd < 0) {
        setError(error, "The requested cdRoot directory could not be opened.");
        return false;
    }
    const int iterationFd = ::dup(directoryFd);
    if (iterationFd < 0) {
        ::close(directoryFd);
        setError(error, "Could not duplicate the cdRoot directory descriptor.");
        return false;
    }

    DIR *directory = ::fdopendir(iterationFd);
    if (directory == nullptr) {
        ::close(iterationFd);
        ::close(directoryFd);
        setError(error, "Could not enumerate the cdRoot directory.");
        return false;
    }

    std::vector<CdRootEntry> parsed;
    bool success = true;
    while (true) {
        errno = 0;
        dirent *entry = ::readdir(directory);
        if (entry == nullptr) {
            if (errno != 0) success = false;
            break;
        }
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (!isSafeComponent(entry->d_name) ||
            parsed.size() >= kMaxDirectoryEntries) {
            setError(error, "The cdRoot directory contains an unsafe name or too many entries.");
            success = false;
            break;
        }

        struct stat status {};
        if (::fstatat(directoryFd, entry->d_name, &status,
                      AT_SYMLINK_NOFOLLOW) != 0) {
            success = false;
            break;
        }

        CdRootEntry item;
        item.name = entry->d_name;
        if (S_ISDIR(status.st_mode)) {
            item.isDirectory = true;
            item.size = 0u;
        } else if (S_ISREG(status.st_mode) && status.st_size >= 0) {
            item.isDirectory = false;
            item.size = static_cast<uint64_t>(status.st_size);
        } else {
            setError(error, "The cdRoot contains an unsupported filesystem entry.");
            success = false;
            break;
        }
        parsed.push_back(std::move(item));
    }

    ::closedir(directory);
    ::close(directoryFd);
    if (!success) {
        if (error != nullptr && error->empty()) {
            setError(error, "Could not read the cdRoot directory entries.");
        }
        return false;
    }

    std::sort(parsed.begin(), parsed.end(),
        [](const CdRootEntry &left, const CdRootEntry &right) {
            return left.name < right.name;
        });
    entries.swap(parsed);
    return true;
}

bool CdRootVfs::openFile(
    std::string_view path, CdRootFile &file, std::string *error) {
    file = {};
    std::vector<std::string> parts;
    if (!splitRelativePath(path, parts, false)) {
        setError(error, "The cdRoot file path is unsafe.");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (rootFd_ < 0) {
        setError(error, "No private cdRoot directory is open.");
        return false;
    }

    std::vector<std::string> parentParts(parts.begin(), parts.end() - 1);
    const int parentFd = openDirectory(parentParts);
    if (parentFd < 0) {
        setError(error, "The cdRoot file parent directory could not be opened.");
        return false;
    }

    const int descriptor = ::openat(
        parentFd, parts.back().c_str(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    ::close(parentFd);
    if (descriptor < 0) {
        setError(error, "The requested cdRoot file could not be opened.");
        return false;
    }

    struct stat status {};
    if (::fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size < 0) {
        ::close(descriptor);
        setError(error, "The requested cdRoot entry is not a regular file.");
        return false;
    }

    uint64_t handle = nextHandle_++;
    while (handle == 0u || openFiles_.find(handle) != openFiles_.end()) {
        handle = nextHandle_++;
    }
    openFiles_.emplace(handle, descriptor);
    const uint64_t size = static_cast<uint64_t>(status.st_size);
    fileSizes_.emplace(handle, size);
    file.handle = handle;
    file.size = size;
    return true;
}

bool CdRootVfs::readFile(
    uint64_t handle, uint64_t offset, void *destination,
    size_t requested, size_t &bytesRead) const {
    bytesRead = 0u;
    if (handle == 0u || (!destination && requested != 0u)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto descriptorIt = openFiles_.find(handle);
    const auto sizeIt = fileSizes_.find(handle);
    if (descriptorIt == openFiles_.end() || sizeIt == fileSizes_.end() ||
        offset > sizeIt->second ||
        offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
        return false;
    }
    if (requested == 0u || offset == sizeIt->second) {
        return true;
    }

    const uint64_t available = sizeIt->second - offset;
    const size_t target = static_cast<size_t>(
        std::min<uint64_t>(available, static_cast<uint64_t>(requested)));
    auto *bytes = static_cast<uint8_t *>(destination);
    while (bytesRead < target) {
        const uint64_t position = offset + static_cast<uint64_t>(bytesRead);
        if (position > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
            return false;
        }
        const ssize_t count = ::pread(
            descriptorIt->second, bytes + bytesRead,
            target - bytesRead, static_cast<off_t>(position));
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (count == 0) break;
        bytesRead += static_cast<size_t>(count);
    }
    return true;
}

void CdRootVfs::closeFile(uint64_t handle) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = openFiles_.find(handle);
    if (found == openFiles_.end()) return;
    ::close(found->second);
    openFiles_.erase(found);
    fileSizes_.erase(handle);
}

} // namespace gow::disc
