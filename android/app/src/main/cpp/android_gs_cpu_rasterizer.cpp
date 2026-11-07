#include "android_gs_cpu_rasterizer.h"

#include "gs_gpu_helpers.h"
#include "gs_texture_store.h"
#include "runtime/gs/ps2_gs_common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gow::android {
namespace {

struct RasterVertex {
    float x = 0.0f;
    float y = 0.0f;
    double z = 0.0;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    float fog = 0.0f;
    float s = 0.0f;
    float t = 0.0f;
    float q = 1.0f;
    float u = 0.0f;
    float v = 0.0f;
};

using Color = std::array<float, 4>;

float channel(uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

float clamp255(float value) {
    return std::clamp(std::floor(value), 0.0f, 255.0f);
}

uint8_t toByte(float value) {
    return static_cast<uint8_t>(std::lround(
        std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

uint32_t packRgba(const RasterVertex &vertex) {
    return static_cast<uint32_t>(toByte(vertex.r)) |
        (static_cast<uint32_t>(toByte(vertex.g)) << 8u) |
        (static_cast<uint32_t>(toByte(vertex.b)) << 16u) |
        (static_cast<uint32_t>(toByte(vertex.a)) << 24u);
}

bool supportedFramebufferPsm(uint8_t psm) {
    return psm == GS_PSM_CT32 || psm == GS_PSM_CT24 ||
        psm == GS_PSM_CT16 || psm == GS_PSM_CT16S;
}

uint32_t packForPsm(uint8_t psm, uint32_t rgba) {
    if (psm == GS_PSM_CT16 || psm == GS_PSM_CT16S) {
        return GSCpuInternal::Rgba8888ToRgba5551(rgba);
    }
    if (psm == GS_PSM_CT24) return rgba & 0x00FFFFFFu;
    return rgba;
}

bool writeFramebufferPixel(
    GSTextureStore &store,
    const GSContext &context,
    uint32_t x,
    uint32_t y,
    uint32_t rgba,
    bool writeAlpha = true) {
    const uint8_t psm = context.frame.psm;
    uint32_t frameMask = context.frame.fbmsk;
    if (psm == GS_PSM_CT16 || psm == GS_PSM_CT16S) {
        frameMask = GSInternal::frameMask16(frameMask);
        if (!writeAlpha) frameMask |= 0x8000u;
    } else if (psm == GS_PSM_CT24) {
        frameMask &= 0x00FFFFFFu;
    } else if (!writeAlpha) {
        frameMask |= 0xFF000000u;
    }

    const uint32_t oldPixel = store.Read(
        psm, context.frame.fbp, context.frame.fbw, x, y);
    const uint32_t packed = packForPsm(psm, rgba);
    const uint32_t merged = (oldPixel & frameMask) | (packed & ~frameMask);
    store.Write(psm, context.frame.fbp, context.frame.fbw, x, y, merged);
    return true;
}

RasterVertex makeRasterVertex(
    const GSVertex &source,
    const GSVertex &color,
    const GSContext &context) {
    RasterVertex vertex{};
    vertex.x = source.x - static_cast<float>(context.xyoffset.ofx) / 16.0f;
    vertex.y = source.y - static_cast<float>(context.xyoffset.ofy) / 16.0f;
    vertex.z = source.z;
    vertex.r = channel(color.r);
    vertex.g = channel(color.g);
    vertex.b = channel(color.b);
    vertex.a = channel(color.a);
    vertex.fog = static_cast<float>(source.fog);
    vertex.s = source.s;
    vertex.t = source.t;
    vertex.q = std::isfinite(source.q) && std::fabs(source.q) > 1.0e-8f
        ? source.q : 1.0f;
    vertex.u = static_cast<float>(source.u) / 16.0f;
    vertex.v = static_cast<float>(source.v) / 16.0f;
    return vertex;
}

RasterVertex interpolate(
    const RasterVertex &a,
    const RasterVertex &b,
    float t) {
    RasterVertex result{};
    result.x = a.x + (b.x - a.x) * t;
    result.y = a.y + (b.y - a.y) * t;
    result.z = a.z + (b.z - a.z) * t;
    result.r = a.r + (b.r - a.r) * t;
    result.g = a.g + (b.g - a.g) * t;
    result.b = a.b + (b.b - a.b) * t;
    result.a = a.a + (b.a - a.a) * t;
    result.fog = a.fog + (b.fog - a.fog) * t;
    result.s = a.s + (b.s - a.s) * t;
    result.t = a.t + (b.t - a.t) * t;
    result.q = a.q + (b.q - a.q) * t;
    result.u = a.u + (b.u - a.u) * t;
    result.v = a.v + (b.v - a.v) * t;
    return result;
}

RasterVertex interpolateTriangle(
    const RasterVertex &a,
    const RasterVertex &b,
    const RasterVertex &c,
    float wa,
    float wb,
    float wc) {
    RasterVertex result{};
    result.z = a.z * wa + b.z * wb + c.z * wc;
    result.r = a.r * wa + b.r * wb + c.r * wc;
    result.g = a.g * wa + b.g * wb + c.g * wc;
    result.b = a.b * wa + b.b * wb + c.b * wc;
    result.a = a.a * wa + b.a * wb + c.a * wc;
    result.fog = a.fog * wa + b.fog * wb + c.fog * wc;
    result.s = a.s * wa + b.s * wb + c.s * wc;
    result.t = a.t * wa + b.t * wb + c.t * wc;
    result.q = a.q * wa + b.q * wb + c.q * wc;
    result.u = a.u * wa + b.u * wb + c.u * wc;
    result.v = a.v * wa + b.v * wb + c.v * wc;
    return result;
}

int wrappedCoordinate(
    int coordinate,
    int textureSize,
    uint8_t mode,
    uint16_t regionMin,
    uint16_t regionMax) {
    if (textureSize <= 0) return 0;
    return GSCpuInternal::wrapTextureCoordinate(
        coordinate, textureSize, mode, regionMin, regionMax);
}

Color loadTextureColor(
    const std::vector<uint32_t> &pixels,
    uint32_t width,
    uint32_t height,
    int x,
    int y,
    uint8_t wrapU,
    uint8_t wrapV,
    uint16_t minU,
    uint16_t maxU,
    uint16_t minV,
    uint16_t maxV) {
    const int wrappedX = wrappedCoordinate(
        x, static_cast<int>(width), wrapU, minU, maxU);
    const int wrappedY = wrappedCoordinate(
        y, static_cast<int>(height), wrapV, minV, maxV);
    const uint32_t pixel = pixels[
        static_cast<size_t>(wrappedY) * width +
        static_cast<size_t>(wrappedX)];
    return {
        static_cast<float>(pixel & 0xFFu),
        static_cast<float>((pixel >> 8u) & 0xFFu),
        static_cast<float>((pixel >> 16u) & 0xFFu),
        static_cast<float>((pixel >> 24u) & 0xFFu)};
}

Color sampleTexture(
    const GSDrawState &state,
    const RasterVertex &vertex,
    const std::vector<uint32_t> &pixels) {
    const uint32_t width = std::max<uint32_t>(1u, state.textureWidth);
    const uint32_t height = std::max<uint32_t>(1u, state.textureHeight);
    const uint64_t clamp = state.context.clamp;
    const uint8_t wrapU = static_cast<uint8_t>(clamp & 0x3u);
    const uint8_t wrapV = static_cast<uint8_t>((clamp >> 2u) & 0x3u);
    const uint16_t minU = static_cast<uint16_t>((clamp >> 4u) & 0x3FFu);
    const uint16_t maxU = static_cast<uint16_t>((clamp >> 14u) & 0x3FFu);
    const uint16_t minV = static_cast<uint16_t>((clamp >> 24u) & 0x3FFu);
    const uint16_t maxV = static_cast<uint16_t>((clamp >> 34u) & 0x3FFu);

    float u = vertex.u;
    float v = vertex.v;
    if (!state.prim.fst) {
        const float q = std::fabs(vertex.q) > 1.0e-8f
            ? vertex.q : 1.0f;
        u = (vertex.s / q) * static_cast<float>(width);
        v = (vertex.t / q) * static_cast<float>(height);
    }

    if (!state.linearFilter) {
        const int x = static_cast<int>(std::floor(u + 0.001f));
        const int y = static_cast<int>(std::floor(v + 0.001f));
        return loadTextureColor(
            pixels, width, height, x, y,
            wrapU, wrapV, minU, maxU, minV, maxV);
    }

    const float fx = u - 0.5f;
    const float fy = v - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - std::floor(fx);
    const float ty = fy - std::floor(fy);
    const Color c00 = loadTextureColor(
        pixels, width, height, x0, y0,
        wrapU, wrapV, minU, maxU, minV, maxV);
    const Color c10 = loadTextureColor(
        pixels, width, height, x0 + 1, y0,
        wrapU, wrapV, minU, maxU, minV, maxV);
    const Color c01 = loadTextureColor(
        pixels, width, height, x0, y0 + 1,
        wrapU, wrapV, minU, maxU, minV, maxV);
    const Color c11 = loadTextureColor(
        pixels, width, height, x0 + 1, y0 + 1,
        wrapU, wrapV, minU, maxU, minV, maxV);

    Color result{};
    for (size_t i = 0u; i < result.size(); ++i) {
        const float top = c00[i] + (c10[i] - c00[i]) * tx;
        const float bottom = c01[i] + (c11[i] - c01[i]) * tx;
        result[i] = top + (bottom - top) * ty;
    }
    return result;
}

RasterVertex applyTexture(
    const RasterVertex &vertex,
    const GSDrawState &state,
    const std::vector<uint32_t> *texturePixels) {
    if (!state.prim.tme || texturePixels == nullptr) return vertex;
    const Color texel = sampleTexture(state, vertex, *texturePixels);
    const float vr = vertex.r * 255.0f;
    const float vg = vertex.g * 255.0f;
    const float vb = vertex.b * 255.0f;
    const float va = vertex.a * 255.0f;
    Color result{vr, vg, vb, va};

    switch (state.context.tex0.tfx & 0x3u) {
    case 0u: // MODULATE
        result[0] = clamp255(texel[0] * vr / 128.0f);
        result[1] = clamp255(texel[1] * vg / 128.0f);
        result[2] = clamp255(texel[2] * vb / 128.0f);
        result[3] = state.context.tex0.tcc != 0u
            ? clamp255(texel[3] * va / 128.0f) : va;
        break;
    case 1u: // DECAL
        result[0] = texel[0];
        result[1] = texel[1];
        result[2] = texel[2];
        result[3] = state.context.tex0.tcc != 0u ? texel[3] : va;
        break;
    case 2u: // HIGHLIGHT
        result[0] = clamp255(texel[0] * vr / 128.0f + va);
        result[1] = clamp255(texel[1] * vg / 128.0f + va);
        result[2] = clamp255(texel[2] * vb / 128.0f + va);
        result[3] = state.context.tex0.tcc != 0u
            ? clamp255(texel[3] + va) : va;
        break;
    default: // HIGHLIGHT2
        result[0] = clamp255(texel[0] * vr / 128.0f + va);
        result[1] = clamp255(texel[1] * vg / 128.0f + va);
        result[2] = clamp255(texel[2] * vb / 128.0f + va);
        result[3] = state.context.tex0.tcc != 0u ? texel[3] : va;
        break;
    }

    RasterVertex output = vertex;
    output.r = std::clamp(result[0], 0.0f, 255.0f) / 255.0f;
    output.g = std::clamp(result[1], 0.0f, 255.0f) / 255.0f;
    output.b = std::clamp(result[2], 0.0f, 255.0f) / 255.0f;
    output.a = std::clamp(result[3], 0.0f, 255.0f) / 255.0f;
    return output;
}

bool insideTarget(
    const GSContext &context,
    uint32_t targetWidth,
    int x,
    int y) {
    return x >= static_cast<int>(context.scissor.x0) &&
        x <= static_cast<int>(context.scissor.x1) &&
        y >= static_cast<int>(context.scissor.y0) &&
        y <= static_cast<int>(context.scissor.y1) &&
        x >= 0 && y >= 0 &&
        x < static_cast<int>(targetWidth) && y < 2048;
}

float edge(const RasterVertex &a, const RasterVertex &b, float x, float y) {
    return (b.x - a.x) * (y - a.y) -
        (b.y - a.y) * (x - a.x);
}

uint32_t rgbaFromGsPixel(uint8_t psm, uint32_t pixel);

uint32_t depthMaskForPsm(uint8_t psm) {
    switch (psm) {
    case GS_PSM_Z32: return 0xFFFFFFFFu;
    case GS_PSM_Z24: return 0x00FFFFFFu;
    case GS_PSM_Z16:
    case GS_PSM_Z16S: return 0x0000FFFFu;
    default: return 0u;
    }
}

uint32_t depthValue(const RasterVertex &vertex, uint32_t mask) {
    if (mask == 0u) return 0u;
    const double value = std::clamp(
        std::floor(vertex.z + 0.5), 0.0, static_cast<double>(mask));
    return static_cast<uint32_t>(value) & mask;
}

bool passesDepthTest(
    GSTextureStore &store,
    const GSDrawState &state,
    uint32_t x,
    uint32_t y,
    uint32_t incomingDepth) {
    const uint64_t test = state.context.test;
    if (((test >> 16u) & 1u) == 0u) return true;
    const uint32_t mask = depthMaskForPsm(state.context.zbuf.psm);
    if (mask == 0u) return false;
    const uint32_t storedDepth = store.Read(
        state.context.zbuf.psm, state.context.zbuf.zbp,
        state.context.frame.fbw, x, y) & mask;
    const uint32_t value = incomingDepth & mask;
    switch (static_cast<uint32_t>((test >> 17u) & 0x3u)) {
    case 0u: return false;                  // NEVER
    case 1u: return true;                   // ALWAYS
    case 2u: return value >= storedDepth;   // GEQUAL
    case 3u: return value > storedDepth;    // GREATER
    default: return false;
    }
}

uint8_t blendSelector(uint8_t selector, uint8_t source, uint8_t destination) {
    if (selector == 0u) return source;
    if (selector == 1u) return destination;
    return 0u;
}

uint32_t applyGsAlphaBlend(
    uint32_t sourceRgba,
    uint32_t destinationPixel,
    uint8_t destinationPsm,
    uint64_t alphaRegister,
    bool colorClamp) {
    const uint32_t destinationRgba = rgbaFromGsPixel(
        destinationPsm, destinationPixel);
    const uint8_t sourceChannels[3] = {
        static_cast<uint8_t>(sourceRgba & 0xFFu),
        static_cast<uint8_t>((sourceRgba >> 8u) & 0xFFu),
        static_cast<uint8_t>((sourceRgba >> 16u) & 0xFFu)};
    const uint8_t destinationChannels[3] = {
        static_cast<uint8_t>(destinationRgba & 0xFFu),
        static_cast<uint8_t>((destinationRgba >> 8u) & 0xFFu),
        static_cast<uint8_t>((destinationRgba >> 16u) & 0xFFu)};
    const uint8_t sourceAlpha = static_cast<uint8_t>((sourceRgba >> 24u) & 0xFFu);
    const uint8_t destinationAlpha = destinationPsm == GS_PSM_CT24
        ? 128u
        : static_cast<uint8_t>((destinationRgba >> 24u) & 0xFFu);
    const uint8_t aSelector = static_cast<uint8_t>(alphaRegister & 0x3u);
    const uint8_t bSelector = static_cast<uint8_t>((alphaRegister >> 2u) & 0x3u);
    const uint8_t cSelector = static_cast<uint8_t>((alphaRegister >> 4u) & 0x3u);
    const uint8_t dSelector = static_cast<uint8_t>((alphaRegister >> 6u) & 0x3u);
    const uint8_t factor = cSelector == 0u ? sourceAlpha
        : (cSelector == 1u ? destinationAlpha
                           : static_cast<uint8_t>((alphaRegister >> 32u) & 0xFFu));
    uint32_t result = sourceRgba & 0xFF000000u;
    for (size_t channelIndex = 0u; channelIndex < 3u; ++channelIndex) {
        const int a = blendSelector(
            aSelector, sourceChannels[channelIndex], destinationChannels[channelIndex]);
        const int b = blendSelector(
            bSelector, sourceChannels[channelIndex], destinationChannels[channelIndex]);
        const int d = blendSelector(
            dSelector, sourceChannels[channelIndex], destinationChannels[channelIndex]);
        const int blended = static_cast<int>(std::floor(
            static_cast<double>((a - b) * static_cast<int>(factor)) / 128.0)) + d;
        const uint32_t output = colorClamp
            ? static_cast<uint32_t>(std::clamp(blended, 0, 255))
            : static_cast<uint32_t>((blended % 256 + 256) % 256);
        result |= output << (channelIndex * 8u);
    }
    return result;
}

int signedDitherValue(uint32_t value) {
    const uint32_t encoded = value & 0x7u;
    return (encoded & 0x4u) != 0u
        ? static_cast<int>(encoded) - 8
        : static_cast<int>(encoded);
}

uint8_t applyDitherChannel(uint8_t channel, int dither, bool colorClamp) {
    const int adjusted = static_cast<int>(channel) + dither;
    if (colorClamp) {
        return static_cast<uint8_t>(std::clamp(adjusted, 0, 255));
    }
    const int wrapped = (adjusted % 256 + 256) % 256;
    return static_cast<uint8_t>(wrapped);
}

uint32_t applyGsDither(
    uint32_t sourceRgba,
    uint32_t x,
    uint32_t y,
    const GSDrawState &state) {
    const uint8_t psm = state.context.frame.psm;
    if ((state.dthe & 1u) == 0u ||
        (psm != GS_PSM_CT16 && psm != GS_PSM_CT16S)) {
        return sourceRgba;
    }

    // DIMX contains sixteen signed 3-bit values, each in a 4-bit slot.
    // The matrix repeats every four framebuffer pixels in both axes.
    const uint32_t matrixIndex = (y & 3u) * 4u + (x & 3u);
    const uint32_t shift = matrixIndex * 4u;
    const int dither = signedDitherValue(
        static_cast<uint32_t>((state.dimx >> shift) & 0x7u));
    const bool colorClamp = (state.colclamp & 1u) != 0u;

    const uint32_t red = applyDitherChannel(
        static_cast<uint8_t>(sourceRgba & 0xFFu), dither, colorClamp);
    const uint32_t green = applyDitherChannel(
        static_cast<uint8_t>((sourceRgba >> 8u) & 0xFFu), dither, colorClamp);
    const uint32_t blue = applyDitherChannel(
        static_cast<uint8_t>((sourceRgba >> 16u) & 0xFFu), dither, colorClamp);
    return (sourceRgba & 0xFF000000u) |
        red | (green << 8u) | (blue << 16u);
}

uint32_t applyGsFog(
    uint32_t sourceRgba,
    float interpolatedFog,
    const GSDrawState &state) {
    // GS F is an 8-bit vertex attribute, linearly interpolated in screen
    // space. The verified GS blend uses F and (256-F), with the two products
    // added before the single >> 8 truncation. Alpha is not affected by fog.
    const double fog = std::clamp(
        std::isfinite(interpolatedFog)
            ? static_cast<double>(interpolatedFog) : 0.0,
        0.0, 255.0);
    const double inverseFog = 256.0 - fog;
    const auto blendChannel = [fog, inverseFog](
        uint32_t source, uint8_t fogColor) -> uint32_t {
        const double weighted =
            fog * static_cast<double>(source) +
            inverseFog * static_cast<double>(fogColor);
        return static_cast<uint32_t>(std::clamp(
            std::floor(weighted / 256.0), 0.0, 255.0));
    };

    const uint32_t red = blendChannel(sourceRgba & 0xFFu, state.fogR);
    const uint32_t green =
        blendChannel((sourceRgba >> 8u) & 0xFFu, state.fogG);
    const uint32_t blue =
        blendChannel((sourceRgba >> 16u) & 0xFFu, state.fogB);
    return (sourceRgba & 0xFF000000u) |
        red | (green << 8u) | (blue << 16u);
}

void writePixelUnchecked(
    GSTextureStore &store,
    const GSDrawState &state,
    int x,
    int y,
    const RasterVertex &inputColor,
    const std::vector<uint32_t> *texturePixels) {
    const uint32_t pixelX = static_cast<uint32_t>(x);
    const uint32_t pixelY = static_cast<uint32_t>(y);
    const uint32_t zMask = depthMaskForPsm(state.context.zbuf.psm);
    const uint32_t z = depthValue(inputColor, zMask);
    if (!passesDepthTest(store, state, pixelX, pixelY, z)) return;

    const uint32_t oldFramebuffer = store.Read(
        state.context.frame.psm, state.context.frame.fbp,
        state.context.frame.fbw, pixelX, pixelY);
    if (((state.context.test >> 14u) & 1u) != 0u &&
        !GSCpuInternal::passesDestinationAlphaTest(
            state.context.test, state.context.frame.psm, oldFramebuffer)) {
        return;
    }

    const RasterVertex color = applyTexture(inputColor, state, texturePixels);
    uint32_t sourceRgba = packRgba(color);
    if (state.prim.fge) {
        sourceRgba = applyGsFog(sourceRgba, inputColor.fog, state);
    }
    const uint8_t alpha = static_cast<uint8_t>((sourceRgba >> 24u) & 0xFFu);
    const GSCpuInternal::PixelWriteMask writeMask =
        GSCpuInternal::classifyAlphaTest(
            state.context.test, alpha, state.context.frame.psm);

    if (writeMask.writesFramebuffer()) {
        uint32_t outputRgba = sourceRgba;
        const bool blendEnabled = state.prim.abe &&
            (!state.pabe || (alpha & 0x80u) != 0u);
        if (blendEnabled) {
            outputRgba = applyGsAlphaBlend(
                sourceRgba, oldFramebuffer, state.context.frame.psm,
                state.context.alpha, (state.colclamp & 1u) != 0u);
        }
        // GS dithering is applied after texture/fog/blending and before
        // CT16/CT16S RGB5551 conversion. It does not affect alpha or CT32.
        outputRgba = applyGsDither(
            outputRgba, pixelX, pixelY, state);
        if ((state.context.fba & 1u) != 0u) {
            if (state.context.frame.psm == GS_PSM_CT32) {
                outputRgba |= 0x80000000u;
            } else if (state.context.frame.psm == GS_PSM_CT16 ||
                       state.context.frame.psm == GS_PSM_CT16S) {
                outputRgba = (outputRgba & 0xFFFFu) | 0x8000u;
            }
        }
        writeFramebufferPixel(
            store, state.context, pixelX, pixelY, outputRgba,
            writeMask.writeAlpha);
    }

    if (writeMask.writeDepth &&
        ((state.context.test >> 16u) & 1u) != 0u &&
        !state.context.zbuf.zmask && zMask != 0u) {
        store.Write(
            state.context.zbuf.psm, state.context.zbuf.zbp,
            state.context.frame.fbw, pixelX, pixelY, z);
    }
}

void writePixel(
    GSTextureStore &store,
    const GSDrawState &state,
    uint32_t targetWidth,
    int x,
    int y,
    const RasterVertex &inputColor,
    const std::vector<uint32_t> *texturePixels) {
    if (!insideTarget(state.context, targetWidth, x, y)) return;
    writePixelUnchecked(store, state, x, y, inputColor, texturePixels);
}

void rasterizePoint(
    GSTextureStore &store,
    const GSDrawState &state,
    uint32_t width,
    const RasterVertex &vertex,
    const std::vector<uint32_t> *texturePixels) {
    writePixel(store, state, width,
               static_cast<int>(std::floor(vertex.x)),
               static_cast<int>(std::floor(vertex.y)), vertex,
               texturePixels);
}

void rasterizeLine(
    GSTextureStore &store,
    const GSDrawState &state,
    uint32_t width,
    const RasterVertex &a,
    const RasterVertex &b,
    const std::vector<uint32_t> *texturePixels) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const int steps = static_cast<int>(std::ceil(
        std::max(std::fabs(dx), std::fabs(dy))));
    if (steps <= 0) {
        rasterizePoint(store, state, width, a, texturePixels);
        return;
    }
    for (int step = 0; step <= steps; ++step) {
        const float t = static_cast<float>(step) /
            static_cast<float>(steps);
        const RasterVertex pixel = interpolate(a, b, t);
        writePixel(store, state, width,
                   static_cast<int>(std::floor(pixel.x)),
                   static_cast<int>(std::floor(pixel.y)), pixel,
                   texturePixels);
    }
}

void rasterizeTriangle(
    GSTextureStore &store,
    const GSDrawState &state,
    uint32_t width,
    const RasterVertex &a,
    const RasterVertex &b,
    const RasterVertex &c,
    const std::vector<uint32_t> *texturePixels) {
    const GSContext &context = state.context;
    const float area = edge(a, b, c.x, c.y);
    if (std::fabs(area) < 1.0e-7f) return;

    const float minX = std::min({a.x, b.x, c.x});
    const float maxX = std::max({a.x, b.x, c.x});
    const float minY = std::min({a.y, b.y, c.y});
    const float maxY = std::max({a.y, b.y, c.y});
    const int left = std::max(
        static_cast<int>(context.scissor.x0),
        static_cast<int>(std::floor(minX)));
    const int right = std::min(
        std::min(static_cast<int>(context.scissor.x1),
                 static_cast<int>(width) - 1),
        static_cast<int>(std::ceil(maxX)));
    const int top = std::max(
        static_cast<int>(context.scissor.y0),
        static_cast<int>(std::floor(minY)));
    const int bottom = std::min(
        std::min(static_cast<int>(context.scissor.y1), 2047),
        static_cast<int>(std::ceil(maxY)));

    // Barycentric coordinates are affine across the triangle. Calculate
    // them once at the first pixel of each scanline, then increment by the
    // exact x/y edge slopes. The previous inner loop performed three edge
    // evaluations and three floating-point divisions for every covered pixel.
    const float inverseArea = 1.0f / area;
    const float waStepX = (b.y - c.y) * inverseArea;
    const float wbStepX = (c.y - a.y) * inverseArea;
    const float wcStepX = (a.y - b.y) * inverseArea;
    const float waStepY = (c.x - b.x) * inverseArea;
    const float wbStepY = (a.x - c.x) * inverseArea;
    const float wcStepY = (b.x - a.x) * inverseArea;
    constexpr float epsilon = -1.0e-6f;

    const float firstX = static_cast<float>(left) + 0.5f;
    const float firstY = static_cast<float>(top) + 0.5f;
    float waRow = edge(b, c, firstX, firstY) * inverseArea;
    float wbRow = edge(c, a, firstX, firstY) * inverseArea;
    float wcRow = edge(a, b, firstX, firstY) * inverseArea;

    for (int y = top; y <= bottom; ++y) {
        float wa = waRow;
        float wb = wbRow;
        float wc = wcRow;

        for (int x = left; x <= right; ++x) {
            if (wa >= epsilon && wb >= epsilon && wc >= epsilon) {
                const RasterVertex pixel = interpolateTriangle(
                    a, b, c, wa, wb, wc);
                writePixelUnchecked(store, state, x, y, pixel, texturePixels);
            }
            wa += waStepX;
            wb += wbStepX;
            wc += wcStepX;
        }
        // The scanline origin advances by the y slope; x accumulation is
        // restarted on each row to limit floating-point drift.
        waRow += waStepY;
        wbRow += wbStepY;
        wcRow += wcStepY;
    }
}

void rasterizeSprite(
    GSTextureStore &store,
    const GSDrawState &state,
    uint32_t width,
    const RasterVertex &first,
    const RasterVertex &second,
    const std::vector<uint32_t> *texturePixels) {
    const GSContext &context = state.context;
    const float minX = std::min(first.x, second.x);
    const float maxX = std::max(first.x, second.x);
    const float minY = std::min(first.y, second.y);
    const float maxY = std::max(first.y, second.y);
    const int left = std::max(
        static_cast<int>(context.scissor.x0),
        static_cast<int>(std::floor(minX)));
    const int right = std::min(
        std::min(static_cast<int>(context.scissor.x1),
                 static_cast<int>(width) - 1),
        static_cast<int>(std::ceil(maxX)));
    const int top = std::max(
        static_cast<int>(context.scissor.y0),
        static_cast<int>(std::floor(minY)));
    const int bottom = std::min(
        std::min(static_cast<int>(context.scissor.y1), 2047),
        static_cast<int>(std::ceil(maxY)));
    const float dx = second.x - first.x;
    const float dy = second.y - first.y;
    // A zero-area sprite covers no pixels. Preserve the sign of each axis:
    // GS coordinates can arrive in either corner order, and a negative span
    // must reverse the UV progression instead of being clamped to a tiny
    // positive value.
    if (!std::isfinite(dx) || !std::isfinite(dy) ||
        std::fabs(dx) <= 1.0e-7f || std::fabs(dy) <= 1.0e-7f) {
        return;
    }
    // Keep the stepped coordinates in double precision. The final UV is
    // still rounded to float before interpolation, but the scanline additions
    // no longer accumulate visible drift across wide sprites.
    const double inverseDx = 1.0 / static_cast<double>(dx);
    const double inverseDy = 1.0 / static_cast<double>(dy);
    double vRawRow =
        (static_cast<double>(top) + 0.5 - static_cast<double>(first.y)) *
        inverseDy;
    for (int y = top; y <= bottom; ++y) {
        double uRaw =
            (static_cast<double>(left) + 0.5 - static_cast<double>(first.x)) *
            inverseDx;
        const float v = std::clamp(static_cast<float>(vRawRow), 0.0f, 1.0f);
        for (int x = left; x <= right; ++x) {
            const float u = std::clamp(static_cast<float>(uRaw), 0.0f, 1.0f);
            const float average = (u + v) * 0.5f;
            RasterVertex pixel = interpolate(first, second, average);
            // PS2 sprites are flat shaded: colour, Z and fog come from
            // the second endpoint, independent of the screen-space UV axes.
            pixel.fog = second.fog;
            if (state.prim.fst) {
                pixel.u = first.u + (second.u - first.u) * u;
                pixel.v = first.v + (second.v - first.v) * v;
            } else {
                pixel.s = first.s + (second.s - first.s) * u;
                pixel.t = first.t + (second.t - first.t) * v;
                pixel.q = first.q + (second.q - first.q) * average;
            }
            writePixelUnchecked(store, state, x, y, pixel, texturePixels);
            uRaw += inverseDx;
        }
        vRawRow += inverseDy;
    }
}

uint32_t displayWidth(uint64_t display) {
    const uint32_t divisor =
        static_cast<uint32_t>((display >> 23u) & 0xFu) + 1u;
    return (static_cast<uint32_t>((display >> 32u) & 0xFFFu) + 1u) /
        divisor;
}

uint32_t displayHeight(uint64_t display) {
    const uint32_t divisor =
        static_cast<uint32_t>((display >> 27u) & 0x3u) + 1u;
    return (static_cast<uint32_t>((display >> 44u) & 0x7FFu) + 1u) /
        divisor;
}

uint32_t rgbaFromGsPixel(uint8_t psm, uint32_t pixel) {
    if (psm == GS_PSM_CT16 || psm == GS_PSM_CT16S) {
        return GSCpuInternal::Rgba5551ToRgba8888(
            static_cast<uint16_t>(pixel));
    }
    if (psm == GS_PSM_CT24) return (pixel & 0x00FFFFFFu) | 0xFF000000u;
    return pixel;
}

} // namespace

bool rasterizeGsBatchToVram(
    GSTextureStore &store,
    const GSPrimitiveBatch &batch,
    const std::vector<uint32_t> *decodedTexture,
    std::string &error) {
    error.clear();
    if (batch.vertexCount == 0u || batch.vertexCount > 3u) {
        error = "GS rasterizer received an invalid vertex count.";
        return false;
    }

    const GSContext &context = batch.state.context;
    if (context.frame.fbw == 0u ||
        !supportedFramebufferPsm(context.frame.psm)) {
        error = "GS rasterizer supports only non-zero FBW CT32/CT24/CT16/CT16S framebuffers.";
        return false;
    }
    if (batch.state.prim.aa1) {
        error = "GS rasterizer currently rejects AA1 until its coverage and edge rules are implemented.";
        return false;
    }
    if (((context.test >> 16u) & 1u) != 0u &&
        depthMaskForPsm(context.zbuf.psm) == 0u) {
        error = "GS depth testing requires a supported Z32/Z24/Z16/Z16S target.";
        return false;
    }
    if (batch.state.prim.tme) {
        const size_t required =
            static_cast<size_t>(std::max<uint32_t>(1u, batch.state.textureWidth)) *
            std::max<uint32_t>(1u, batch.state.textureHeight);
        if (decodedTexture == nullptr || decodedTexture->size() < required) {
            error = "GS texture decoding did not provide a complete image.";
            return false;
        }
    }

    const uint32_t targetWidth =
        static_cast<uint32_t>(context.frame.fbw) * 64u;
    if (targetWidth == 0u || targetWidth > 4096u) {
        error = "GS framebuffer width is outside the supported range.";
        return false;
    }

    RasterVertex vertices[3]{};
    const GSVertex &flatColor = batch.vertices[batch.vertexCount - 1u];
    for (uint32_t i = 0u; i < batch.vertexCount; ++i) {
        const GSVertex &color = batch.state.prim.iip
            ? batch.vertices[i] : flatColor;
        vertices[i] = makeRasterVertex(batch.vertices[i], color, context);
    }

    switch (batch.state.prim.type) {
    case GS_PRIM_POINT:
        if (batch.vertexCount != 1u) break;
        rasterizePoint(store, batch.state, targetWidth, vertices[0], decodedTexture);
        return true;
    case GS_PRIM_LINE:
    case GS_PRIM_LINESTRIP:
        if (batch.vertexCount != 2u) break;
        rasterizeLine(store, batch.state, targetWidth, vertices[0], vertices[1], decodedTexture);
        return true;
    case GS_PRIM_SPRITE:
        if (batch.vertexCount != 2u) break;
        rasterizeSprite(store, batch.state, targetWidth, vertices[0], vertices[1], decodedTexture);
        return true;
    case GS_PRIM_TRIANGLE:
    case GS_PRIM_TRISTRIP:
    case GS_PRIM_TRIFAN:
        if (batch.vertexCount != 3u) break;
        rasterizeTriangle(
            store, batch.state, targetWidth,
            vertices[0], vertices[1], vertices[2], decodedTexture);
        return true;
    default:
        error = "GS rasterizer received an unsupported primitive type.";
        return false;
    }

    error = "GS primitive vertex count does not match the primitive type.";
    return false;
}

namespace {

struct DisplayCircuit {
    bool valid = false;
    uint32_t fbp = 0u;
    uint32_t fbw = 0u;
    uint8_t psm = GS_PSM_CT32;
    uint32_t dbx = 0u;
    uint32_t dby = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
};

DisplayCircuit decodeDisplayCircuit(
    bool enabled, uint64_t dispfb, uint64_t display) {
    DisplayCircuit circuit{};
    if (!enabled || display == 0u) return circuit;

    circuit.fbp = static_cast<uint32_t>(dispfb & 0x1FFu);
    circuit.fbw = std::max<uint32_t>(
        1u, static_cast<uint32_t>((dispfb >> 9u) & 0x3Fu));
    circuit.psm = static_cast<uint8_t>((dispfb >> 15u) & 0x1Fu);
    circuit.dbx = static_cast<uint32_t>((dispfb >> 32u) & 0x7FFu);
    circuit.dby = static_cast<uint32_t>((dispfb >> 43u) & 0x7FFu);
    circuit.width = displayWidth(display);
    circuit.height = displayHeight(display);
    circuit.valid = circuit.width != 0u && circuit.height != 0u &&
        circuit.width <= 640u && supportedFramebufferPsm(circuit.psm);
    return circuit;
}

uint32_t readDisplayPixel(
    const GSTextureStore &store,
    const DisplayCircuit &circuit,
    uint32_t x,
    uint32_t y) {
    // The Waffle reference presenter repeats the first visible source row and
    // column from coordinate 2 to hide the game's deliberately cleared edge.
    const uint32_t sourceX = std::max<uint32_t>(2u, circuit.dbx + x);
    const uint32_t sourceY = std::max<uint32_t>(2u, circuit.dby + y);
    if (sourceX >= 2048u || sourceY >= 2048u) return 0u;
    return store.Read(
        circuit.psm, circuit.fbp, circuit.fbw, sourceX, sourceY);
}

uint32_t displayAlpha(const DisplayCircuit &circuit, uint32_t pixel) {
    const uint32_t alpha = circuit.psm == GS_PSM_CT24
        ? 128u
        : ((rgbaFromGsPixel(circuit.psm, pixel) >> 24u) & 0xFFu);
    return std::min<uint32_t>(255u, alpha * 2u);
}

uint8_t blendChannel(uint8_t foreground, uint8_t background, uint32_t alpha) {
    const uint32_t inverse = 255u - alpha;
    const uint32_t value =
        static_cast<uint32_t>(foreground) * alpha +
        static_cast<uint32_t>(background) * inverse;
    return static_cast<uint8_t>((value + 127u) / 255u);
}

} // namespace

PresentationFrame presentGsVramCircuit(
    const GSTextureStore &store,
    const GSPresentationRequest &request) {
    PresentationFrame frame{};
    const DisplayCircuit circuit1 = decodeDisplayCircuit(
        (request.pmode & 1u) != 0u, request.dispfb1, request.display1);
    const DisplayCircuit circuit2 = decodeDisplayCircuit(
        (request.pmode & 2u) != 0u, request.dispfb2, request.display2);
    if (!circuit1.valid && !circuit2.valid) return frame;

    // Match the reference GS presenter: circuit 1 defines the output extent
    // when enabled; otherwise circuit 2 is the sole displayed source.
    const DisplayCircuit &output = circuit1.valid ? circuit1 : circuit2;
    const uint32_t width = output.width;
    const uint32_t height = std::min<uint32_t>(output.height, 448u);
    if (width == 0u || height == 0u) return frame;

    frame.width = width;
    frame.height = height;
    frame.displayFbp = output.fbp;
    frame.sourceFbp = output.fbp;
    frame.pixels.assign(static_cast<size_t>(640u) * height * 4u, 0u);

    const bool composeBoth = circuit1.valid && circuit2.valid;
    const bool useBackground = composeBoth && ((request.pmode >> 7u) & 1u) != 0u;
    const bool fixedAlpha = ((request.pmode >> 5u) & 1u) != 0u;
    const uint32_t alp = static_cast<uint32_t>((request.pmode >> 8u) & 0xFFu);

    for (uint32_t y = 0u; y < height; ++y) {
        for (uint32_t x = 0u; x < width; ++x) {
            uint32_t rgba = 0xFF000000u;
            if (composeBoth) {
                if (useBackground) {
                    rgba = static_cast<uint32_t>(request.bgcolor & 0x00FFFFFFu) |
                        0xFF000000u;
                } else {
                    const uint32_t backPixel =
                        readDisplayPixel(store, circuit2, x, y);
                    rgba = rgbaFromGsPixel(circuit2.psm, backPixel) |
                        (0xFF000000u);
                }

                const uint32_t frontPixel =
                    readDisplayPixel(store, circuit1, x, y);
                const uint32_t frontRgba =
                    rgbaFromGsPixel(circuit1.psm, frontPixel);
                const uint32_t alpha = fixedAlpha
                    ? alp
                    : displayAlpha(circuit1, frontPixel);
                const uint8_t red = blendChannel(
                    static_cast<uint8_t>(frontRgba & 0xFFu),
                    static_cast<uint8_t>(rgba & 0xFFu), alpha);
                const uint8_t green = blendChannel(
                    static_cast<uint8_t>((frontRgba >> 8u) & 0xFFu),
                    static_cast<uint8_t>((rgba >> 8u) & 0xFFu), alpha);
                const uint8_t blue = blendChannel(
                    static_cast<uint8_t>((frontRgba >> 16u) & 0xFFu),
                    static_cast<uint8_t>((rgba >> 16u) & 0xFFu), alpha);
                rgba = static_cast<uint32_t>(red) |
                    (static_cast<uint32_t>(green) << 8u) |
                    (static_cast<uint32_t>(blue) << 16u) |
                    0xFF000000u;
            } else {
                const uint32_t pixel = readDisplayPixel(store, output, x, y);
                rgba = rgbaFromGsPixel(output.psm, pixel) | 0xFF000000u;
            }

            const size_t offset =
                (static_cast<size_t>(y) * 640u + x) * 4u;
            frame.pixels[offset + 0u] = static_cast<uint8_t>(rgba & 0xFFu);
            frame.pixels[offset + 1u] = static_cast<uint8_t>((rgba >> 8u) & 0xFFu);
            frame.pixels[offset + 2u] = static_cast<uint8_t>((rgba >> 16u) & 0xFFu);
            frame.pixels[offset + 3u] = 255u;
        }
    }
    return frame;
}

} // namespace gow::android
