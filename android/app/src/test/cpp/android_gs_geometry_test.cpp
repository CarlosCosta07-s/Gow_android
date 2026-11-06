#include "android_gs_geometry.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

GSPrimitiveBatch makeBatch(GSPrimType type, uint8_t count) {
    GSPrimitiveBatch batch{};
    batch.vertexCount = count;
    batch.state.prim.type = type;
    batch.state.context.frame.fbw = 10u;
    batch.state.context.frame.fbp = 0x120u;
    batch.state.context.frame.psm = GS_PSM_CT32;
    batch.state.context.scissor = {3u, 600u, 5u, 430u};
    batch.state.context.xyoffset = {16u, 32u};
    batch.state.context.zbuf = {0x80u, GS_PSM_Z24, false};
    batch.state.context.test = 1ull << 16u;
    batch.state.prim.abe = true;
    batch.state.prim.fge = true;
    batch.state.prim.tme = true;
    batch.state.prim.iip = true;
    batch.vertices[0] = {16.0f, 32.0f, 0xFFFFFFFFu, 255, 0, 0, 128, 1.0f, 0.25f, 0.5f, 16u, 32u, 64u};
    batch.vertices[1] = {176.0f, 32.0f, 0x7FFFFFFFu, 0, 255, 0, 255, 1.0f, 0.75f, 1.0f, 80u, 32u, 128u};
    batch.vertices[2] = {16.0f, 192.0f, 0x3FFFFFFFu, 0, 0, 255, 255, 1.0f, 0.25f, 0.75f, 16u, 96u, 192u};
    return batch;
}

} // namespace

int main() {
    try {
        GsGpuBatch output;
        std::string error;

        auto triangle = makeBatch(GS_PRIM_TRIANGLE, 3u);
        require(gow::android::expandGsPrimitiveBatch(triangle, output, error),
                "Triangle expansion failed.");
        require(output.vertices.size() == 3u,
                "Triangle must produce three GPU vertices.");
        require(output.targetWidth == 640u && output.targetHeight == 431u,
                "GS framebuffer dimensions were not derived correctly.");
        require(output.vertices[0].x == 0.0f && output.vertices[0].y == 0.0f,
                "XYOFFSET was not removed from the first vertex.");
        require(output.vertices[0].r == 1.0f &&
                output.vertices[0].g == 0.0f &&
                output.vertices[0].a > 0.50f &&
                output.vertices[0].a < 0.51f,
                "Gouraud color conversion is incorrect.");
        require(output.vertices[1].u == 5.0f &&
                output.vertices[1].v == 2.0f,
                "Fixed-point UV conversion is incorrect.");
        require(output.vertices[0].z > 0.999f &&
                output.vertices[1].z > 0.499f &&
                output.vertices[2].z < 0.251f,
                "Z24 values were not normalized using the selected Z format.");
        require(output.textured && output.blend && output.depthTest &&
                output.depthWrite && output.fogEnabled,
                "GS draw-state flags were not preserved.");
        require(output.frameBase == 0x120u &&
                output.depthBase == 0x80u &&
                output.framePsm == GS_PSM_CT32 &&
                output.depthPsm == GS_PSM_Z24,
                "Framebuffer/depth metadata was not preserved.");

        auto sprite = makeBatch(GS_PRIM_SPRITE, 2u);
        require(gow::android::expandGsPrimitiveBatch(sprite, output, error),
                "Sprite expansion failed.");
        require(output.topology == gow::android::GsPrimitiveTopology::Triangles &&
                output.vertices.size() == 6u,
                "Sprite must expand to two GPU triangles.");
        require(output.vertices[0].x == 0.0f &&
                output.vertices[0].y == 0.0f &&
                output.vertices[1].x == 160.0f &&
                output.vertices[1].y == 0.0f &&
                output.vertices[5].x == 160.0f &&
                output.vertices[5].y == 160.0f,
                "Sprite corner expansion is incorrect.");

        auto line = makeBatch(GS_PRIM_LINE, 2u);
        require(gow::android::expandGsPrimitiveBatch(line, output, error),
                "Line expansion failed.");
        require(output.topology == gow::android::GsPrimitiveTopology::Lines &&
                output.vertices.size() == 2u,
                "Line must produce two vertices.");

        auto point = makeBatch(GS_PRIM_POINT, 1u);
        require(gow::android::expandGsPrimitiveBatch(point, output, error),
                "Point expansion failed.");
        require(output.topology == gow::android::GsPrimitiveTopology::Points &&
                output.vertices.size() == 1u,
                "Point must produce one vertex.");

        auto flat = makeBatch(GS_PRIM_TRIANGLE, 3u);
        flat.state.prim.iip = false;
        require(gow::android::expandGsPrimitiveBatch(flat, output, error),
                "Flat-shaded triangle expansion failed.");
        require(output.vertices[0].r == 0.0f &&
                output.vertices[1].g == 0.0f &&
                output.vertices[2].b == 1.0f,
                "Flat-shaded color must use the provoking last vertex.");

        auto invalid = makeBatch(GS_PRIM_TRIANGLE, 2u);
        require(!gow::android::expandGsPrimitiveBatch(invalid, output, error) &&
                !error.empty(),
                "Mismatched GS primitive/vertex count was accepted.");

        std::cout << "Android GS geometry expansion tests passed.\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "Android GS geometry expansion test failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
