#include "test.h"

#include "render/render_leaves4.h"
#include "render/colorformat.h"  // ComputeChannelShifts / PackColor / UnpackColor
#include "crt/rand.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;

namespace {

// Bit-exact float compare helper (these are golden vectors, equality expected).
bool FEq(float a, float b) {
    std::uint32_t ua, ub;
    std::memcpy(&ua, &a, 4);
    std::memcpy(&ub, &b, 4);
    return ua == ub;
}

template <class T> void Put(void* base, int off, T v) {
    std::memcpy(static_cast<unsigned char*>(base) + off, &v, sizeof(T));
}
template <class T> T Get(const void* base, int off) {
    T v; std::memcpy(&v, static_cast<const unsigned char*>(base) + off, sizeof(T)); return v;
}

} // namespace

// RGB565 channel format used by the surface pixel tests.
render::ColorFormat Fmt565() {
    return render::ComputeChannelShifts(0xF800u, 0x07E0u, 0x001Fu);
}

// ---------------------------------------------------------------------------
// FlickerEnvelopeStep — deterministic RNG-blended envelope.
// ---------------------------------------------------------------------------
namespace guild { namespace render {
float FlickerEnvelopeStep(u32, u32, bool, float, float, float, float*, float*);
} }

TEST(RenderLeaves4_Flicker, EnvelopeNoReseed) {
    float p = 0, n = 0;
    float out = render::FlickerEnvelopeStep(5, 10, false, 0.3f, 0.7f, 0.5f, &p, &n);
    CHECK(FEq(out, 0.5f));            // 0.5*0.3 + 0.5*0.7
    CHECK(FEq(p, 0.3f));
    CHECK(FEq(n, 0.7f));
}

TEST(RenderLeaves4_Flicker, EnvelopeReseed) {
    crt::Srand(12345);
    float p = 0, n = 0;
    float out = render::FlickerEnvelopeStep(15, 10, false, 0.3f, 0.7f, 0.5f, &p, &n);
    // elapsed resets to 0 -> frac 0 -> out == next (the new randomized 'next').
    CHECK(FEq(out, 1.1551713943481445f));
    CHECK(FEq(n, 1.1551713943481445f));
    CHECK(FEq(p, 0.804818868637085f));
}

TEST(RenderLeaves4_Flicker, EnvelopeReseedSuppressed) {
    crt::Srand(99);
    float p = 0, n = 0;
    float out = render::FlickerEnvelopeStep(20, 10, true, 0.4f, 0.9f, 0.25f, &p, &n);
    // suppressed -> next := prev(0.4); only one RandNext consumed (for prev).
    CHECK(FEq(out, 0.4000000059604645f));
    CHECK(FEq(n, 0.4000000059604645f));
}

// ---------------------------------------------------------------------------
// UpdateFlickerIntensity — full envelope + raw-path colour write over a graph.
// ---------------------------------------------------------------------------
TEST(RenderLeaves4_Flicker, UpdateRawPathColours) {
    // A LightNode with one trailing pair; storage holds node + 1 extra pair.
    std::vector<unsigned char> light(0x400, 0), cfg(0x400, 0);
    std::vector<unsigned char> obj(0x400, 0), mesh(0x1000, 0), sub(0x40, 0);
    std::vector<unsigned char> v0(0x80, 0);
    std::vector<unsigned char> nodeBuf(sizeof(render::LightNode) + sizeof(render::LightChildPair), 0);
    auto* node = reinterpret_cast<render::LightNode*>(nodeBuf.data());

    // light wiring
    Put<void*>(light.data(), 0x1E8, cfg.data());
    Put<float>(light.data(), 0x60, 1.0f);    // peak base
    Put<float>(light.data(), 0x58, 0.3f);    // prev endpoint
    Put<float>(light.data(), 0x68, 0.7f);    // next endpoint
    // cfg: period and timing -> NOT elapsed (elapsed < period), so no reseed.
    Put<std::uint32_t>(cfg.data(), 0x1A0, 10);
    Put<std::uint32_t>(cfg.data(), 0x194, 0);
    render::g_flickerTick = 5;               // elapsed = 5 - 0 = 5 < 10
    Put<void*>(cfg.data(), 0x198, node);      // cfg list head

    node->count = 1;
    node->obj   = obj.data();
    node->next  = nullptr;
    node->pairs[0].vbase = v0.data();
    node->pairs[0].color = 30u | (20u << 8) | (10u << 16); // cr=30,cg=20,cb=10

    // obj wiring
    Put<void*>(obj.data(), 0x1EC, mesh.data());
    Put<void*>(obj.data(), 0x1CC, sub.data());
    Put<unsigned char>(mesh.data(), 0x90D, 0); // not hidden
    Put<std::int32_t>(sub.data(), 8, 1);       // count guard >= node count

    // vertex base colour bytes (b,g,r) = (5,6,7) at +64..+66; not sentinel.
    Put<unsigned char>(v0.data(), 64, 5);
    Put<unsigned char>(v0.data(), 65, 6);
    Put<unsigned char>(v0.data(), 66, 7);

    render::g_rawLightingFlag = 1;
    crt::Srand(1);
    render::UpdateFlickerIntensity(light.data());

    // elapsed=5,period=10 -> frac 0.5; intensity = 0.5*0.3 + 0.5*0.7 = 0.5.
    //   v30(->[66]) = cb*I + [66] = 10*0.5 + 7 = 12
    //   v34(->[65]) = cg*I + [65] = 20*0.5 + 6 = 16
    //   v28(->[64]) = [64] + I*cr = 5 + 0.5*30 = 20  (max=20 < 255 -> no rescale)
    CHECK_EQ(Get<unsigned char>(v0.data(), 64), (unsigned char)20);
    CHECK_EQ(Get<unsigned char>(v0.data(), 65), (unsigned char)16);
    CHECK_EQ(Get<unsigned char>(v0.data(), 66), (unsigned char)12);

    render::g_rawLightingFlag = 0;  // reset shared global
}

// ---------------------------------------------------------------------------
// RefreshChildBrightness — both lighting modes over a one-node list.
// ---------------------------------------------------------------------------
TEST(RenderLeaves4_ChildBrightness, RawAndShiftModes) {
    std::vector<unsigned char> root(0x400, 0), wrap(0x200, 0);
    std::vector<unsigned char> obj(0x400, 0), mesh(0x1000, 0), sub(0x40, 0);
    std::vector<unsigned char> child(0x80, 0);
    std::vector<unsigned char> nodeBuf(sizeof(render::LightNode) + sizeof(render::LightChildPair), 0);
    auto* node = reinterpret_cast<render::LightNode*>(nodeBuf.data());

    Put<void*>(root.data(), 0x1E8, wrap.data());
    Put<void*>(wrap.data(), 0x198, node);
    node->count = 1;
    node->obj = obj.data();
    node->next = nullptr;
    node->pairs[0].vbase = child.data();
    node->pairs[0].color = 0;
    Put<void*>(obj.data(), 0x1EC, mesh.data());
    Put<void*>(obj.data(), 0x1CC, sub.data());
    Put<unsigned char>(mesh.data(), 0x90D, 0);
    Put<std::int32_t>(sub.data(), 8, 1);

    // raw mode: child[+0x40] = child[+0x44]
    Put<std::uint32_t>(child.data(), 0x44, 0xABCD1234u);
    render::g_rawLightingFlag = 1;
    render::RefreshChildBrightness(root.data());  (void)node;
    CHECK_EQ(Get<std::uint32_t>(child.data(), 0x40), 0xABCD1234u);

    // shift mode: child[+0x42] = (u8)((u8)child[+0x46] >> 2) (zero-extend, logical)
    Put<unsigned char>(child.data(), 0x46, 0x80);  // 128 -> 128>>2 == 32 (0x20)
    render::g_rawLightingFlag = 0;
    render::RefreshChildBrightness(root.data());
    CHECK_EQ(Get<unsigned char>(child.data(), 0x42), (unsigned char)0x20);

    Put<unsigned char>(child.data(), 0x46, 40);    // 40 >> 2 == 10
    render::RefreshChildBrightness(root.data());
    CHECK_EQ(Get<unsigned char>(child.data(), 0x42), (unsigned char)10);
}

TEST(RenderLeaves4_ChildBrightness, SkipsHiddenAndMissing) {
    std::vector<unsigned char> root(0x400, 0), wrap(0x200, 0);
    std::vector<unsigned char> obj(0x400, 0), mesh(0x1000, 0), sub(0x40, 0);
    std::vector<unsigned char> child(0x80, 0);
    std::vector<unsigned char> nodeBuf(sizeof(render::LightNode) + sizeof(render::LightChildPair), 0);
    auto* node = reinterpret_cast<render::LightNode*>(nodeBuf.data());
    Put<void*>(root.data(), 0x1E8, wrap.data());
    Put<void*>(wrap.data(), 0x198, node);
    node->count = 1;
    node->obj = obj.data();
    node->next = nullptr;
    node->pairs[0].vbase = child.data();
    node->pairs[0].color = 0;
    Put<void*>(obj.data(), 0x1EC, mesh.data());
    Put<void*>(obj.data(), 0x1CC, sub.data());
    Put<std::int32_t>(sub.data(), 8, 1);
    Put<unsigned char>(mesh.data(), 0x90D, 0x40);  // hidden -> skip
    Put<std::uint32_t>(child.data(), 0x44, 0x11112222u);
    render::g_rawLightingFlag = 1;
    render::RefreshChildBrightness(root.data());  (void)node;
    CHECK_EQ(Get<std::uint32_t>(child.data(), 0x40), 0u);  // untouched
    render::g_rawLightingFlag = 0;
}

// ---------------------------------------------------------------------------
// AABB merge — AccumulateAabbRecursive over an 8-corner box.
// ---------------------------------------------------------------------------
TEST(RenderLeaves4_Aabb, AccumulateMergesCorners) {
    // mesh geometry record (native struct so the two pointer fields don't clash).
    std::vector<unsigned char> obj(0x400, 0);
    render::MeshGeom mesh{};
    std::vector<float> corners(8 * 20, 0.0f);  // 8 corners, 80-byte stride = 20 floats
    Put<void*>(obj.data(), 0x1CC, &mesh);
    mesh.corners = corners.data();
    mesh.startIndex = 0;
    Put<void*>(obj.data(), 0x1FC, nullptr);  // no children

    // place 8 corners spanning [-2,-1,0]..[3,4,5]
    float pts[8][3] = {
        {-2,-1, 0}, {3, 0, 1}, {0, 4, 2}, {1, 2, 5},
        {2, 1, 3}, {-1, 3, 4}, {0, 0, 0}, {1, 1, 1}};
    for (int i = 0; i < 8; ++i) {
        corners[i * 20 + 0] = pts[i][0];
        corners[i * 20 + 1] = pts[i][1];
        corners[i * 20 + 2] = pts[i][2];
    }

    float box[7] = {1e30f, 1e30f, 1e30f, 0, -1e30f, -1e30f, -1e30f};
    render::AccumulateAabbRecursive(box, obj.data());
    CHECK(FEq(box[0], -2.0f));
    CHECK(FEq(box[1], -1.0f));
    CHECK(FEq(box[2], 0.0f));
    CHECK(FEq(box[4], 3.0f));
    CHECK(FEq(box[5], 4.0f));
    CHECK(FEq(box[6], 5.0f));
}

// ---------------------------------------------------------------------------
// Surface pixel leaves — Get/Set roundtrip and bpp dispatch (golden 565 vectors).
// ---------------------------------------------------------------------------
TEST(RenderLeaves4_Surface, SetGet16bppRoundtrip) {
    render::ColorFormat fmt = Fmt565();
    std::vector<unsigned char> surf(0x40, 0);
    std::vector<std::uint16_t> pixels(64 * 64, 0);
    Put<std::int32_t>(surf.data(), 16, 64);          // pitch
    Put<unsigned char>(surf.data(), 20, 16);          // 16bpp
    Put<void*>(surf.data(), 28, pixels.data());
    Put<std::int32_t>(surf.data(), 36, 0);            // clip x0
    Put<std::int32_t>(surf.data(), 40, 0);            // clip y0
    Put<std::int32_t>(surf.data(), 44, 64);           // clip x1
    Put<std::int32_t>(surf.data(), 48, 64);           // clip y1

    // 565: r/g lose low bits; choose channel values that survive the round trip.
    // 0x423ec6 — the original packs VIBE_Result_Handler_Final(a4, a3, a5) where
    // a3==r, a4==g, a5==b: i.e. PackColor(g, r, b) (the r/g PACK args are swapped
    // vs the 24bpp byte order). Set(r=248,g=124,b=16) -> PackColor(124, 248, 16).
    CHECK_EQ(render::SetPixelRgb(fmt, 3, 5, 248, 124, 16, surf.data()), 1);
    CHECK_EQ(pixels[3 + 64 * 5], (std::uint16_t)render::PackColor(fmt, 124, 248, 16));

    unsigned char out[3] = {0, 0, 0};
    render::GetPixelRgb(fmt, 3, 5, out, surf.data());
    // The pixel's r-channel holds 124, g-channel 248, b-channel 16. The 565 red
    // channel is only 5 bits, so 124 read back becomes 120; green keeps 248, blue
    // keeps 16. UnpackColor -> r=120, g=248, b=16; the 15/16 GetPixel path stores
    // out[0]=r, out[1]=b, out[2]=g -> (120, 16, 248).
    CHECK_EQ(out[0], (unsigned char)120);
    CHECK_EQ(out[1], (unsigned char)16);
    CHECK_EQ(out[2], (unsigned char)248);
}

TEST(RenderLeaves4_Surface, SetPixelClipRejects) {
    render::ColorFormat fmt = Fmt565();
    std::vector<unsigned char> surf(0x40, 0);
    std::vector<std::uint16_t> pixels(16 * 16, 0);
    Put<std::int32_t>(surf.data(), 16, 16);
    Put<unsigned char>(surf.data(), 20, 16);
    Put<void*>(surf.data(), 28, pixels.data());
    Put<std::int32_t>(surf.data(), 36, 2);   // clip x0
    Put<std::int32_t>(surf.data(), 40, 2);   // clip y0
    Put<std::int32_t>(surf.data(), 44, 10);  // clip x1
    Put<std::int32_t>(surf.data(), 48, 10);  // clip y1
    CHECK_EQ(render::SetPixelRgb(fmt, 0, 5, 255, 255, 255, surf.data()), 0);  // x<x0
    CHECK_EQ(render::SetPixelRgb(fmt, 5, 11, 255, 255, 255, surf.data()), 0); // y>=y1
    CHECK_EQ(render::SetPixelRgb(fmt, 5, 5, 255, 255, 255, surf.data()), 1);  // inside
    CHECK_EQ(render::SetPixelRgb(fmt, 0, 0, 0, 0, 0, nullptr), 0);            // null
}

TEST(RenderLeaves4_Surface, SetGet24And32Bpp) {
    render::ColorFormat fmt = Fmt565();
    // 24bpp SetPixel writes bytes (g,r,b) at base = shift*x + y*pitch (faithful to
    // the original: the row term has NO shift multiply). pitch is the byte stride.
    std::vector<unsigned char> surf24(0x40, 0), buf24(256 * 64, 0);
    const int pitch24 = 192;  // 64 px * 3 bytes/px row stride
    Put<std::int32_t>(surf24.data(), 16, pitch24);
    Put<unsigned char>(surf24.data(), 20, 24);
    Put<void*>(surf24.data(), 28, buf24.data());
    Put<std::int32_t>(surf24.data(), 44, 64);
    Put<std::int32_t>(surf24.data(), 48, 64);
    CHECK_EQ(render::SetPixelRgb(fmt, 2, 1, 11, 22, 33, surf24.data()), 1);  // r,g,b
    int sbase = 3 * 2 + 1 * pitch24;   // shift*x + y*pitch
    CHECK_EQ(buf24[sbase + 0], (unsigned char)22);  // g
    CHECK_EQ(buf24[sbase + 1], (unsigned char)11);  // r
    CHECK_EQ(buf24[sbase + 2], (unsigned char)33);  // b

    // 24bpp GetPixel reads (b,g,r) at base = shift*x + shift*pitch*y -> out[0..2].
    // Seed a buffer slot at the Get base and confirm the byte routing.
    std::vector<unsigned char> gbuf(4096, 0);
    std::vector<unsigned char> gsurf(0x40, 0);
    const int gpitch = 4;
    Put<std::int32_t>(gsurf.data(), 16, gpitch);
    Put<unsigned char>(gsurf.data(), 20, 24);
    Put<void*>(gsurf.data(), 28, gbuf.data());
    int gbase = 3 * 2 + 3 * gpitch * 1;   // shift*x + shift*pitch*y
    gbuf[gbase + 0] = 77;  // -> out[0] (b slot)
    gbuf[gbase + 1] = 88;  // -> out[1] (g slot)
    gbuf[gbase + 2] = 99;  // -> out[2] (r slot)
    unsigned char o24[3] = {0, 0, 0};
    render::GetPixelRgb(fmt, 2, 1, o24, gsurf.data());
    CHECK_EQ(o24[0], (unsigned char)77);
    CHECK_EQ(o24[1], (unsigned char)88);
    CHECK_EQ(o24[2], (unsigned char)99);

    // 32bpp: packed dword written; low 16 bits == the 565 pack.
    std::vector<unsigned char> surf32(0x40, 0), buf32(8 * 8 * 4, 0);
    Put<std::int32_t>(surf32.data(), 16, 8);
    Put<unsigned char>(surf32.data(), 20, 32);
    Put<void*>(surf32.data(), 28, buf32.data());
    Put<std::int32_t>(surf32.data(), 44, 8);
    Put<std::int32_t>(surf32.data(), 48, 8);
    CHECK_EQ(render::SetPixelRgb(fmt, 1, 1, 248, 124, 16, surf32.data()), 1);
    std::uint32_t dw;
    std::memcpy(&dw, buf32.data() + 4 * (1 + 8 * 1), 4);
    // Same r/g PACK swap as the 16bpp path: PackColor(g, r, b).
    CHECK_EQ(dw, (std::uint32_t)render::PackColor(fmt, 124, 248, 16));
}

TEST(RenderLeaves4_Surface, BlitRgbBlock) {
    render::ColorFormat fmt = Fmt565();
    std::vector<unsigned char> surf(0x40, 0);
    std::vector<std::uint16_t> pixels(16 * 16, 0);
    Put<std::int32_t>(surf.data(), 16, 16);          // pitch
    Put<void*>(surf.data(), 28, pixels.data());
    // 2 rows x 3 cols of RGB triples
    unsigned char src[2 * 3 * 3] = {
        248,124,16,  0,0,0,        255,252,248,
        8,8,8,       248,0,0,      0,252,0,
    };
    render::BlitRgbToPixels(fmt, 2, 3, src, surf.data());
    CHECK_EQ(pixels[0 + 16 * 0], (std::uint16_t)render::PackColor(fmt, 248, 124, 16));
    CHECK_EQ(pixels[2 + 16 * 0], (std::uint16_t)render::PackColor(fmt, 255, 252, 248));
    CHECK_EQ(pixels[1 + 16 * 1], (std::uint16_t)render::PackColor(fmt, 248, 0, 0));
    CHECK_EQ(pixels[2 + 16 * 1], (std::uint16_t)render::PackColor(fmt, 0, 252, 0));
}
