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

// Counts non-zero sectors that are not covered by any file extent. This is
// the exact set written to .disc_meta for DiscSource::openExtracted().
bool measureIsoDiscMetadata(
    const Iso9660Reader &reader,
    const IsoDiscLayout &layout,
    IsoDiscMetadataInfo &info,
    std::string *error = nullptr);

// Writes .disc_layout and .disc_meta atomically into an already extracted,
// private disc root. All listed files must exist with their original sizes.
// The caller must keep this root in staging until the complete install passes.
bool writeIsoDiscMetadata(
    const Iso9660Reader &reader,
    const IsoDiscLayout &layout,
    const std::string &extractedRoot,
    IsoDiscMetadataInfo *info = nullptr,
    std::string *error = nullptr);

} // namespace gow::disc
