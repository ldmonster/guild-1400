// Unit tests for guild::render camera control + sprite/shape + BGF loader.
//   camera_control.{h,cpp}  shapebank.{h,cpp}  shapeanim.{h,cpp}
//   shape.{h,cpp}           bgf_loader.{h,cpp}
#include "render/camera_control.h"
#include "render/shapebank.h"
#include "render/shapeanim.h"
#include "render/shape.h"
#include "render/bgf_loader.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>
#include <vector>
#include <string>

using namespace guild;
using namespace guild::render;

namespace {

bool nearf(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// ---- small LE buffer builder for the synthetic BGF buffer -------------------
struct Buf {
    std::vector<u8> b;
    void u32v(u32 v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); }
    void i32v(i32 v) { u32v(static_cast<u32>(v)); }
    void f32v(float v) { u32 bits; std::memcpy(&bits, &v, 4); u32v(bits); }
    void vec3(float x, float y, float z) { f32v(x); f32v(y); f32v(z); }
    void byte(u8 v) { b.push_back(v); }
    void cstr(const char* s) { while (*s) b.push_back(static_cast<u8>(*s++)); b.push_back(0); }
};

} // namespace

// =============================================================================
// Camera view frustum (BuildViewFrustum @0x5ACCD0) — golden vs python.
// =============================================================================
TEST(RenderCameraUnit, BuildViewFrustumGolden) {
    CameraViewInput in{0.5f, 0.8660254f, 1.0f, 200.0f};
    CameraViewFrustum f = BuildViewFrustum(in);

    CHECK(nearf(f.theta, 0.5235987901687622f));
    CHECK(nearf(f.phi, 1.0471975803375244f));

    // side0 = ( cos T, 0, sin T, -eps )
    CHECK(nearf(f.side[0].a, 0.8660253882408142f));
    CHECK(nearf(f.side[0].b, 0.0f));
    CHECK(nearf(f.side[0].c, 0.5f));
    CHECK(nearf(f.side[0].w, -kViewPlaneEps));
    // side1 = (-cos T, 0, sin T, +eps )
    CHECK(nearf(f.side[1].a, -0.8660253882408142f));
    CHECK(nearf(f.side[1].c, 0.5f));
    CHECK(nearf(f.side[1].w, +kViewPlaneEps));
    // side2 = ( 0, cos P, sin P, +eps )
    CHECK(nearf(f.side[2].b, 0.5f));
    CHECK(nearf(f.side[2].c, 0.8660253882408142f));
    CHECK(nearf(f.side[2].w, +kViewPlaneEps));
    // side3 = ( 0, -cos P, sin P, -eps )
    CHECK(nearf(f.side[3].b, -0.5f));
    CHECK(nearf(f.side[3].c, 0.8660253882408142f));
    CHECK(nearf(f.side[3].w, -kViewPlaneEps));
    // near / far
    CHECK(nearf(f.nearP.c, 1.0f));
    CHECK(nearf(f.nearP.w, 1.0f));
    CHECK(nearf(f.farP.c, -1.0f));
    CHECK(nearf(f.farP.w, -200.0f));
}

// AABB plane-selection table: popcount + plane selection per outcode.
TEST(RenderCameraUnit, BoundingBoxPlaneTable) {
    CameraViewInput in{0.5f, 0.8660254f, 1.0f, 200.0f};
    CameraViewFrustum f = BuildViewFrustum(in);
    std::vector<BBoxPlaneEntry> table(64);
    BuildBoundingBoxPlaneTable(f, table.data());

    CHECK_EQ(table[0].count, 0);
    CHECK_EQ(table[0b000001].count, 1);
    CHECK_EQ(table[0b000011].count, 2);
    CHECK_EQ(table[0b111111].count, 6);
    // outcode 0b000101 -> bit0 (side0) + bit2 (side2).
    BBoxPlaneEntry& e = table[0b000101];
    CHECK_EQ(e.count, 2);
    CHECK(nearf(e.planes[0].a, f.side[0].a));
    CHECK(nearf(e.planes[1].b, f.side[2].b));
    // outcode 0b110000 -> bit4 (near) + bit5 (far).
    BBoxPlaneEntry& g = table[0b110000];
    CHECK_EQ(g.count, 2);
    CHECK(nearf(g.planes[0].w, f.nearP.w));
    CHECK(nearf(g.planes[1].w, f.farP.w));
}

// Orbit / zoom rate math (ComputeOrbitRates @0x5E9024) — golden vs python.
TEST(RenderCameraUnit, OrbitRatesGolden) {
    OrbitRateConfig cfg{2.0f, 4.0f, 2.5f, 1.5f, 1.25f};
    OrbitRates r = ComputeOrbitRates(10, 6, 640, 480, cfg);
    CHECK(nearf(r.pan, 0.03125f));
    CHECK(nearf(r.panY, 0.02500000037252903f));
    CHECK(nearf(r.yaw, 0.0016113281017169356f, 1e-7f));
    CHECK(nearf(r.pitch, 0.0009114583372138441f, 1e-7f));
}

// Orbit rate acceleration clamp -> 3.0 when cx exceeds accelClamp.
TEST(RenderCameraUnit, OrbitRatesAccelClamp) {
    // Large dx with a tiny clamp forces the 3.0 fallback.
    OrbitRateConfig cfg{1.0f, 100.0f, 0.01f, 1.0f, 1.0f};
    OrbitRates r = ComputeOrbitRates(100, 0, 200, 200, cfg);
    // yaw = (100/200) * 1.0 * 3.0 = 1.5
    CHECK(nearf(r.yaw, 1.5f));
}

// =============================================================================
// ShapeBank management (AddShape / AppendShape / RemoveShape).
// =============================================================================
namespace {
// Build a minimal shape blob: size, width@+6, height@+10, depth@+12, then filler.
std::vector<u8> MakeShape(u32 size, u16 w, u16 h, u8 depth, u8 fill) {
    std::vector<u8> s(size, fill);
    std::memcpy(s.data() + shape_off::kSize, &size, 4);
    std::memcpy(s.data() + shape_off::kWidth, &w, 2);
    std::memcpy(s.data() + shape_off::kHeight, &h, 2);
    s[shape_off::kColorDepth] = depth;
    return s;
}
} // namespace

TEST(RenderShapeBankUnit, AddShapeInitAndStore) {
    std::vector<u8> bank(1 << 16, 0);
    auto s0 = MakeShape(64, 10, 20, 0, 0xAA);
    auto s1 = MakeShape(48, 30, 8, 0, 0xBB);

    CHECK_EQ(ShapeBankAddShape(bank.data(), s0.data()), 1);
    CHECK_EQ(ShapeBankCount(bank.data()), 1);
    CHECK_EQ(std::memcmp(bank.data(), "SHAPBANK", 8), 0);
    CHECK_EQ(bank[bank_off::kVersion], 1);
    // first shape stored at the initial write cursor (2117).
    CHECK_EQ(ShapeBankShapeOffset(bank.data(), 0), kBankFirstDataOffset);

    CHECK_EQ(ShapeBankAddShape(bank.data(), s1.data()), 1);
    CHECK_EQ(ShapeBankCount(bank.data()), 2);
    // second shape stored right after the first.
    CHECK_EQ(ShapeBankShapeOffset(bank.data(), 1), kBankFirstDataOffset + 64);
    // max width/height tracked.
    CHECK_EQ(ShapeBankMaxWidth(bank.data()), 30);
    CHECK_EQ(ShapeBankMaxHeight(bank.data()), 20);

    // payload integrity: byte just past the shape header in slot 1 is 0xBB.
    const u8* sp1 = ShapeBankShape(bank.data(), 1);
    CHECK_EQ(sp1[16], 0xBB);
}

TEST(RenderShapeBankUnit, DepthMismatchRefused) {
    std::vector<u8> bank(1 << 16, 0);
    auto s0 = MakeShape(64, 10, 20, 0, 0xAA);   // depth 0
    auto s1 = MakeShape(48, 30, 8, 1, 0xBB);    // depth 1
    CHECK_EQ(ShapeBankAddShape(bank.data(), s0.data()), 1);
    CHECK_EQ(ShapeBankAddShape(bank.data(), s1.data()), 0);  // mismatch
    CHECK_EQ(ShapeBankCount(bank.data()), 1);
}

TEST(RenderShapeBankUnit, RemoveShapeCompacts) {
    std::vector<u8> bank(1 << 16, 0);
    auto s0 = MakeShape(64, 10, 20, 0, 0xAA);
    auto s1 = MakeShape(48, 30, 8, 0, 0xBB);
    auto s2 = MakeShape(32, 5, 5, 0, 0xCC);
    ShapeBankAddShape(bank.data(), s0.data());
    ShapeBankAddShape(bank.data(), s1.data());
    ShapeBankAddShape(bank.data(), s2.data());
    CHECK_EQ(ShapeBankCount(bank.data()), 3);

    // Remove the middle shape (index 1).
    CHECK_EQ(ShapeBankRemoveShape(bank.data(), 1), 1);
    CHECK_EQ(ShapeBankCount(bank.data()), 2);
    // slot 0 still at the base; slot 1 (was slot 2) shifted down by 48 bytes.
    CHECK_EQ(ShapeBankShapeOffset(bank.data(), 0), kBankFirstDataOffset);
    CHECK_EQ(ShapeBankShapeOffset(bank.data(), 1), kBankFirstDataOffset + 64);
    // the (former) third shape's payload is now reachable at slot 1.
    const u8* sp = ShapeBankShape(bank.data(), 1);
    u32 sz; std::memcpy(&sz, sp + shape_off::kSize, 4);
    CHECK_EQ(sz, 32u);
    CHECK_EQ(sp[16], 0xCC);
}

// =============================================================================
// ShapeAnim slot table (RegisterSlot / OffsetSlotPos / AdvanceFrames).
// =============================================================================
namespace {
// Build a tiny anim clip: frameBase@+62, loopMode@+66, frameCount@+67, then a
// frame table of {shapeValue, duration, 6 pad} per frame.
std::vector<u8> MakeClip(int frameCount, u8 loopMode) {
    std::vector<u8> c(0x80 + 8 * frameCount, 0);
    u32 frameBase = 0x80;
    std::memcpy(c.data() + anim_clip_off::kFrameTableBase, &frameBase, 4);
    c[anim_clip_off::kLoopMode] = loopMode;
    u16 fc = static_cast<u16>(frameCount);
    std::memcpy(c.data() + anim_clip_off::kFrameCount, &fc, 2);
    for (int i = 0; i < frameCount; ++i) {
        c[frameBase + 8 * i + 0] = static_cast<u8>(100 + i);  // shapeValue
        c[frameBase + 8 * i + 1] = 5;                          // duration ticks
    }
    return c;
}
const u8* ResolveClip(u32 /*obj*/, void* ctx) { return reinterpret_cast<std::vector<u8>*>(ctx)->data(); }
} // namespace

TEST(RenderShapeAnimUnit, RegisterAndOffset) {
    ShapeAnimState st; st.Reset();
    auto clip = MakeClip(4, 0);
    int slot = ShapeAnimRegisterSlot(st, 100, 50, 0, 0x1234, clip.data());
    CHECK_EQ(slot, 0);
    ShapeAnimSlotView s = ShapeAnimSlot(st, slot);
    CHECK_EQ(s.Object(), 0x1234u);
    CHECK_EQ((int)s.Frame(), 0);
    CHECK_EQ(s.X(), 100);
    CHECK_EQ(s.Y(), 50);
    CHECK_EQ(s.ShapeValue(), 100);   // first frame's shape value

    ShapeAnimOffsetSlotPos(st, 5, -3, slot);
    CHECK_EQ(s.X(), 105);
    CHECK_EQ((i16)s.Y(), 47);

    // A second register lands in slot 1.
    int slot1 = ShapeAnimRegisterSlot(st, 0, 0, 1, 0x5678, clip.data());
    CHECK_EQ(slot1, 1);
    CHECK_EQ(ShapeAnimSlot(st, 1).Flag8(), 1u);  // mode&1 sets flag8
}

TEST(RenderShapeAnimUnit, AdvanceForwardLoop) {
    ShapeAnimState st; st.Reset();
    auto clip = MakeClip(3, 0);  // 3 frames, loop
    int slot = ShapeAnimRegisterSlot(st, 0, 0, 0, 1, clip.data());
    ShapeAnimSlotView s = ShapeAnimSlot(st, slot);

    // duration is 5; tick by 6 -> advance one frame each time.
    st.clock = 6;
    ShapeAnimAdvanceFrames(st, ResolveClip, &clip);
    CHECK_EQ((int)s.Frame(), 1);
    CHECK_EQ(s.ShapeValue(), 101);

    st.clock = 12;
    ShapeAnimAdvanceFrames(st, ResolveClip, &clip);
    CHECK_EQ((int)s.Frame(), 2);
    CHECK_EQ(s.ShapeValue(), 102);

    // Next advance hits the end (frame 3 >= count 3) -> loop to 0.
    st.clock = 18;
    ShapeAnimAdvanceFrames(st, ResolveClip, &clip);
    CHECK_EQ((int)s.Frame(), 0);
    CHECK_EQ(s.ShapeValue(), 100);
}

TEST(RenderShapeAnimUnit, AdvanceClampLast) {
    ShapeAnimState st; st.Reset();
    auto clip = MakeClip(2, 1);  // 2 frames, clamp-last
    int slot = ShapeAnimRegisterSlot(st, 0, 0, 0, 1, clip.data());
    ShapeAnimSlotView s = ShapeAnimSlot(st, slot);

    st.clock = 6;
    ShapeAnimAdvanceFrames(st, ResolveClip, &clip);
    CHECK_EQ((int)s.Frame(), 1);

    // End: frame 2 >= count 2, mode 1 -> frame = -1 (inactive marker).
    st.clock = 12;
    ShapeAnimAdvanceFrames(st, ResolveClip, &clip);
    CHECK_EQ((int)s.Frame(), -1);
    // shapeValue set to the last real frame's value (8*count - 8 = frame 1 -> 101).
    CHECK_EQ(s.ShapeValue(), 101);
}

// =============================================================================
// Shape RLE blit (ShapeDecodeRle @0x5D70CC).
// =============================================================================
TEST(RenderShapeUnit, DecodeRleBlit) {
    // A 2-row shape. Row 0: skip 1 pixel, then 2 pixels {0xAAAA,0xBBBB}.
    //                Row 1: skip 0 pixels, then 1 pixel {0xCCCC}.
    std::vector<u8> shape(0x32 + 64, 0);
    u16 height = 2;
    std::memcpy(shape.data() + shape_rle_off::kHeight, &height, 2);

    auto putU32 = [&](size_t off, u32 v) { std::memcpy(shape.data() + off, &v, 4); };
    auto putU16 = [&](size_t off, u16 v) { std::memcpy(shape.data() + off, &v, 2); };

    size_t p = 0x32;
    // Row 0: runCount = 1
    putU32(p, 1); p += 4;
    //   run: skip = 2 (>>1 = 1 pixel), nPixels = 2, pixels {0xAAAA,0xBBBB}
    putU32(p, 2); p += 4;
    putU32(p, 2); p += 4;
    putU16(p, 0xAAAA); p += 2;
    putU16(p, 0xBBBB); p += 2;
    // Row 1: runCount = 1
    putU32(p, 1); p += 4;
    //   run: skip = 0, nPixels = 1, pixel {0xCCCC}
    putU32(p, 0); p += 4;
    putU32(p, 1); p += 4;
    putU16(p, 0xCCCC); p += 2;

    std::vector<u16> fb(8 * 4, 0);
    BlitTarget16 dst{8, fb.data()};
    ShapeDecodeRle(0, 0, shape.data(), dst);

    // Row 0 (y=0): pixel x=1 -> 0xAAAA, x=2 -> 0xBBBB, x=0 stays 0.
    CHECK_EQ(fb[0 * 8 + 0], 0x0000);
    CHECK_EQ(fb[0 * 8 + 1], 0xAAAA);
    CHECK_EQ(fb[0 * 8 + 2], 0xBBBB);
    // Row 1 (y=1): pixel x=0 -> 0xCCCC.
    CHECK_EQ(fb[1 * 8 + 0], 0xCCCC);

    // With an x/y offset the pixels land at (x+..., y+...).
    std::fill(fb.begin(), fb.end(), 0);
    ShapeDecodeRle(2, 1, shape.data(), dst);
    CHECK_EQ(fb[1 * 8 + 3], 0xAAAA);   // y=1, x=2+1
    CHECK_EQ(fb[1 * 8 + 4], 0xBBBB);
    CHECK_EQ(fb[2 * 8 + 2], 0xCCCC);   // y=2, x=2
}

// =============================================================================
// BGF loader (token scan + fast-chunk parse).
// =============================================================================
namespace {
Buf MakeBgf() {
    Buf chunk;  // body AFTER the magic
    u32 matCount = 2, vtxCount = 3, polyCount = 1, dummyCount = 1;
    chunk.u32v(matCount);
    chunk.u32v(vtxCount);
    chunk.u32v(polyCount);
    // vertices: (vtxCount + 8) records of pos vec3 + normal vec3.
    float verts[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    for (u32 i = 0; i < vtxCount + 8; ++i) {
        if (i < vtxCount) { chunk.vec3(verts[i][0], verts[i][1], verts[i][2]); chunk.vec3(0, 0, 1); }
        else { chunk.vec3(0, 0, 0); chunk.vec3(0, 0, 0); }
    }
    chunk.u32v(0x10);  // objectFlags
    // poly: vtx0,1,2; uv0 vec3; uv1 vec3; uv2 vec3; matIndex byte.
    chunk.u32v(0); chunk.u32v(1); chunk.u32v(2);
    chunk.vec3(0.1f, 0.2f, 0.3f);
    chunk.vec3(0.4f, 0.5f, 0.6f);
    chunk.vec3(0.7f, 0.8f, 0.9f);
    chunk.byte(1);  // matIndex
    // materials.
    chunk.cstr("tex0.bmp"); chunk.cstr(""); chunk.cstr("");
    chunk.byte(0); chunk.byte(0); chunk.byte(0); chunk.byte(0); chunk.byte(0); chunk.byte(0);
    chunk.cstr("tex1.bmp"); chunk.cstr(""); chunk.cstr("");
    chunk.byte(1); chunk.byte(0); chunk.byte(0); chunk.byte(0); chunk.byte(0); chunk.byte(0);
    // dummies.
    chunk.u32v(dummyCount);
    chunk.cstr("dummy0");
    chunk.vec3(7, 8, 9);
    chunk.vec3(0.5f, 0.25f, 0.125f);

    Buf out;
    out.u32v(0xDEADBEEF);   // leading tag (skipped)
    // a name token + sized block we skip.
    out.byte(0x41);         // 'A' > 0x3A
    out.u32v(5);
    for (char ch : std::string("hello")) out.byte(static_cast<u8>(ch));
    // the '-' fast chunk.
    out.byte(kBgfTokenChunk);
    out.u32v(static_cast<u32>(chunk.b.size()) + 4);  // chunkSize (incl. magic)
    out.u32v(kBgfFastChunkMagic);
    for (u8 c : chunk.b) out.byte(c);
    return out;
}
} // namespace

TEST(RenderBgfUnit, FindChunkStart) {
    Buf bgf = MakeBgf();
    size_t off = 0;
    CHECK(BgfFindChunkStart(bgf.b.data(), bgf.b.size(), &off));
    // The offset points just past the magic; first dword there is materialCount=2.
    u32 mat; std::memcpy(&mat, bgf.b.data() + off, 4);
    CHECK_EQ(mat, 2u);
}

TEST(RenderBgfUnit, LoadFastChunkFields) {
    Buf bgf = MakeBgf();
    BgfModel m;
    CHECK(LoadFastChunk(bgf.b.data(), bgf.b.size(), m));
    CHECK_EQ(m.materialCount, 2u);
    CHECK_EQ(m.vertexCount, 3u);
    CHECK_EQ(m.polyCount, 1u);
    CHECK_EQ(m.objectFlags, 0x10u);
    CHECK_EQ(m.dummyCount, 1u);
    // vertex array sized count + 8.
    CHECK_EQ(m.vertices.size(), 3u + 8u);
    CHECK(nearf(m.vertices[1].pos[0], 1.0f));
    CHECK(nearf(m.vertices[2].pos[1], 1.0f));
    CHECK(nearf(m.vertices[0].normal[2], 1.0f));
    // poly indices + uv rows.
    CHECK_EQ(m.polygons[0].vtx[0], 0u);
    CHECK_EQ(m.polygons[0].vtx[2], 2u);
    CHECK_EQ(m.polygons[0].matIndex, 1);
    CHECK(nearf(m.polygons[0].uv0[0], 0.1f));
    CHECK(nearf(m.polygons[0].uv1[1], 0.5f));
    CHECK(nearf(m.polygons[0].uv2[2], 0.9f));
    // materials.
    CHECK_EQ(m.materials.size(), 2u);
    CHECK(m.materials[0].name0 == "tex0.bmp");
    CHECK(m.materials[1].name0 == "tex1.bmp");
    CHECK_EQ(m.materials[1].flag, 1);
    // dummy.
    CHECK_EQ(m.dummies.size(), 1u);
    CHECK(std::string(m.dummies[0].name) == "dummy0");
    CHECK(nearf(m.dummies[0].pos[0], 7.0f));
    CHECK(nearf(m.dummies[0].rot[2], 0.125f));
}

TEST(RenderBgfUnit, BuildGeometry) {
    Buf bgf = MakeBgf();
    BgfModel m;
    CHECK(LoadFastChunk(bgf.b.data(), bgf.b.size(), m));
    BgfGeometry g;
    CHECK(BuildGeometry(m, g));
    MeshGeometry* geom = g.View();
    CHECK_EQ(geom->polyCount, 1);
    CHECK_EQ(geom->vertexCount, 11);  // 3 + 8
    // Polygon vertex pointers resolve into the vertex array.
    CHECK(geom->polygons[0].v0 == &g.vertices[0]);
    CHECK(geom->polygons[0].v2 == &g.vertices[2]);
    CHECK(nearf(g.vertices[1].x, 1.0f));
}

TEST(RenderBgfUnit, TruncatedBufferFails) {
    Buf bgf = MakeBgf();
    bgf.b.resize(bgf.b.size() - 10);  // chop the tail
    BgfModel m;
    CHECK(!LoadFastChunk(bgf.b.data(), bgf.b.size(), m));
}

TEST(RenderBgfUnit, MissingMagicFails) {
    Buf bgf = MakeBgf();
    size_t off = 0;
    CHECK(BgfFindChunkStart(bgf.b.data(), bgf.b.size(), &off));
    // Corrupt the magic.
    bgf.b[off - 4] ^= 0xFF;
    BgfModel m;
    CHECK(!LoadFastChunk(bgf.b.data(), bgf.b.size(), m));
}
