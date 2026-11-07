#pragma once

#include <string>

namespace gow::android {

// Writes a private, atomic identity record next to the VU1/IOP captures.
// The record binds subsequent host-generated AOT to the exact BOOT2 ELF
// loaded from the currently installed private disc.
bool writePrivateAotGameIdentity(
    const std::string &privateDiscRoot,
    const std::string &bootRelativePath,
    const std::string &captureRoot,
    std::string *error = nullptr) noexcept;

} // namespace gow::android
