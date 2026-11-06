#pragma once

#include "iso9660_reader.h"

#include <cstdint>
#include <string>

namespace gow::disc {

struct IsoDiscMetadataInfo {
    uint64_t metadataSectorCount = 0u;
    uint64_t metadataFileBytes = 0u;
    uint64_t layoutFileBytes = 0u;
};

struct IsoDiscMetadataProgress {
    uint64_t completedSectors = 0u;
    uint64_t totalSectors = 0u;
};

using IsoDiscMetadataProgressCallback =
    bool (*)(const IsoDiscMetadataProgress &progress, void *userData);

// Counts non-zero sectors that are not covered by any file extent. This is
// the exact set written to .disc_meta for DiscSource::openExtracted().
bool measureIsoDiscMetadata(
    const Iso9660Reader &reader,
    const IsoDiscLayout &layout,
    IsoDiscMetadataInfo &info,
    std::string *error = nullptr,
    IsoDiscMetadataProgressCallback progressCallback = nullptr,
    void *userData = nullptr);

// Writes .disc_layout and .disc_meta atomically into an already extracted,
// private disc root. All listed files must exist with their original sizes.
// The caller must keep this root in staging until the complete install passes.
bool writeIsoDiscMetadata(
    const Iso9660Reader &reader,
    const IsoDiscLayout &layout,
    const std::string &extractedRoot,
    IsoDiscMetadataInfo *info = nullptr,
    std::string *error = nullptr,
    IsoDiscMetadataProgressCallback progressCallback = nullptr,
    void *userData = nullptr);

// Uses a previously measured metadata size to avoid scanning the image twice.
// The reader must still refer to the same, read-only image used for measurement.
bool writeIsoDiscMetadataMeasured(
    const Iso9660Reader &reader,
    const IsoDiscLayout &layout,
    const std::string &extractedRoot,
    const IsoDiscMetadataInfo &measured,
    IsoDiscMetadataInfo *info = nullptr,
    std::string *error = nullptr,
    IsoDiscMetadataProgressCallback progressCallback = nullptr,
    void *userData = nullptr);

} // namespace gow::disc
