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

        GSPresentationRequest presentation{};
        presentation.pmode = 1u;
        presentation.dispfb1 =
            (1ull << 9u) |
            (static_cast<uint64_t>(GS_PSM_CT32) << 15u);
        presentation.display1 =
            (63ull << 32u) | (31ull << 44u);
        const PresentationFrame frame = backend.Present(presentation);
        require(frame.width == 64u && frame.height == 32u,
                "CRTC presentation did not decode the requested 64x32 display mode.");
        require(frame.pixels.size() == 640u * 32u * 4u,
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
        const auto emptyPixel = pixelAt(60u, 30u);
        require(emptyPixel[0] == 0u && emptyPixel[1] == 0u &&
                emptyPixel[2] == 0u,
                "CRTC frame contains unexpected color outside the rasterized primitives.");

        std::cout << "Android GS VRAM adapter tests passed.\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "Android GS VRAM adapter test failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
