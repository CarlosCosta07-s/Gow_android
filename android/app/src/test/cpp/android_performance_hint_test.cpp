#include "android_performance_hint.h"

#include <cstdint>
#include <iostream>

int main() {
    using gow::android::kPs2NtscVblankBudgetNs;
    using gow::android::performanceHintTargetDurationNs;
    using gow::android::reportPerformanceHintFrame;

    if (performanceHintTargetDurationNs(0u) != kPs2NtscVblankBudgetNs) {
        std::cerr << "zero VSync delta must use one bounded NTSC blank\n";
        return 1;
    }
    if (performanceHintTargetDurationNs(1u) != 16'683'350ull ||
        performanceHintTargetDurationNs(2u) != 33'366'700ull ||
        performanceHintTargetDurationNs(3u) != 50'050'050ull ||
        performanceHintTargetDurationNs(4u) != 66'733'400ull) {
        std::cerr << "VSync-to-ADPF target conversion is incorrect\n";
        return 1;
    }
    if (performanceHintTargetDurationNs(100u) != 66'733'400ull) {
        std::cerr << "large VSync gaps must be bounded to four blanks\n";
        return 1;
    }

    // On host platforms ADPF is intentionally a no-op; this also verifies the
    // shared API remains safe to link into host-side diagnostic tests.
    reportPerformanceHintFrame(10'000'000ull, 16'683'350ull);
    reportPerformanceHintFrame(0u, 16'683'350ull);
    reportPerformanceHintFrame(10'000'000ull, 0u);

    std::cout << "Android performance-hint timing tests passed\n";
    return 0;
}
