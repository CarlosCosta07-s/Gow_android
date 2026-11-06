#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "runtime/gs/gs_types.h"

namespace gow::android {

enum class GsPrimitiveTopology : uint8_t {
    Points,
    Lines,
    Triangles,
};

struct GsGpuVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    float s = 0.0f;
    float t = 0.0f;
    float q = 1.0f;
    float u = 0.0f;
    float v = 0.0f;
    float fog = 0.0f;
};

struct GsGpuBatch {
    std::vector<GsGpuVertex> vertices;
    GsPrimitiveTopology topology = GsPrimitiveTopology::Triangles;
    uint32_t targetWidth = 640u;
    uint32_t targetHeight = 448u;
    uint16_t scissorX0 = 0u;
    uint16_t scissorY0 = 0u;
    uint16_t scissorX1 = 639u;
    uint16_t scissorY1 = 447u;
    uint32_t frameBase = 0u;
    uint8_t framePsm = GS_PSM_CT32;
    uint32_t depthBase = 0u;
    uint8_t depthPsm = GS_PSM_Z32;
    bool textured = false;
    bool gouraud = false;
    bool blend = false;
    bool depthTest = false;
    bool depthWrite = false;
    bool fogEnabled = false;
};

// Converts one already-decoded Waffle GS batch into GPU-ready vertices.
// Coordinates remain in PS2 pixel units; XYOFFSET is removed here.
bool expandGsPrimitiveBatch(
    const GSPrimitiveBatch &source,
    GsGpuBatch &destination,
    std::string &error);

} // namespace gow::android
