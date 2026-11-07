#include "android_gs_vulkan_backend.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        std::vector<uint8_t> vram(4u * 1024u * 1024u, 0u);
        gow::android::AndroidGSVulkanBackend backend(nullptr, nullptr);
        backend.Initialize(vram.data(), static_cast<uint32_t>(vram.size()));

        // Direct GS memory accesses must preserve the real PS2 swizzled layout.
        backend.WriteVram(GS_PSM_CT32, 0u, 1u, 5u, 7u, 0x11223344u);
        require(
            backend.ReadVram(GS_PSM_CT32, 0u, 1u, 5u, 7u) == 0x11223344u,
            "CT32 VRAM round-trip failed.");

        // Clear only the scissor rectangle and preserve channels selected by FBMSK.
        GSContext context{};
        context.frame.fbp = 0u;
        context.frame.fbw = 1u;
        context.frame.psm = GS_PSM_CT32;
        context.frame.fbmsk = 0x0000FF00u;
        context.scissor = {1u, 2u, 3u, 4u};
        backend.WriteVram(GS_PSM_CT32, 0u, 1u, 1u, 3u, 0x11223344u);
        require(
            backend.ClearFramebuffer(context, 0xAABBCCDDu),
            "GS framebuffer clear did not accept a valid CT32 target.");
        require(
            backend.ReadVram(GS_PSM_CT32, 0u, 1u, 1u, 3u) == 0xAABB33DDu,
            "GS framebuffer clear did not preserve the FBMSK channel.");
        require(
            backend.ReadVram(GS_PSM_CT32, 0u, 1u, 0u, 3u) == 0u,
            "GS framebuffer clear wrote outside the scissor rectangle.");

        // A synthetic HOST->LOCAL IMAGE packet must update the same VRAM image.
        GSTransferCommand transfer{};
        transfer.direction = 0u;
        transfer.bitbltbuf.dbp = 0u;
        transfer.bitbltbuf.dbw = 1u;
        transfer.bitbltbuf.dpsm = GS_PSM_CT32;
        transfer.trxpos.dsax = 10u;
        transfer.trxpos.dsay = 11u;
        transfer.trxreg.rrw = 2u;
        transfer.trxreg.rrh = 1u;
        backend.BeginTransfer(transfer);

        const uint32_t upload[2] = {0xDEADBEEFu, 0xCAFEBABEu};
        backend.UploadImage(
            reinterpret_cast<const uint8_t *>(upload),
            static_cast<uint32_t>(sizeof(upload)));
        require(
            backend.ReadVram(GS_PSM_CT32, 0u, 1u, 10u, 11u) == upload[0] &&
            backend.ReadVram(GS_PSM_CT32, 0u, 1u, 11u, 11u) == upload[1],
            "HOST->LOCAL image transfer did not reach GS VRAM.");
        const GSTransferSnapshot snapshot = backend.GetTransferSnapshot();
        require(snapshot.totalPixels == 0u &&
                snapshot.copiedPixels == 2u &&
                snapshot.direction == 3u,
                "Completed GS transfer did not reach the idle state.");

        std::vector<uint8_t> snapshotBytes;
        backend.SnapshotVram(snapshotBytes);
        require(snapshotBytes.size() == 4u * 1024u * 1024u,
                "GS backend VRAM snapshot does not contain all 4 MiB.");

        // A solid CT32 triangle must update the FBP VRAM, not bypass it to draw
        // directly into the Android surface.
        GSPrimitiveBatch triangle{};
        triangle.vertexCount = 3u;
        triangle.state.context.frame = {0u, 1u, GS_PSM_CT32, 0u};
        triangle.state.context.scissor = {0u, 63u, 0u, 63u};
        triangle.state.prim.type = GS_PRIM_TRIANGLE;
        triangle.state.prim.iip = false;
        triangle.vertices[0].x = 4.0f;
        triangle.vertices[0].y = 4.0f;
        triangle.vertices[1].x = 20.0f;
        triangle.vertices[1].y = 4.0f;
        triangle.vertices[2].x = 4.0f;
        triangle.vertices[2].y = 20.0f;
        for (auto &vertex : triangle.vertices) {
            vertex.r = 255u;
            vertex.g = 0u;
            vertex.b = 0u;
            vertex.a = 255u;
        }
        backend.Submit(triangle);

        // A sprite is converted to a filled rectangle in the same GS FBP.
        GSPrimitiveBatch sprite{};
        sprite.vertexCount = 2u;
        sprite.state.context.frame = triangle.state.context.frame;
        sprite.state.context.scissor = triangle.state.context.scissor;
        sprite.state.prim.type = GS_PRIM_SPRITE;
        sprite.state.prim.iip = false;
        sprite.vertices[0].x = 24.0f;
        sprite.vertices[0].y = 24.0f;
        sprite.vertices[1].x = 32.0f;
        sprite.vertices[1].y = 32.0f;
        for (auto &vertex : sprite.vertices) {
            vertex.r = 0u;
            vertex.g = 0u;
            vertex.b = 255u;
            vertex.a = 255u;
        }
        backend.Submit(sprite);

        // Textured sprites must decode texels from the shared GS VRAM image,
        // apply UV sampling and TFX, and still land in the selected FBP.
        backend.WriteVram(GS_PSM_CT32, 64u, 1u, 0u, 0u, 0xFF0000FFu);
        backend.WriteVram(GS_PSM_CT32, 64u, 1u, 1u, 0u, 0xFF00FF00u);
        backend.WriteVram(GS_PSM_CT32, 64u, 1u, 0u, 1u, 0xFFFF0000u);
        backend.WriteVram(GS_PSM_CT32, 64u, 1u, 1u, 1u, 0xFFFFFFFFu);

        GSPrimitiveBatch texturedSprite{};
        texturedSprite.vertexCount = 2u;
        texturedSprite.state.context.frame = triangle.state.context.frame;
        texturedSprite.state.context.scissor = triangle.state.context.scissor;
        texturedSprite.state.context.tex0.tbp0 = 64u;
        texturedSprite.state.context.tex0.tbw = 1u;
        texturedSprite.state.context.tex0.psm = GS_PSM_CT32;
        texturedSprite.state.context.tex0.tcc = 1u;
        texturedSprite.state.context.tex0.tfx = 0u;
        texturedSprite.state.prim.type = GS_PRIM_SPRITE;
        texturedSprite.state.prim.tme = true;
        texturedSprite.state.prim.fst = true;
        texturedSprite.state.textureWidth = 2u;
        texturedSprite.state.textureHeight = 2u;
        texturedSprite.vertices[0].x = 36.0f;
        texturedSprite.vertices[0].y = 12.0f;
        texturedSprite.vertices[0].u = 0u;
        texturedSprite.vertices[0].v = 0u;
        texturedSprite.vertices[1].x = 40.0f;
        texturedSprite.vertices[1].y = 16.0f;
        texturedSprite.vertices[1].u = 32u;
        texturedSprite.vertices[1].v = 32u;
        for (auto &vertex : texturedSprite.vertices) {
            vertex.r = 128u;
            vertex.g = 128u;
            vertex.b = 128u;
            vertex.a = 128u;
        }
        backend.Submit(texturedSprite);

        // The same texture must keep the correct UV orientation when the two
        // GS sprite vertices arrive with a negative X span, negative Y span,
        // or both. The texture's four corners are red, green, blue and white.
        GSPrimitiveBatch reversedXSprite = texturedSprite;
        reversedXSprite.vertices[0].x = 40.0f;
        reversedXSprite.vertices[0].y = 20.0f;
        reversedXSprite.vertices[1].x = 36.0f;
        reversedXSprite.vertices[1].y = 24.0f;
        backend.Submit(reversedXSprite);

        GSPrimitiveBatch reversedYSprite = texturedSprite;
        reversedYSprite.vertices[0].x = 36.0f;
        reversedYSprite.vertices[0].y = 32.0f;
        reversedYSprite.vertices[1].x = 40.0f;
        reversedYSprite.vertices[1].y = 28.0f;
        backend.Submit(reversedYSprite);

        GSPrimitiveBatch reversedBothSprite = texturedSprite;
        reversedBothSprite.vertices[0].x = 40.0f;
        reversedBothSprite.vertices[0].y = 40.0f;
        reversedBothSprite.vertices[1].x = 36.0f;
        reversedBothSprite.vertices[1].y = 36.0f;
        backend.Submit(reversedBothSprite);

        // GS fog is applied after the texture function, interpolates as a
        // screen-space vertex attribute, and leaves alpha unchanged.
        GSPrimitiveBatch fogPoint{};
        fogPoint.vertexCount = 1u;
        fogPoint.state.context.frame = triangle.state.context.frame;
        fogPoint.state.context.scissor = triangle.state.context.scissor;
        fogPoint.state.prim.type = GS_PRIM_POINT;
        fogPoint.state.prim.fge = true;
        fogPoint.state.fogR = 50u;
        fogPoint.state.fogG = 100u;
        fogPoint.state.fogB = 200u;
        fogPoint.vertices[0].r = 100u;
        fogPoint.vertices[0].g = 150u;
        fogPoint.vertices[0].b = 200u;
        fogPoint.vertices[0].a = 173u;

        fogPoint.vertices[0].x = 4.0f;
        fogPoint.vertices[0].y = 48.0f;
        fogPoint.vertices[0].fog = 0u;
        backend.Submit(fogPoint);

        GSPrimitiveBatch halfFogPoint = fogPoint;
        halfFogPoint.vertices[0].x = 5.0f;
        halfFogPoint.vertices[0].fog = 128u;
        backend.Submit(halfFogPoint);

        GSPrimitiveBatch minimumFogPoint = fogPoint;
        minimumFogPoint.vertices[0].x = 6.0f;
        minimumFogPoint.vertices[0].fog = 255u;
        backend.Submit(minimumFogPoint);

        // Match both six-value RGB pattern groups used by the reference GS
        // fog probe, including the F=1 and F=255 boundary behaviour.
        const std::array<uint8_t, 6> fogFactors = {0u, 1u, 77u, 128u, 201u, 255u};
        const std::array<std::array<uint8_t, 3>, 6> firstFogExpected = {{
            {{50u, 100u, 200u}}, {{50u, 99u, 199u}}, {{107u, 79u, 159u}},
            {{145u, 66u, 132u}}, {{199u, 46u, 93u}}, {{239u, 32u, 64u}}
        }};
        const std::array<std::array<uint8_t, 3>, 6> secondFogExpected = {{
            {{255u, 0u, 128u}}, {{254u, 0u, 128u}}, {{178u, 76u, 128u}},
            {{127u, 127u, 128u}}, {{54u, 200u, 128u}}, {{0u, 254u, 128u}}
        }};
        for (size_t i = 0u; i < fogFactors.size(); ++i) {
            GSPrimitiveBatch firstPattern = fogPoint;
            firstPattern.vertices[0].x = 10.0f + static_cast<float>(i);
            firstPattern.vertices[0].y = 44.0f;
            firstPattern.vertices[0].r = 240u;
            firstPattern.vertices[0].g = 32u;
            firstPattern.vertices[0].b = 64u;
            firstPattern.vertices[0].fog = fogFactors[i];
            backend.Submit(firstPattern);

            GSPrimitiveBatch secondPattern = fogPoint;
            secondPattern.vertices[0].x = 20.0f + static_cast<float>(i);
            secondPattern.vertices[0].y = 44.0f;
            secondPattern.vertices[0].r = 0u;
            secondPattern.vertices[0].g = 255u;
            secondPattern.vertices[0].b = 128u;
            secondPattern.vertices[0].fog = fogFactors[i];
            secondPattern.state.fogR = 255u;
            secondPattern.state.fogG = 0u;
            secondPattern.state.fogB = 128u;
            backend.Submit(secondPattern);
        }

        GSPrimitiveBatch fogDisabledPoint = fogPoint;
        fogDisabledPoint.state.prim.fge = false;
        fogDisabledPoint.vertices[0].x = 7.0f;
        fogDisabledPoint.vertices[0].fog = 0u;
        backend.Submit(fogDisabledPoint);

        // The texture function produces red at (0,0); F=0 must replace that
        // textured result with FOGCOL, proving fog runs after TFX.
        GSPrimitiveBatch foggedTexturedPoint = fogPoint;
        foggedTexturedPoint.vertices[0].x = 8.0f;
        foggedTexturedPoint.vertices[0].fog = 0u;
        foggedTexturedPoint.vertices[0].r = 128u;
        foggedTexturedPoint.vertices[0].g = 128u;
        foggedTexturedPoint.vertices[0].b = 128u;
        foggedTexturedPoint.vertices[0].a = 128u;
        foggedTexturedPoint.state.context.tex0.tbp0 = 64u;
        foggedTexturedPoint.state.context.tex0.tbw = 1u;
        foggedTexturedPoint.state.context.tex0.psm = GS_PSM_CT32;
        foggedTexturedPoint.state.context.tex0.tcc = 1u;
        foggedTexturedPoint.state.context.tex0.tfx = 0u;
        foggedTexturedPoint.state.prim.tme = true;
        foggedTexturedPoint.state.prim.fst = true;
        foggedTexturedPoint.state.textureWidth = 2u;
        foggedTexturedPoint.state.textureHeight = 2u;
        backend.Submit(foggedTexturedPoint);

        // The midpoint of this line has F=64 after linear interpolation
        // between the endpoint values 0 and 128.
        GSPrimitiveBatch fogLine{};
        fogLine.vertexCount = 2u;
        fogLine.state.context.frame = triangle.state.context.frame;
        fogLine.state.context.scissor = triangle.state.context.scissor;
        fogLine.state.prim.type = GS_PRIM_LINE;
        fogLine.state.prim.fge = true;
        fogLine.state.fogR = 50u;
        fogLine.state.fogG = 100u;
        fogLine.state.fogB = 200u;
        for (auto &vertex : fogLine.vertices) {
            vertex.r = 100u;
            vertex.g = 150u;
            vertex.b = 200u;
            vertex.a = 173u;
        }
        fogLine.vertices[0].x = 16.0f;
        fogLine.vertices[0].y = 48.0f;
        fogLine.vertices[0].fog = 0u;
        fogLine.vertices[1].x = 20.0f;
        fogLine.vertices[1].y = 48.0f;
        fogLine.vertices[1].fog = 128u;
        backend.Submit(fogLine);

        // Triangle F is an affine screen-space attribute, interpolated
        // independently at each covered pixel.
        GSPrimitiveBatch fogTriangle{};
        fogTriangle.vertexCount = 3u;
        fogTriangle.state.context.frame = triangle.state.context.frame;
        fogTriangle.state.context.scissor = triangle.state.context.scissor;
        fogTriangle.state.prim.type = GS_PRIM_TRIANGLE;
        fogTriangle.state.prim.iip = true;
        fogTriangle.state.prim.fge = true;
        fogTriangle.state.fogR = 50u;
        fogTriangle.state.fogG = 100u;
        fogTriangle.state.fogB = 200u;
        const std::array<std::array<float, 3>, 3> fogTriangleVertices = {{
            {{12.0f, 52.0f, 0.0f}},
            {{20.0f, 52.0f, 128.0f}},
            {{12.0f, 60.0f, 255.0f}}
        }};
        for (size_t i = 0u; i < fogTriangleVertices.size(); ++i) {
            auto &vertex = fogTriangle.vertices[i];
            vertex.x = fogTriangleVertices[i][0];
            vertex.y = fogTriangleVertices[i][1];
            vertex.fog = static_cast<uint8_t>(fogTriangleVertices[i][2]);
            vertex.r = 100u;
            vertex.g = 150u;
            vertex.b = 200u;
            vertex.a = 173u;
        }
        backend.Submit(fogTriangle);

        // A sprite is flat shaded: its F value comes from the second endpoint,
        // not the first endpoint's value.
        GSPrimitiveBatch fogSprite{};
        fogSprite.vertexCount = 2u;
        fogSprite.state.context.frame = triangle.state.context.frame;
        fogSprite.state.context.scissor = triangle.state.context.scissor;
        fogSprite.state.prim.type = GS_PRIM_SPRITE;
        fogSprite.state.prim.fge = true;
        fogSprite.state.fogR = 50u;
        fogSprite.state.fogG = 100u;
        fogSprite.state.fogB = 200u;
        fogSprite.vertices[0].x = 24.0f;
        fogSprite.vertices[0].y = 52.0f;
        fogSprite.vertices[0].fog = 255u;
        fogSprite.vertices[1].x = 28.0f;
        fogSprite.vertices[1].y = 56.0f;
        fogSprite.vertices[1].fog = 0u;
        for (auto &vertex : fogSprite.vertices) {
            vertex.r = 100u;
            vertex.g = 150u;
            vertex.b = 200u;
            vertex.a = 173u;
        }
        backend.Submit(fogSprite);

        // DIMX dithering applies signed 3-bit matrix values only to 16-bit
        // framebuffer RGB, before the RGB5551 conversion.
        const uint64_t ditherMatrix =
            1ull | (7ull << 4u) | (2ull << 8u) | (6ull << 12u) |
            (3ull << 16u);
        GSPrimitiveBatch ditherPoint{};
        ditherPoint.vertexCount = 1u;
        ditherPoint.state.context.frame = {300u, 1u, GS_PSM_CT16, 0u};
        ditherPoint.state.context.scissor = triangle.state.context.scissor;
        ditherPoint.state.prim.type = GS_PRIM_POINT;
        ditherPoint.state.dthe = 1u;
        ditherPoint.state.dimx = ditherMatrix;
        ditherPoint.vertices[0].x = 0.0f;
        ditherPoint.vertices[0].y = 40.0f;
        ditherPoint.vertices[0].r = 15u;
        ditherPoint.vertices[0].g = 16u;
        ditherPoint.vertices[0].b = 23u;
        ditherPoint.vertices[0].a = 255u;
        backend.Submit(ditherPoint);

        GSPrimitiveBatch negativeDitherPoint = ditherPoint;
        negativeDitherPoint.vertices[0].x = 1.0f;
        negativeDitherPoint.vertices[0].r = 16u;
        negativeDitherPoint.vertices[0].g = 16u;
        negativeDitherPoint.vertices[0].b = 16u;
        backend.Submit(negativeDitherPoint);

        GSPrimitiveBatch rowDitherPoint = ditherPoint;
        rowDitherPoint.vertices[0].y = 41.0f;
        rowDitherPoint.vertices[0].r = 15u;
        rowDitherPoint.vertices[0].g = 15u;
        rowDitherPoint.vertices[0].b = 15u;
        backend.Submit(rowDitherPoint);

        GSPrimitiveBatch disabledDitherPoint = ditherPoint;
        disabledDitherPoint.state.dthe = 0u;
        disabledDitherPoint.vertices[0].x = 2.0f;
        backend.Submit(disabledDitherPoint);

        GSPrimitiveBatch dither16SPoint = ditherPoint;
        dither16SPoint.state.context.frame.psm = GS_PSM_CT16S;
        dither16SPoint.state.context.frame.fbp = 301u;
        backend.Submit(dither16SPoint);

        // COLCLAMP=0 wraps an over-range dither result to 8 bits; COLCLAMP=1
        // saturates it before the 5-bit framebuffer conversion.
        GSPrimitiveBatch wrappedDitherPoint = ditherPoint;
        wrappedDitherPoint.vertices[0].x = 4.0f;
        wrappedDitherPoint.vertices[0].r = 255u;
        wrappedDitherPoint.vertices[0].g = 0u;
        wrappedDitherPoint.vertices[0].b = 0u;
        wrappedDitherPoint.state.colclamp = 0u;
        backend.Submit(wrappedDitherPoint);

        GSPrimitiveBatch clampedDitherPoint = wrappedDitherPoint;
        clampedDitherPoint.vertices[0].x = 8.0f;
        clampedDitherPoint.state.colclamp = 1u;
        backend.Submit(clampedDitherPoint);

        // DTHE must not alter 32-bit framebuffer output.
        GSPrimitiveBatch dither32Point = ditherPoint;
        dither32Point.state.context.frame = {302u, 1u, GS_PSM_CT32, 0u};
        dither32Point.vertices[0].x = 0.0f;
        backend.Submit(dither32Point);

        // Indexed T8 textures must resolve their palette through TEX0/CLUT
        // state instead of treating the palette index as a direct color.
        const std::array<uint32_t, 4> palette = {
            0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
        for (uint32_t index = 0u; index < palette.size(); ++index) {
            backend.WriteVram(
                GS_PSM_CT32, 256u, 4u, index, 0u, palette[index]);
        }
        backend.WriteVram(GS_PSM_T8, 128u, 1u, 0u, 0u, 0u);
        backend.WriteVram(GS_PSM_T8, 128u, 1u, 1u, 0u, 1u);
        backend.WriteVram(GS_PSM_T8, 128u, 1u, 0u, 1u, 2u);
        backend.WriteVram(GS_PSM_T8, 128u, 1u, 1u, 1u, 3u);

        GSTex0Reg indexedTex0{};
        indexedTex0.tbp0 = 128u;
        indexedTex0.tbw = 1u;
        indexedTex0.psm = GS_PSM_T8;
        indexedTex0.tcc = 1u;
        indexedTex0.tfx = 0u;
        indexedTex0.cbp = 256u;
        indexedTex0.cpsm = GS_PSM_CT32;
        indexedTex0.csm = 1u;
        indexedTex0.cld = 1u;
        GSTexClutReg indexedClut{};
        indexedClut.cbw = 4u;
        backend.LoadClut(indexedTex0, indexedClut);

        GSPrimitiveBatch indexedSprite{};
        indexedSprite.vertexCount = 2u;
        indexedSprite.state.context.frame = triangle.state.context.frame;
        indexedSprite.state.context.scissor = triangle.state.context.scissor;
        indexedSprite.state.context.tex0 = indexedTex0;
        indexedSprite.state.prim.type = GS_PRIM_SPRITE;
        indexedSprite.state.prim.tme = true;
        indexedSprite.state.prim.fst = true;
        indexedSprite.state.textureWidth = 2u;
        indexedSprite.state.textureHeight = 2u;
        indexedSprite.vertices[0].x = 44.0f;
        indexedSprite.vertices[0].y = 12.0f;
        indexedSprite.vertices[0].u = 0u;
        indexedSprite.vertices[0].v = 0u;
        indexedSprite.vertices[1].x = 48.0f;
        indexedSprite.vertices[1].y = 16.0f;
        indexedSprite.vertices[1].u = 32u;
        indexedSprite.vertices[1].v = 32u;
        for (auto &vertex : indexedSprite.vertices) {
            vertex.r = 128u;
            vertex.g = 128u;
            vertex.b = 128u;
            vertex.a = 128u;
        }
        backend.Submit(indexedSprite);

        // Four-bit indexed textures use the same CLUT path with a 16-entry palette.
        const std::array<uint32_t, 16> palette4 = {
            0xFF000000u, 0xFFFFFF00u, 0xFF00FFFFu, 0xFFFF00FFu,
            0xFFFFFFFFu, 0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u,
            0xFF808080u, 0xFF404040u, 0xFF804000u, 0xFF008040u,
            0xFF400080u, 0xFF804080u, 0xFF408080u, 0xFF808040u};
        for (uint32_t index = 0u; index < palette4.size(); ++index) {
            backend.WriteVram(
                GS_PSM_CT32, 320u, 1u, index, 0u, palette4[index]);
        }
        backend.WriteVram(GS_PSM_T4, 192u, 1u, 0u, 0u, 1u);
        backend.WriteVram(GS_PSM_T4, 192u, 1u, 1u, 0u, 2u);
        backend.WriteVram(GS_PSM_T4, 192u, 1u, 0u, 1u, 3u);
        backend.WriteVram(GS_PSM_T4, 192u, 1u, 1u, 1u, 4u);

        GSTex0Reg indexed4Tex0{};
        indexed4Tex0.tbp0 = 192u;
        indexed4Tex0.tbw = 1u;
        indexed4Tex0.psm = GS_PSM_T4;
        indexed4Tex0.tcc = 1u;
        indexed4Tex0.tfx = 0u;
        indexed4Tex0.cbp = 320u;
        indexed4Tex0.cpsm = GS_PSM_CT32;
        indexed4Tex0.csm = 1u;
        indexed4Tex0.cld = 1u;
        GSTexClutReg indexed4Clut{};
        indexed4Clut.cbw = 1u;
        backend.LoadClut(indexed4Tex0, indexed4Clut);

        GSPrimitiveBatch indexed4Sprite{};
        indexed4Sprite.vertexCount = 2u;
        indexed4Sprite.state.context.frame = triangle.state.context.frame;
        indexed4Sprite.state.context.scissor = triangle.state.context.scissor;
        indexed4Sprite.state.context.tex0 = indexed4Tex0;
        indexed4Sprite.state.prim.type = GS_PRIM_SPRITE;
        indexed4Sprite.state.prim.tme = true;
        indexed4Sprite.state.prim.fst = true;
        indexed4Sprite.state.textureWidth = 2u;
        indexed4Sprite.state.textureHeight = 2u;
        indexed4Sprite.vertices[0].x = 52.0f;
        indexed4Sprite.vertices[0].y = 12.0f;
        indexed4Sprite.vertices[0].u = 0u;
        indexed4Sprite.vertices[0].v = 0u;
        indexed4Sprite.vertices[1].x = 56.0f;
        indexed4Sprite.vertices[1].y = 16.0f;
        indexed4Sprite.vertices[1].u = 32u;
        indexed4Sprite.vertices[1].v = 32u;
        for (auto &vertex : indexed4Sprite.vertices) {
            vertex.r = 128u;
            vertex.g = 128u;
            vertex.b = 128u;
            vertex.a = 128u;
        }
        backend.Submit(indexed4Sprite);

        // CSM1 swaps the 8/16 index bits in the source CLUT coordinates.
        // Entry 8 is loaded from source coordinate (0,1); entry 16 from (8,0).
        backend.WriteVram(GS_PSM_CT32, 400u, 1u, 0u, 1u, 0xFF0000FFu);
        backend.WriteVram(GS_PSM_CT32, 400u, 1u, 8u, 0u, 0xFFFF0000u);
        backend.WriteVram(GS_PSM_T8, 448u, 1u, 0u, 0u, 8u);
        backend.WriteVram(GS_PSM_T8, 448u, 1u, 1u, 0u, 16u);
        GSTex0Reg csm1Tex0{};
        csm1Tex0.tbp0 = 448u;
        csm1Tex0.tbw = 1u;
        csm1Tex0.psm = GS_PSM_T8;
        csm1Tex0.tcc = 1u;
        csm1Tex0.cbp = 400u;
        csm1Tex0.cpsm = GS_PSM_CT32;
        csm1Tex0.csm = 0u;
        csm1Tex0.cld = 1u;
        backend.LoadClut(csm1Tex0, GSTexClutReg{});
        GSPrimitiveBatch csm1Sprite{};
        csm1Sprite.vertexCount = 2u;
        csm1Sprite.state.context.frame = triangle.state.context.frame;
        csm1Sprite.state.context.scissor = triangle.state.context.scissor;
        csm1Sprite.state.context.tex0 = csm1Tex0;
        csm1Sprite.state.prim.type = GS_PRIM_SPRITE;
        csm1Sprite.state.prim.tme = true;
        csm1Sprite.state.prim.fst = true;
        csm1Sprite.state.textureWidth = 2u;
        csm1Sprite.state.textureHeight = 1u;
        csm1Sprite.vertices[0].x = 56.0f;
        csm1Sprite.vertices[0].y = 12.0f;
        csm1Sprite.vertices[0].u = 0u;
        csm1Sprite.vertices[0].v = 0u;
        csm1Sprite.vertices[1].x = 60.0f;
        csm1Sprite.vertices[1].y = 16.0f;
        csm1Sprite.vertices[1].u = 32u;
        csm1Sprite.vertices[1].v = 0u;
        for (auto &vertex : csm1Sprite.vertices) {
            vertex.r = 128u;
            vertex.g = 128u;
            vertex.b = 128u;
            vertex.a = 128u;
        }
        backend.Submit(csm1Sprite);

        // T4HL/T4HH select one of the two high nibbles in a 32-bit word.
        GSTex0Reg t4hlTex0 = indexed4Tex0;
        t4hlTex0.tbp0 = 224u;
        t4hlTex0.psm = GS_PSM_T4HL;
        backend.LoadClut(t4hlTex0, indexed4Clut);
        backend.WriteVram(GS_PSM_T4HL, 224u, 1u, 0u, 0u, 1u);
        GSPrimitiveBatch t4hlSprite{};
        t4hlSprite.vertexCount = 2u;
        t4hlSprite.state.context.frame = triangle.state.context.frame;
        t4hlSprite.state.context.scissor = triangle.state.context.scissor;
        t4hlSprite.state.context.tex0 = t4hlTex0;
        t4hlSprite.state.prim.type = GS_PRIM_SPRITE;
        t4hlSprite.state.prim.tme = true;
        t4hlSprite.state.prim.fst = true;
        t4hlSprite.state.textureWidth = 1u;
        t4hlSprite.state.textureHeight = 1u;
        t4hlSprite.vertices[0].x = 8.0f;
        t4hlSprite.vertices[0].y = 24.0f;
        t4hlSprite.vertices[1].x = 12.0f;
        t4hlSprite.vertices[1].y = 28.0f;
        for (auto &vertex : t4hlSprite.vertices) {
            vertex.r = 128u; vertex.g = 128u; vertex.b = 128u; vertex.a = 128u;
        }
        backend.Submit(t4hlSprite);
        const uint32_t t4hlRawIndex = backend.ReadVram(
            GS_PSM_T4HL, 224u, 1u, 0u, 0u);
        require(t4hlRawIndex == 1u,
                "T4HL VRAM read did not return the stored lower high-byte nibble.");

        GSTex0Reg t4hhTex0 = indexed4Tex0;
        t4hhTex0.tbp0 = 240u;
        t4hhTex0.psm = GS_PSM_T4HH;
        backend.LoadClut(t4hhTex0, indexed4Clut);
        backend.WriteVram(GS_PSM_T4HH, 240u, 1u, 0u, 0u, 4u);
        GSPrimitiveBatch t4hhSprite = t4hlSprite;
        t4hhSprite.state.context.tex0 = t4hhTex0;
        t4hhSprite.vertices[0].x = 12.0f;
        t4hhSprite.vertices[0].y = 24.0f;
        t4hhSprite.vertices[1].x = 16.0f;
        t4hhSprite.vertices[1].y = 28.0f;
        backend.Submit(t4hhSprite);

        // T8H reads indices from the high byte plane and resolves them via CSM2.
        const std::array<uint32_t, 5> palette8h = {
            0xFF000000u, 0xFFFFFF00u, 0xFF00FFFFu, 0xFFFF00FFu,
            0xFFFFFFFFu};
        for (uint32_t index = 0u; index < palette8h.size(); ++index) {
            backend.WriteVram(
                GS_PSM_CT32, 512u, 4u, index, 0u, palette8h[index]);
        }
        backend.WriteVram(GS_PSM_T8H, 480u, 1u, 0u, 0u, 1u);
        backend.WriteVram(GS_PSM_T8H, 480u, 1u, 1u, 0u, 2u);
        backend.WriteVram(GS_PSM_T8H, 480u, 1u, 0u, 1u, 3u);
        backend.WriteVram(GS_PSM_T8H, 480u, 1u, 1u, 1u, 4u);
        GSTex0Reg t8hTex0{};
        t8hTex0.tbp0 = 480u;
        t8hTex0.tbw = 1u;
        t8hTex0.psm = GS_PSM_T8H;
        t8hTex0.tcc = 1u;
        t8hTex0.cbp = 512u;
        t8hTex0.cpsm = GS_PSM_CT32;
        t8hTex0.csm = 1u;
        t8hTex0.cld = 1u;
        GSTexClutReg t8hClut{};
        t8hClut.cbw = 4u;
        backend.LoadClut(t8hTex0, t8hClut);
        GSPrimitiveBatch t8hSprite{};
        t8hSprite.vertexCount = 2u;
        t8hSprite.state.context.frame = triangle.state.context.frame;
        t8hSprite.state.context.scissor = triangle.state.context.scissor;
        t8hSprite.state.context.tex0 = t8hTex0;
        t8hSprite.state.prim.type = GS_PRIM_SPRITE;
        t8hSprite.state.prim.tme = true;
        t8hSprite.state.prim.fst = true;
        t8hSprite.state.textureWidth = 2u;
        t8hSprite.state.textureHeight = 2u;
        t8hSprite.vertices[0].x = 16.0f;
        t8hSprite.vertices[0].y = 24.0f;
        t8hSprite.vertices[0].u = 0u;
        t8hSprite.vertices[0].v = 0u;
        t8hSprite.vertices[1].x = 20.0f;
        t8hSprite.vertices[1].y = 28.0f;
        t8hSprite.vertices[1].u = 32u;
        t8hSprite.vertices[1].v = 32u;
        for (auto &vertex : t8hSprite.vertices) {
            vertex.r = 128u; vertex.g = 128u; vertex.b = 128u; vertex.a = 128u;
        }
        backend.Submit(t8hSprite);
        const uint32_t t8hRawIndex = backend.ReadVram(
            GS_PSM_T8H, 480u, 1u, 0u, 0u);
        require(t8hRawIndex == 1u,
                "T8H VRAM read did not return the stored high-byte index.");

        GSPresentationRequest presentation{};
        presentation.pmode = 1u;
        presentation.dispfb1 =
            (1ull << 9u) |
            (static_cast<uint64_t>(GS_PSM_CT32) << 15u);
        presentation.display1 =
            (63ull << 32u) | (63ull << 44u);
        const PresentationFrame frame = backend.Present(presentation);
        require(frame.width == 64u && frame.height == 64u,
                "CRTC presentation did not decode the requested 64x64 display mode.");
        require(frame.pixels.size() == 640u * 64u * 4u,
                "CRTC frame does not use the Waffle 640-pixel source row stride.");
        const auto pixelAt = [&frame](uint32_t x, uint32_t y) {
            const size_t offset =
                (static_cast<size_t>(y) * 640u + x) * 4u;
            return std::array<uint8_t, 4>{
                frame.pixels[offset + 0u],
                frame.pixels[offset + 1u],
                frame.pixels[offset + 2u],
                frame.pixels[offset + 3u]};
        };
        const auto trianglePixel = pixelAt(6u, 6u);
        require(trianglePixel[0] == 255u && trianglePixel[1] == 0u &&
                trianglePixel[2] == 0u && trianglePixel[3] == 255u,
                "CRTC frame did not contain the rasterized red triangle.");
        const auto spritePixel = pixelAt(28u, 28u);
        require(spritePixel[0] == 0u && spritePixel[1] == 0u &&
                spritePixel[2] == 255u && spritePixel[3] == 255u,
                "CRTC frame did not contain the rasterized blue sprite.");
        const auto textureTopLeft = pixelAt(36u, 12u);
        require(textureTopLeft[0] == 255u && textureTopLeft[1] == 0u &&
                textureTopLeft[2] == 0u && textureTopLeft[3] == 255u,
                "Textured GS sprite did not sample its top-left CT32 texel.");
        const auto textureBottomRight = pixelAt(39u, 15u);
        require(textureBottomRight[0] == 255u && textureBottomRight[1] == 255u &&
                textureBottomRight[2] == 255u && textureBottomRight[3] == 255u,
                "Textured GS sprite did not sample its bottom-right CT32 texel.");

        const auto reversedXTopLeft = pixelAt(36u, 20u);
        const auto reversedXTopRight = pixelAt(39u, 20u);
        require(reversedXTopLeft[0] == 0u && reversedXTopLeft[1] == 255u &&
                reversedXTopLeft[2] == 0u && reversedXTopLeft[3] == 255u &&
                reversedXTopRight[0] == 255u && reversedXTopRight[1] == 0u &&
                reversedXTopRight[2] == 0u && reversedXTopRight[3] == 255u,
                "GS sprite with reversed X endpoints did not mirror texture coordinates.");

        const auto reversedYTopLeft = pixelAt(36u, 28u);
        const auto reversedYBottomLeft = pixelAt(36u, 31u);
        require(reversedYTopLeft[0] == 0u && reversedYTopLeft[1] == 0u &&
                reversedYTopLeft[2] == 255u && reversedYTopLeft[3] == 255u &&
                reversedYBottomLeft[0] == 255u && reversedYBottomLeft[1] == 0u &&
                reversedYBottomLeft[2] == 0u && reversedYBottomLeft[3] == 255u,
                "GS sprite with reversed Y endpoints did not mirror texture coordinates.");

        const auto reversedBothTopLeft = pixelAt(36u, 36u);
        const auto reversedBothBottomRight = pixelAt(39u, 39u);
        require(reversedBothTopLeft[0] == 255u &&
                reversedBothTopLeft[1] == 255u &&
                reversedBothTopLeft[2] == 255u &&
                reversedBothTopLeft[3] == 255u &&
                reversedBothBottomRight[0] == 255u &&
                reversedBothBottomRight[1] == 0u &&
                reversedBothBottomRight[2] == 0u &&
                reversedBothBottomRight[3] == 255u,
                "GS sprite with reversed X and Y endpoints did not preserve UV orientation.");

        const auto fullFogPixel = pixelAt(4u, 48u);
        require(fullFogPixel[0] == 50u && fullFogPixel[1] == 100u &&
                fullFogPixel[2] == 200u && fullFogPixel[3] == 255u,
                "GS F=0 fog did not reproduce FOGCOL exactly.");
        const auto halfFogPixel = pixelAt(5u, 48u);
        require(halfFogPixel[0] == 75u && halfFogPixel[1] == 125u &&
                halfFogPixel[2] == 200u,
                "GS F=128 fog did not blend the source color with FOGCOL.");

        for (size_t i = 0u; i < fogFactors.size(); ++i) {
            const auto firstPatternPixel = pixelAt(10u + static_cast<uint32_t>(i), 44u);
            require(firstPatternPixel[0] == firstFogExpected[i][0] &&
                    firstPatternPixel[1] == firstFogExpected[i][1] &&
                    firstPatternPixel[2] == firstFogExpected[i][2],
                    "GS fog RGB pattern group one differs from the reference values.");
            const auto secondPatternPixel = pixelAt(20u + static_cast<uint32_t>(i), 44u);
            require(secondPatternPixel[0] == secondFogExpected[i][0] &&
                    secondPatternPixel[1] == secondFogExpected[i][1] &&
                    secondPatternPixel[2] == secondFogExpected[i][2],
                    "GS fog RGB pattern group two differs from the reference values.");
        }
        const auto minimumFogPixel = pixelAt(6u, 48u);
        require(minimumFogPixel[0] == 99u && minimumFogPixel[1] == 149u &&
                minimumFogPixel[2] == 200u,
                "GS F=255 fog did not preserve the 1/256 FOGCOL contribution.");
        const auto fogDisabledPixel = pixelAt(7u, 48u);
        require(fogDisabledPixel[0] == 100u && fogDisabledPixel[1] == 150u &&
                fogDisabledPixel[2] == 200u,
                "PRIM.FGE=0 changed a pixel despite fog being disabled.");
        const uint32_t fogAlphaStored = backend.ReadVram(
            GS_PSM_CT32, 0u, 1u, 4u, 48u);
        require((fogAlphaStored >> 24u) == 173u,
                "GS fog modified the source alpha channel.");
        const auto foggedTexturePixel = pixelAt(8u, 48u);
        require(foggedTexturePixel[0] == 50u &&
                foggedTexturePixel[1] == 100u &&
                foggedTexturePixel[2] == 200u,
                "GS fog was not applied after the texture color function.");
        const auto interpolatedFogPixel = pixelAt(18u, 48u);
        require(interpolatedFogPixel[0] == 62u &&
                interpolatedFogPixel[1] == 112u &&
                interpolatedFogPixel[2] == 200u,
                "GS fog factor was not linearly interpolated along the line.");

        const auto triangleFogPixel = pixelAt(14u, 54u);
        require(triangleFogPixel[0] == 73u &&
                triangleFogPixel[1] == 123u &&
                triangleFogPixel[2] == 200u,
                "GS triangle F was not barycentrically interpolated.");
        const auto spriteFogPixel = pixelAt(25u, 53u);
        require(spriteFogPixel[0] == 50u &&
                spriteFogPixel[1] == 100u &&
                spriteFogPixel[2] == 200u,
                "GS sprite fog did not use the second endpoint F value.");

        const uint32_t positiveDither = backend.ReadVram(
            GS_PSM_CT16, 300u, 1u, 0u, 40u);
        require((positiveDither & 0x7FFFu) == 0x0C42u &&
                (positiveDither & 0x8000u) != 0u,
                "GS DIMX positive dither did not adjust CT16 RGB before packing.");
        const uint32_t negativeDither = backend.ReadVram(
            GS_PSM_CT16, 300u, 1u, 1u, 40u);
        require((negativeDither & 0x7FFFu) == 0x0421u,
                "GS DIMX signed negative matrix value was not sign-extended.");
        const uint32_t nextDitherRow = backend.ReadVram(
            GS_PSM_CT16, 300u, 1u, 0u, 41u);
        require((nextDitherRow & 0x7FFFu) == 0x0842u,
                "GS DIMX did not select its next matrix row using framebuffer Y.");
        const uint32_t ditherDisabled = backend.ReadVram(
            GS_PSM_CT16, 300u, 1u, 2u, 40u);
        require((ditherDisabled & 0x7FFFu) == 0x0841u,
                "GS DTHE=0 modified a 16-bit framebuffer pixel.");
        const uint32_t dither16S = backend.ReadVram(
            GS_PSM_CT16S, 301u, 1u, 0u, 40u);
        require((dither16S & 0x7FFFu) == 0x0C42u,
                "GS DIMX dithering was not applied to CT16S framebuffers.");
        const uint32_t wrappedDither = backend.ReadVram(
            GS_PSM_CT16, 300u, 1u, 4u, 40u);
        require((wrappedDither & 0x7FFFu) == 0u,
                "GS COLCLAMP=0 did not wrap an over-range dither result.");
        const uint32_t clampedDither = backend.ReadVram(
            GS_PSM_CT16, 300u, 1u, 8u, 40u);
        require((clampedDither & 0x7FFFu) == 0x001Fu,
                "GS COLCLAMP=1 did not saturate an over-range dither result.");
        const uint32_t dither32 = backend.ReadVram(
            GS_PSM_CT32, 302u, 1u, 0u, 40u);
        require(dither32 == 0xFF17100Fu,
                "GS DTHE unexpectedly modified a CT32 framebuffer.");
        const auto indexedTopLeft = pixelAt(44u, 12u);
        require(indexedTopLeft[0] == 255u && indexedTopLeft[1] == 0u &&
                indexedTopLeft[2] == 0u && indexedTopLeft[3] == 255u,
                "T8 GS sprite did not resolve its top-left CLUT entry.");
        const auto indexedBottomRight = pixelAt(47u, 15u);
        require(indexedBottomRight[0] == 255u && indexedBottomRight[1] == 255u &&
                indexedBottomRight[2] == 255u && indexedBottomRight[3] == 255u,
                "T8 GS sprite did not resolve its bottom-right CLUT entry.");
        const auto indexed4TopLeft = pixelAt(52u, 12u);
        require(indexed4TopLeft[0] == 0u && indexed4TopLeft[1] == 255u &&
                indexed4TopLeft[2] == 255u && indexed4TopLeft[3] == 255u,
                "T4 GS sprite did not resolve its top-left 16-entry CLUT color.");
        const auto indexed4BottomRight = pixelAt(55u, 15u);
        require(indexed4BottomRight[0] == 255u && indexed4BottomRight[1] == 255u &&
                indexed4BottomRight[2] == 255u && indexed4BottomRight[3] == 255u,
                "T4 GS sprite did not resolve its bottom-right 16-entry CLUT color.");
        const auto csm1First = pixelAt(56u, 12u);
        require(csm1First[0] == 255u && csm1First[1] == 0u &&
                csm1First[2] == 0u && csm1First[3] == 255u,
                "CSM1 T8 texture did not load CLUT entry 8 from the swapped source index 16.");
        const auto csm1Second = pixelAt(59u, 12u);
        require(csm1Second[0] == 0u && csm1Second[1] == 0u &&
                csm1Second[2] == 255u && csm1Second[3] == 255u,
                "CSM1 T8 texture did not load CLUT entry 16 from the swapped source index 8.");
        const auto t4hlPixel = pixelAt(8u, 24u);
        if (!(t4hlPixel[0] == 0u && t4hlPixel[1] == 255u &&
              t4hlPixel[2] == 255u && t4hlPixel[3] == 255u)) {
            std::cerr << "T4HL sampled RGBA="
                      << static_cast<unsigned>(t4hlPixel[0]) << ','
                      << static_cast<unsigned>(t4hlPixel[1]) << ','
                      << static_cast<unsigned>(t4hlPixel[2]) << ','
                      << static_cast<unsigned>(t4hlPixel[3]) << '\n';
        }
        require(t4hlPixel[0] == 0u && t4hlPixel[1] == 255u &&
                t4hlPixel[2] == 255u && t4hlPixel[3] == 255u,
                "T4HL did not resolve CLUT index one from its high-byte plane.");
        const auto t4hhPixel = pixelAt(12u, 24u);
        require(t4hhPixel[0] == 255u && t4hhPixel[1] == 255u &&
                t4hhPixel[2] == 255u && t4hhPixel[3] == 255u,
                "T4HH did not read the upper nibble of the high-byte plane.");
        const auto t8hPixel = pixelAt(16u, 24u);
        if (!(t8hPixel[0] == 0u && t8hPixel[1] == 255u &&
              t8hPixel[2] == 255u && t8hPixel[3] == 255u)) {
            std::cerr << "T8H sampled RGBA="
                      << static_cast<unsigned>(t8hPixel[0]) << ','
                      << static_cast<unsigned>(t8hPixel[1]) << ','
                      << static_cast<unsigned>(t8hPixel[2]) << ','
                      << static_cast<unsigned>(t8hPixel[3]) << '\n';
        }
        require(t8hPixel[0] == 0u && t8hPixel[1] == 255u &&
                t8hPixel[2] == 255u && t8hPixel[3] == 255u,
                "T8H did not resolve CLUT index one from its high-byte plane.");
        const auto t8hBottomRight = pixelAt(19u, 27u);
        require(t8hBottomRight[0] == 255u && t8hBottomRight[1] == 255u &&
                t8hBottomRight[2] == 255u && t8hBottomRight[3] == 255u,
                "T8H did not sample the bottom-right CLUT index.");
        const auto emptyPixel = pixelAt(61u, 30u);
        require(emptyPixel[0] == 0u && emptyPixel[1] == 0u &&
                emptyPixel[2] == 0u,
                "CRTC frame contains unexpected color outside the rasterized primitives.");

        // PMODE must compose circuit 1 over circuit 2 using source alpha x2.
        backend.WriteVram(GS_PSM_CT32, 0u, 1u, 40u, 20u, 0x400000FFu);
        backend.WriteVram(GS_PSM_CT32, 32u, 1u, 40u, 20u, 0xFFFF0000u);
        GSPresentationRequest dualCircuit{};
        dualCircuit.pmode = 3u;
        dualCircuit.dispfb1 = presentation.dispfb1;
        dualCircuit.display1 = presentation.display1;
        dualCircuit.dispfb2 = (32ull) | (1ull << 9u) |
            (static_cast<uint64_t>(GS_PSM_CT32) << 15u);
        dualCircuit.display2 = presentation.display1;
        const PresentationFrame blended = backend.Present(dualCircuit);
        const auto pixelAtFrame = [](const PresentationFrame &source,
                                     uint32_t x, uint32_t y) {
            const size_t offset = (static_cast<size_t>(y) * 640u + x) * 4u;
            return std::array<uint8_t, 4>{
                source.pixels[offset + 0u], source.pixels[offset + 1u],
                source.pixels[offset + 2u], source.pixels[offset + 3u]};
        };
        const auto blendedPixel = pixelAtFrame(blended, 40u, 20u);
        require(blendedPixel[0] >= 127u && blendedPixel[0] <= 129u &&
                blendedPixel[1] == 0u &&
                blendedPixel[2] >= 126u && blendedPixel[2] <= 128u,
                "PMODE did not blend circuit 1 over circuit 2 using alpha x2.");

        // MMOD selects fixed ALP instead of circuit 1's own alpha.
        dualCircuit.pmode = 3u | (1ull << 5u) | (64ull << 8u);
        const PresentationFrame fixedBlend = backend.Present(dualCircuit);
        const auto fixedPixel = pixelAtFrame(fixedBlend, 40u, 20u);
        require(fixedPixel[0] >= 63u && fixedPixel[0] <= 65u &&
                fixedPixel[2] >= 190u && fixedPixel[2] <= 192u,
                "PMODE MMOD did not use the fixed ALP blend factor.");

        // SLBG uses BGCOLOR as circuit 2's base before circuit 1 blending.
        dualCircuit.pmode = 3u | (1ull << 7u);
        dualCircuit.bgcolor = 0x0000FF00u;
        const PresentationFrame backgroundBlend = backend.Present(dualCircuit);
        const auto backgroundPixel = pixelAtFrame(backgroundBlend, 40u, 20u);
        require(backgroundPixel[0] >= 127u && backgroundPixel[0] <= 129u &&
                backgroundPixel[1] >= 126u && backgroundPixel[1] <= 128u &&
                backgroundPixel[2] == 0u,
                "PMODE SLBG did not blend circuit 1 over BGCOLOR.");

        // The fragment path must depth-test before writing both color and Z.
        GSPrimitiveBatch depthPoint{};
        depthPoint.vertexCount = 1u;
        depthPoint.state.context.frame = triangle.state.context.frame;
        depthPoint.state.context.scissor = triangle.state.context.scissor;
        depthPoint.state.context.zbuf = {384u, GS_PSM_Z32, false};
        depthPoint.state.context.test = (1ull << 16u) | (2ull << 17u);
        depthPoint.state.prim.type = GS_PRIM_POINT;
        depthPoint.vertices[0].x = 52.0f;
        depthPoint.vertices[0].y = 20.0f;
        depthPoint.vertices[0].z = 100.0;
        depthPoint.vertices[0].r = 255u;
        depthPoint.vertices[0].g = 0u;
        depthPoint.vertices[0].b = 0u;
        depthPoint.vertices[0].a = 255u;
        backend.Submit(depthPoint);
        depthPoint.vertices[0].z = 50.0;
        depthPoint.vertices[0].r = 0u;
        depthPoint.vertices[0].b = 255u;
        backend.Submit(depthPoint);
        require(backend.ReadVram(GS_PSM_Z32, 384u, 1u, 52u, 20u) == 100u,
                "GS GEQUAL depth test did not preserve the nearest Z value.");

        // A failed alpha test with AFAIL=KEEP must leave the framebuffer intact.
        backend.WriteVram(GS_PSM_CT32, 0u, 1u, 54u, 20u, 0xFF00FF00u);
        GSPrimitiveBatch alphaPoint{};
        alphaPoint.vertexCount = 1u;
        alphaPoint.state.context.frame = triangle.state.context.frame;
        alphaPoint.state.context.scissor = triangle.state.context.scissor;
        alphaPoint.state.context.test = 1ull | (4ull << 1u) | (128ull << 4u);
        alphaPoint.state.prim.type = GS_PRIM_POINT;
        alphaPoint.vertices[0].x = 54.0f;
        alphaPoint.vertices[0].y = 20.0f;
        alphaPoint.vertices[0].r = 255u;
        alphaPoint.vertices[0].g = 0u;
        alphaPoint.vertices[0].b = 0u;
        alphaPoint.vertices[0].a = 64u;
        backend.Submit(alphaPoint);
        require(backend.ReadVram(GS_PSM_CT32, 0u, 1u, 54u, 20u) == 0xFF00FF00u,
                "GS alpha-test KEEP action modified the framebuffer.");

        // DATE compares the stored destination alpha bit before fragment writes.
        backend.WriteVram(GS_PSM_CT32, 0u, 1u, 56u, 20u, 0xFF0000FFu);
        GSPrimitiveBatch datePoint = alphaPoint;
        datePoint.state.context.test = 1ull << 14u; // DATE=1, DATM=0
        datePoint.vertices[0].x = 56.0f;
        datePoint.vertices[0].r = 0u;
        datePoint.vertices[0].g = 255u;
        datePoint.vertices[0].b = 0u;
        datePoint.vertices[0].a = 255u;
        backend.Submit(datePoint);
        require(backend.ReadVram(GS_PSM_CT32, 0u, 1u, 56u, 20u) == 0xFF0000FFu,
                "GS DATE did not reject a destination with alpha bit set.");

        // GS ALPHA=(Cs-Cd)*As/128+Cd blends the source red over destination blue.
        backend.WriteVram(GS_PSM_CT32, 0u, 1u, 58u, 20u, 0xFFFF0000u);
        GSPrimitiveBatch blendPoint{};
        blendPoint.vertexCount = 1u;
        blendPoint.state.context.frame = triangle.state.context.frame;
        blendPoint.state.context.scissor = triangle.state.context.scissor;
        blendPoint.state.context.alpha = 0x44u;
        blendPoint.state.colclamp = 1u;
        blendPoint.state.prim.type = GS_PRIM_POINT;
        blendPoint.state.prim.abe = true;
        blendPoint.vertices[0].x = 58.0f;
        blendPoint.vertices[0].y = 20.0f;
        blendPoint.vertices[0].r = 255u;
        blendPoint.vertices[0].g = 0u;
        blendPoint.vertices[0].b = 0u;
        blendPoint.vertices[0].a = 64u;
        backend.Submit(blendPoint);
        const uint32_t blendedVram =
            backend.ReadVram(GS_PSM_CT32, 0u, 1u, 58u, 20u);
        require((blendedVram & 0xFFu) == 127u &&
                ((blendedVram >> 16u) & 0xFFu) == 127u,
                "GS ALPHA fixed-point equation did not produce the expected RGB blend.");

        std::cout << "Android GS VRAM adapter tests passed.\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "Android GS VRAM adapter test failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
