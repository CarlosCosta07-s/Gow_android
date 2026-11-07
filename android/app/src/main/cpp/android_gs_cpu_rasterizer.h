#pragma once

#include <string>
#include <vector>

#include "runtime/gs/gs_types.h"

class GSTextureStore;

namespace gow::android {

// CPU GS rasterizer for CT32/CT24/CT16/CT16S framebuffers. Supports decoded texture
// sampling, GS alpha/depth tests and GS alpha blending; FGE fog and AA1 coverage remain
// unsupported and are rejected explicitly. Writes into the 4 MiB swizzled PS2 VRAM image.
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
