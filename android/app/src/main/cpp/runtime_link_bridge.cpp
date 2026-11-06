#include "ps2_runtime.h"
#include "android_cdroot_bridge.h"
#include "ps2x/iop/disc_source.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_kernel.h"
#include "emulator/core/iop_memory.h"
#include "emulator/imports/iop_cdvd.h"
#include "ps2x/iop/iop_host.h"
#include "private_boot_path.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

namespace {
class RuntimeCdvdProbeHost final : public ps2x::iop::IopHost {
public:
    explicit RuntimeCdvdProbeHost(std::string cdRoot) : cdRoot_(std::move(cdRoot)) {}

    bool readGuest(uint32_t, void *, size_t) const override { return false; }
    bool writeGuest(uint32_t, const void *, size_t) override { return false; }
    bool zeroGuest(uint32_t, size_t) override { return false; }
    bool normalizeGuestAddress(uint32_t, uint32_t &normalized) const override {
        normalized = 0u;
        return false;
    }
    uint32_t allocateIopHandle(ps2x::iop::IopHandleKind) override { return nextHandle_++; }
    uint32_t allocateGuest(uint32_t, uint32_t) override { return 0u; }
    void freeGuest(uint32_t) override {}
    void audioCommand(uint32_t, uint32_t, ps2x::iop::GuestBuffer,
                      ps2x::iop::GuestBuffer) override {}
    std::string hostPath(ps2x::iop::HostPathKind kind) const override {
        return kind == ps2x::iop::HostPathKind::CdRoot ? cdRoot_ : std::string{};
    }
    std::string translateGuestPath(std::string_view) const override { return {}; }
    uint64_t openHostFile(std::string_view) override { return 0u; }
    bool hostFileSize(uint64_t, uint64_t &) const override { return false; }
    bool readHostFile(uint64_t, uint64_t, void *, size_t, size_t &bytesRead) override {
        bytesRead = 0u;
        return false;
    }
    void closeHostFile(uint64_t) override {}
    int32_t memoryCard(const ps2x::iop::MemoryCardRequest &) override { return -1; }
    bool hasGuestFunction(uint32_t) const override { return false; }
    bool invokeGuestFunction(uint64_t, uint32_t, uint32_t, uint32_t,
                             uint32_t, uint32_t, uint32_t *) override {
        return false;
    }
    void log(ps2x::iop::LogLevel, std::string_view) override {}

private:
    std::string cdRoot_;
    uint32_t nextHandle_ = 1u;
};

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

extern "C" __attribute__((visibility("default")))
bool gowInitializePs2RuntimeCoreHeadless(
    PS2Runtime *runtime, char *message, size_t capacity) noexcept {
    if (message != nullptr && capacity != 0u) message[0] = '\0';
    if (runtime == nullptr) {
        writeMessage(message, capacity, "PS2Runtime pointer is null.");
        return false;
    }
    try {
        if (!runtime->initializeCoreHeadless()) {
            writeMessage(
                message, capacity,
                "PS2Runtime could not initialize its Android headless EE/IOP/VU core.");
            return false;
        }
        writeMessage(
            message, capacity,
            "OK: PS2 memory + EE/IOP/VU core initialized; GS presentation, audio and guest execution remain disabled.");
        return true;
    } catch (const std::exception &exception) {
        writeMessage(
            message, capacity,
            std::string("Android PS2 core initialization failed: ") + exception.what());
        return false;
    } catch (...) {
        writeMessage(message, capacity,
                     "Unknown Android PS2 core initialization failure.");
        return false;
    }
}

// Loads the user's already validated BOOT2 into PS2 memory only when a
// generated static AOT table is linked and the ELF entry point is covered.
// This function deliberately does not call PS2Runtime::run().
extern "C" bool gowRuntimeLoadElfAndCheckAot(
    PS2Runtime *runtime, const char *bootRelativePath,
    char *message, size_t capacity) noexcept {
    if (message != nullptr && capacity != 0u) message[0] = '\0';
    try {
        if (runtime == nullptr) {
            writeMessage(message, capacity, "PS2Runtime não foi criado.");
            return false;
        }
        if (bootRelativePath == nullptr || bootRelativePath[0] == '\0') {
            writeMessage(message, capacity, "O caminho BOOT2 está vazio.");
            return false;
        }
        if (g_ps2RecompiledFunctionTableSlotCount == 0u ||
            g_ps2RecompiledFunctionTable == nullptr) {
            writeMessage(
                message, capacity,
                "A tabela AOT do jogo está vazia. Este APK ainda não contém C++ recompilado do ELF privado.");
            return false;
        }

        std::array<char, 4096> privateRoot{};
        if (!gowAndroidCdRootCopyPath(privateRoot.data(), privateRoot.size())) {
            writeMessage(message, capacity, "A pasta privada cdRoot não está montada.");
            return false;
        }

        std::filesystem::path candidate;
        std::string pathError;
        if (!gow::disc::resolvePrivateBootElfPath(
                privateRoot.data(), bootRelativePath, candidate, pathError)) {
            writeMessage(
                message, capacity,
                pathError.empty()
                    ? "O caminho BOOT2 não pôde ser resolvido dentro da instalação privada."
                    : pathError);
            return false;
        }

        if (!runtime->loadELF(candidate.string())) {
            writeMessage(
                message, capacity,
                "PS2Runtime::loadELF rejeitou o BOOT2. Consulte o Logcat GowAndroid para o detalhe do ELF.");
            return false;
        }

        const uint32_t entryPoint = runtime->cpu().pc;
        if (!runtime->hasFunction(entryPoint)) {
            char summary[256]{};
            std::snprintf(
                summary, sizeof(summary),
                "ELF carregado, mas a tabela AOT não contém a função de entrada 0x%08X. O jogo não será executado.",
                entryPoint);
            writeMessage(message, capacity, summary);
            return false;
        }

        char summary[256]{};
        std::snprintf(
            summary, sizeof(summary),
            "OK: ELF carregado · entry=0x%08X · tabela AOT=%u slots · guest execution ainda bloqueada.",
            entryPoint, g_ps2RecompiledFunctionTableSlotCount);
        writeMessage(message, capacity, summary);
        return true;
    } catch (const std::exception &exception) {
        writeMessage(
            message, capacity,
            std::string("Falha ao carregar/verificar ELF AOT: ") + exception.what());
        return false;
    } catch (...) {
        writeMessage(message, capacity,
                     "Falha desconhecida ao carregar/verificar ELF AOT.");
        return false;
    }
}

// Starts the already validated static AOT guest on a dedicated Android worker.
// This is an explicit first-boot diagnostic path; audio output is intentionally
// silent until an Android audio backend is connected. It never calls run().
extern "C" bool gowRuntimeStartGuestExecutionAndroid(
    PS2Runtime *runtime, char *message, size_t capacity) noexcept {
    if (message != nullptr && capacity != 0u) message[0] = '\0';
    if (runtime == nullptr) {
        writeMessage(message, capacity, "PS2Runtime não foi criado.");
        return false;
    }
    try {
        if (!runtime->startGuestExecutionAndroid()) {
            writeMessage(
                message, capacity,
                "O worker EE Android não iniciou. Consulte o Logcat GowAndroid para o motivo.");
            return false;
        }
        writeMessage(
            message, capacity,
            "OK: worker EE AOT iniciado no Android; saída de áudio permanece silenciosa neste teste inicial.");
        return true;
    } catch (const std::exception &exception) {
        writeMessage(
            message, capacity,
            std::string("Falha ao iniciar o worker EE Android: ") + exception.what());
        return false;
    } catch (...) {
        writeMessage(message, capacity,
                     "Falha desconhecida ao iniciar o worker EE Android.");
        return false;
    }
}

extern "C" bool gowRuntimeGuestExecutionFinishedAndroid(
    PS2Runtime *runtime) noexcept {
    if (runtime == nullptr) return true;
    try {
        return runtime->isGuestExecutionFinishedAndroid();
    } catch (...) {
        return true;
    }
}

extern "C" bool gowRuntimeValidateIopCdvd(
    const char *privateRoot, const char *bootRelativePath,
    char *message, size_t capacity) noexcept {
    if (message != nullptr && capacity != 0u) message[0] = '\0';
    try {
        if (privateRoot == nullptr || privateRoot[0] == '\0' ||
            bootRelativePath == nullptr || bootRelativePath[0] == '\0') {
            writeMessage(message, capacity, "O caminho da instalação ou BOOT2 está vazio.");
            return false;
        }

        RuntimeCdvdProbeHost host(privateRoot);
        ps2x::iop::detail::IopMemory memory;
        ps2x::iop::detail::IopKernel kernel(memory);
        ps2x::iop::detail::IopCdvd cdvd(host, memory, kernel);

        std::string bootPath(bootRelativePath);
        std::replace(bootPath.begin(), bootPath.end(), '/', '\\');
        const std::string guestPath = "cdrom0:\\" + bootPath;
        constexpr uint32_t kGuestPathAddress = 0x6000u;
        constexpr uint32_t kSearchResultAddress = 0x7000u;
        constexpr uint32_t kReadBufferAddress = 0x8000u;
        constexpr size_t kSectorSize = ps2x::iop::DiscSource::kSectorSize;

        if (!memory.writeRam(
                kGuestPathAddress, guestPath.c_str(), guestPath.size() + 1u) ||
            !memory.zeroRam(kSearchResultAddress, 32u)) {
            writeMessage(message, capacity,
                         "Não foi possível preparar a RAM sintética do IOP para sceCdSearchFile.");
            return false;
        }

        ps2x::iop::detail::IopCpuState searchCpu{};
        searchCpu.gpr[4] = kSearchResultAddress;
        searchCpu.gpr[5] = kGuestPathAddress;
        if (!cdvd.dispatchImport(10u, searchCpu) || searchCpu.gpr[2] != 1u) {
            writeMessage(
                message, capacity,
                "IopCdvd::dispatchImport(sceCdSearchFile) não encontrou o BOOT2.");
            return false;
        }

        std::array<uint8_t, 32u> searchResult{};
        if (!memory.readRam(
                kSearchResultAddress, searchResult.data(), searchResult.size())) {
            writeMessage(message, capacity,
                         "Não foi possível ler o resultado de sceCdSearchFile.");
            return false;
        }
        auto readLe32 = [](const uint8_t *bytes) {
            return static_cast<uint32_t>(bytes[0]) |
                   (static_cast<uint32_t>(bytes[1]) << 8u) |
                   (static_cast<uint32_t>(bytes[2]) << 16u) |
                   (static_cast<uint32_t>(bytes[3]) << 24u);
        };
        const uint32_t bootLsn = readLe32(searchResult.data());
        const uint32_t bootSize = readLe32(searchResult.data() + 4u);
        if (bootSize == 0u) {
            writeMessage(message, capacity,
                         "sceCdSearchFile retornou um BOOT2 vazio.");
            return false;
        }

        std::array<uint8_t, kSectorSize> sector{};
        if (!memory.zeroRam(kReadBufferAddress, sector.size())) {
            writeMessage(message, capacity,
                         "Não foi possível preparar a RAM sintética do IOP para sceCdRead.");
            return false;
        }

        ps2x::iop::detail::IopCpuState pvdCpu{};
        pvdCpu.gpr[4] = 16u;
        pvdCpu.gpr[5] = 1u;
        pvdCpu.gpr[6] = kReadBufferAddress;
        if (!cdvd.dispatchImport(6u, pvdCpu) || pvdCpu.gpr[2] != 1u ||
            !memory.readRam(kReadBufferAddress, sector.data(), sector.size()) ||
            std::memcmp(sector.data() + 1u, "CD001", 5u) != 0) {
            writeMessage(
                message, capacity,
                "IopCdvd::dispatchImport(sceCdRead) falhou ao reconstruir o PVD no LSN 16.");
            return false;
        }

        if (!memory.zeroRam(kReadBufferAddress, sector.size())) {
            writeMessage(message, capacity,
                         "Não foi possível limpar a RAM antes da leitura do BOOT2.");
            return false;
        }
        ps2x::iop::detail::IopCpuState bootCpu{};
        bootCpu.gpr[4] = bootLsn;
        bootCpu.gpr[5] = 1u;
        bootCpu.gpr[6] = kReadBufferAddress;
        if (!cdvd.dispatchImport(6u, bootCpu) || bootCpu.gpr[2] != 1u ||
            !memory.readRam(kReadBufferAddress, sector.data(), sector.size()) ||
            sector[0] != 0x7Fu || sector[1] != 'E' ||
            sector[2] != 'L' || sector[3] != 'F') {
            writeMessage(
                message, capacity,
                "IopCdvd::dispatchImport(sceCdRead) não conseguiu ler a assinatura ELF do BOOT2.");
            return false;
        }

        char summary[320]{};
        std::snprintf(
            summary, sizeof(summary),
            "OK: IopCdvd sceCdSearchFile + sceCdRead · BOOT2 LSN=%u · %u bytes · PVD LSN=16",
            bootLsn, bootSize);
        writeMessage(message, capacity, summary);
        return true;
    } catch (const std::exception &exception) {
        writeMessage(
            message, capacity,
            std::string("Falha interna no probe CDVD do IOP: ") + exception.what());
        return false;
    } catch (...) {
        writeMessage(message, capacity,
                     "Falha desconhecida no probe CDVD do IOP.");
        return false;
    }
}

