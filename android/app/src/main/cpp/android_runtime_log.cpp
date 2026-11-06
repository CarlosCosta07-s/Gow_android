#include "android_runtime_log.h"

#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gow::android {
namespace {

std::mutex gLogMutex;
std::filesystem::path gLogPath;
constexpr std::size_t kMaxStageBytes = 32u;
constexpr std::size_t kMaxMessageBytes = 4096u;

bool writeAll(int descriptor, std::string_view bytes) noexcept {
    std::size_t offset = 0u;
    while (offset < bytes.size()) {
        const ssize_t written = ::write(
            descriptor, bytes.data() + offset, bytes.size() - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

std::string makeLine(
    std::string_view stage, std::string_view message) {
    const std::size_t stageSize = std::min(stage.size(), kMaxStageBytes);
    const std::size_t messageSize = std::min(message.size(), kMaxMessageBytes);
    std::string line;
    line.reserve(stageSize + messageSize + 5u);
    line.push_back('[');
    line.append(stage.data(), stageSize);
    line.append("] ");
    for (std::size_t i = 0u; i < messageSize; ++i) {
        const char value = message[i];
        line.push_back(value == '\r' || value == '\n' ? ' ' : value);
    }
    line.push_back('\n');
    return line;
}

} // namespace

bool configurePrivateRuntimeLog(
    const std::filesystem::path &appFilesDirectory) noexcept {
    try {
        {
            std::lock_guard<std::mutex> lock(gLogMutex);
            gLogPath.clear();
        }
        if (appFilesDirectory.empty() || !appFilesDirectory.is_absolute() ||
            std::filesystem::is_symlink(appFilesDirectory) ||
            !std::filesystem::is_directory(appFilesDirectory)) {
            return false;
        }

        const auto candidate = appFilesDirectory / "gow-runtime.log";
        std::error_code error;
        const auto status = std::filesystem::symlink_status(candidate, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            return false;
        }
        if (!error && std::filesystem::is_symlink(status)) return false;
        if (!error && std::filesystem::exists(status) &&
            !std::filesystem::is_regular_file(status)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(gLogMutex);
        gLogPath = candidate;
        return true;
    } catch (...) {
        return false;
    }
}

void appendPrivateRuntimeLog(
    std::string_view stage, std::string_view message) noexcept {
    try {
        std::lock_guard<std::mutex> lock(gLogMutex);
        if (gLogPath.empty()) return;

        int flags = O_WRONLY | O_CREAT | O_APPEND;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        const int descriptor = ::open(gLogPath.c_str(), flags, 0600);
        if (descriptor < 0) return;

        struct stat metadata {};
        if (::fstat(descriptor, &metadata) != 0 ||
            !S_ISREG(metadata.st_mode) || metadata.st_size < 0) {
            ::close(descriptor);
            return;
        }

        const std::string line = makeLine(stage, message);
        const auto currentSize = static_cast<std::uintmax_t>(metadata.st_size);
        if (currentSize > kMaxPrivateRuntimeLogBytes ||
            line.size() > kMaxPrivateRuntimeLogBytes -
                std::min(currentSize, kMaxPrivateRuntimeLogBytes)) {
            if (::ftruncate(descriptor, 0) != 0) {
                ::close(descriptor);
                return;
            }
            constexpr std::string_view marker =
                "[INFO] Log anterior truncado ao atingir o limite de 2 MiB.\n";
            if (!writeAll(descriptor, marker)) {
                ::close(descriptor);
                return;
            }
        }

        (void)writeAll(descriptor, line);
        ::close(descriptor);
    } catch (...) {
        // Diagnostics must never interrupt or destabilize the guest runtime.
    }
}

} // namespace gow::android
