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
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
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
    uint32_t rgba) {
    const uint8_t psm = context.frame.psm;
    uint32_t frameMask = context.frame.fbmsk;
    if (psm == GS_PSM_CT16 || psm == GS_PSM_CT16S) {
        frameMask = GSInternal::frameMask16(frameMask);
    } else if (psm == GS_PSM_CT24) {
        frameMask &= 0x00FFFFFFu;
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
    vertex.r = channel(color.r);
    vertex.g = channel(color.g);
    vertex.b = channel(color.b);
    vertex.a = channel(color.a);
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
    result.r = a.r + (b.r - a.r) * t;
    result.g = a.g + (b.g - a.g) * t;
    result.b = a.b + (b.b - a.b) * t;
    result.a = a.a + (b.a - a.a) * t;
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
    result.r = a.r * wa + b.r * wb + c.r * wc;
    result.g = a.g * wa + b.g * wb + c.g * wc;
    result.b = a.b * wa + b.b * wb + c.b * wc;
    result.a = a.a * wa + b.a * wb + c.a * wc;
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

void writePixel(
    GSTextureStore &store,
    const GSDrawState &state,
    uint32_t targetWidth,
    int x,
    int y,
    const RasterVertex &inputColor,
    const std::vector<uint32_t> *texturePixels) {
    if (!insideTarget(state.context, targetWidth, x, y)) return;
    const RasterVertex color = applyTexture(inputColor, state, texturePixels);
    writeFramebufferPixel(
        store, state.context,
        static_cast<uint32_t>(x), static_cast<uint32_t>(y),
        packRgba(color));
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

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float wa = edge(b, c, px, py) / area;
            const float wb = edge(c, a, px, py) / area;
            const float wc = edge(a, b, px, py) / area;
            constexpr float epsilon = -1.0e-6f;
            if (wa < epsilon || wb < epsilon || wc < epsilon) continue;

            const RasterVertex pixel = interpolateTriangle(
                a, b, c, wa, wb, wc);
            writePixel(store, state, width, x, y, pixel, texturePixels);
        }
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
    const float dx = std::max(1.0e-7f, second.x - first.x);
    const float dy = std::max(1.0e-7f, second.y - first.y);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const float u = std::clamp(
                (static_cast<float>(x) + 0.5f - first.x) / dx, 0.0f, 1.0f);
            const float v = std::clamp(
                (static_cast<float>(y) + 0.5f - first.y) / dy, 0.0f, 1.0f);
            RasterVertex pixel = interpolate(first, second, (u + v) * 0.5f);
            if (state.prim.fst) {
                pixel.u = first.u + (second.u - first.u) * u;
                pixel.v = first.v + (second.v - first.v) * v;
            } else {
                pixel.s = first.s + (second.s - first.s) * u;
                pixel.t = first.t + (second.t - first.t) * v;
                pixel.q = first.q + (second.q - first.q) * ((u + v) * 0.5f);
            }
            writePixel(store, state, width, x, y, pixel, texturePixels);
        }
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
        error = "GS rasterizer supports only non-zero FBW CT32/CT24/CT16 framebuffers.";
        return false;
    }
    if (batch.state.prim.abe || batch.state.prim.fge ||
        batch.state.pabe || batch.state.prim.aa1 ||
        ((context.test >> 16u) & 1u) != 0u ||
        (context.test & 1u) != 0u ||
        ((context.test >> 14u) & 1u) != 0u) {
        error = "GS rasterizer currently rejects blending, fog, antialiasing, depth, alpha and destination-alpha tests.";
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

PresentationFrame presentGsVramCircuit(
    const GSTextureStore &store,
    const GSPresentationRequest &request) {
    PresentationFrame frame{};
    const bool enable1 = (request.pmode & 1u) != 0u;
    const bool enable2 = (request.pmode & 2u) != 0u;

    const uint64_t dispfb = enable1 ? request.dispfb1 : request.dispfb2;
    const uint64_t display = enable1 ? request.display1 : request.display2;
    if ((!enable1 && !enable2) || display == 0u) return frame;

    const uint32_t fbp = static_cast<uint32_t>(dispfb & 0x1FFu);
    const uint32_t fbw =
        std::max<uint32_t>(1u, static_cast<uint32_t>((dispfb >> 9u) & 0x3Fu));
    const uint8_t psm = static_cast<uint8_t>((dispfb >> 15u) & 0x1Fu);
    const uint32_t dbx = static_cast<uint32_t>((dispfb >> 32u) & 0x7FFu);
    const uint32_t dby = static_cast<uint32_t>((dispfb >> 43u) & 0x7FFu);
    uint32_t width = displayWidth(display);
    uint32_t height = std::min<uint32_t>(displayHeight(display), 448u);
    if (width == 0u || height == 0u || width > 640u ||
        !supportedFramebufferPsm(psm)) {
        return frame;
    }

    frame.width = width;
    frame.height = height;
    frame.displayFbp = fbp;
    frame.sourceFbp = fbp;
    frame.pixels.assign(
        static_cast<size_t>(640u) * height * 4u, 0u);
    for (uint32_t y = 0u; y < height; ++y) {
        for (uint32_t x = 0u; x < width; ++x) {
            const uint32_t pixel = store.Read(
                psm, fbp, fbw, dbx + x, dby + y);
            const uint32_t rgba = rgbaFromGsPixel(psm, pixel);
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
