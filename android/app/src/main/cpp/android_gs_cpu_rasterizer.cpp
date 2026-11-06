#include "android_gs_cpu_rasterizer.h"

#include "gs_texture_store.h"
#include "runtime/gs/ps2_gs_common.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace gow::android {
namespace {

struct RasterVertex {
    float x = 0.0f;
    float y = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

float channel(uint8_t value) {
    return static_cast<float>(value) / 255.0f;
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
        const uint32_t r = (rgba & 0xFFu) >> 3u;
        const uint32_t g = ((rgba >> 8u) & 0xFFu) >> 3u;
        const uint32_t b = ((rgba >> 16u) & 0xFFu) >> 3u;
        const uint32_t a = ((rgba >> 24u) & 0xFFu) >= 128u ? 1u : 0u;
        return r | (g << 5u) | (b << 10u) | (a << 15u);
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
    return result;
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
    const GSContext &context,
    uint32_t targetWidth,
    int x,
    int y,
    const RasterVertex &color) {
    if (!insideTarget(context, targetWidth, x, y)) return;
    writeFramebufferPixel(
        store, context,
        static_cast<uint32_t>(x), static_cast<uint32_t>(y),
        packRgba(color));
}

void rasterizePoint(
    GSTextureStore &store,
    const GSContext &context,
    uint32_t width,
    const RasterVertex &vertex) {
    writePixel(store, context, width,
               static_cast<int>(std::floor(vertex.x)),
               static_cast<int>(std::floor(vertex.y)), vertex);
}

void rasterizeLine(
    GSTextureStore &store,
    const GSContext &context,
    uint32_t width,
    const RasterVertex &a,
    const RasterVertex &b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const int steps = static_cast<int>(std::ceil(
        std::max(std::fabs(dx), std::fabs(dy))));
    if (steps <= 0) {
        rasterizePoint(store, context, width, a);
        return;
    }
    for (int step = 0; step <= steps; ++step) {
        const float t = static_cast<float>(step) /
            static_cast<float>(steps);
        const RasterVertex pixel = interpolate(a, b, t);
        writePixel(store, context, width,
                   static_cast<int>(std::floor(pixel.x)),
                   static_cast<int>(std::floor(pixel.y)), pixel);
    }
}

void rasterizeTriangle(
    GSTextureStore &store,
    const GSContext &context,
    uint32_t width,
    const RasterVertex &a,
    const RasterVertex &b,
    const RasterVertex &c) {
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

            RasterVertex color{};
            color.r = a.r * wa + b.r * wb + c.r * wc;
            color.g = a.g * wa + b.g * wb + c.g * wc;
            color.b = a.b * wa + b.b * wb + c.b * wc;
            color.a = a.a * wa + b.a * wb + c.a * wc;
            writePixel(store, context, width, x, y, color);
        }
    }
}

void rasterizeSprite(
    GSTextureStore &store,
    const GSContext &context,
    uint32_t width,
    const RasterVertex &first,
    const RasterVertex &second) {
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
            const float t = (u + v) * 0.5f;
            const RasterVertex color = interpolate(first, second, t);
            writePixel(store, context, width, x, y, color);
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
        const uint32_t r5 = pixel & 0x1Fu;
        const uint32_t g5 = (pixel >> 5u) & 0x1Fu;
        const uint32_t b5 = (pixel >> 10u) & 0x1Fu;
        const uint32_t a1 = (pixel >> 15u) & 1u;
        const uint32_t r = (r5 << 3u) | (r5 >> 2u);
        const uint32_t g = (g5 << 3u) | (g5 >> 2u);
        const uint32_t b = (b5 << 3u) | (b5 >> 2u);
        const uint32_t a = a1 ? 255u : 0u;
        return r | (g << 8u) | (b << 16u) | (a << 24u);
    }
    if (psm == GS_PSM_CT24) return (pixel & 0x00FFFFFFu) | 0xFF000000u;
    return pixel;
}

} // namespace

bool rasterizeGsBatchToVram(
    GSTextureStore &store,
    const GSPrimitiveBatch &batch,
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
    if (batch.state.prim.tme || batch.state.prim.abe ||
        batch.state.prim.fge || ((context.test >> 16u) & 1u) != 0u ||
        (context.test & 1u) != 0u) {
        error = "GS rasterizer currently rejects textured, blended, fogged, depth-tested or alpha-tested draws.";
        return false;
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
        rasterizePoint(store, context, targetWidth, vertices[0]);
        return true;
    case GS_PRIM_LINE:
    case GS_PRIM_LINESTRIP:
        if (batch.vertexCount != 2u) break;
        rasterizeLine(store, context, targetWidth, vertices[0], vertices[1]);
        return true;
    case GS_PRIM_SPRITE:
        if (batch.vertexCount != 2u) break;
        rasterizeSprite(store, context, targetWidth, vertices[0], vertices[1]);
        return true;
    case GS_PRIM_TRIANGLE:
    case GS_PRIM_TRISTRIP:
    case GS_PRIM_TRIFAN:
        if (batch.vertexCount != 3u) break;
        rasterizeTriangle(
            store, context, targetWidth,
            vertices[0], vertices[1], vertices[2]);
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
