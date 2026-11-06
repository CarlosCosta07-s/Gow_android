#include "iso9660_installer.h"
#include "iso9660_disc_layout.h"
#include "sha256.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace gow::disc {
namespace {

constexpr size_t kCopyBufferSize = 4u * 1024u * 1024u;
constexpr uint64_t kCopyProgressInterval = 4u * 1024u * 1024u;
constexpr size_t kMaxManifestFiles = 65536u;
constexpr size_t kMaxRelativePathLength = 1024u;
constexpr size_t kMaxPathComponentLength = 255u;
constexpr char kIntegrityManifestName[] = ".gow_disc_integrity";
constexpr char kIntegrityManifestHeader[] = "GOW-DISC-INTEGRITY\t1\n";
constexpr size_t kMaxIntegrityManifestBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxIntegrityEntries = kMaxManifestFiles + 2u;

struct ResolvedInstallFile {
    IsoFile source{};
    std::string destinationPath;
};

bool isSafeNameComponent(const std::string &component) {
    if (component.empty() || component == "." || component == ".." ||
        component.size() > kMaxPathComponentLength) {
        return false;
    }
    for (const unsigned char ch : component) {
        if (ch == 0u || ch < 0x20u || ch == ':' || ch == '/' || ch == '\\') return false;
    }
    return true;
}

bool splitRelativePath(const std::string &path, std::vector<std::string> &parts) {
    parts.clear();
    if (path.empty() || path.size() > kMaxRelativePathLength ||
        path.front() == '/' || path.back() == '/') {
        return false;
    }

    size_t cursor = 0u;
    while (cursor < path.size()) {
        const size_t end = path.find('/', cursor);
        const size_t componentEnd = end == std::string::npos ? path.size() : end;
        const std::string component = path.substr(cursor, componentEnd - cursor);
        if (!isSafeNameComponent(component) || parts.size() >= 32u) return false;
        parts.push_back(component);
        if (end == std::string::npos) break;
        cursor = end + 1u;
    }
    return !parts.empty();
}

std::string foldedPath(const std::vector<std::string> &parts) {
    std::string result;
    for (const auto &part : parts) {
        if (!result.empty()) result.push_back('/');
        for (const unsigned char ch : part) {
            result.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    return result;
}

bool validateManifest(const Iso9660Reader &reader,
                      const std::vector<IsoInstallFile> &manifest,
                      std::vector<ResolvedInstallFile> &resolved,
                      uint64_t &totalBytes, std::string &error) {
    resolved.clear();
    totalBytes = 0u;
    if (!reader.isOpen()) {
        error = "No ISO image is open.";
        return false;
    }
    if (manifest.empty() || manifest.size() > kMaxManifestFiles) {
        error = "The installation manifest is empty or exceeds its file-count limit.";
        return false;
    }

    std::unordered_set<std::string> destinations;
    destinations.reserve(manifest.size());

    for (const auto &entry : manifest) {
        std::vector<std::string> sourceParts;
        std::vector<std::string> destinationParts;
        if (!splitRelativePath(entry.sourcePath, sourceParts)) {
            error = "The manifest contains an unsafe ISO source path.";
            return false;
        }
        if (!splitRelativePath(entry.destinationPath, destinationParts)) {
            error = "The manifest contains an unsafe destination path.";
            return false;
        }

        const std::string key = foldedPath(destinationParts);
        if (!destinations.emplace(key).second) {
            error = "The manifest contains duplicate destination paths.";
            return false;
        }

        IsoFile source{};
        std::string lookupError;
        if (!reader.lookupFile(entry.sourcePath, source, &lookupError)) {
            error = "Could not resolve ISO file '" + entry.sourcePath + "': " +
                    (lookupError.empty() ? "file not found." : lookupError);
            return false;
        }
        if (entry.expectedSize != kUnknownExpectedFileSize &&
            entry.expectedSize != source.size) {
            error = "The ISO file size does not match the installation manifest: " +
                    entry.sourcePath;
            return false;
        }
        if (source.size > std::numeric_limits<uint64_t>::max() - totalBytes) {
            error = "The total installation size overflows the supported range.";
            return false;
        }

        totalBytes += source.size;
        resolved.push_back({source, entry.destinationPath});
    }

    // Reject file/directory prefix collisions before touching the filesystem.
    for (const auto &key : destinations) {
        size_t slash = key.find('/');
        while (slash != std::string::npos) {
            if (destinations.find(key.substr(0u, slash)) != destinations.end()) {
                error = "The manifest uses a file path as a parent directory.";
                return false;
            }
            slash = key.find('/', slash + 1u);
        }
    }
    return true;
}

bool writeAll(int fd, const uint8_t *bytes, size_t size) {
    size_t written = 0u;
    while (written < size) {
        const ssize_t count = ::write(fd, bytes + written, size - written);
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (count == 0) return false;
        written += static_cast<size_t>(count);
    }
    return true;
}

struct IntegrityEntry {
    std::string path;
    uint64_t size = 0u;
    std::string sha256;
};

bool isReservedDiscPath(const std::string &path) {
    std::string folded;
    folded.reserve(path.size());
    for (const unsigned char ch : path)
        folded.push_back(static_cast<char>(std::toupper(ch)));
    return folded == ".DISC_LAYOUT" || folded == ".DISC_META" ||
           folded == ".GOW_DISC_INTEGRITY";
}

uint64_t estimatedIntegrityManifestBytes(
    const std::vector<ResolvedInstallFile> &files,
    uint64_t layoutBytes, uint64_t metadataBytes) {
    uint64_t total = sizeof(kIntegrityManifestHeader) - 1u;
    auto add = [&total](uint64_t value) {
        if (value > std::numeric_limits<uint64_t>::max() - total) return false;
        total += value;
        return true;
    };
    for (const auto &file : files) {
        if (file.destinationPath.size() < 5u) return std::numeric_limits<uint64_t>::max();
        const size_t pathLength = file.destinationPath.size() - 5u;
        const size_t digits = std::to_string(file.source.size).size();
        if (!add(67u + static_cast<uint64_t>(digits) + pathLength))
            return std::numeric_limits<uint64_t>::max();
    }
    if (!add(67u + std::to_string(layoutBytes).size() + std::strlen(".disc_layout")) ||
        !add(67u + std::to_string(metadataBytes).size() + std::strlen(".disc_meta")))
        return std::numeric_limits<uint64_t>::max();
    return total;
}

int openRegularRelativeAt(int rootFd, const std::string &relativePath) {
    std::vector<std::string> parts;
    if (!splitRelativePath(relativePath, parts)) return -1;
    int currentFd = ::dup(rootFd);
    if (currentFd < 0) return -1;
    for (size_t i = 0u; i + 1u < parts.size(); ++i) {
        const int nextFd = ::openat(
            currentFd, parts[i].c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        ::close(currentFd);
        if (nextFd < 0) return -1;
        currentFd = nextFd;
    }
    const int fileFd = ::openat(
        currentFd, parts.back().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ::close(currentFd);
    if (fileFd < 0) return -1;
    struct stat status {};
    if (::fstat(fileFd, &status) != 0 || !S_ISREG(status.st_mode)) {
        ::close(fileFd);
        return -1;
    }
    return fileFd;
}

bool hashFileDescriptor(
    int fd, uint64_t expectedSize, std::string &digest, std::string &error,
    IsoDiscIntegrityProgress &progress,
    IsoDiscIntegrityProgressCallback progressCallback, void *userData,
    bool &cancelled) {
    struct stat before {};
    if (::fstat(fd, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_size < 0 ||
        static_cast<uint64_t>(before.st_size) != expectedSize) {
        error = "The file is not regular or its size differs from the manifest.";
        return false;
    }
    Sha256 hash;
    std::vector<uint8_t> buffer(kCopyBufferSize);
    uint64_t totalRead = 0u;
    uint64_t lastNotification = progress.verifiedBytes;
    constexpr uint64_t kProgressInterval = 16u * 1024u * 1024u;
    while (true) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR) continue;
            error = "A file could not be read during SHA-256 verification.";
            return false;
        }
        if (count == 0) break;
        const size_t bytes = static_cast<size_t>(count);
        const uint64_t byteCount = static_cast<uint64_t>(bytes);
        if (totalRead > expectedSize || byteCount > expectedSize - totalRead ||
            progress.verifiedBytes > progress.totalBytes ||
            byteCount > progress.totalBytes - progress.verifiedBytes) {
            error = "A file grew or exceeded the integrity verification plan.";
            return false;
        }
        hash.update(buffer.data(), bytes);
        totalRead += byteCount;
        progress.verifiedBytes += byteCount;

        const bool intervalReached =
            progress.verifiedBytes - lastNotification >= kProgressInterval;
        const bool finalChunk = progress.verifiedBytes == progress.totalBytes;
        if (progressCallback != nullptr && (intervalReached || finalChunk)) {
            if (!progressCallback(progress, userData)) {
                cancelled = true;
                error = "The SHA-256 integrity verification was cancelled.";
                return false;
            }
            lastNotification = progress.verifiedBytes;
        }
    }
    if (totalRead != expectedSize) {
        error = "A file changed size while its SHA-256 hash was calculated.";
        return false;
    }
    struct stat after {};
    if (::fstat(fd, &after) != 0 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtime != after.st_mtime) {
        error = "A file changed during SHA-256 verification.";
        return false;
    }
    digest = sha256Hex(hash.finish());
    return true;
}

bool hashRelativeFileAt(int rootFd, const std::string &relativePath,
                        uint64_t &size, std::string &digest,
                        std::string &error) {
    const int fd = openRegularRelativeAt(rootFd, relativePath);
    if (fd < 0) {
        error = "Could not safely open integrity file: " + relativePath;
        return false;
    }
    struct stat status {};
    if (::fstat(fd, &status) != 0 || status.st_size < 0) {
        ::close(fd);
        error = "Could not inspect integrity file: " + relativePath;
        return false;
    }
    size = static_cast<uint64_t>(status.st_size);
    IsoDiscIntegrityProgress progress;
    progress.totalBytes = size;
    progress.totalFiles = 1u;
    bool cancelled = false;
    const bool success = hashFileDescriptor(
        fd, size, digest, error, progress, nullptr, nullptr, cancelled);
    ::close(fd);
    if (!success) error = relativePath + ": " + error;
    return success;
}

bool writeDiscIntegrityManifest(
    int stageFd, std::vector<IntegrityEntry> &entries,
    uint64_t &manifestBytes, std::string &error) {
    const int discFd = ::openat(
        stageFd, "disc", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (discFd < 0) {
        error = "Could not open the extracted disc directory for integrity metadata.";
        return false;
    }
    for (const char *metadataPath : {".disc_layout", ".disc_meta"}) {
        IntegrityEntry entry;
        entry.path = metadataPath;
        if (!hashRelativeFileAt(discFd, entry.path, entry.size, entry.sha256, error)) {
            ::close(discFd);
            return false;
        }
        entries.push_back(std::move(entry));
    }

    const int manifestFd = ::openat(
        discFd, kIntegrityManifestName,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (manifestFd < 0) {
        ::close(discFd);
        error = "Could not create the private SHA-256 integrity manifest.";
        return false;
    }
    uint64_t writtenBytes = 0u;
    auto writeText = [&](const std::string &text) {
        if (!writeAll(manifestFd,
                      reinterpret_cast<const uint8_t *>(text.data()), text.size()))
            return false;
        writtenBytes += static_cast<uint64_t>(text.size());
        return true;
    };
    bool success = writeText(kIntegrityManifestHeader);
    for (const auto &entry : entries) {
        if (!success) break;
        const std::string line = entry.sha256 + "\t" +
            std::to_string(entry.size) + "\t" + entry.path + "\n";
        success = writeText(line);
    }
    if (success && ::fsync(manifestFd) != 0) success = false;
    if (::close(manifestFd) != 0) success = false;
    if (success && ::fsync(discFd) != 0) success = false;
    ::close(discFd);
    if (!success) {
        error = "Could not write or synchronize the SHA-256 integrity manifest.";
        return false;
    }
    manifestBytes = writtenBytes;
    return true;
}

bool removeDirectoryContentsAt(int parentFd, const char *name);

bool removeDirectoryFdContents(int directoryFd) {
    const int iterationFd = ::dup(directoryFd);
    if (iterationFd < 0) return false;
    DIR *directory = ::fdopendir(iterationFd);
    if (!directory) {
        ::close(iterationFd);
        return false;
    }

    bool success = true;
    while (true) {
        errno = 0;
        dirent *entry = ::readdir(directory);
        if (!entry) {
            if (errno != 0) success = false;
            break;
        }
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        struct stat status {};
        if (::fstatat(directoryFd, entry->d_name, &status,
                      AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT) continue;
            success = false;
            break;
        }

        if (S_ISDIR(status.st_mode)) {
            if (!removeDirectoryContentsAt(directoryFd, entry->d_name)) {
                success = false;
                break;
            }
        } else if (::unlinkat(directoryFd, entry->d_name, 0) != 0 &&
                   errno != ENOENT) {
            success = false;
            break;
        }
    }
    ::closedir(directory);
    return success;
}

bool removeDirectoryContentsAt(int parentFd, const char *name) {
    const int directoryFd = ::openat(parentFd, name,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directoryFd < 0) return errno == ENOENT;
    const bool cleared = removeDirectoryFdContents(directoryFd);
    ::close(directoryFd);
    if (!cleared) return false;
    return ::unlinkat(parentFd, name, AT_REMOVEDIR) == 0 || errno == ENOENT;
}

bool ensureDirectoryAt(int parentFd, const std::string &name, int &directoryFd) {
    const bool created = ::mkdirat(parentFd, name.c_str(), 0700) == 0;
    if (!created && errno != EEXIST) {
        return false;
    }
    if (created && ::fsync(parentFd) != 0) {
        return false;
    }
    directoryFd = ::openat(parentFd, name.c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    return directoryFd >= 0;
}

bool openDestinationParent(int stageFd,
                           const std::vector<std::string> &parts,
                           int &parentFd,
                           std::string &leaf) {
    parentFd = ::dup(stageFd);
    if (parentFd < 0) return false;

    for (size_t i = 0u; i + 1u < parts.size(); ++i) {
        int childFd = -1;
        if (!ensureDirectoryAt(parentFd, parts[i], childFd)) {
            ::close(parentFd);
            parentFd = -1;
            return false;
        }
        ::close(parentFd);
        parentFd = childFd;
    }
    leaf = parts.back();
    return true;
}

int openDirectoryRelativeAt(int rootFd, const std::string &relativePath) {
    std::vector<std::string> parts;
    if (!splitRelativePath(relativePath, parts)) return -1;

    int currentFd = ::dup(rootFd);
    if (currentFd < 0) return -1;
    for (const std::string &part : parts) {
        const int nextFd = ::openat(
            currentFd, part.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        ::close(currentFd);
        if (nextFd < 0) return -1;
        currentFd = nextFd;
    }
    return currentFd;
}

void notifyProgress(IsoInstallResult &result,
                    const IsoInstallProgressCallback callback,
                    void *userData,
                    bool &keepGoing) {
    if (!callback || !keepGoing) return;
    const IsoInstallProgress progress{
        result.totalBytes, result.copiedBytes,
        result.completedFiles, result.totalFiles};
    keepGoing = callback(progress, userData);
    if (!keepGoing) {
        result.cancelled = true;
        result.error = "Installation was cancelled.";
    }
}

struct MetadataProgressContext {
    IsoInstallResult *result = nullptr;
    const IsoDiscMetadataInfo *metadata = nullptr;
    IsoInstallProgressCallback callback = nullptr;
    void *userData = nullptr;
};

bool forwardMetadataMeasureProgress(
    const IsoDiscMetadataProgress &progress, void *opaque) {
    auto *context = static_cast<MetadataProgressContext *>(opaque);
    if (context == nullptr || context->callback == nullptr) return true;
    const IsoInstallProgress installProgress{
        progress.totalSectors, progress.completedSectors, 0u, 0u};
    const bool keepGoing = context->callback(installProgress, context->userData);
    if (!keepGoing && context->result != nullptr) {
        context->result->cancelled = true;
        context->result->error = "Installation was cancelled during disc analysis.";
    }
    return keepGoing;
}

bool forwardMetadataWriteProgress(
    const IsoDiscMetadataProgress &progress, void *opaque) {
    auto *context = static_cast<MetadataProgressContext *>(opaque);
    if (context == nullptr || context->callback == nullptr ||
        context->result == nullptr || context->metadata == nullptr) {
        return true;
    }

    uint64_t metadataBytesWritten = 0u;
    if (progress.totalSectors != 0u) {
        const long double ratio =
            static_cast<long double>(progress.completedSectors) /
            static_cast<long double>(progress.totalSectors);
        metadataBytesWritten = static_cast<uint64_t>(
            static_cast<long double>(context->metadata->metadataFileBytes) * ratio);
    }
    const uint64_t generatedBytes =
        context->metadata->layoutFileBytes + metadataBytesWritten;
    const uint64_t copiedBytes = std::min(
        context->result->totalBytes,
        context->result->copiedBytes + generatedBytes);
    const IsoInstallProgress installProgress{
        context->result->totalBytes,
        copiedBytes,
        context->result->completedFiles,
        context->result->totalFiles};
    const bool keepGoing = context->callback(installProgress, context->userData);
    if (!keepGoing) {
        context->result->cancelled = true;
        context->result->error = "Installation was cancelled during disc metadata generation.";
    }
    return keepGoing;
}

IsoInstallResult installIsoManifestImpl(
    const Iso9660Reader &reader,
    const std::vector<IsoInstallFile> &manifest,
    const std::string &privateRoot,
    const std::string &finalDirectoryName,
    const IsoDiscLayout *discLayout,
    const IsoDiscMetadataInfo *metadataInfo,
    IsoInstallProgressCallback progressCallback,
    void *userData) {
    IsoInstallResult result;
    result.totalFiles = manifest.size();

    if (!isSafeNameComponent(finalDirectoryName) ||
        finalDirectoryName.size() > 240u) {
        result.error = "The installation directory name is unsafe or too long.";
        return result;
    }
    if (privateRoot.empty()) {
        result.error = "The private application directory is empty.";
        return result;
    }

    std::vector<ResolvedInstallFile> resolved;
    if (!validateManifest(reader, manifest, resolved,
                          result.totalBytes, result.error)) {
        return result;
    }
    // Read the source ISO in physical-sector order to reduce seek churn on
    // file-backed document providers and slow removable storage.
    std::stable_sort(
        resolved.begin(), resolved.end(),
        [](const ResolvedInstallFile &left, const ResolvedInstallFile &right) {
            return left.source.byteOffset < right.source.byteOffset;
        });
    const uint64_t extractedBytes = result.totalBytes;
    uint64_t integrityManifestExpectedBytes = 0u;
    if ((discLayout == nullptr) != (metadataInfo == nullptr)) {
        result.error = "The disc layout and measured metadata must be supplied together.";
        return result;
    }
    if (discLayout != nullptr) {
        if (discLayout->files.size() != manifest.size() ||
            metadataInfo->layoutFileBytes >
                std::numeric_limits<uint64_t>::max() - result.totalBytes) {
            result.error = "The disc metadata plan is inconsistent or too large.";
            return result;
        }
        result.totalBytes += metadataInfo->layoutFileBytes;
        if (metadataInfo->metadataFileBytes >
            std::numeric_limits<uint64_t>::max() - result.totalBytes) {
            result.error = "The complete disc installation size overflows the supported range.";
            return result;
        }
        result.totalBytes += metadataInfo->metadataFileBytes;
        integrityManifestExpectedBytes =
            estimatedIntegrityManifestBytes(
                resolved, metadataInfo->layoutFileBytes,
                metadataInfo->metadataFileBytes);
        if (integrityManifestExpectedBytes ==
                std::numeric_limits<uint64_t>::max() ||
            integrityManifestExpectedBytes >
                std::numeric_limits<uint64_t>::max() - result.totalBytes) {
            result.error = "The SHA-256 integrity manifest size overflows the supported range.";
            return result;
        }
        result.totalBytes += integrityManifestExpectedBytes;
    }

    const int rootFd = ::open(privateRoot.c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (rootFd < 0) {
        result.error = "Could not open the private application directory.";
        return result;
    }

    struct statvfs storageInfo {};
    if (::fstatvfs(rootFd, &storageInfo) != 0 ||
        storageInfo.f_frsize == 0u) {
        ::close(rootFd);
        result.error = "Could not query available private storage.";
        return result;
    }
    const uint64_t availableBlocks =
        static_cast<uint64_t>(storageInfo.f_bavail);
    const uint64_t blockSize =
        static_cast<uint64_t>(storageInfo.f_frsize);
    const uint64_t availableBytes =
        availableBlocks > std::numeric_limits<uint64_t>::max() / blockSize
            ? std::numeric_limits<uint64_t>::max()
            : availableBlocks * blockSize;
    if (result.totalBytes > availableBytes) {
        ::close(rootFd);
        result.error = "Insufficient private storage for the selected ISO files.";
        return result;
    }

    const std::string stageName = "." + finalDirectoryName + ".staging";
    struct stat finalStatus {};
    if (::fstatat(rootFd, finalDirectoryName.c_str(), &finalStatus,
                  AT_SYMLINK_NOFOLLOW) == 0) {
        ::close(rootFd);
        result.error = "The requested installation directory already exists.";
        return result;
    }
    if (errno != ENOENT) {
        ::close(rootFd);
        result.error = "Could not inspect the requested installation directory.";
        return result;
    }

    if (!removeDirectoryContentsAt(rootFd, stageName.c_str())) {
        ::close(rootFd);
        result.error = "Could not remove an incomplete previous staging directory.";
        return result;
    }
    if (::mkdirat(rootFd, stageName.c_str(), 0700) != 0) {
        ::close(rootFd);
        result.error = "Could not create the private staging directory.";
        return result;
    }

    const int stageFd = ::openat(rootFd, stageName.c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (stageFd < 0) {
        removeDirectoryContentsAt(rootFd, stageName.c_str());
        ::close(rootFd);
        result.error = "Could not open the private staging directory.";
        return result;
    }

    bool keepGoing = true;
    notifyProgress(result, progressCallback, userData, keepGoing);
    std::vector<uint8_t> buffer(kCopyBufferSize);
    uint64_t lastCopyNotification = result.copiedBytes;
    bool copySucceeded = keepGoing;
    std::vector<IntegrityEntry> integrityEntries;
    if (discLayout != nullptr) integrityEntries.reserve(resolved.size() + 2u);
    std::unordered_set<std::string> touchedDirectories;
    touchedDirectories.reserve(std::min<size_t>(resolved.size(), 4096u));

    for (const auto &entry : resolved) {
        if (!copySucceeded) break;

        std::vector<std::string> destinationParts;
        if (!splitRelativePath(entry.destinationPath, destinationParts)) {
            result.error = "The manifest destination path became invalid.";
            copySucceeded = false;
            break;
        }

        int parentFd = -1;
        std::string leaf;
        if (!openDestinationParent(stageFd, destinationParts, parentFd, leaf)) {
            result.error = "Could not create a destination directory in staging.";
            copySucceeded = false;
            break;
        }

        const int outputFd = ::openat(parentFd, leaf.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (outputFd < 0) {
            ::close(parentFd);
            result.error = "Could not create an extracted file in staging.";
            copySucceeded = false;
            break;
        }

        uint64_t fileOffset = 0u;
        bool fileSucceeded = true;
        Sha256 fileHash;
        while (fileOffset < entry.source.size) {
            const uint64_t remaining = entry.source.size - fileOffset;
            const size_t request = static_cast<size_t>(
                std::min<uint64_t>(remaining, buffer.size()));
            size_t bytesRead = 0u;
            if (!reader.readFile(entry.source, fileOffset,
                                 buffer.data(), request, bytesRead) ||
                bytesRead == 0u || bytesRead > request ||
                !writeAll(outputFd, buffer.data(), bytesRead)) {
                fileSucceeded = false;
                result.error = "An ISO read or private file write failed.";
                break;
            }

            if (discLayout != nullptr)
                fileHash.update(buffer.data(), bytesRead);
            fileOffset += static_cast<uint64_t>(bytesRead);
            result.copiedBytes += static_cast<uint64_t>(bytesRead);
            const bool fileComplete = fileOffset == entry.source.size;
            if (result.copiedBytes - lastCopyNotification >=
                    kCopyProgressInterval || fileComplete) {
                // Keep a callback at each file boundary so cancellation and UI
                // progress remain observable even for files smaller than 4 MiB.
                notifyProgress(result, progressCallback, userData, keepGoing);
                lastCopyNotification = result.copiedBytes;
                if (!keepGoing) {
                    fileSucceeded = false;
                    break;
                }
            }
        }

        // fdatasync persists file contents and the size needed to read them.
        // Parent directories are synchronized once per directory below rather
        // than once for every file, avoiding thousands of redundant barriers.
        if (fileSucceeded && ::fdatasync(outputFd) != 0) {
            fileSucceeded = false;
            result.error = "Could not flush extracted file data to private storage.";
        }
        if (::close(outputFd) != 0 && fileSucceeded) {
            fileSucceeded = false;
            result.error = "Could not close an extracted file cleanly.";
        }
        ::close(parentFd);
        if (!fileSucceeded) {
            copySucceeded = false;
            break;
        }

        const size_t parentSeparator = entry.destinationPath.rfind('/');
        if (parentSeparator != std::string::npos) {
            touchedDirectories.emplace(
                entry.destinationPath.substr(0u, parentSeparator));
        }

        if (discLayout != nullptr) {
            IntegrityEntry integrityEntry;
            integrityEntry.path = entry.destinationPath.substr(5u);
            integrityEntry.size = entry.source.size;
            integrityEntry.sha256 = sha256Hex(fileHash.finish());
            integrityEntries.push_back(std::move(integrityEntry));
        }

        ++result.completedFiles;
        notifyProgress(result, progressCallback, userData, keepGoing);
        if (!keepGoing) {
            copySucceeded = false;
            break;
        }
    }

    if (copySucceeded) {
        for (const std::string &relativeDirectory : touchedDirectories) {
            const int directoryFd =
                openDirectoryRelativeAt(stageFd, relativeDirectory);
            if (directoryFd < 0) {
                copySucceeded = false;
                result.error = "Could not reopen an extracted directory for synchronization.";
                break;
            }
            const bool synchronized = ::fsync(directoryFd) == 0;
            const bool closed = ::close(directoryFd) == 0;
            if (!synchronized || !closed) {
                copySucceeded = false;
                result.error = "Could not synchronize an extracted directory.";
                break;
            }
        }
    }

    if (copySucceeded && result.copiedBytes != extractedBytes) {
        copySucceeded = false;
        result.error = "The extracted file byte count does not match the installation plan.";
    }

    if (copySucceeded && discLayout != nullptr && metadataInfo != nullptr) {
        std::string extractedRoot = privateRoot;
        if (!extractedRoot.empty() && extractedRoot.back() != '/') {
            extractedRoot.push_back('/');
        }
        extractedRoot += stageName + "/disc";

        std::string metadataError;
        MetadataProgressContext metadataProgress{
            &result, metadataInfo, progressCallback, userData};
        if (!writeIsoDiscMetadataMeasured(
                reader, *discLayout, extractedRoot, *metadataInfo,
                nullptr, &metadataError,
                progressCallback != nullptr ? forwardMetadataWriteProgress : nullptr,
                progressCallback != nullptr ? &metadataProgress : nullptr)) {
            copySucceeded = false;
            if (result.error.empty()) {
                result.error = metadataError.empty()
                    ? "Could not create the PS2 disc metadata."
                    : metadataError;
            }
            if (metadataProgress.result != nullptr &&
                metadataProgress.result->cancelled) {
                result.cancelled = true;
            }
        } else {
            result.copiedBytes =
                extractedBytes + metadataInfo->layoutFileBytes +
                metadataInfo->metadataFileBytes;
        }
    }

    if (copySucceeded && discLayout != nullptr) {
        uint64_t manifestBytes = 0u;
        std::string integrityError;
        if (!writeDiscIntegrityManifest(
                stageFd, integrityEntries, manifestBytes, integrityError)) {
            copySucceeded = false;
            result.error = integrityError.empty()
                ? "Could not create the SHA-256 integrity manifest."
                : integrityError;
        } else if (manifestBytes != integrityManifestExpectedBytes ||
                   integrityEntries.size() != resolved.size() + 2u) {
            copySucceeded = false;
            result.error = "The SHA-256 integrity manifest differs from its installation plan.";
        } else {
            result.copiedBytes += manifestBytes;
        }
    }

    if (copySucceeded && result.copiedBytes != result.totalBytes) {
        copySucceeded = false;
        result.error = "The installed byte count does not match the complete installation plan.";
    }

    if (copySucceeded && ::fsync(stageFd) != 0) {
        copySucceeded = false;
        result.error = "Could not flush the private staging directory.";
    }
    // Give the UI one final opportunity to cancel after all file and directory
    // synchronization, but before the staging tree becomes visible as complete.
    if (copySucceeded) {
        notifyProgress(result, progressCallback, userData, keepGoing);
        if (!keepGoing) copySucceeded = false;
    }

    ::close(stageFd);
    if (!copySucceeded) {
        removeDirectoryContentsAt(rootFd, stageName.c_str());
        ::close(rootFd);
        if (result.error.empty()) result.error = "Installation failed before publication.";
        return result;
    }

    if (::renameat(rootFd, stageName.c_str(), rootFd,
                   finalDirectoryName.c_str()) != 0) {
        removeDirectoryContentsAt(rootFd, stageName.c_str());
        ::close(rootFd);
        result.error = "Could not atomically publish the completed installation.";
        return result;
    }
    if (::fsync(rootFd) != 0) {
        // rename already succeeded. Report the install as successful but leave
        // a clear durability warning rather than deleting a published install.
        result.error = "Installation published, but the parent directory could not be synchronized.";
    }
    ::close(rootFd);

    result.success = true;
    return result;
}

} // namespace

bool removeIsoDiscInstallation(
    const std::string &privateRoot,
    const std::string &installationDirectoryName,
    std::string *error) {
    auto setError = [error](const char *message) {
        if (error != nullptr) *error = message;
    };
    if (!isSafeNameComponent(installationDirectoryName)) {
        setError("The installation directory name is unsafe.");
        return false;
    }
    if (privateRoot.empty()) {
        setError("The private application directory is empty.");
        return false;
    }

    const int rootFd = ::open(
        privateRoot.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (rootFd < 0) {
        setError("Could not open the private application directory.");
        return false;
    }

    struct stat status {};
    if (::fstatat(rootFd, installationDirectoryName.c_str(), &status,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        const int savedError = errno;
        ::close(rootFd);
        if (savedError == ENOENT) {
            if (error != nullptr) error->clear();
            return true;
        }
        setError("Could not inspect the requested installation directory.");
        return false;
    }
    if (!S_ISDIR(status.st_mode)) {
        ::close(rootFd);
        setError("The requested installation path is not a real directory.");
        return false;
    }

    if (!removeDirectoryContentsAt(rootFd, installationDirectoryName.c_str())) {
        ::close(rootFd);
        setError("Could not safely remove the private installation directory.");
        return false;
    }
    if (::fsync(rootFd) != 0) {
        ::close(rootFd);
        setError("The installation was removed, but its parent directory could not be synchronized.");
        return false;
    }
    ::close(rootFd);
    if (error != nullptr) error->clear();
    return true;
}

IsoDiscIntegrityResult verifyIsoDiscIntegrity(
    const std::string &discRoot,
    IsoDiscIntegrityProgressCallback progressCallback,
    void *userData) {
    IsoDiscIntegrityResult result;
    if (discRoot.empty()) {
        result.error = "The private disc root is empty.";
        return result;
    }

    const int rootFd = ::open(
        discRoot.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (rootFd < 0) {
        result.error = "Could not safely open the private disc root.";
        return result;
    }

    const int manifestFd = ::openat(
        rootFd, kIntegrityManifestName, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (manifestFd < 0) {
        ::close(rootFd);
        result.error = "The SHA-256 integrity manifest is missing or is not a regular file.";
        return result;
    }
    struct stat manifestStatus {};
    if (::fstat(manifestFd, &manifestStatus) != 0 ||
        !S_ISREG(manifestStatus.st_mode) || manifestStatus.st_size < 0 ||
        static_cast<uint64_t>(manifestStatus.st_size) > kMaxIntegrityManifestBytes) {
        ::close(manifestFd);
        ::close(rootFd);
        result.error = "The SHA-256 integrity manifest has an invalid type or size.";
        return result;
    }

    FILE *manifest = ::fdopen(manifestFd, "r");
    if (manifest == nullptr) {
        ::close(manifestFd);
        ::close(rootFd);
        result.error = "Could not read the SHA-256 integrity manifest.";
        return result;
    }

    std::array<char, 1200> lineBuffer{};
    if (::fgets(lineBuffer.data(), static_cast<int>(lineBuffer.size()), manifest) == nullptr ||
        std::string(lineBuffer.data()) != kIntegrityManifestHeader) {
        std::fclose(manifest);
        ::close(rootFd);
        result.error = "The SHA-256 integrity manifest header or version is unsupported.";
        return result;
    }

    std::vector<IntegrityEntry> entries;
    entries.reserve(1024u);
    std::unordered_set<std::string> seenPaths;
    seenPaths.reserve(1024u);
    bool sawLayout = false;
    bool sawMetadata = false;
    bool parseSucceeded = true;
    while (::fgets(lineBuffer.data(), static_cast<int>(lineBuffer.size()), manifest) != nullptr) {
        const size_t lineLength = std::strlen(lineBuffer.data());
        if (lineLength < 68u || lineLength >= lineBuffer.size() - 1u ||
            lineBuffer[lineLength - 1u] != '\n') {
            result.error = "The SHA-256 integrity manifest contains a malformed or overlong entry.";
            parseSucceeded = false;
            break;
        }

        std::string line(lineBuffer.data(), lineLength - 1u);
        const size_t firstTab = line.find('\t');
        const size_t secondTab = firstTab == std::string::npos
            ? std::string::npos : line.find('\t', firstTab + 1u);
        if (firstTab != 64u || secondTab == std::string::npos ||
            secondTab == firstTab + 1u || secondTab + 1u >= line.size()) {
            result.error = "The SHA-256 integrity manifest entry fields are invalid.";
            parseSucceeded = false;
            break;
        }

        const std::string expectedDigest = line.substr(0u, firstTab);
        for (const unsigned char ch : expectedDigest) {
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
                result.error = "The SHA-256 integrity manifest contains an invalid digest.";
                parseSucceeded = false;
                break;
            }
        }
        if (!parseSucceeded) break;

        const std::string sizeText =
            line.substr(firstTab + 1u, secondTab - firstTab - 1u);
        uint64_t expectedSize = 0u;
        const auto parsed = std::from_chars(
            sizeText.data(), sizeText.data() + sizeText.size(), expectedSize);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != sizeText.data() + sizeText.size()) {
            result.error = "The SHA-256 integrity manifest contains an invalid file size.";
            parseSucceeded = false;
            break;
        }

        const std::string relativePath = line.substr(secondTab + 1u);
        if (relativePath == kIntegrityManifestName) {
            result.error = "The SHA-256 integrity manifest cannot list itself.";
            parseSucceeded = false;
            break;
        }
        std::vector<std::string> pathParts;
        if (!splitRelativePath(relativePath, pathParts)) {
            result.error = "The SHA-256 integrity manifest contains an unsafe file path.";
            parseSucceeded = false;
            break;
        }
        const std::string pathKey = foldedPath(pathParts);
        if (!seenPaths.emplace(pathKey).second) {
            result.error = "The SHA-256 integrity manifest contains a duplicate file path.";
            parseSucceeded = false;
            break;
        }
        if (pathParts.size() == 1u && pathParts[0] == ".disc_layout")
            sawLayout = true;
        if (pathParts.size() == 1u && pathParts[0] == ".disc_meta")
            sawMetadata = true;
        if (entries.size() >= kMaxIntegrityEntries) {
            result.error = "The SHA-256 integrity manifest exceeds its file-count limit.";
            parseSucceeded = false;
            break;
        }
        if (expectedSize >
            std::numeric_limits<uint64_t>::max() - result.totalBytes) {
            result.error = "The SHA-256 integrity manifest total size overflows the supported range.";
            parseSucceeded = false;
            break;
        }
        result.totalBytes += expectedSize;
        entries.push_back({relativePath, expectedSize, expectedDigest});
    }

    if (parseSucceeded && std::ferror(manifest) != 0) {
        result.error = "An I/O error occurred while reading the SHA-256 integrity manifest.";
        parseSucceeded = false;
    }
    std::fclose(manifest);

    if (!parseSucceeded) {
        ::close(rootFd);
        return result;
    }
    if (!sawLayout || !sawMetadata || entries.size() < 3u) {
        ::close(rootFd);
        result.error = "The SHA-256 integrity manifest is incomplete or lacks disc metadata.";
        return result;
    }

    IsoDiscIntegrityProgress progress;
    progress.totalBytes = result.totalBytes;
    progress.totalFiles = entries.size();
    if (progressCallback != nullptr && !progressCallback(progress, userData)) {
        ::close(rootFd);
        result.cancelled = true;
        result.error = "The SHA-256 integrity verification was cancelled.";
        return result;
    }

    for (const auto &entry : entries) {
        const int fileFd = openRegularRelativeAt(rootFd, entry.path);
        if (fileFd < 0) {
            result.error = "An integrity file is missing, not regular, or uses a symlink: " +
                           entry.path;
            ::close(rootFd);
            return result;
        }

        std::string actualDigest;
        std::string hashError;
        const bool hashed = hashFileDescriptor(
            fileFd, entry.size, actualDigest, hashError,
            progress, progressCallback, userData, result.cancelled);
        ::close(fileFd);
        if (!hashed) {
            result.error = entry.path + ": " + hashError;
            ::close(rootFd);
            result.verifiedBytes = progress.verifiedBytes;
            return result;
        }
        if (actualDigest != entry.sha256) {
            result.error = "SHA-256 mismatch: " + entry.path;
            ::close(rootFd);
            result.verifiedBytes = progress.verifiedBytes;
            return result;
        }

        ++progress.completedFiles;
        result.verifiedFiles = progress.completedFiles;
        if (progressCallback != nullptr &&
            !progressCallback(progress, userData)) {
            result.cancelled = true;
            result.error = "The SHA-256 integrity verification was cancelled.";
            ::close(rootFd);
            result.verifiedBytes = progress.verifiedBytes;
            return result;
        }
    }

    ::close(rootFd);
    result.verifiedBytes = progress.verifiedBytes;
    result.valid = result.verifiedFiles == progress.totalFiles &&
                   result.verifiedBytes == result.totalBytes;
    if (!result.valid && result.error.empty()) {
        result.error = "The SHA-256 integrity verification ended with incomplete totals.";
    }
    return result;
}

IsoInstallResult installIsoManifest(
    const Iso9660Reader &reader,
    const std::vector<IsoInstallFile> &manifest,
    const std::string &privateRoot,
    const std::string &finalDirectoryName,
    IsoInstallProgressCallback progressCallback,
    void *userData) {
    return installIsoManifestImpl(
        reader, manifest, privateRoot, finalDirectoryName,
        nullptr, nullptr, progressCallback, userData);
}

IsoInstallResult installIsoDisc(
    const Iso9660Reader &reader,
    const std::string &privateRoot,
    const std::string &finalDirectoryName,
    IsoInstallProgressCallback progressCallback,
    void *userData) {
    IsoInstallResult failure;
    IsoDiscLayout layout;
    if (!reader.scanDiscLayout(layout, &failure.error)) {
        if (failure.error.empty()) {
            failure.error = "Could not enumerate the ISO9660 disc layout.";
        }
        return failure;
    }

    for (const auto &file : layout.files) {
        if (isReservedDiscPath(file.path)) {
            failure.error = "The ISO contains a path reserved for private disc metadata.";
            return failure;
        }
    }

    std::vector<IsoInstallFile> manifest;
    manifest.reserve(layout.files.size());
    for (const IsoDiscFile &file : layout.files) {
        manifest.push_back({
            file.path,
            "disc/" + file.path,
            static_cast<uint64_t>(file.size)
        });
    }

    IsoDiscMetadataInfo metadata{};
    MetadataProgressContext measureProgress{
        &failure, nullptr, progressCallback, userData};
    if (!measureIsoDiscMetadata(
            reader, layout, metadata, &failure.error,
            progressCallback != nullptr ? forwardMetadataMeasureProgress : nullptr,
            progressCallback != nullptr ? &measureProgress : nullptr)) {
        if (measureProgress.result != nullptr && measureProgress.result->cancelled) {
            failure.cancelled = true;
        }
        if (failure.error.empty()) {
            failure.error = "Could not measure the PS2 disc metadata.";
        }
        return failure;
    }

    return installIsoManifestImpl(
        reader, manifest, privateRoot, finalDirectoryName,
        &layout, &metadata, progressCallback, userData);
}

} // namespace gow::disc
