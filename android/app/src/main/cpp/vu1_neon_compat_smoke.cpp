#include "../../../../../port/kiexter/vu1/ps2_vu1_neon_compat.h"

// Compile-time smoke coverage for the ARM64 VU1 SIMD compatibility layer.
// This function is intentionally not called by the game; the Android NDK must compile and link it.
extern "C" __attribute__((visibility("hidden"))) float gow_vu1_neon_compat_smoke(float *lanes)
{
    const __m128 a = _mm_loadu_ps(lanes);
    const __m128 shuffled = _mm_shuffle_ps(a, a, _MM_SHUFFLE(0, 3, 2, 1));
    const __m128 blended = _mm_blend_ps(a, shuffled, 0x5);
    const __m128 sum = _mm_add_ps(blended, _mm_set1_ps(0.25f));
    const __m128 product = _mm_mul_ps(sum, _mm_set1_ps(2.0f));
    const __m128 difference = _mm_sub_ps(product, a);
    const __m128 maximum = _mm_max_ps(difference, _mm_min_ps(a, shuffled));
    const __m128 greater = _mm_cmpge_ps(maximum, _mm_set1_ps(0.0f));
    const __m128 selected = _mm_blendv_ps(maximum, sum, greater);

    const __m128i bits = _mm_castps_si128(selected);
    const __m128i all = _mm_set1_epi32(-1);
    const __m128i masked = _mm_and_si128(bits, all);
    const __m128i equal = _mm_cmpeq_epi32(masked, _mm_setzero_si128());
    const __m128i greaterInt = _mm_cmpgt_epi32(masked, _mm_setzero_si128());
    const __m128i chosen = _mm_blendv_epi8(equal, greaterInt, equal);
    const __m128i converted = _mm_cvttps_epi32(selected);
    const __m128 roundTrip = _mm_cvtepi32_ps(converted);
    const int scalar = _mm_cvtsi128_si32(converted);
    const int floatMask = _mm_movemask_ps(selected);
    const int byteMask = _mm_movemask_epi8(chosen);

    const uint32_t saved = _mm_getcsr();
    _mm_setcsr((saved & ~0x6000u) | 0x6000u | 0x8040u);
    _mm_setcsr(saved);

    _mm_storeu_ps(lanes, roundTrip);
    _mm_store_ps(lanes, _mm_add_ps(roundTrip, _mm_castsi128_ps(_mm_set1_epi32(0))));
    return static_cast<float>(scalar ^ floatMask ^ byteMask);
}
