#pragma once

#include "cdroot_vfs.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gow::disc {

struct Ps2ElfInfo {
    uint64_t fileSize = 0u;
    uint32_t entryPoint = 0u;
    uint16_t programHeaderCount = 0u;
    size_t loadableSegmentCount = 0u;
};

// Validates an ELF32 little-endian MIPS ET_EXEC stored in the private cdRoot.
// Reads only the ELF header and program-header table; segment payloads are not
// loaded into memory. This verifies file structure, not game revision identity.
bool validatePs2Elf(CdRootVfs &vfs, std::string_view path,
                    Ps2ElfInfo &info, std::string *error = nullptr);

} // namespace gow::disc
