#include "ps2_elf_validator.h"

#include <array>
#include <limits>

namespace gow::disc {
namespace {

constexpr size_t kElf32HeaderSize = 52u;
constexpr size_t kElf32ProgramHeaderSize = 32u;
constexpr uint16_t kElfTypeExecutable = 2u;
constexpr uint16_t kElfMachineMips = 8u;
constexpr uint32_t kProgramTypeLoad = 1u;
constexpr uint16_t kMaxProgramHeaders = 4096u;
constexpr uint64_t kAddressSpaceEnd = uint64_t{1u} << 32u;

void setError(std::string *error, const char *message) {
    if (error != nullptr) *error = message;
}

uint16_t readU16Le(const uint8_t *bytes) noexcept {
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8u);
}

uint32_t readU32Le(const uint8_t *bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8u) |
           (static_cast<uint32_t>(bytes[2]) << 16u) |
           (static_cast<uint32_t>(bytes[3]) << 24u);
}

bool readExact(CdRootVfs &vfs, uint64_t handle, uint64_t offset,
               void *destination, size_t size) {
    size_t bytesRead = 0u;
    return vfs.readFile(handle, offset, destination, size, bytesRead) &&
           bytesRead == size;
}

class CdRootHandleGuard {
public:
    CdRootHandleGuard(CdRootVfs &vfs, uint64_t handle)
        : vfs_(vfs), handle_(handle) {}
    ~CdRootHandleGuard() {
        if (handle_ != 0u) vfs_.closeFile(handle_);
    }
    CdRootHandleGuard(const CdRootHandleGuard &) = delete;
    CdRootHandleGuard &operator=(const CdRootHandleGuard &) = delete;

private:
    CdRootVfs &vfs_;
    uint64_t handle_ = 0u;
};

} // namespace

bool validatePs2Elf(CdRootVfs &vfs, std::string_view path,
                    Ps2ElfInfo &info, std::string *error) {
    info = {};
    if (error != nullptr) error->clear();

    CdRootFile file{};
    if (!vfs.openFile(path, file, error)) {
        if (error != nullptr && error->empty()) {
            *error = "Could not open the boot ELF inside cdRoot.";
        }
        return false;
    }
    CdRootHandleGuard handleGuard(vfs, file.handle);

    if (file.size < kElf32HeaderSize) {
        setError(error, "The boot ELF is shorter than an ELF32 header.");
        return false;
    }

    std::array<uint8_t, kElf32HeaderSize> header{};
    if (!readExact(vfs, file.handle, 0u, header.data(), header.size())) {
        setError(error, "Could not read the complete boot ELF header.");
        return false;
    }

    constexpr std::array<uint8_t, 4> kElfMagic{0x7Fu, 'E', 'L', 'F'};
    if (!std::equal(kElfMagic.begin(), kElfMagic.end(), header.begin())) {
        setError(error, "The BOOT2 file has invalid ELF magic.");
        return false;
    }
    if (header[4] != 1u) {
        setError(error, "The boot executable is not a 32-bit ELF.");
        return false;
    }
    if (header[5] != 1u) {
        setError(error, "The boot executable is not little-endian.");
        return false;
    }
    if (header[6] != 1u) {
        setError(error, "The boot ELF identification version is unsupported.");
        return false;
    }

    const uint16_t elfType = readU16Le(header.data() + 16u);
    const uint16_t machine = readU16Le(header.data() + 18u);
    const uint32_t version = readU32Le(header.data() + 20u);
    const uint32_t entryPoint = readU32Le(header.data() + 24u);
    const uint32_t programHeaderOffset = readU32Le(header.data() + 28u);
    const uint16_t headerSize = readU16Le(header.data() + 40u);
    const uint16_t programHeaderEntrySize = readU16Le(header.data() + 42u);
    const uint16_t programHeaderCount = readU16Le(header.data() + 44u);

    if (elfType != kElfTypeExecutable) {
        setError(error, "The boot ELF is not ET_EXEC.");
        return false;
    }
    if (machine != kElfMachineMips) {
        setError(error, "The boot ELF machine is not MIPS.");
        return false;
    }
    if (version != 1u) {
        setError(error, "The boot ELF header version is unsupported.");
        return false;
    }
    if (headerSize < kElf32HeaderSize || headerSize > file.size) {
        setError(error, "The boot ELF header size is invalid.");
        return false;
    }
    if (programHeaderCount == 0u || programHeaderCount > kMaxProgramHeaders) {
        setError(error, "The boot ELF program-header count is empty or exceeds the safety limit.");
        return false;
    }
    if (programHeaderEntrySize != kElf32ProgramHeaderSize) {
        setError(error, "The boot ELF program-header entry size is unsupported.");
        return false;
    }

    const uint64_t tableSize =
        static_cast<uint64_t>(programHeaderCount) * programHeaderEntrySize;
    const uint64_t tableEnd =
        static_cast<uint64_t>(programHeaderOffset) + tableSize;
    if (programHeaderOffset < headerSize || tableEnd > file.size) {
        setError(error, "The boot ELF program-header table is outside the file.");
        return false;
    }

    size_t loadableSegments = 0u;
    bool entryPointIsLoadable = false;
    std::array<uint8_t, kElf32ProgramHeaderSize> programHeader{};

    for (uint16_t index = 0u; index < programHeaderCount; ++index) {
        const uint64_t offset =
            static_cast<uint64_t>(programHeaderOffset) +
            static_cast<uint64_t>(index) * programHeaderEntrySize;
        if (!readExact(vfs, file.handle, offset,
                       programHeader.data(), programHeader.size())) {
            setError(error, "Could not read a complete boot ELF program header.");
            return false;
        }

        const uint32_t type = readU32Le(programHeader.data());
        if (type != kProgramTypeLoad) continue;

        const uint32_t fileOffset = readU32Le(programHeader.data() + 4u);
        const uint32_t virtualAddress = readU32Le(programHeader.data() + 8u);
        const uint32_t fileBytes = readU32Le(programHeader.data() + 16u);
        const uint32_t memoryBytes = readU32Le(programHeader.data() + 20u);
        if (memoryBytes == 0u) continue;
        if (fileBytes > memoryBytes) {
            setError(error, "A boot ELF PT_LOAD segment has p_filesz greater than p_memsz.");
            return false;
        }
        if (static_cast<uint64_t>(fileOffset) + fileBytes > file.size) {
            setError(error, "A boot ELF PT_LOAD segment exceeds the file bounds.");
            return false;
        }

        const uint64_t memoryEnd =
            static_cast<uint64_t>(virtualAddress) + memoryBytes;
        if (memoryEnd > kAddressSpaceEnd) {
            setError(error, "A boot ELF PT_LOAD segment overflows the 32-bit address space.");
            return false;
        }

        ++loadableSegments;
        if (virtualAddress <= entryPoint &&
            static_cast<uint64_t>(entryPoint) < memoryEnd) {
            entryPointIsLoadable = true;
        }
    }

    if (loadableSegments == 0u) {
        setError(error, "The boot ELF has no non-empty PT_LOAD segments.");
        return false;
    }
    if (!entryPointIsLoadable) {
        setError(error, "The boot ELF entry point is not covered by a PT_LOAD segment.");
        return false;
    }

    info.fileSize = file.size;
    info.entryPoint = entryPoint;
    info.programHeaderCount = programHeaderCount;
    info.loadableSegmentCount = loadableSegments;
    return true;
}

} // namespace gow::disc
