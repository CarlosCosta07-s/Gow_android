#pragma once

#include "iso9660_reader.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gow::disc {

inline constexpr uint64_t kUnknownExpectedFileSize = UINT64_MAX;

struct IsoInstallFile {
    // Path inside the ISO9660 image, relative to its root.
    std::string sourcePath;
    // Relative destination path inside the private installation directory.
    std::string destinationPath;
    // Set to kUnknownExpectedFileSize when the manifest has no known size.
    uint64_t expectedSize = kUnknownExpectedFileSize;
};

struct IsoInstallProgress {
    uint64_t totalBytes = 0u;
    uint64_t copiedBytes = 0u;
    size_t completedFiles = 0u;
    size_t totalFiles = 0u;
};

struct IsoInstallResult {
    bool success = false;
    bool cancelled = false;
    uint64_t totalBytes = 0u;
    uint64_t copiedBytes = 0u;
    size_t completedFiles = 0u;
    size_t totalFiles = 0u;
    std::string error;
};

using IsoInstallProgressCallback =
    bool (*)(const IsoInstallProgress &progress, void *userData);

// Extracts only manifest entries into a private staging directory and publishes
// the completed directory with one same-filesystem rename. privateRoot must
// already exist and be owned by the application. The final directory name must
// be a single safe path component and must not already exist.
IsoInstallResult installIsoManifest(
    const Iso9660Reader &reader,
    const std::vector<IsoInstallFile> &manifest,
    const std::string &privateRoot,
    const std::string &finalDirectoryName,
    IsoInstallProgressCallback progressCallback = nullptr,
    void *userData = nullptr);

// Installs every supported ISO9660 file into <installation>/disc, generates
// the Waffle DiscSource .disc_layout/.disc_meta pair inside staging, and only
// then atomically publishes the complete installation.
IsoInstallResult installIsoDisc(
    const Iso9660Reader &reader,
    const std::string &privateRoot,
    const std::string &finalDirectoryName,
    IsoInstallProgressCallback progressCallback = nullptr,
    void *userData = nullptr);

} // namespace gow::disc
