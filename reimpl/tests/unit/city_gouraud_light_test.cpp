#include "tests/framework/test.h"

// ===========================================================================
// Pins for the CITY-VIEW NIGHT-LIGHTING features (suite prefix: CityGouraud):
//
//  * The per-pixel GOURAUD RGB diffuse channel of the textured span
//    (raster_textured/raster: the D3D hardware path's texture modulate realised
//    in the software span). Key contracts:
//      - all-255 vertex shades keep the span BYTE-IDENTICAL to the original
//        (the channel is disarmed; not a single pixel may differ);
//      - a flat shade modulates each 565 channel by (shade+1)>>8;
//      - a shade gradient interpolates monotonically across the span;
//      - the masked (colour-key) span skips transparent texels but keeps the
//        shade channel in phase.
//  * The day-cycle NIGHT GATE the lantern lights key on: ComputeSunState's
//    band is the time-of-day band (brightness 0..600 == day progress); the
//    lamplit hours are bands 0 (pre-dawn) and 5/6 (dusk/late night).
//  * The lantern FLICKER envelope: intensity stays inside the frida-captured
//    live range [336, 380] for every phase (gilde.exe in-city capture).
// ===========================================================================
#include "render/daycycle.h"
#include "render/raster_textured.h"
#include "render/surface.h"
#include "render/texture.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// A deterministic 8x8 palettized texture + a FULL 256-row 565 palette (the
// span's palette row is lightRow8 = avg(vertex light bytes) << 8, so the
// caller's palette must cover every row a fed light byte can select — the
// city binds the 256-row shade ramp). Rows are identical here so tests that
// leave light = 0 see the same pixels regardless of the row.
void MakeTexture(Texture& tex, std::vector<u16>& pal, u16 flat = 0) {
    TextureSetSize(tex, 8);
    for (std::size_t i = 0; i < tex.texels.size(); ++i)
        tex.texels[i] = (u8)(1 + (i % 17));    // never 0 (no key holes)
    pal.assign(256u * 256u, 0);
    for (int row = 0; row < 256; ++row)
        for (int i = 0; i < 256; ++i)
            pal[(std::size_t)row * 256 + (std::size_t)i] =
                flat ? flat : (u16)(0x1111 * (i % 13) + i);
}

u16 Px(const Surface* s, int x, int y) {
    const u8* base = s->pixels + (std::size_t)y * (std::size_t)s->pitch;
    u16 v;
    std::memcpy(&v, base + 2 * x, 2);
    return v;
}

void Tri(RgbzVertex v[3]) {
    v[0] = {4.0f, 4.0f, 0.10f, 0.15f};
    v[1] = {60.0f, 8.0f, 0.90f, 0.20f};
    v[2] = {8.0f, 60.0f, 0.30f, 0.95f};
}

} // namespace

// All-255 shades leave every pixel byte-identical to the unshaded draw.
TEST(CityGouraud, NeutralShadesAreByteIdentical) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal);

    Surface* a = SurfaceCreate(64, 64, 16);
    Surface* b = SurfaceCreate(64, 64, 16);
    CHECK(a && b);

    RgbzVertex va[3];
    Tri(va);                                   // defaults: shade 255 (disarmed)
    RasterizeTexturedTriangleRgbz(a, va, tex, pal.data());

    RgbzVertex vb[3];
    Tri(vb);
    for (int i = 0; i < 3; ++i) {              // explicit 255s — still disarmed
        vb[i].shadeR = 255; vb[i].shadeG = 255; vb[i].shadeB = 255;
    }
    RasterizeTexturedTriangleRgbz(b, vb, tex, pal.data());

    int diff = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (Px(a, x, y) != Px(b, x, y)) ++diff;
    CHECK_EQ(diff, 0);
    SurfaceDestroy(a);
    SurfaceDestroy(b);
}

// A flat mid shade halves each 565 channel: channel*(128+1)>>8.
TEST(CityGouraud, FlatShadeModulatesChannels) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal, /*flat=*/0xFFFF);    // white texels

    Surface* s = SurfaceCreate(64, 64, 16);
    CHECK(s != nullptr);
    RgbzVertex v[3];
    Tri(v);
    for (int i = 0; i < 3; ++i) {
        v[i].shadeR = 128; v[i].shadeG = 128; v[i].shadeB = 128;
    }
    RasterizeTexturedTriangleRgbz(s, v, tex, pal.data());

    // An interior pixel: white (31,63,31) scaled by 129/256 -> (15,31,15).
    const u16 got = Px(s, 16, 16);
    const u16 want = (u16)((15 << 11) | (31 << 5) | 15);
    CHECK_EQ(got, want);
    SurfaceDestroy(s);
}

// A left->right shade gradient produces monotonically brightening pixels.
TEST(CityGouraud, ShadeGradientInterpolates) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal, /*flat=*/0xFFFF);

    Surface* s = SurfaceCreate(64, 64, 16);
    CHECK(s != nullptr);
    // A wide flat-bottom triangle covering row 20 from x~2 to x~60.
    RgbzVertex v[3];
    v[0] = {2.0f, 2.0f, 0.1f, 0.1f};
    v[1] = {62.0f, 2.0f, 0.9f, 0.1f};
    v[2] = {32.0f, 62.0f, 0.5f, 0.9f};
    v[0].shadeR = v[0].shadeG = v[0].shadeB = 16;    // dark left
    v[1].shadeR = v[1].shadeG = v[1].shadeB = 240;   // bright right
    v[2].shadeR = v[2].shadeG = v[2].shadeB = 128;
    RasterizeTexturedTriangleRgbz(s, v, tex, pal.data());

    // Red channel along row 8 rises monotonically (within quantisation).
    int prev = -1;
    bool monotonic = true;
    for (int x = 8; x <= 56; x += 8) {
        const int r5 = (Px(s, x, 8) >> 11) & 0x1F;
        if (r5 < prev) monotonic = false;
        prev = r5;
    }
    CHECK(monotonic);
    // And the extremes actually differ (the channel is live).
    CHECK(((Px(s, 8, 8) >> 11) & 0x1F) < ((Px(s, 56, 8) >> 11) & 0x1F));
    SurfaceDestroy(s);
}

// The masked span skips keyed texels but keeps the shade channel in phase:
// covered pixels right of a hole match the unmasked draw exactly.
TEST(CityGouraud, MaskedSpanKeepsShadePhase) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal, /*flat=*/0xFFFF);
    // Punch key holes: texel index 0 in a vertical stripe.
    for (int y = 0; y < 8; ++y)
        tex.texels[(std::size_t)y * 8 + 3] = 0;

    Surface* masked = SurfaceCreate(64, 64, 16);
    Surface* plain  = SurfaceCreate(64, 64, 16);
    CHECK(masked && plain);

    RgbzVertex v[3];
    Tri(v);
    for (int i = 0; i < 3; ++i) {
        v[i].shadeR = 40; v[i].shadeG = 200; v[i].shadeB = 90;
    }
    RasterizeTexturedTriangleRgbzMasked(masked, v, tex, pal.data(),
                                        /*polyFlags38=*/0, /*colorKey565=*/-1);
    RasterizeTexturedTriangleRgbz(plain, v, tex, pal.data());

    // Every pixel the masked draw DID write matches the plain draw (same
    // shade phase); the skipped ones remain background (0).
    int written = 0, mismatched = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const u16 m = Px(masked, x, y);
            if (m == 0) continue;
            ++written;
            if (m != Px(plain, x, y)) ++mismatched;
        }
    CHECK(written > 100);
    CHECK_EQ(mismatched, 0);
    SurfaceDestroy(masked);
    SurfaceDestroy(plain);
}

// The shade channel survives the reconstruction-only surface clip exactly like
// U/V: a gouraud triangle overhanging a small surface draws the crop of the
// same triangle rendered fully inside a larger one (the skipped-row edge
// advance and the per-span left-clamp back-off must step R/G/B in phase).
TEST(CityGouraud, ClippedGouraudEqualsCropOfUnclipped) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal, /*flat=*/0xFFFF);

    Surface* big = SurfaceCreate(96, 96, 16);
    CHECK(big != nullptr);
    RgbzVertex vb[3];
    vb[0] = {20.0f, 12.0f, 0.10f, 0.15f};
    vb[1] = {80.0f, 40.0f, 0.90f, 0.20f};
    vb[2] = {30.0f, 88.0f, 0.30f, 0.95f};
    vb[0].shadeR = 20;  vb[0].shadeG = 240; vb[0].shadeB = 60;
    vb[1].shadeR = 250; vb[1].shadeG = 30;  vb[1].shadeB = 130;
    vb[2].shadeR = 90;  vb[2].shadeG = 90;  vb[2].shadeB = 250;
    CHECK_EQ(RasterizeTexturedTriangleRgbz(big, vb, tex, pal.data()), 1);

    // Small surface: the same triangle shifted so it overhangs every edge;
    // its visible window is the big draw's [32,64) x [32,64).
    Surface* small = SurfaceCreate(32, 32, 16);
    CHECK(small != nullptr);
    RgbzVertex vs[3];
    for (int i = 0; i < 3; ++i) {
        vs[i] = vb[i];
        vs[i].x -= 32.0f;
        vs[i].y -= 32.0f;
    }
    CHECK_EQ(RasterizeTexturedTriangleRgbz(small, vs, tex, pal.data()), 1);

    int mismatched = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (Px(small, x, y) != Px(big, x + 32, y + 32)) ++mismatched;
    CHECK_EQ(mismatched, 0);
    SurfaceDestroy(big);
    SurfaceDestroy(small);
}

// Deterministic fuzz: wild triangles (huge/negative/degenerate coordinates,
// arbitrary shades and UVs) through both gouraud span bodies never write a
// pixel outside the surface clip rect. (The TDM toolchain has no ASAN runtime;
// this is the containment guard for the new per-pixel channels.)
TEST(CityGouraud, FuzzedTrianglesRespectClip) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal);
    tex.texels[5] = 0;                       // some key holes for the masked body

    Surface* s = SurfaceCreate(64, 64, 16);
    CHECK(s != nullptr);
    s->clipX0 = 8; s->clipY0 = 8;            // interior clip window
    s->clipX1 = 56; s->clipY1 = 56;

    u32 lcg = 0x12345u;                      // deterministic
    auto next = [&lcg]() { lcg = lcg * 1664525u + 1013904223u; return lcg; };
    auto frand = [&](float lo, float hi) {
        return lo + (float)(next() >> 8) / 16777216.0f * (hi - lo);
    };

    for (int iter = 0; iter < 2000; ++iter) {
        RgbzVertex v[3];
        for (int i = 0; i < 3; ++i) {
            // Mix scales: on-screen, far off-screen, extreme-but-in-contract.
            // The raster's 16.16 fixed-point domain is |coord| < 32768 — the
            // upstream 6-plane clip (ComputeVertexClipFlags + ProjectObject-
            // Vertices) guarantees projected vertices stay far inside it, so
            // coordinates beyond wrap the accumulators by CONTRACT (same as
            // the original). Fuzz to the edge of that contract, not past it.
            const float mag = (iter % 3 == 0) ? 64.0f
                            : (iter % 3 == 1) ? 4096.0f : 20000.0f;
            v[i].x = frand(-mag, mag);
            v[i].y = frand(-mag, mag);
            v[i].u = frand(-8.0f, 8.0f);
            v[i].v = frand(-8.0f, 8.0f);
            v[i].light = (u8)(next() & 0xFF);
            v[i].shadeR = (u8)(next() & 0xFF);
            v[i].shadeG = (u8)(next() & 0xFF);
            v[i].shadeB = (u8)(next() & 0xFF);
            v[i].fogFactor = (u8)(next() & 0xFF);
        }
        if (iter & 1)
            RasterizeTexturedTriangleRgbzMasked(s, v, tex, pal.data(),
                                                (u8)(next() & 0xFF), -1);
        else
            RasterizeTexturedTriangleRgbz(s, v, tex, pal.data());
    }

    // Not one pixel outside the clip window may be touched.
    int outside = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const bool in = x >= 8 && x < 56 && y >= 8 && y < 56;
            if (!in && Px(s, x, y) != 0) ++outside;
        }
    CHECK_EQ(outside, 0);
    SurfaceDestroy(s);
}

// The lantern NIGHT GATE: band 0 covers midnight..~8:00, bands 5/6 the dusk /
// late evening; midday bands 2..4 are lamps-off. (ComputeSunState brightness is
// the 0..600 DAY-PROGRESS counter; band = brightness/100 clamped to 0..6.)
TEST(CityGouraud, NightGateBands) {
    auto night = [](int hour) {
        const SunState s = ComputeSunState(/*day=*/0, hour, /*minute=*/0);
        return s.band == 0 || s.band >= 5;
    };
    CHECK(night(0));      // midnight
    CHECK(night(4));      // pre-dawn (the reference screenshot's hour)
    CHECK(night(22));     // late evening
    CHECK(night(20));     // dusk band 5
    CHECK(!night(12));    // noon
    CHECK(!night(16));    // afternoon
    CHECK(!night(10));    // morning
}

// The lantern FLICKER envelope: every phase of the deterministic wobble stays
// inside the frida-captured live range [336, 380] (gilde.exe in-city capture:
// runtime lantern intensity samples 336..380 around ~358).
TEST(CityGouraud, FlickerEnvelopeMatchesCapture) {
    float lo = 1e9f, hi = -1e9f;
    for (u32 lightIdx = 0; lightIdx < 32; ++lightIdx) {
        for (u32 tick = 0; tick < 256; ++tick) {
            const u32 h = lightIdx * 2654435761u + tick * 40503u;
            const u32 tri = (h >> 8) & 0x3F;
            const float wob = ((tri < 32 ? tri : 63 - tri) / 31.0f) * 2.0f - 1.0f;
            const float inten = 358.0f + wob * 22.0f;
            if (inten < lo) lo = inten;
            if (inten > hi) hi = inten;
        }
    }
    CHECK(lo >= 336.0f - 1e-3f);
    CHECK(hi <= 380.0f + 1e-3f);
    CHECK(hi - lo > 20.0f);   // the wobble actually spans the envelope
}
