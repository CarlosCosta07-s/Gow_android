#include "android_gs_geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gow::android {
namespace {

GsGpuVertex convertVertex(
    const GSVertex &vertex,
    const GSVertex &flatColor,
    uint8_t stateDepthPsm,
    float xOffset,
    float yOffset) {
    GsGpuVertex result;
    result.x = vertex.x - xOffset;
    result.y = vertex.y - yOffset;
    double depthMaximum = 4294967295.0;
    switch (stateDepthPsm) {
    case GS_PSM_Z16:
    case GS_PSM_Z16S:
        depthMaximum = 65535.0;
        break;
    case GS_PSM_Z24:
        depthMaximum = 16777215.0;
        break;
    default:
        break;
    }
    result.z = static_cast<float>(std::clamp(
        vertex.z / depthMaximum, 0.0, 1.0));
    result.r = static_cast<float>(flatColor.r) / 255.0f;
    result.g = static_cast<float>(flatColor.g) / 255.0f;
    result.b = static_cast<float>(flatColor.b) / 255.0f;
    result.a = static_cast<float>(flatColor.a) / 255.0f;
    result.s = vertex.s;
    result.t = vertex.t;
    result.q = std::isfinite(vertex.q) && std::fabs(vertex.q) > 1.0e-8f
        ? vertex.q : 1.0f;
    result.u = static_cast<float>(vertex.u) / 16.0f;
    result.v = static_cast<float>(vertex.v) / 16.0f;
    result.fog = static_cast<float>(vertex.fog) / 255.0f;
    return result;
}

void appendSprite(
    const GsGpuVertex &first,
    const GsGpuVertex &second,
    std::vector<GsGpuVertex> &out) {
    GsGpuVertex a = first;
    GsGpuVertex b = first;
    GsGpuVertex c = second;
    GsGpuVertex d = second;

    a.x = first.x;  a.y = first.y;
    a.u = first.u;  a.v = first.v;
    b.x = second.x; b.y = first.y;
    b.u = second.u; b.v = first.v;
    c.x = first.x;  c.y = second.y;
    c.u = first.u;  c.v = second.v;
    d.x = second.x; d.y = second.y;
    d.u = second.u; d.v = second.v;

    out.insert(out.end(), {a, b, c, c, b, d});
}

} // namespace

bool expandGsPrimitiveBatch(
    const GSPrimitiveBatch &source,
    GsGpuBatch &destination,
    std::string &error) {
    destination = {};
    error.clear();

    if (source.vertexCount == 0u || source.vertexCount > 3u) {
        error = "GS batch vertex count is outside the supported range 1..3.";
        return false;
    }

    const GSDrawState &state = source.state;
    const GSContext &context = state.context;
    const uint32_t requiredVertices =
        state.prim.type == GS_PRIM_POINT ? 1u :
        (state.prim.type == GS_PRIM_LINE ||
         state.prim.type == GS_PRIM_LINESTRIP ||
         state.prim.type == GS_PRIM_SPRITE) ? 2u : 3u;
    if (source.vertexCount != requiredVertices) {
        error = "GS primitive type and vertex count do not match.";
        return false;
    }

    destination.targetWidth = context.frame.fbw != 0u
        ? static_cast<uint32_t>(context.frame.fbw) * 64u
        : std::max<uint32_t>(640u, static_cast<uint32_t>(context.scissor.x1) + 1u);
    destination.targetHeight = std::max<uint32_t>(
        1u, static_cast<uint32_t>(context.scissor.y1) + 1u);
    destination.scissorX0 = context.scissor.x0;
    destination.scissorY0 = context.scissor.y0;
    destination.scissorX1 = context.scissor.x1;
    destination.scissorY1 = context.scissor.y1;
    destination.frameBase = context.frame.fbp;
    destination.framePsm = context.frame.psm;
    destination.depthBase = context.zbuf.zbp;
    destination.depthPsm = context.zbuf.psm;
    destination.textured = state.prim.tme;
    destination.gouraud = state.prim.iip;
    destination.blend = state.prim.abe;
    destination.depthTest = ((context.test >> 16u) & 1u) != 0u;
    destination.depthWrite = !context.zbuf.zmask;
    destination.fogEnabled = state.prim.fge;

    const float xOffset = static_cast<float>(context.xyoffset.ofx) / 16.0f;
    const float yOffset = static_cast<float>(context.xyoffset.ofy) / 16.0f;
    GsGpuVertex converted[3]{};
    const GSVertex &flatColor = source.vertices[source.vertexCount - 1u];
    for (uint32_t i = 0u; i < source.vertexCount; ++i) {
        const GSVertex &color = state.prim.iip ? source.vertices[i] : flatColor;
        converted[i] = convertVertex(
            source.vertices[i], color, context.zbuf.psm, xOffset, yOffset);
    }

    switch (state.prim.type) {
    case GS_PRIM_POINT:
        destination.topology = GsPrimitiveTopology::Points;
        destination.vertices.push_back(converted[0]);
        break;
    case GS_PRIM_LINE:
    case GS_PRIM_LINESTRIP:
        destination.topology = GsPrimitiveTopology::Lines;
        destination.vertices.push_back(converted[0]);
        destination.vertices.push_back(converted[1]);
        break;
    case GS_PRIM_SPRITE:
        destination.topology = GsPrimitiveTopology::Triangles;
        appendSprite(converted[0], converted[1], destination.vertices);
        break;
    case GS_PRIM_TRIANGLE:
    case GS_PRIM_TRISTRIP:
    case GS_PRIM_TRIFAN:
        destination.topology = GsPrimitiveTopology::Triangles;
        destination.vertices.insert(
            destination.vertices.end(), converted, converted + 3);
        break;
    default:
        error = "Unsupported GS primitive type.";
        destination = {};
        return false;
    }

    return !destination.vertices.empty();
}

} // namespace gow::android
