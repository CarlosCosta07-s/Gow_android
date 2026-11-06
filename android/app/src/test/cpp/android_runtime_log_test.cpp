#include "android_runtime_log.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace {

std::string readText(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    char temporary[] = "/tmp/gow-android-runtime-log-XXXXXX";
    char *created = ::mkdtemp(temporary);
    assert(created != nullptr);
    const std::filesystem::path root(created);
    const auto logPath = root / "gow-runtime.log";

    assert(gow::android::configurePrivateRuntimeLog(root));
    gow::android::appendPrivateRuntimeLog("BOOT", "runtime initialized");
    std::string content = readText(logPath);
    assert(content.find("[BOOT] runtime initialized\n") != std::string::npos);

    const std::string largeMessage(2048u, 'x');
    for (int index = 0; index < 1500; ++index) {
        gow::android::appendPrivateRuntimeLog("TEST", largeMessage);
    }
    assert(std::filesystem::file_size(logPath) <=
           gow::android::kMaxPrivateRuntimeLogBytes);
    content = readText(logPath);
    assert(content.find("Log anterior truncado") != std::string::npos);

    const auto targetPath = root / "outside.txt";
    {
        std::ofstream target(targetPath, std::ios::binary);
        target << "preserve-me";
    }
    std::filesystem::remove(logPath);
    std::filesystem::create_symlink(targetPath, logPath);
    assert(!gow::android::configurePrivateRuntimeLog(root));
    gow::android::appendPrivateRuntimeLog("TEST", "must not follow symlink");
    assert(readText(targetPath) == "preserve-me");

    std::filesystem::remove_all(root);
    return 0;
}
