#pragma once

#include <string>
#include <vector>

#include "runtime/gs/gs_types.h"

class GSTextureStore;

namespace gow::android {

// CPU GS rasterizer for CT32/CT24/CT16/CT16S framebuffers. Supports decoded texture
// sampling, FGE fog, GS alpha/depth tests and GS alpha blending; AA1 coverage
// remains unsupported and is rejected explicitly. Writes into 4 MiB swizzled VRAM.
bool rasterizeGsBatchToVram(
    GSTextureStore &store,
    const GSPrimitiveBatch &batch,
    const std::vector<uint32_t> *decodedTexture,
    std::string &error);

// Reads a single active CRTC circuit from VRAM and returns the Waffle host
// frame layout (640 RGBA pixels per source row, even when visible width < 640).
PresentationFrame presentGsVramCircuit(
    const GSTextureStore &store,
    const GSPresentationRequest &request);

} // namespace gow::android
