#pragma once

#include "ps2_vu1_fpcr_mapping.h"

#if defined(__aarch64__) && defined(__ARM_NEON) && defined(USE_SSE2NEON)
#include <arm_neon.h>
#include <sse2neon.h>
#include <cstdint>

// The Waffle runtime already supplies the broad SSE2 API through sse2neon.
// This layer only overrides the semantics that matter to VU1: SSE MAX/MIN
// operand selection and the MXCSR subset used for rounding/flush control.
namespace gow_vu1_neon
{
    inline __m128 max_ps(__m128 a, __m128 b) noexcept
    {
        return vbslq_f32(vcgtq_f32(a, b), a, b);
    }

    inline __m128 min_ps(__m128 a, __m128 b) noexcept
    {
        return vbslq_f32(vcltq_f32(a, b), a, b);
    }

    inline uint32_t getcsr() noexcept
    {
        uint64_t fpcr = 0;
        uint64_t fpsr = 0;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        asm volatile("mrs %0, fpsr" : "=r"(fpsr));

        uint32_t value = 0x1F80u;
        value |= armFpcrRModeToMxcsr(static_cast<uint32_t>((fpcr >> 22u) & 3u));
        if ((fpcr & (1ull << 24u)) != 0u)
            value |= 0x8040u;
        if ((fpsr & (1ull << 0u)) != 0u) value |= 1u << 0u;
        if ((fpsr & (1ull << 7u)) != 0u) value |= 1u << 1u;
        if ((fpsr & (1ull << 1u)) != 0u) value |= 1u << 2u;
        if ((fpsr & (1ull << 2u)) != 0u) value |= 1u << 3u;
        if ((fpsr & (1ull << 3u)) != 0u) value |= 1u << 4u;
        if ((fpsr & (1ull << 4u)) != 0u) value |= 1u << 5u;
        return value;
    }

    inline void setcsr(uint32_t value) noexcept
    {
        uint64_t fpcr = 0;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        fpcr &= ~((3ull << 22u) | (1ull << 24u));
        fpcr |= static_cast<uint64_t>(mxcsrToArmFpcrRMode((value >> 13u) & 3u)) << 22u;
        if ((value & (0x8000u | 0x0040u)) != 0u)
            fpcr |= 1ull << 24u;
        asm volatile("msr fpcr, %0" : : "r"(fpcr) : "memory");

        uint64_t fpsr = 0;
        asm volatile("mrs %0, fpsr" : "=r"(fpsr));
        fpsr &= ~((1ull << 0u) | (1ull << 1u) | (1ull << 2u) |
                  (1ull << 3u) | (1ull << 4u) | (1ull << 7u));
        if ((value & (1u << 0u)) != 0u) fpsr |= 1ull << 0u;
        if ((value & (1u << 1u)) != 0u) fpsr |= 1ull << 7u;
        if ((value & (1u << 2u)) != 0u) fpsr |= 1ull << 1u;
        if ((value & (1u << 3u)) != 0u) fpsr |= 1ull << 2u;
        if ((value & (1u << 4u)) != 0u) fpsr |= 1ull << 3u;
        if ((value & (1u << 5u)) != 0u) fpsr |= 1ull << 4u;
        asm volatile("msr fpsr, %0" : : "r"(fpsr) : "memory");
    }
}

#ifdef _mm_max_ps
#undef _mm_max_ps
#endif
#ifdef _mm_min_ps
#undef _mm_min_ps
#endif
#ifdef _mm_getcsr
#undef _mm_getcsr
#endif
#ifdef _mm_setcsr
#undef _mm_setcsr
#endif
#define _mm_max_ps(a, b) ::gow_vu1_neon::max_ps((a), (b))
#define _mm_min_ps(a, b) ::gow_vu1_neon::min_ps((a), (b))
#define _mm_getcsr() ::gow_vu1_neon::getcsr()
#define _mm_setcsr(value) ::gow_vu1_neon::setcsr((value))

#else
// ARM64 compatibility layer for the SSE-style vector API used by Waffle's native VU1 blocks.
// The implementation is deliberately limited to the intrinsics actually used by
// ps2_vu1_native.h and vu1n_gen.py. Keep semantics aligned with SSE, especially lane masks,
// NaN handling, signed zero, truncating conversion, and the VU1 rounding-mode guard.

#if !defined(__aarch64__) || !defined(__ARM_NEON)
#error "ps2_vu1_neon_compat.h is only for Android ARM64/NEON builds."
#endif

#include <arm_neon.h>
#include <cstdint>
#include <cstring>

using __m128 = float32x4_t;
using __m128i = int32x4_t;

#ifndef _MM_SHUFFLE
#define _MM_SHUFFLE(z, y, x, w) (((z) << 6) | ((y) << 4) | ((x) << 2) | (w))
#endif

namespace gow_vu1_neon
{
    inline __m128 add_ps(__m128 a, __m128 b) noexcept { return vaddq_f32(a, b); }
    inline __m128 sub_ps(__m128 a, __m128 b) noexcept { return vsubq_f32(a, b); }
    inline __m128 mul_ps(__m128 a, __m128 b) noexcept { return vmulq_f32(a, b); }
    inline __m128 set1_ps(float value) noexcept { return vdupq_n_f32(value); }
    inline __m128i set1_epi32(int value) noexcept { return vdupq_n_s32(value); }
    inline __m128i setzero_si128() noexcept { return vdupq_n_s32(0); }

    inline __m128 load_ps(const float *p) noexcept { return vld1q_f32(p); }
    inline __m128 loadu_ps(const float *p) noexcept { return vld1q_f32(p); }
    inline void store_ps(float *p, __m128 v) noexcept { vst1q_f32(p, v); }
    inline void storeu_ps(float *p, __m128 v) noexcept { vst1q_f32(p, v); }

    inline __m128 castsi128_ps(__m128i v) noexcept { return vreinterpretq_f32_s32(v); }
    inline __m128i castps_si128(__m128 v) noexcept { return vreinterpretq_s32_f32(v); }

    inline __m128 and_ps(__m128 a, __m128 b) noexcept
    {
        return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
    }

    inline __m128i and_si128(__m128i a, __m128i b) noexcept
    {
        return vreinterpretq_s32_u32(vandq_u32(vreinterpretq_u32_s32(a), vreinterpretq_u32_s32(b)));
    }

    inline __m128i or_si128(__m128i a, __m128i b) noexcept
    {
        return vreinterpretq_s32_u32(vorrq_u32(vreinterpretq_u32_s32(a), vreinterpretq_u32_s32(b)));
    }

    inline __m128 cmpeq_ps(__m128 a, __m128 b) noexcept
    {
        return vreinterpretq_f32_u32(vceqq_f32(a, b));
    }

    inline __m128 cmpge_ps(__m128 a, __m128 b) noexcept
    {
        return vreinterpretq_f32_u32(vcgeq_f32(a, b));
    }

    inline __m128i cmpeq_epi32(__m128i a, __m128i b) noexcept
    {
        return vreinterpretq_s32_u32(vceqq_s32(a, b));
    }

    inline __m128i cmpgt_epi32(__m128i a, __m128i b) noexcept
    {
        return vreinterpretq_s32_u32(vcgtq_s32(a, b));
    }

    inline __m128 cvtepi32_ps(__m128i a) noexcept { return vcvtq_f32_s32(a); }
    inline __m128i cvttps_epi32(__m128 a) noexcept { return vcvtq_s32_f32(a); }
    inline int cvtsi128_si32(__m128i a) noexcept { return vgetq_lane_s32(a, 0); }

    // SSE MAXPS/MINPS select the second operand for equal values and unordered comparisons.
    // Using a strict comparison plus bit-select preserves those rules, including signed zero.
    inline __m128 max_ps(__m128 a, __m128 b) noexcept
    {
        return vbslq_f32(vcgtq_f32(a, b), a, b);
    }

    inline __m128 min_ps(__m128 a, __m128 b) noexcept
    {
        return vbslq_f32(vcltq_f32(a, b), a, b);
    }

    // BLENDVPS/BLENDV_epi8 use only each element's most significant bit as the selector.
    inline __m128 blendv_ps(__m128 a, __m128 b, __m128 mask) noexcept
    {
        const uint32x4_t sign = vshrq_n_u32(vreinterpretq_u32_f32(mask), 31);
        const uint32x4_t full = vsubq_u32(vdupq_n_u32(0), sign);
        return vbslq_f32(full, b, a);
    }

    inline __m128i blendv_epi8(__m128i a, __m128i b, __m128i mask) noexcept
    {
        const uint8x16_t bytes = vreinterpretq_u8_s32(mask);
        const uint8x16_t sign = vshrq_n_u8(bytes, 7);
        const uint8x16_t full = vsubq_u8(vdupq_n_u8(0), sign);
        const uint8x16_t selected = vbslq_u8(
            full, vreinterpretq_u8_s32(b), vreinterpretq_u8_s32(a));
        return vreinterpretq_s32_u8(selected);
    }

    template <int Immediate>
    inline __m128 shuffle_ps(__m128 a, __m128 b) noexcept
    {
        static_assert(Immediate >= 0 && Immediate <= 255);
        const uint32x4_t av = vreinterpretq_u32_f32(a);
        const uint32x4_t bv = vreinterpretq_u32_f32(b);
        uint32x4_t out = vdupq_n_u32(0);
        out = vsetq_lane_u32(vgetq_lane_u32(av, (Immediate >> 0) & 3), out, 0);
        out = vsetq_lane_u32(vgetq_lane_u32(av, (Immediate >> 2) & 3), out, 1);
        out = vsetq_lane_u32(vgetq_lane_u32(bv, (Immediate >> 4) & 3), out, 2);
        out = vsetq_lane_u32(vgetq_lane_u32(bv, (Immediate >> 6) & 3), out, 3);
        return vreinterpretq_f32_u32(out);
    }

    template <int Mask>
    inline __m128 blend_ps(__m128 a, __m128 b) noexcept
    {
        static_assert(Mask >= 0 && Mask <= 15);
        uint32x4_t mask = vdupq_n_u32(0);
        mask = vsetq_lane_u32((Mask & 1) ? 0xFFFFFFFFu : 0u, mask, 0);
        mask = vsetq_lane_u32((Mask & 2) ? 0xFFFFFFFFu : 0u, mask, 1);
        mask = vsetq_lane_u32((Mask & 4) ? 0xFFFFFFFFu : 0u, mask, 2);
        mask = vsetq_lane_u32((Mask & 8) ? 0xFFFFFFFFu : 0u, mask, 3);
        return vbslq_f32(mask, b, a);
    }

    inline int movemask_ps(__m128 value) noexcept
    {
        const uint32x4_t bits = vshrq_n_u32(vreinterpretq_u32_f32(value), 31);
        alignas(16) uint32_t lanes[4];
        vst1q_u32(lanes, bits);
        return static_cast<int>((lanes[0] & 1u) |
                                ((lanes[1] & 1u) << 1u) |
                                ((lanes[2] & 1u) << 2u) |
                                ((lanes[3] & 1u) << 3u));
    }

    inline int movemask_epi8(__m128i value) noexcept
    {
        const uint8x16_t bits = vshrq_n_u8(vreinterpretq_u8_s32(value), 7);
        alignas(16) uint8_t lanes[16];
        vst1q_u8(lanes, bits);
        int result = 0;
        for (unsigned i = 0; i < 16; ++i)
            result |= static_cast<int>(lanes[i] & 1u) << i;
        return result;
    }

    // Emulate the MXCSR subset used by the VU1 native engine with ARM FPCR/FPSR.
    // SSE rounding-control bits 13-14 map directly to FPCR.RMode bits 22-23.
    inline uint32_t getcsr() noexcept
    {
        uint64_t fpcr = 0;
        uint64_t fpsr = 0;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        asm volatile("mrs %0, fpsr" : "=r"(fpsr));

        uint32_t value = 0x1F80u; // SSE exception masks: all masked, as in the default host state.
        value |= armFpcrRModeToMxcsr(static_cast<uint32_t>((fpcr >> 22u) & 3u));
        if ((fpcr & (1ull << 24u)) != 0u)
            value |= 0x8040u; // FTZ + DAZ share ARM's flush-to-zero control.

        if ((fpsr & (1ull << 0u)) != 0u) value |= 1u << 0u; // IOC -> IE
        if ((fpsr & (1ull << 7u)) != 0u) value |= 1u << 1u; // IDC -> DE
        if ((fpsr & (1ull << 1u)) != 0u) value |= 1u << 2u; // DZC -> ZE
        if ((fpsr & (1ull << 2u)) != 0u) value |= 1u << 3u; // OFC -> OE
        if ((fpsr & (1ull << 3u)) != 0u) value |= 1u << 4u; // UFC -> UE
        if ((fpsr & (1ull << 4u)) != 0u) value |= 1u << 5u; // IXC -> PE
        return value;
    }

    inline void setcsr(uint32_t value) noexcept
    {
        uint64_t fpcr = 0;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        fpcr &= ~((3ull << 22u) | (1ull << 24u));
        fpcr |= static_cast<uint64_t>(mxcsrToArmFpcrRMode((value >> 13u) & 3u)) << 22u;
        if ((value & (0x8000u | 0x0040u)) != 0u)
            fpcr |= 1ull << 24u;
        asm volatile("msr fpcr, %0" : : "r"(fpcr) : "memory");

        uint64_t fpsr = 0;
        asm volatile("mrs %0, fpsr" : "=r"(fpsr));
        fpsr &= ~((1ull << 0u) | (1ull << 1u) | (1ull << 2u) |
                  (1ull << 3u) | (1ull << 4u) | (1ull << 7u));
        if ((value & (1u << 0u)) != 0u) fpsr |= 1ull << 0u; // IE -> IOC
        if ((value & (1u << 1u)) != 0u) fpsr |= 1ull << 7u; // DE -> IDC
        if ((value & (1u << 2u)) != 0u) fpsr |= 1ull << 1u; // ZE -> DZC
        if ((value & (1u << 3u)) != 0u) fpsr |= 1ull << 2u; // OE -> OFC
        if ((value & (1u << 4u)) != 0u) fpsr |= 1ull << 3u; // UE -> UFC
        if ((value & (1u << 5u)) != 0u) fpsr |= 1ull << 4u; // PE -> IXC
        asm volatile("msr fpsr, %0" : : "r"(fpsr) : "memory");
    }
}

// Keep the upstream source and generated C++ unchanged at their call sites.
#define _mm_add_ps(a, b) ::gow_vu1_neon::add_ps((a), (b))
#define _mm_sub_ps(a, b) ::gow_vu1_neon::sub_ps((a), (b))
#define _mm_mul_ps(a, b) ::gow_vu1_neon::mul_ps((a), (b))
#define _mm_set1_ps(value) ::gow_vu1_neon::set1_ps((value))
#define _mm_set1_epi32(value) ::gow_vu1_neon::set1_epi32((value))
#define _mm_setzero_si128() ::gow_vu1_neon::setzero_si128()
#define _mm_load_ps(p) ::gow_vu1_neon::load_ps((p))
#define _mm_loadu_ps(p) ::gow_vu1_neon::loadu_ps((p))
#define _mm_store_ps(p, v) ::gow_vu1_neon::store_ps((p), (v))
#define _mm_storeu_ps(p, v) ::gow_vu1_neon::storeu_ps((p), (v))
#define _mm_castps_si128(v) ::gow_vu1_neon::castps_si128((v))
#define _mm_castsi128_ps(v) ::gow_vu1_neon::castsi128_ps((v))
#define _mm_and_ps(a, b) ::gow_vu1_neon::and_ps((a), (b))
#define _mm_and_si128(a, b) ::gow_vu1_neon::and_si128((a), (b))
#define _mm_or_si128(a, b) ::gow_vu1_neon::or_si128((a), (b))
#define _mm_cmpeq_epi32(a, b) ::gow_vu1_neon::cmpeq_epi32((a), (b))
#define _mm_cmpgt_epi32(a, b) ::gow_vu1_neon::cmpgt_epi32((a), (b))
#define _mm_cmpge_ps(a, b) ::gow_vu1_neon::cmpge_ps((a), (b))
#define _mm_cvtepi32_ps(a) ::gow_vu1_neon::cvtepi32_ps((a))
#define _mm_cvttps_epi32(a) ::gow_vu1_neon::cvttps_epi32((a))
#define _mm_cvtsi128_si32(a) ::gow_vu1_neon::cvtsi128_si32((a))
#define _mm_max_ps(a, b) ::gow_vu1_neon::max_ps((a), (b))
#define _mm_min_ps(a, b) ::gow_vu1_neon::min_ps((a), (b))
#define _mm_blendv_ps(a, b, m) ::gow_vu1_neon::blendv_ps((a), (b), (m))
#define _mm_blendv_epi8(a, b, m) ::gow_vu1_neon::blendv_epi8((a), (b), (m))
#define _mm_movemask_ps(a) ::gow_vu1_neon::movemask_ps((a))
#define _mm_movemask_epi8(a) ::gow_vu1_neon::movemask_epi8((a))
#define _mm_shuffle_ps(a, b, imm) ::gow_vu1_neon::shuffle_ps<(imm)>((a), (b))
#define _mm_blend_ps(a, b, imm) ::gow_vu1_neon::blend_ps<(imm)>((a), (b))
#define _mm_getcsr() ::gow_vu1_neon::getcsr()
#define _mm_setcsr(value) ::gow_vu1_neon::setcsr((value))

#endif // ARM64 sse2neon branch
