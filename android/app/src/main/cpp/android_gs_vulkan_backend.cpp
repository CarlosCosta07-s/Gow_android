#include "android_gs_vulkan_backend.h"

#include "gs_texture_store.h"
#include "android_gs_cpu_rasterizer.h"
#include "runtime/gs/ps2_gs_common.h"

#include <algorithm>
#include <android/log.h>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gow::android {
namespace {

std::atomic<bool> gUnsupportedBatchLogged{false};

} // namespace

struct AndroidGSVulkanBackend::Impl {
    void *userData = nullptr;
    GeometrySink geometrySink = nullptr;
    GSTextureStore textureStore;
    uint8_t *vram = nullptr;
    uint32_t vramSize = 0u;
    bool initialized = false;
};

AndroidGSVulkanBackend::AndroidGSVulkanBackend(
    void *userData, GeometrySink geometrySink)
    : impl_(std::make_unique<Impl>()) {
    impl_->userData = userData;
    impl_->geometrySink = geometrySink;
}

AndroidGSVulkanBackend::~AndroidGSVulkanBackend() {
    Flush();
}

void AndroidGSVulkanBackend::Initialize(uint8_t *vram, uint32_t vramSize) {
    if (vram == nullptr || vramSize < 4u * 1024u * 1024u) {
        throw std::invalid_argument(
            "Android GS backend requires the full 4 MiB PS2 VRAM image.");
    }
    impl_->textureStore.Initialize(vram, vramSize);
    impl_->vram = vram;
    impl_->vramSize = vramSize;
    impl_->initialized = true;
}

void AndroidGSVulkanBackend::Reset() {
    if (!impl_->initialized) return;
    impl_->textureStore.Reset();
}

void AndroidGSVulkanBackend::Submit(const GSPrimitiveBatch &batch) {
    if (!impl_->initialized) return;

    std::string error;
    std::vector<uint32_t> decodedTexture;
    const std::vector<uint32_t> *texturePixels = nullptr;
    if (batch.state.prim.tme) {
        impl_->textureStore.DecodeTextureRgba(batch.state, decodedTexture);
        texturePixels = &decodedTexture;
    }
    if (!rasterizeGsBatchToVram(
            impl_->textureStore, batch, texturePixels, error)) {
        bool expected = false;
        if (gUnsupportedBatchLogged.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            __android_log_print(
                ANDROID_LOG_WARN, "GowAndroid",
                "GS CPU rasterizer skipped a batch with unsupported state: %s",
                error.empty() ? "no detail supplied" : error.c_str());
        }
        return;
    }

    // The optional sink remains available for geometry diagnostics, but the
    // default Android GameActivity path presents the VRAM-derived CRTC frame.
    if (impl_->geometrySink != nullptr) {
        GsGpuBatch geometry;
        if (expandGsPrimitiveBatch(batch, geometry, error)) {
            impl_->geometrySink(impl_->userData, geometry);
        }
    }
}

void AndroidGSVulkanBackend::LoadClut(
    const GSTex0Reg &tex0, const GSTexClutReg &texclut) {
    if (impl_->initialized) {
        impl_->textureStore.LoadClut(tex0, texclut);
    }
}

void AndroidGSVulkanBackend::BeginTransfer(
    const GSTransferCommand &command) {
    if (impl_->initialized) {
        impl_->textureStore.BeginTransfer(command);
    }
}

void AndroidGSVulkanBackend::UploadImage(
    const uint8_t *data, uint32_t sizeBytes) {
    if (impl_->initialized && data != nullptr && sizeBytes != 0u) {
        impl_->textureStore.Upload(data, sizeBytes);
    }
}

void AndroidGSVulkanBackend::Flush() {}
void AndroidGSVulkanBackend::TextureFlush() {}
void AndroidGSVulkanBackend::Sync(GSSyncReason) {}

PresentationFrame AndroidGSVulkanBackend::Present(
    const GSPresentationRequest &request) {
    if (!impl_->initialized) return {};
    return presentGsVramCircuit(impl_->textureStore, request);
}

bool AndroidGSVulkanBackend::ClearFramebuffer(
    const GSContext &context, uint32_t rgba) {
    if (!impl_->initialized || context.frame.fbw == 0u) return false;
    const uint32_t width = static_cast<uint32_t>(context.frame.fbw) * 64u;
    const uint32_t x0 = std::min<uint32_t>(context.scissor.x0, width);
    const uint32_t x1 = std::min<uint32_t>(context.scissor.x1, width - 1u);
    const uint32_t y0 = context.scissor.y0;
    const uint32_t y1 = std::min<uint32_t>(context.scissor.y1, 2047u);
    if (x0 > x1 || y0 > y1) return false;

    uint32_t frameMask = context.frame.fbmsk;
    if (context.frame.psm == GS_PSM_CT16 ||
        context.frame.psm == GS_PSM_CT16S) {
        frameMask = GSInternal::frameMask16(frameMask);
    }
    for (uint32_t y = y0; y <= y1; ++y) {
        for (uint32_t x = x0; x <= x1; ++x) {
            const uint32_t previous = impl_->textureStore.Read(
                context.frame.psm,
                context.frame.fbp,
                context.frame.fbw,
                x,
                y);
            const uint32_t merged =
                (previous & frameMask) | (rgba & ~frameMask);
            impl_->textureStore.Write(
                context.frame.psm,
                context.frame.fbp,
                context.frame.fbw,
                x,
                y,
                merged);
        }
    }
    return true;
}

uint32_t AndroidGSVulkanBackend::ConsumeLocalToHostBytes(
    uint8_t *, uint32_t) {
    return 0u;
}

uint32_t AndroidGSVulkanBackend::ReadVram(
    uint32_t psm, uint32_t base, uint32_t bw,
    uint32_t x, uint32_t y) const {
    return impl_->initialized
        ? impl_->textureStore.Read(psm, base, bw, x, y)
        : 0u;
}

void AndroidGSVulkanBackend::WriteVram(
    uint32_t psm, uint32_t base, uint32_t bw,
    uint32_t x, uint32_t y, uint32_t value) {
    if (impl_->initialized) {
        impl_->textureStore.Write(psm, base, bw, x, y, value);
    }
}

void AndroidGSVulkanBackend::SnapshotVram(std::vector<uint8_t> &out) const {
    if (impl_->initialized) {
        impl_->textureStore.Snapshot(out);
    } else {
        out.clear();
    }
}

GSTransferSnapshot AndroidGSVulkanBackend::GetTransferSnapshot() const {
    return impl_->initialized
        ? impl_->textureStore.TransferState()
        : GSTransferSnapshot{};
}

} // namespace gow::android
