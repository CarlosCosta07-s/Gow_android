#pragma once

#include <string>
#include <vector>

#include "runtime/gs/gs_types.h"

class GSTextureStore;

namespace gow::android {

// Initial software GS path: opaque CT32/CT24/CT16 primitives with optional decoded texture sampling.
// Writes pixels into the same 4 MiB swizzled VRAM image used by CDVD/GS.
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
