#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gow::disc {

// Resolves a SYSTEM.CNF BOOT2 path to a regular file strictly inside the
// application's private cdRoot. ISO version suffixes are removed and path
// components are normalized to the primary ISO9660 uppercase namespace.
bool resolvePrivateBootElfPath(
    std::string_view privateRoot,
    std::string_view bootRelativePath,
    std::filesystem::path &resolvedPath,
    std::string &error);

} // namespace gow::disc
