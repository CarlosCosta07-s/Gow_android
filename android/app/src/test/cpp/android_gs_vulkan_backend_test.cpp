#include "android_gs_vulkan_backend.h"

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

        std::cout << "Android GS VRAM adapter tests passed.\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "Android GS VRAM adapter test failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
