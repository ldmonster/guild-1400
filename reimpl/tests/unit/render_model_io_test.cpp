// tests/unit/render_model_io_test.cpp — guild::render synthetic model loader
// (render/model_io.cpp, LoadSyntheticModel). Golden round-trip plus W11 hardening:
// 0-byte / truncated / oversized-count / out-of-range-index inputs must fail safe
// with no over-read and no runaway allocation. Build with -fsanitize=address,
// undefined to exercise the bounds.
#include "tests/framework/test.h"
#include "render/model_io.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

void Pu32(std::vector<u8>& v, u32 x) {
    v.push_back((u8)(x & 0xFF)); v.push_back((u8)((x >> 8) & 0xFF));
    v.push_back((u8)((x >> 16) & 0xFF)); v.push_back((u8)((x >> 24) & 0xFF));
}
void Pf32(std::vector<u8>& v, float f) { u32 b; std::memcpy(&b, &f, 4); Pu32(v, b); }
constexpr u32 kMagic = 0x4C444D47;  // 'GMDL'

// One triangle: 3 vertices, 1 polygon.
std::vector<u8> MakeTriangle() {
    std::vector<u8> v;
    Pu32(v, kMagic);
    Pu32(v, 3);  // vertexCount
    Pu32(v, 1);  // polyCount
    for (int i = 0; i < 3; ++i) {
        Pf32(v, (float)i); Pf32(v, 0); Pf32(v, 0);  // x,y,z
        Pf32(v, 0); Pf32(v, 0);                     // u,v
        v.push_back((u8)i);                          // light
    }
    Pu32(v, 0); Pu32(v, 1); Pu32(v, 2);  // indices
    Pf32(v, 0.5f); Pf32(v, 0.5f); Pf32(v, 0.0f);  // uvX/Y/Z
    v.push_back(0); v.push_back(0);                // f36, f38
    return v;
}

} // namespace

// ---- golden round trip ------------------------------------------------------
TEST(RenderModelIo, LoadsTriangle) {
    auto buf = MakeTriangle();
    Model m;
    CHECK(LoadSyntheticModel(buf.data(), buf.size(), m));
    CHECK_EQ((int)m.vertices.size(), 3);
    CHECK_EQ((int)m.polygons.size(), 1);
    CHECK(m.vertices[2].x == 2.0f);
    CHECK(m.polygons[0].v1 == &m.vertices[1]);
}

// ---- 0-byte / header-only buffers reject ------------------------------------
TEST(RenderModelIo, EmptyAndTiny) {
    Model m;
    const u8 z = 0;
    CHECK(!LoadSyntheticModel(&z, 0, m));
    CHECK(!LoadSyntheticModel(&z, 1, m));
    std::vector<u8> hdr; Pu32(hdr, kMagic);  // magic only, counts missing
    CHECK(!LoadSyntheticModel(hdr.data(), hdr.size(), m));
}

// ---- bad magic --------------------------------------------------------------
TEST(RenderModelIo, BadMagic) {
    std::vector<u8> v; Pu32(v, 0xDEADBEEF); Pu32(v, 0); Pu32(v, 0);
    Model m;
    CHECK(!LoadSyntheticModel(v.data(), v.size(), m));
}

// ---- vertexCount larger than the buffer -> reject, no OOB / no OOM ----------
TEST(RenderModelIo, OversizedVertexCount) {
    std::vector<u8> v; Pu32(v, kMagic); Pu32(v, 1000000u); Pu32(v, 0);
    Model m;
    CHECK(!LoadSyntheticModel(v.data(), v.size(), m));
}

// ---- polyCount larger than the buffer -> reject -----------------------------
TEST(RenderModelIo, OversizedPolyCount) {
    std::vector<u8> v; Pu32(v, kMagic); Pu32(v, 0); Pu32(v, 1000000u);
    Model m;
    CHECK(!LoadSyntheticModel(v.data(), v.size(), m));
}

// ---- truncated mid-vertex block -> reject, no over-read ---------------------
TEST(RenderModelIo, TruncatedMidVertex) {
    auto good = MakeTriangle();
    std::vector<u8> bad(good.begin(), good.begin() + 12 + 10);  // header + part of v0
    Model m;
    CHECK(!LoadSyntheticModel(bad.data(), bad.size(), m));
}

// ---- polygon index past vertexCount -> reject -------------------------------
TEST(RenderModelIo, OutOfRangePolyIndex) {
    auto buf = MakeTriangle();
    // The first polygon index byte sits right after the 3 vertex records
    // (12 header + 3*21 = 75). Overwrite v0's index dword with 99.
    const size_t polyIdx0 = 12u + 3u * 21u;
    buf[polyIdx0 + 0] = 99; buf[polyIdx0 + 1] = 0;
    buf[polyIdx0 + 2] = 0;  buf[polyIdx0 + 3] = 0;
    Model m;
    CHECK(!LoadSyntheticModel(buf.data(), buf.size(), m));  // index >= vcount
}
