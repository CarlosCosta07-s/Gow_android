#include "android_cdroot_bridge.h"
#include "cdroot_vfs.h"
#include "iso9660_installer.h"
#include "iso9660_reader.h"
#include "ps2_elf_validator.h"

#include <jni.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" bool gowRuntimePrepareIoPaths(
    const char *bootRelativePath, char *message, size_t capacity) noexcept
    __attribute__((weak));
extern "C" bool gowRuntimeValidateDiscSource(
    const char *privateRoot, const char *bootRelativePath,
    char *message, size_t capacity) noexcept
    __attribute__((weak));

namespace {
std::mutex g_discMutex;
std::mutex g_cdRootPathMutex;
gow::disc::Iso9660Reader g_discReader;
gow::disc::CdRootVfs g_cdRootVfs;
std::string g_cdRootPath;
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

extern "C" bool gowAndroidCdRootCopyPath(
    char *destination, size_t capacity) noexcept {
    if (destination == nullptr || capacity == 0u) return false;
    std::lock_guard<std::mutex> lock(g_cdRootPathMutex);
    if (g_cdRootPath.empty() || g_cdRootPath.size() >= capacity) return false;
    std::memcpy(destination, g_cdRootPath.c_str(), g_cdRootPath.size() + 1u);
    return true;
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

extern "C" bool gowAndroidCdRootStat(
    const char *path, bool *isDirectory, uint64_t *fileSize) noexcept {
    if (isDirectory != nullptr) *isDirectory = false;
    if (fileSize != nullptr) *fileSize = 0u;
    if (path == nullptr || isDirectory == nullptr || fileSize == nullptr) return false;

    gow::disc::CdRootFile file{};
    if (g_cdRootVfs.openFile(path, file, nullptr)) {
        *isDirectory = false;
        *fileSize = file.size;
        g_cdRootVfs.closeFile(file.handle);
        return true;
    }

    std::vector<gow::disc::CdRootEntry> entries;
    if (!g_cdRootVfs.listDirectory(path, entries, nullptr)) return false;
    *isDirectory = true;
    *fileSize = 0u;
    return true;
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
    std::lock_guard<std::mutex> lock(g_cdRootPathMutex);
    g_cdRootPath.clear();
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
    if (!g_cdRootVfs.openRoot(path, nullptr)) {
        std::lock_guard<std::mutex> lock(g_cdRootPathMutex);
        g_cdRootPath.clear();
        return JNI_FALSE;
    }
    {
        std::lock_guard<std::mutex> lock(g_cdRootPathMutex);
        g_cdRootPath = std::move(path);
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_gowandroid_app_MainActivity_nativeValidateDiscSource(
    JNIEnv *env, jclass, jstring bootRelativePath) {
    auto makeResult = [env](const std::string &message) -> jstring {
        return env->NewStringUTF(message.c_str());
    };

    try {
        std::string bootPath;
        if (!copyJavaString(env, bootRelativePath, bootPath) || bootPath.empty()) {
            return makeResult("O caminho BOOT2 está vazio ou inválido.");
        }

        std::string rootPath;
        {
            std::lock_guard<std::mutex> lock(g_cdRootPathMutex);
            rootPath = g_cdRootPath;
        }
        if (rootPath.empty()) {
            return makeResult("A raiz privada cdRoot ainda não foi montada.");
        }
        if (gowRuntimeValidateDiscSource == nullptr) {
            return makeResult(
                "A biblioteca Waffle/IOP não está preparada neste build; DiscSource indisponível.");
        }

        std::array<char, 512> message{};
        const bool valid = gowRuntimeValidateDiscSource(
            rootPath.c_str(), bootPath.c_str(), message.data(), message.size());
        if (message[0] != '\0') return makeResult(message.data());
        return makeResult(valid
            ? "OK: DiscSource validou a instalação por LSN."
            : "DiscSource recusou a instalação sem fornecer um diagnóstico.");
    } catch (const std::exception &exception) {
        return makeResult(
            std::string("Falha interna ao validar DiscSource: ") + exception.what());
    } catch (...) {
        return makeResult("Falha interna desconhecida ao validar DiscSource.");
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_gowandroid_app_MainActivity_nativePrepareRuntimeIoPaths(
    JNIEnv *env, jclass, jstring bootRelativePath) {
    auto makeResult = [env](const std::string &message) -> jstring {
        return env->NewStringUTF(message.c_str());
    };

    try {
        std::string bootPath;
        if (!copyJavaString(env, bootRelativePath, bootPath) || bootPath.empty()) {
            return makeResult("O caminho BOOT2 está vazio ou inválido.");
        }
        if (gowRuntimePrepareIoPaths == nullptr) {
            return makeResult(
                "A biblioteca PS2Runtime não está preparada neste build; IoPaths indisponível.");
        }

        std::array<char, 512> message{};
        const bool valid = gowRuntimePrepareIoPaths(
            bootPath.c_str(), message.data(), message.size());
        if (message[0] != '\0') return makeResult(message.data());
        return makeResult(valid
            ? "OK: IoPaths do PS2Runtime configurado."
            : "PS2Runtime não conseguiu configurar IoPaths.");
    } catch (const std::exception &exception) {
        return makeResult(
            std::string("Falha interna ao configurar IoPaths: ") + exception.what());
    } catch (...) {
        return makeResult("Falha interna desconhecida ao configurar IoPaths.");
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_gowandroid_app_MainActivity_nativeValidateCdRootElf(
    JNIEnv *env, jclass, jstring relativePath) {
    auto makeResult = [env](const std::string &message) -> jstring {
        return env->NewStringUTF(message.c_str());
    };

    try {
        std::string path;
        if (!copyJavaString(env, relativePath, path) || path.empty()) {
            return makeResult("O caminho privado do executável está vazio ou inválido.");
        }

        gow::disc::Ps2ElfInfo info{};
        std::string error;
        if (!gow::disc::validatePs2Elf(g_cdRootVfs, path, info, &error)) {
            return makeResult(error.empty()
                ? "O executável BOOT2 não passou pela validação ELF32/MIPS."
                : error);
        }

        char summary[192]{};
        std::snprintf(
            summary, sizeof(summary),
            "OK: ELF32/MIPS válido · entry=0x%08X · PT_LOAD=%zu · %llu bytes",
            info.entryPoint,
            info.loadableSegmentCount,
            static_cast<unsigned long long>(info.fileSize));
        return makeResult(summary);
    } catch (const std::exception &exception) {
        return makeResult(std::string("Falha interna na validação ELF: ") + exception.what());
    } catch (...) {
        return makeResult("Falha interna desconhecida na validação ELF.");
    }
}

// Imports the complete ISO9660 file tree and creates the sector metadata
// consumed by Waffle DiscSource::openExtracted(). Publication happens only
// after both files and metadata have been generated in the staging directory.
extern "C" JNIEXPORT jstring JNICALL
Java_org_gowandroid_app_MainActivity_nativeInstallIsoDisc(
    JNIEnv *env, jclass, jint descriptor, jstring privateRoot,
    jstring finalDirectoryName, jobject progressCallback) {
    auto makeResult = [env](const std::string &message) -> jstring {
        return env->NewStringUTF(message.c_str());
    };
    try {
        if (descriptor < 0 || privateRoot == nullptr || finalDirectoryName == nullptr) {
            return makeResult("Parâmetros de instalação completa inválidos.");
        }
        std::string rootPath;
        std::string finalName;
        if (!copyJavaString(env, privateRoot, rootPath) || rootPath.empty() ||
            !copyJavaString(env, finalDirectoryName, finalName) || finalName.empty()) {
            return makeResult("O caminho privado ou o identificador da instalação é inválido.");
        }
        ProgressContext progress;
        if (progressCallback != nullptr) {
            progress.env = env;
            progress.callback = progressCallback;
            jclass callbackClass = env->GetObjectClass(progressCallback);
            if (callbackClass == nullptr) {
                return makeResult("Não foi possível acessar o callback de progresso.");
            }
            progress.method = env->GetMethodID(callbackClass, "onProgress", "(JJII)Z");
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
        const auto result = gow::disc::installIsoDisc(
            reader, rootPath, finalName,
            progressCallback != nullptr ? forwardInstallProgress : nullptr,
            progressCallback != nullptr ? &progress : nullptr);
        if (!result.success) {
            if (result.cancelled) {
                return makeResult("Instalação cancelada. O diretório temporário foi removido.");
            }
            return makeResult(result.error.empty()
                ? "A instalação completa do disco falhou."
                : result.error);
        }
        return makeResult("");
    } catch (const std::exception &exception) {
        return makeResult(std::string("Falha interna na instalação do disco: ") + exception.what());
    } catch (...) {
        return makeResult("Falha interna desconhecida na instalação do disco.");
    }
}
