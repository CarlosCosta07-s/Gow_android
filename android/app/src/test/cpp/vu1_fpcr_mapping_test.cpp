#include "ps2_vu1_fpcr_mapping.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        // SSE MXCSR controls map to ARM FPCR modes as 0->0, 1->2, 2->1, 3->3.
        constexpr std::array<uint32_t, 4> expectedArmModes{0u, 2u, 1u, 3u};
        for (uint32_t mxcsrControl = 0u; mxcsrControl < 4u; ++mxcsrControl) {
            const uint32_t armMode =
                gow_vu1_neon::mxcsrToArmFpcrRMode(mxcsrControl);
            require(armMode == expectedArmModes[mxcsrControl],
                    "MXCSR rounding mode mapped to the wrong ARM FPCR mode.");

            const uint32_t roundTrip =
                gow_vu1_neon::armFpcrRModeToMxcsr(armMode) >> 13u;
            require(roundTrip == mxcsrControl,
                    "ARM FPCR rounding mode did not round-trip to MXCSR.");

            require(
                gow_vu1_neon::armFpcrRModeToMxcsr(armMode) ==
                    (mxcsrControl << 13u),
                "MXCSR rounding control was placed in the wrong bit positions.");
        }

        require(
            gow_vu1_neon::mxcsrToArmFpcrRMode(0xFFFFFFFFu) == 3u,
            "MXCSR mapping must ignore bits outside the rounding-control field.");
        require(
            gow_vu1_neon::armFpcrRModeToMxcsr(0xFFFFFFFFu) == (3u << 13u),
            "FPCR mapping must ignore bits outside the rounding-mode field.");

        std::cout << "VU1 MXCSR/FPCR rounding-mode mapping tests passed.\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "VU1 MXCSR/FPCR mapping test failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
