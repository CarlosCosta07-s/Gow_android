#include "android_aot_identity.h"

#include "sha256.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace gow::android {
namespace fs = std::filesystem;
namespace {

void setError(std::string *error, const char *message) noexcept {
    if (error != nullptr) {
        try {
            *error = message;
        } catch (...) {
        }
    }
}

bool validRelativeBootPath(const std::string &value) noexcept {
    if (value.empty() || value.size() > 1024u || value.front() == '/') {
        return false;
    }
    size_t start = 0u;
    while (start < value.size()) {
        const size_t end = value.find('/', start);
        const size_t length = (end == std::string::npos ? value.size() : end) - start;
        if (length == 0u || length > 255u) return false;
        const std::string component = value.substr(start, length);
        if (component == "." || component == "..") return false;
        for (const unsigned char character : component) {
            const bool accepted =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '_' || character == '-' ||
                character == '+' || character == '.';
            if (!accepted) return false;
        }
        if (end == std::string::npos) break;
        start = end + 1u;
    }
    return true;
}

bool hasSymlinkComponent(const fs::path &path) noexcept {
    fs::path current;
    for (const auto &component : path) {
        current /= component;
        std::error_code error;
        const fs::file_status status = fs::symlink_status(current, error);
        if (error) return true;
        if (fs::is_symlink(status)) return true;
    }
    return false;
}

bool safeInstallationId(const std::string &value) noexcept {
    if (value.empty() || value.size() > 128u) return false;
    for (const unsigned char character : value) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '-')) {
            return false;
        }
    }
    return true;
}

} // namespace

bool writePrivateAotGameIdentity(
    const std::string &privateDiscRoot,
    const std::string &bootRelativePath,
    const std::string &captureRoot,
    std::string *error) noexcept {
    try {
        if (error != nullptr) error->clear();
        if (!validRelativeBootPath(bootRelativePath)) {
            setError(error, "BOOT2 path is not a safe relative path.");
            return false;
        }

        const fs::path discRoot(privateDiscRoot);
        const fs::path captureDirectory(captureRoot);
        if (!discRoot.is_absolute() || discRoot.filename() != "disc" ||
            discRoot.parent_path().parent_path().filename() != "installations" ||
            !captureDirectory.is_absolute() ||
            captureDirectory.filename() != "gow-aot-captures" ||
            hasSymlinkComponent(discRoot) ||
            hasSymlinkComponent(captureDirectory)) {
            setError(error, "Private disc or AOT capture root is outside the expected layout or contains a symlink.");
            return false;
        }

        const std::string installationId = discRoot.parent_path().filename().string();
        if (!safeInstallationId(installationId)) {
            setError(error, "Private installation identifier is invalid.");
            return false;
        }

        fs::path elfPath = discRoot;
        size_t start = 0u;
        while (start < bootRelativePath.size()) {
            const size_t end = bootRelativePath.find('/', start);
            const size_t length =
                (end == std::string::npos ? bootRelativePath.size() : end) - start;
            elfPath /= bootRelativePath.substr(start, length);
            if (hasSymlinkComponent(elfPath)) {
                setError(error, "BOOT2 path contains a symbolic link.");
                return false;
            }
            if (end == std::string::npos) break;
            start = end + 1u;
        }

        std::error_code fileError;
        const fs::file_status elfStatus = fs::symlink_status(elfPath, fileError);
        if (fileError || !fs::is_regular_file(elfStatus)) {
            setError(error, "BOOT2 executable is missing or is not a regular file.");
            return false;
        }

        const uintmax_t elfSize = fs::file_size(elfPath, fileError);
        if (fileError || elfSize == 0u) {
            setError(error, "BOOT2 executable is empty or its size cannot be read.");
            return false;
        }

        std::ifstream input(elfPath, std::ios::binary);
        if (!input) {
            setError(error, "Could not open the private BOOT2 executable for hashing.");
            return false;
        }

        gow::disc::Sha256 hash;
        std::array<char, 1024u * 1024u> buffer{};
        uintmax_t bytesRead = 0u;
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0) {
                hash.update(buffer.data(), static_cast<size_t>(count));
                bytesRead += static_cast<uintmax_t>(count);
            }
        }
        if (!input.eof() || bytesRead != elfSize) {
            setError(error, "Could not hash the complete BOOT2 executable.");
            return false;
        }
        const std::string elfSha256 = gow::disc::sha256Hex(hash.finish());

        const fs::path identityPath = captureDirectory / "game_identity.txt";
        const fs::path temporaryPath = captureDirectory / "game_identity.txt.tmp";
        std::error_code identityStatusError;
        const fs::file_status identityStatus =
            fs::symlink_status(identityPath, identityStatusError);
        if (identityStatusError != std::errc::no_such_file_or_directory &&
            identityStatusError) {
            setError(error, "Could not inspect the previous private AOT identity record.");
            return false;
        }
        if (!identityStatusError && fs::is_symlink(identityStatus)) {
            setError(error, "Private AOT game identity path is a symbolic link.");
            return false;
        }
        if (!identityStatusError && !fs::is_regular_file(identityStatus)) {
            setError(error, "Previous private AOT game identity is not a regular file.");
            return false;
        }

        // Never merge VU1/IOP captures collected from a different executable.
        // Older installations without an identity record are also cleared,
        // because their capture provenance cannot be established.
        bool sameGameIdentity = false;
        if (!identityStatusError) {
            std::ifstream previous(identityPath, std::ios::binary);
            std::string line;
            while (std::getline(previous, line)) {
                constexpr char kShaPrefix[] = "elf_sha256=";
                if (line.compare(0u, sizeof(kShaPrefix) - 1u, kShaPrefix) == 0) {
                    sameGameIdentity =
                        line.substr(sizeof(kShaPrefix) - 1u) == elfSha256;
                    break;
                }
            }
            if (!previous.eof() && previous.fail()) {
                setError(error, "Could not read the previous private AOT identity record.");
                return false;
            }
        }

        for (const char *folder : {"vu1", "iop"}) {
            const fs::path captureSubdirectory = captureDirectory / folder;
            std::error_code statusError;
            const fs::file_status status =
                fs::symlink_status(captureSubdirectory, statusError);
            if (statusError != std::errc::no_such_file_or_directory && statusError) {
                setError(error, "Could not inspect an existing private AOT capture directory.");
                return false;
            }
            if (!statusError && fs::is_symlink(status)) {
                setError(error, "A private AOT capture directory is a symbolic link.");
                return false;
            }
            if (!statusError && !fs::is_directory(status)) {
                setError(error, "A private AOT capture path is not a directory.");
                return false;
            }
            if (!sameGameIdentity && !statusError) {
                std::error_code removeError;
                fs::remove_all(captureSubdirectory, removeError);
                if (removeError) {
                    setError(error, "Could not clear AOT captures from a different game identity.");
                    return false;
                }
            }
            std::error_code createError;
            fs::create_directories(captureSubdirectory, createError);
            if (createError) {
                setError(error, "Could not create private AOT capture subdirectories.");
                return false;
            }
        }

        std::error_code tempStatusError;
        const fs::file_status tempStatus = fs::symlink_status(temporaryPath, tempStatusError);
        if (tempStatusError != std::errc::no_such_file_or_directory && tempStatusError) {
            setError(error, "Could not inspect the temporary AOT identity path.");
            return false;
        }
        if (!tempStatusError && fs::is_symlink(tempStatus)) {
            setError(error, "Temporary AOT game identity path is a symbolic link.");
            return false;
        }
        if (!tempStatusError && fs::is_directory(tempStatus)) {
            setError(error, "Temporary AOT game identity path is a directory.");
            return false;
        }

        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            setError(error, "Could not create the temporary private AOT identity record.");
            return false;
        }
        output
            << "format=gow-android-game-identity-v1\n"
            << "installation_id=" << installationId << '\n'
            << "boot_path=" << bootRelativePath << '\n'
            << "elf_size=" << elfSize << '\n'
            << "elf_sha256=" << elfSha256 << '\n';
        output.flush();
        if (!output) {
            output.close();
            std::error_code removeError;
            fs::remove(temporaryPath, removeError);
            setError(error, "Could not flush the private AOT identity record.");
            return false;
        }
        output.close();
        if (!output) {
            std::error_code removeError;
            fs::remove(temporaryPath, removeError);
            setError(error, "Could not close the private AOT identity record.");
            return false;
        }

        std::error_code renameError;
        fs::rename(temporaryPath, identityPath, renameError);
        if (renameError) {
            std::error_code removeError;
            fs::remove(temporaryPath, removeError);
            setError(error, "Could not atomically publish the private AOT identity record.");
            return false;
        }
        return true;
    } catch (...) {
        setError(error, "Unexpected failure while writing the private AOT identity record.");
        return false;
    }
}

} // namespace gow::android
