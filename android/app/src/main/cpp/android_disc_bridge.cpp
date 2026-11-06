#include "iso9660_reader.h"

#include <jni.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {
std::mutex g_discMutex;
gow::disc::Iso9660Reader g_discReader;
std::unordered_map<uint64_t, gow::disc::IsoFile> g_openFiles;
uint64_t g_nextHandle = 1u;

bool setDiscDescriptor(int descriptor) noexcept {
    std::lock_guard<std::mutex> lock(g_discMutex);
    g_openFiles.clear();
    return g_discReader.openDescriptor(descriptor, nullptr);
}
}

extern "C" bool gowAndroidDiscIsReady() noexcept {
    std::lock_guard<std::mutex> lock(g_discMutex);
    return g_discReader.isOpen();
}

extern "C" uint64_t gowAndroidDiscOpenFile(
    const char *path, uint64_t *fileSize) noexcept {
    if (fileSize != nullptr) {
        *fileSize = 0u;
    }
    if (path == nullptr || path[0] == '\0' || fileSize == nullptr) {
        return 0u;
    }

    std::lock_guard<std::mutex> lock(g_discMutex);
    if (!g_discReader.isOpen()) {
        return 0u;
    }

    gow::disc::IsoFile file{};
    if (!g_discReader.lookupFile(path, file, nullptr)) {
        return 0u;
    }

    uint64_t handle = g_nextHandle++;
    if (handle == 0u) {
        handle = g_nextHandle++;
    }
    g_openFiles.emplace(handle, file);
    *fileSize = file.size;
    return handle;
}

extern "C" bool gowAndroidDiscReadFile(
    uint64_t handle, uint64_t offset, void *destination,
    size_t requested, size_t *bytesRead) noexcept {
    if (bytesRead == nullptr) {
        return false;
    }
    *bytesRead = 0u;

    std::lock_guard<std::mutex> lock(g_discMutex);
    const auto it = g_openFiles.find(handle);
    if (it == g_openFiles.end()) {
        return false;
    }
    return g_discReader.readFile(
        it->second, offset, destination, requested, *bytesRead);
}

extern "C" void gowAndroidDiscCloseFile(uint64_t handle) noexcept {
    std::lock_guard<std::mutex> lock(g_discMutex);
    g_openFiles.erase(handle);
}

extern "C" void gowAndroidDiscCloseImage() noexcept {
    std::lock_guard<std::mutex> lock(g_discMutex);
    g_openFiles.clear();
    g_discReader.close();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_gowandroid_app_GowGameActivity_nativeSetDiscFileDescriptor(
    JNIEnv *, jclass, jint descriptor) {
    return setDiscDescriptor(descriptor) ? JNI_TRUE : JNI_FALSE;
}
