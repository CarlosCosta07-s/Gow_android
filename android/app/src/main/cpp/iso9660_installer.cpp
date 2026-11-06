#include "iso9660_installer.h"
#include "iso9660_disc_layout.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
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

constexpr size_t kCopyBufferSize = 64u * 1024u;
constexpr size_t kMaxManifestFiles = 65536u;
constexpr size_t kMaxRelativePathLength = 1024u;
constexpr size_t kMaxPathComponentLength = 255u;

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
    const uint64_t extractedBytes = result.totalBytes;
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
    std::array<uint8_t, kCopyBufferSize> buffer{};
    bool copySucceeded = keepGoing;

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

            fileOffset += static_cast<uint64_t>(bytesRead);
            result.copiedBytes += static_cast<uint64_t>(bytesRead);
            notifyProgress(result, progressCallback, userData, keepGoing);
            if (!keepGoing) {
                fileSucceeded = false;
                break;
            }
        }

        if (fileSucceeded && ::fsync(outputFd) != 0) {
            fileSucceeded = false;
            result.error = "Could not flush an extracted file to private storage.";
        }
        if (fileSucceeded && ::fsync(parentFd) != 0) {
            fileSucceeded = false;
            result.error = "Could not flush an extracted file's parent directory.";
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

        ++result.completedFiles;
        notifyProgress(result, progressCallback, userData, keepGoing);
        if (!keepGoing) {
            copySucceeded = false;
            break;
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
            result.copiedBytes = result.totalBytes;
        }
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
