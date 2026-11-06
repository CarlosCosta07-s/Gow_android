#include "android_cdroot_bridge.h"
#include "cdroot_vfs.h"
#include "iso9660_installer.h"
#include "iso9660_reader.h"

#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
std::mutex g_discMutex;
gow::disc::Iso9660Reader g_discReader;
gow::disc::CdRootVfs g_cdRootVfs;
std::unordered_map<uint64_t, gow::disc::IsoFile> g_openFiles;
uint64_t g_nextHandle = 1u;

bool setDiscDescriptor(int descriptor) noexcept {
    std::lock_guard<std::mutex> lock(g_discMutex);
    g_openFiles.clear();
    return g_discReader.openDescriptor(descriptor, nullptr);
}

bool copyJavaString(JNIEnv *env, jstring value, std::string &output) {
    if (value == nullptr) return false;
    const char *utf = env->GetStringUTFChars(value, nullptr);
    if (utf == nullptr) return false;
    output.assign(utf);
    env->ReleaseStringUTFChars(value, utf);
    return true;
}

struct ProgressContext {
    JNIEnv *env = nullptr;
    jobject callback = nullptr;
    jmethodID method = nullptr;
};

bool forwardInstallProgress(const gow::disc::IsoInstallProgress &progress,
                            void *opaque) {
    auto *context = static_cast<ProgressContext *>(opaque);
    if (context == nullptr || context->env == nullptr ||
        context->callback == nullptr || context->method == nullptr) {
        return true;
    }

    const jboolean keepGoing = context->env->CallBooleanMethod(
        context->callback,
        context->method,
        static_cast<jlong>(progress.totalBytes),
        static_cast<jlong>(progress.copiedBytes),
        static_cast<jint>(progress.completedFiles),
        static_cast<jint>(progress.totalFiles));
    if (context->env->ExceptionCheck()) {
        context->env->ExceptionClear();
        return false;
    }
    return keepGoing == JNI_TRUE;
}
} // namespace

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

extern "C" bool gowAndroidCdRootIsReady() noexcept {
    return g_cdRootVfs.isOpen();
}

extern "C" uint64_t gowAndroidCdRootOpenFile(
    const char *path, uint64_t *fileSize) noexcept {
    if (fileSize != nullptr) *fileSize = 0u;
    if (path == nullptr || path[0] == '\\0' || fileSize == nullptr) return 0u;

    gow::disc::CdRootFile file{};
    if (!g_cdRootVfs.openFile(path, file, nullptr)) return 0u;
    *fileSize = file.size;
    return file.handle;
}

extern "C" bool gowAndroidCdRootReadFile(
    uint64_t handle, uint64_t offset, void *destination,
    size_t requested, size_t *bytesRead) noexcept {
    if (bytesRead == nullptr) return false;
    *bytesRead = 0u;
    return g_cdRootVfs.readFile(
        handle, offset, destination, requested, *bytesRead);
}

extern "C" void gowAndroidCdRootCloseFile(uint64_t handle) noexcept {
    g_cdRootVfs.closeFile(handle);
}

extern "C" void gowAndroidCdRootClose() noexcept {
    g_cdRootVfs.close();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_gowandroid_app_GowGameActivity_nativeSetDiscFileDescriptor(
    JNIEnv *, jclass, jint descriptor) {
    return setDiscDescriptor(descriptor) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_gowandroid_app_MainActivity_nativeSetCdRootDirectory(
    JNIEnv *env, jclass, jstring privateCdRoot) {
    std::string path;
    if (!copyJavaString(env, privateCdRoot, path) || path.empty()) {
        return JNI_FALSE;
    }
    return g_cdRootVfs.openRoot(path, nullptr) ? JNI_TRUE : JNI_FALSE;
}

// Imports a small, explicit set of boot files into private app storage. It does
// not imply that the complete game data set is installed or that the game can
// boot yet. Returns an empty string on success, otherwise a diagnostic message.
extern "C" JNIEXPORT jstring JNICALL
Java_org_gowandroid_app_MainActivity_nativeInstallIsoManifest(
    JNIEnv *env, jclass, jint descriptor, jstring privateRoot,
    jobjectArray sourcePaths, jobjectArray destinationPaths,
    jobject progressCallback) {
    auto makeResult = [env](const std::string &message) -> jstring {
        return env->NewStringUTF(message.c_str());
    };

    try {
        if (descriptor < 0 || privateRoot == nullptr ||
            sourcePaths == nullptr || destinationPaths == nullptr) {
            return makeResult("Parâmetros de instalação inválidos.");
        }

        const jsize sourceCount = env->GetArrayLength(sourcePaths);
        const jsize destinationCount = env->GetArrayLength(destinationPaths);
        if (sourceCount <= 0 || sourceCount != destinationCount ||
            sourceCount > 65536) {
            return makeResult("O manifesto está vazio ou possui tamanhos incompatíveis.");
        }

        std::string rootPath;
        if (!copyJavaString(env, privateRoot, rootPath) || rootPath.empty()) {
            return makeResult("Não foi possível ler o caminho privado do aplicativo.");
        }

        std::vector<gow::disc::IsoInstallFile> manifest;
        manifest.reserve(static_cast<size_t>(sourceCount));
        for (jsize index = 0; index < sourceCount; ++index) {
            auto source = static_cast<jstring>(
                env->GetObjectArrayElement(sourcePaths, index));
            auto destination = static_cast<jstring>(
                env->GetObjectArrayElement(destinationPaths, index));
            std::string sourcePath;
            std::string destinationPath;
            const bool copied =
                copyJavaString(env, source, sourcePath) &&
                copyJavaString(env, destination, destinationPath);
            if (source != nullptr) env->DeleteLocalRef(source);
            if (destination != nullptr) env->DeleteLocalRef(destination);
            if (!copied) {
                return makeResult("O manifesto contém um caminho inválido.");
            }
            manifest.push_back({
                std::move(sourcePath),
                std::move(destinationPath),
                gow::disc::kUnknownExpectedFileSize
            });
        }

        ProgressContext progress;
        if (progressCallback != nullptr) {
            progress.env = env;
            progress.callback = progressCallback;
            jclass callbackClass = env->GetObjectClass(progressCallback);
            if (callbackClass == nullptr) {
                return makeResult("Não foi possível acessar o callback de progresso.");
            }
            progress.method = env->GetMethodID(
                callbackClass, "onProgress", "(JJII)Z");
            env->DeleteLocalRef(callbackClass);
            if (progress.method == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                return makeResult("O callback de progresso possui uma assinatura inválida.");
            }
        }

        gow::disc::Iso9660Reader reader;
        std::string openError;
        if (!reader.openDescriptor(descriptor, &openError)) {
            return makeResult(openError.empty()
                ? "Não foi possível abrir a imagem ISO no leitor nativo."
                : openError);
        }

        const auto result = gow::disc::installIsoManifest(
            reader, manifest, rootPath, "gow_boot_seed",
            progressCallback != nullptr ? forwardInstallProgress : nullptr,
            progressCallback != nullptr ? &progress : nullptr);
        if (!result.success) {
            if (result.cancelled) {
                return makeResult("Importação cancelada. Os arquivos temporários foram removidos.");
            }
            return makeResult(result.error.empty()
                ? "A extração dos arquivos de boot falhou."
                : result.error);
        }
        return makeResult("");
    } catch (const std::exception &exception) {
        return makeResult(std::string("Falha interna no instalador: ") + exception.what());
    } catch (...) {
        return makeResult("Falha interna desconhecida no instalador.");
    }
}
