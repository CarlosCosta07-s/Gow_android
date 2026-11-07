#pragma once

#include <cstdint>

namespace gow::android {

// NTSC PS2 VBlank budget, matching the Android performance-hint target used
// by Dragon Rage. A frame can cover more than one blank when the game presents
// at 30 Hz or when the host has missed a presentation tick.
constexpr uint64_t kPs2NtscVblankBudgetNs = 16'683'350ull;

// Keep scheduler targets within one to four NTSC blanks. Large pauses/loads
// must not create an unbounded target duration.
uint64_t performanceHintTargetDurationNs(uint64_t vsyncTickDelta) noexcept;

// Report work performed by the Android host/presentation thread. On Android
// 13+ this uses ADPF when the system exports the APIs; older devices are a no-op.
// It never enables a sustained-performance mode or pins CPU frequencies.
void reportPerformanceHintFrame(
    uint64_t actualWorkDurationNs,
    uint64_t targetWorkDurationNs) noexcept;

} // namespace gow::android
