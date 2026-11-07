#pragma once

#include "runtime/ps2_memory.h"

#include <cstdint>

struct GSDisplayState
{
    uint64_t pmode = 0u;
    uint64_t smode2 = 0u;
    uint64_t dispfb1 = 0u;
    uint64_t display1 = 0u;
    uint64_t dispfb2 = 0u;
    uint64_t display2 = 0u;
    uint64_t bgcolor = 0u;
};

// Android-side equivalent of the small GS display helper used by the PC port.
// Keep this limited to privileged display/status registers; the actual GS draw
// stream continues through the Android raster backend and Vulkan presenter.
namespace ps2_gs_display
{
inline GSDisplayState display(const PS2Memory &memory)
{
    const GSRegisters &regs = memory.gs();
    GSDisplayState state{};
    state.pmode = regs.pmode;
    state.smode2 = regs.smode2;
    state.dispfb1 = regs.dispfb1;
    state.display1 = regs.display1;
    state.dispfb2 = regs.dispfb2;
    state.display2 = regs.display2;
    state.bgcolor = regs.bgcolor;
    return state;
}

inline void setDisplay(PS2Memory &memory, const GSDisplayState &state)
{
    // PS2 GS privileged registers: PMODE .. BGCOLOR, on 16-byte boundaries.
    // write64 routes these addresses through PS2Memory's GS register path.
    memory.write64(PS2_GS_PRIV_REG_BASE + 0x00u, state.pmode);
    memory.write64(PS2_GS_PRIV_REG_BASE + 0x20u, state.smode2);
    memory.write64(PS2_GS_PRIV_REG_BASE + 0x70u, state.dispfb1);
    memory.write64(PS2_GS_PRIV_REG_BASE + 0x80u, state.display1);
    memory.write64(PS2_GS_PRIV_REG_BASE + 0x90u, state.dispfb2);
    memory.write64(PS2_GS_PRIV_REG_BASE + 0xA0u, state.display2);
    memory.write64(PS2_GS_PRIV_REG_BASE + 0xE0u, state.bgcolor);
}

inline bool finishPending(const PS2Memory &memory)
{
    constexpr uint64_t kFinish = 1ull << 1u;
    return (memory.gs().csr.load(std::memory_order_acquire) & kFinish) != 0u;
}

inline void ackFinish(PS2Memory &memory)
{
    constexpr uint64_t kFinish = 1ull << 1u;
    // CSR FINISH is write-one-to-clear; PS2Memory::write64 preserves the
    // other CSR status bits through its atomic W1C implementation.
    memory.write64(PS2_GS_PRIV_REG_BASE + 0x1000u, kFinish);
}
} // namespace ps2_gs_display
