#include "ps2_runtime.h"
#include "android_cdroot_bridge.h"
#include "ps2x/iop/disc_source.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>

namespace {
bool writeMessage(char *destination, size_t capacity,
                  const std::string &message) noexcept {
    if (destination == nullptr || capacity == 0u) return false;
    const size_t count = std::min(capacity - 1u, message.size());
    if (count != 0u) std::memcpy(destination, message.data(), count);
    destination[count] = '\0';
    return true;
}
} // namespace

// Configures the exact static IoPaths object used by PS2Runtime::loadELF(),
// without initializing Raylib graphics/audio or executing guest instructions.
extern "C" bool gowRuntimePrepareIoPaths(
    const char *bootRelativePath, char *message, size_t capacity) noexcept {
    if (message != nullptr && capacity != 0u) message[0] = '\0';
    try {
        if (bootRelativePath == nullptr || bootRelativePath[0] == '\0') {
            writeMessage(message, capacity, "O caminho BOOT2 está vazio ou inválido.");
            return false;
        }
        if (!gowAndroidCdRootIsReady()) {
            writeMessage(message, capacity, "O provider cdRoot Android ainda não está montado.");
            return false;
        }

        std::array<char, 4096> privateRoot{};
        if (!gowAndroidCdRootCopyPath(privateRoot.data(), privateRoot.size())) {
            writeMessage(message, capacity, "Não foi possível recuperar o caminho físico do cdRoot.");
            return false;
        }

        const std::filesystem::path root =
            std::filesystem::u8path(privateRoot.data());
        const std::filesystem::path elf =
            root / std::filesystem::u8path(bootRelativePath);
        PS2Runtime::configureIoPathsFromElf(elf.string());

        const auto &paths = PS2Runtime::getIoPaths();
        if (paths.cdRoot.empty() ||
            !std::filesystem::is_regular_file(paths.cdRoot / ".disc_layout") ||
            !std::filesystem::is_regular_file(paths.cdRoot / ".disc_meta") ||
            !std::filesystem::is_regular_file(paths.elfPath)) {
            writeMessage(
                message, capacity,
                "PS2Runtime::configureIoPathsFromElf não resolveu a instalação e o BOOT2.");
            return false;
        }

        writeMessage(
            message, capacity,
            "OK: PS2Runtime::IoPaths.cdRoot = " + paths.cdRoot.string()
                + " · ELF = " + paths.elfPath.string());
        return true;
    } catch (const std::exception &exception) {
        writeMessage(
            message, capacity,
            std::string("Falha ao configurar IoPaths do PS2Runtime: ") + exception.what());
        return false;
    } catch (...) {
        writeMessage(message, capacity,
                     "Falha desconhecida ao configurar IoPaths do PS2Runtime.");
        return false;
    }
}

// Uses the same DiscSource class linked into ps2_iop. It verifies the extracted
// layer map and the BOOT2 extent without constructing or running PS2Runtime.
extern "C" bool gowRuntimeValidateDiscSource(
    const char *privateRoot, const char *bootRelativePath,
    char *message, size_t capacity) noexcept {
    if (message != nullptr && capacity != 0u) message[0] = '\0';
    try {
        if (privateRoot == nullptr || privateRoot[0] == '\0' ||
            bootRelativePath == nullptr || bootRelativePath[0] == '\0') {
            writeMessage(message, capacity, "O caminho da instalação ou BOOT2 está vazio.");
            return false;
        }

        auto source = ps2x::iop::DiscSource::openExtracted(
            std::filesystem::u8path(privateRoot));
        if (!source) {
            writeMessage(
                message, capacity,
                "DiscSource::openExtracted() rejeitou a instalação privada. "
                "Verifique .disc_layout, .disc_meta e os arquivos extraídos.");
            return false;
        }

        std::array<uint8_t, ps2x::iop::DiscSource::kSectorSize> sector{};
        if (!source->readSectors(16u, 1u, sector.data()) ||
            std::memcmp(sector.data() + 1u, "CD001", 5u) != 0) {
            writeMessage(
                message, capacity,
                "DiscSource abriu, mas a leitura do descritor primário no LSN 16 falhou.");
            return false;
        }

        const uint32_t layer1Start = source->layer1Start();
        if (layer1Start != 0u) {
            const uint64_t layer1PvdLsn =
                static_cast<uint64_t>(layer1Start) + 16u;
            if (layer1PvdLsn > std::numeric_limits<uint32_t>::max() ||
                !source->readSectors(
                    static_cast<uint32_t>(layer1PvdLsn), 1u, sector.data()) ||
                std::memcmp(sector.data() + 1u, "CD001", 5u) != 0) {
                writeMessage(
                    message, capacity,
                    "DiscSource detectou a segunda camada, mas o descritor ISO9660 dela não pôde ser lido.");
                return false;
            }
        }

        std::string bootPath(bootRelativePath);
        std::replace(bootPath.begin(), bootPath.end(), '/', '\\');
        const std::string guestBootPath = "cdrom0:\\" + bootPath;
        const auto bootFile = source->findFile(guestBootPath);
        if (!bootFile || bootFile->directory || bootFile->size == 0u) {
            writeMessage(
                message, capacity,
                "DiscSource não encontrou o executável BOOT2 pelo caminho CDROM0.");
            return false;
        }

        if (!source->readSectors(bootFile->lsn, 1u, sector.data())) {
            writeMessage(
                message, capacity,
                "DiscSource encontrou o BOOT2, mas não conseguiu ler seu setor inicial.");
            return false;
        }

        char summary[384]{};
        std::snprintf(
            summary, sizeof(summary),
            "OK: DiscSource::openExtracted · setores por LSN válidos · camada 1=%u · BOOT2 LSN=%u · %u bytes",
            layer1Start, bootFile->lsn, bootFile->size);
        writeMessage(message, capacity, summary);
        return true;
    } catch (const std::exception &exception) {
        writeMessage(
            message, capacity,
            std::string("Falha interna ao validar DiscSource: ") + exception.what());
        return false;
    } catch (...) {
        writeMessage(message, capacity,
                     "Falha interna desconhecida ao validar DiscSource.");
        return false;
    }
}

extern "C" __attribute__((visibility("default")))
PS2Runtime *gowCreatePs2Runtime() noexcept {
    try {
        return new PS2Runtime();
    } catch (...) {
        return nullptr;
    }
}

extern "C" __attribute__((visibility("default")))
void gowDestroyPs2Runtime(PS2Runtime *runtime) noexcept {
    delete runtime;
}
