#include "ps2_runtime.h"

#include "android_cdroot_bridge.h"

#include <jni.h>

#include <array>
#include <exception>
#include <filesystem>
#include <string>

#include <new>

// C ABI boundary reserved for the future JNI/GameActivity bridge.
// Creating the object does not initialize a desktop window, start audio,
// read a disc, or execute guest instructions.
extern "C" __attribute__((visibility("default")))
PS2Runtime *gowCreatePs2Runtime() noexcept
{
    try
    {
        return new PS2Runtime();
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" __attribute__((visibility("default")))
void gowDestroyPs2Runtime(PS2Runtime *runtime) noexcept
{
    delete runtime;
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_gowandroid_app_MainActivity_nativePrepareRuntimeIoPaths(
    JNIEnv *env, jclass, jstring bootRelativePath)
{
    auto makeResult = [env](const std::string &message) -> jstring
    {
        return env->NewStringUTF(message.c_str());
    };

    try
    {
        if (!gowAndroidCdRootIsReady())
            return makeResult("A pasta cdRoot ainda não foi aberta pelo provider Android.");

        const char *utfPath = bootRelativePath != nullptr
            ? env->GetStringUTFChars(bootRelativePath, nullptr)
            : nullptr;
        if (utfPath == nullptr)
            return makeResult("O caminho do ELF BOOT2 está vazio ou inválido.");

        const std::string relativePath(utfPath);
        env->ReleaseStringUTFChars(bootRelativePath, utfPath);
        if (relativePath.empty())
            return makeResult("O caminho do ELF BOOT2 está vazio ou inválido.");

        std::array<char, 4096> privateRoot{};
        if (!gowAndroidCdRootCopyPath(privateRoot.data(), privateRoot.size()))
            return makeResult("Não foi possível recuperar o caminho físico da instalação privada.");

        const std::filesystem::path root =
            std::filesystem::u8path(privateRoot.data());
        const std::filesystem::path elfPath =
            root / std::filesystem::u8path(relativePath);

        // This is the same path-configuration entry point used by
        // PS2Runtime::loadELF(). It does not initialize graphics/audio or run
        // guest instructions, but verifies the Android cdRoot override.
        PS2Runtime::configureIoPathsFromElf(elfPath.string());
        const auto &paths = PS2Runtime::getIoPaths();
        if (paths.cdRoot.empty())
            return makeResult("PS2Runtime configurou um cdRoot vazio.");

        std::error_code error;
        if (!std::filesystem::is_regular_file(paths.cdRoot / ".disc_layout", error) || error)
            return makeResult("PS2Runtime não está apontando para o .disc_layout da instalação.");
        error.clear();
        if (!std::filesystem::is_regular_file(paths.cdRoot / ".disc_meta", error) || error)
            return makeResult("PS2Runtime não está apontando para o .disc_meta da instalação.");
        error.clear();
        if (!std::filesystem::is_regular_file(paths.elfPath, error) || error)
            return makeResult("O caminho BOOT2 configurado no runtime não existe na instalação.");

        return makeResult(
            "OK: PS2Runtime::IoPaths.cdRoot = " + paths.cdRoot.string()
            + " · ELF = " + paths.elfPath.string());
    }
    catch (const std::exception &exception)
    {
        return makeResult(
            std::string("Falha ao configurar IoPaths do PS2Runtime: ") + exception.what());
    }
    catch (...)
    {
        return makeResult("Falha desconhecida ao configurar IoPaths do PS2Runtime.");
    }
}
