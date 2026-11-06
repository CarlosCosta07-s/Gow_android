#pragma once

#include <cstdint>

namespace gow_vu1_neon {

// MXCSR.RC: 00 nearest, 01 down, 10 up, 11 toward zero.
// FPCR.RMode: 00 nearest, 01 up, 10 down, 11 toward zero.
// The downward and upward encodings therefore need their low bits swapped.
constexpr uint32_t mxcsrToArmFpcrRMode(uint32_t mxcsrRoundControl) noexcept {
    const uint32_t control = mxcsrRoundControl & 0x3u;
    return ((control & 0x1u) << 1u) | ((control & 0x2u) >> 1u);
}

constexpr uint32_t armFpcrRModeToMxcsr(uint32_t armRoundingMode) noexcept {
    const uint32_t control = armRoundingMode & 0x3u;
    const uint32_t mxcsrControl =
        ((control & 0x1u) << 1u) | ((control & 0x2u) >> 1u);
    return mxcsrControl << 13u;
}

} // namespace gow_vu1_neon
