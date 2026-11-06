#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace gow::android {

inline constexpr std::uintmax_t kMaxPrivateRuntimeLogBytes = 2u * 1024u * 1024u;

// Configures a bounded log inside Android's private files directory.
// Returns false when the directory or an existing log file is unsafe.
bool configurePrivateRuntimeLog(
    const std::filesystem::path &appFilesDirectory) noexcept;

// Appends a short diagnostic line. Failures are intentionally non-fatal.
void appendPrivateRuntimeLog(
    std::string_view stage, std::string_view message) noexcept;

} // namespace gow::android
