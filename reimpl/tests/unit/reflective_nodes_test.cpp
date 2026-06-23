// Golden-vector unit tests for REFLECTIVE-NODE SETUP (wave-8). Vectors derived
// directly from the gilde.exe decompile:
//   0x5DA714  VIBE_Texture_LoadByName        (sets texRec+104 bit5 = v85)
//   0x5da74f..0x5da76a  the `v85` predicate  (flag2 bit6 && (u8)flag0 < 0xFF)
//   0x5dac7b/0x5dac92   the bit5 write       ((32*v85) | (flags & 0xDF))
//   0x5f67ad..0x5f67c4  the reflective-child scan (child[104] & 0x20)
//   0x5f68c8            the mirror-plane d store (d = n . p)
#include "tests/framework/test.h"
#include "render/reflective_nodes.h"

#include <cmath>

using namespace guild;
using namespace guild::render;

namespace {
bool feq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
}

// ---- ComputeReflectiveBit (v85) --------------------------------------------

TEST(reflective_nodes, v85_needs_flag2_bit6) {
    // flag2 bit6 clear => v85 = 0 regardless of flag0.
    CHECK(!ComputeReflectiveBit(/*flag0*/0x00, /*flag2*/0x00));
    CHECK(!ComputeReflectiveBit(/*flag0*/0x00, /*flag2*/0x3F)); // bits0..5 set, bit6 clear
    CHECK(!ComputeReflectiveBit(/*flag0*/0x10, /*flag2*/0x01));
}

TEST(reflective_nodes, v85_needs_flag0_lowbyte_below_FF) {
    // flag2 bit6 set; flag0 low byte must be < 0xFF.
    CHECK( ComputeReflectiveBit(/*flag0*/0x00, /*flag2*/0x40)); // lo=0x00 < 0xFF
    CHECK( ComputeReflectiveBit(/*flag0*/0xFE, /*flag2*/0x40)); // lo=0xFE < 0xFF
    CHECK(!ComputeReflectiveBit(/*flag0*/0xFF, /*flag2*/0x40)); // lo=0xFF => jnb, v85=0
    // High bytes of flag0 do not affect the byte compare.
    CHECK( ComputeReflectiveBit(/*flag0*/0x0001007Fu, /*flag2*/0x40)); // lo=0x7F
    CHECK(!ComputeReflectiveBit(/*flag0*/0x000100FFu, /*flag2*/0x40)); // lo=0xFF
}

TEST(reflective_nodes, v85_combined) {
    // Both clauses must hold.
    CHECK( ComputeReflectiveBit(0x42, 0xC0));  // lo<FF, bit6 set (also bit7) -> 1
    CHECK(!ComputeReflectiveBit(0xFF, 0xC0));  // bit6 set but lo==FF -> 0
    CHECK(!ComputeReflectiveBit(0x42, 0x80));  // lo<FF but bit6 clear (bit7 only) -> 0
}

// ---- IsMaterialReflective (mesh_load.cpp flag assembly) ---------------------

TEST(reflective_nodes, material_reflective_when_present_and_highshift) {
    // present + real palette index + shiftHi bit6 -> reflective.
    CHECK( IsMaterialReflective(/*present*/true,  /*paletteByte*/3,    /*shiftHi*/1));
    // shiftHi must contribute bit6: shiftHi&1 -> flag2 bit6.
    CHECK(!IsMaterialReflective(/*present*/true,  /*paletteByte*/3,    /*shiftHi*/0));
    // not present -> flag0 low byte stays 0xFF default -> never reflective.
    CHECK(!IsMaterialReflective(/*present*/false, /*paletteByte*/3,    /*shiftHi*/1));
    // present but paletteByte 0xFF -> lo==FF -> not reflective.
    CHECK(!IsMaterialReflective(/*present*/true,  /*paletteByte*/0xFF, /*shiftHi*/1));
}

// ---- ApplyReflectiveBit / TextureFlagIsReflective --------------------------

TEST(reflective_nodes, apply_and_read_bit5) {
    // Set bit5 while preserving all other bits (mask 0xDF == ~0x20).
    u8 f = 0x0B;                                  // bits0,1,3 set (alias/2sided/_NM)
    u8 set = ApplyReflectiveBit(f, true);
    CHECK_EQ((int)set, 0x2B);                     // 0x0B | 0x20
    CHECK(TextureFlagIsReflective(set));
    u8 cleared = ApplyReflectiveBit(set, false);
    CHECK_EQ((int)cleared, 0x0B);                 // bit5 cleared, rest intact
    CHECK(!TextureFlagIsReflective(cleared));
    // idempotent: applying true twice keeps a single bit5.
    CHECK_EQ((int)ApplyReflectiveBit(set, true), 0x2B);
}

TEST(reflective_nodes, read_matches_engine_test) {
    CHECK(!TextureFlagIsReflective(0x00));
    CHECK(!TextureFlagIsReflective(0xDF));        // everything but bit5
    CHECK( TextureFlagIsReflective(0x20));
    CHECK( TextureFlagIsReflective(0xFF));
}

// ---- DeriveReflectionPlane (d = n . p) -------------------------------------

TEST(reflective_nodes, plane_distance_is_n_dot_p) {
    // Axis-aligned floor mirror: n = +Y up, point at height 5.
    float n[3] = {0.f, 1.f, 0.f};
    float p[3] = {12.f, 5.f, -7.f};
    MirrorPlane m = DeriveReflectionPlane(n, p);
    CHECK(feq(m.nx, 0.f) && feq(m.ny, 1.f) && feq(m.nz, 0.f));
    CHECK(feq(m.d, 5.f));                         // 0*12 + 1*5 + 0*-7
}

TEST(reflective_nodes, plane_distance_oblique) {
    float n[3] = {0.6f, 0.8f, 0.f};              // unit normal
    float p[3] = {2.f, 1.f, 9.f};
    MirrorPlane m = DeriveReflectionPlane(n, p);
    CHECK(feq(m.d, 0.6f * 2.f + 0.8f * 1.f));     // 1.2 + 0.8 = 2.0
    CHECK(feq(m.d, 2.0f));
}

TEST(reflective_nodes, plane_feeds_reflect_point) {
    // Sanity: the derived plane reflects a point through a Y=5 floor correctly via
    // wave-6 ReflectPointAcrossPlane (P' = P + t n, t = -(P.n - d)*2).
    float n[3] = {0.f, 1.f, 0.f};
    float p[3] = {0.f, 5.f, 0.f};
    MirrorPlane m = DeriveReflectionPlane(n, p);
    float P[3] = {3.f, 9.f, -1.f};               // 4 above the plane
    float out[3];
    ReflectPointAcrossPlane(P, m, out);
    CHECK(feq(out[0], 3.f) && feq(out[2], -1.f)); // x,z unchanged
    CHECK(feq(out[1], 1.f));                      // 9 -> 1 (reflected through y=5)
}

// ---- FindReflectiveTexture (the child-scan loop) ---------------------------

TEST(reflective_nodes, find_reflective_texture_in_array) {
    // Build three fake texture records: only the middle one has bit5.
    u8 rec0[128] = {0}; rec0[104] = 0x0B;        // not reflective
    u8 rec1[128] = {0}; rec1[104] = 0x2B;        // reflective (bit5)
    u8 rec2[128] = {0}; rec2[104] = 0x40;        // not reflective
    void* arr[3] = {rec0, rec1, rec2};
    CHECK_EQ(FindReflectiveTexture(arr, 3), 1);

    // None reflective -> -1.
    void* arr2[2] = {rec0, rec2};
    CHECK_EQ(FindReflectiveTexture(arr2, 2), -1);

    // Null entries are skipped (engine: `if (result && ...)`).
    void* arr3[3] = {nullptr, rec1, nullptr};
    CHECK_EQ(FindReflectiveTexture(arr3, 3), 1);

    // First match wins (break on first).
    void* arr4[3] = {rec1, rec1, rec2};
    CHECK_EQ(FindReflectiveTexture(arr4, 3), 0);

    CHECK_EQ(FindReflectiveTexture(nullptr, 5), -1);
    CHECK_EQ(FindReflectiveTexture(arr, 0), -1);
}

// ---- WAVE-10 HARDENING — null / empty / boundary (ASAN+UBSAN) ---------------

TEST(reflective_nodes, harden_find_null_and_empty) {
    // Null array pointer -> -1 (guarded), any count.
    CHECK_EQ(FindReflectiveTexture(nullptr, 0), -1);
    CHECK_EQ(FindReflectiveTexture(nullptr, 100), -1);

    // Non-null array, count 0 -> -1 (loop body never runs, no deref).
    u8 rec[128] = {0}; rec[104] = 0x20;          // would match, but count is 0
    void* arr[1] = {rec};
    CHECK_EQ(FindReflectiveTexture(arr, 0), -1);

    // All entries null -> -1; the `if (rec && ...)` skips every null (no OOB read
    // of byte +104 on a null record).
    void* nulls[4] = {nullptr, nullptr, nullptr, nullptr};
    CHECK_EQ(FindReflectiveTexture(nulls, 4), -1);

    // A null record interleaved before the match: null is skipped, match found.
    void* mix[2] = {nullptr, rec};
    CHECK_EQ(FindReflectiveTexture(mix, 2), 1);
}

// "reflective detect on a null material": the material-level predicate takes plain
// values (no pointer to deref). The `present == false` path is the engine's
// "no material" case -> flag0 low byte defaults 0xFF -> never reflective, whatever
// the other args. No memory access at all.
TEST(reflective_nodes, harden_null_material_never_reflective) {
    CHECK(!IsMaterialReflective(/*present*/false, /*paletteByte*/0,    /*shiftHi*/0));
    CHECK(!IsMaterialReflective(/*present*/false, /*paletteByte*/0,    /*shiftHi*/1));
    CHECK(!IsMaterialReflective(/*present*/false, /*paletteByte*/0xFF, /*shiftHi*/1));
    CHECK(!IsMaterialReflective(/*present*/false, /*paletteByte*/0x7F, /*shiftHi*/1));
    // The raw bit predicate with the "absent" flag0 default (0xFF lo) is never set.
    CHECK(!ComputeReflectiveBit(/*flag0*/0xFFu, /*flag2*/0x40u));
}

// ApplyReflectiveBit over the full byte range never corrupts non-bit5 bits.
TEST(reflective_nodes, harden_apply_bit_preserves_other_bits) {
    for (int f = 0; f < 256; ++f) {
        u8 fb = static_cast<u8>(f);
        u8 set = ApplyReflectiveBit(fb, true);
        u8 clr = ApplyReflectiveBit(fb, false);
        CHECK_EQ((int)(set & 0xDF), (int)(fb & 0xDF));   // other bits intact (set)
        CHECK_EQ((int)(clr & 0xDF), (int)(fb & 0xDF));   // other bits intact (clear)
        CHECK((set & 0x20) != 0);
        CHECK((clr & 0x20) == 0);
    }
}

// ---- End-to-end: material -> bit -> find -> plane --------------------------

TEST(reflective_nodes, end_to_end_setup) {
    // A reflective material's texture record carries bit5; a found record then
    // yields a plane via DeriveReflectionPlane — the full setup handoff.
    bool refl = IsMaterialReflective(true, /*pal*/2, /*shiftHi*/1);
    CHECK(refl);
    u8 flags = ApplyReflectiveBit(0x02 /*alias*/, refl);   // mimic loader write
    CHECK_EQ((int)flags, 0x22);
    u8 rec[128] = {0}; rec[104] = flags;
    void* arr[1] = {rec};
    int idx = FindReflectiveTexture(arr, 1);
    CHECK_EQ(idx, 0);
    float n[3] = {0.f, 0.f, 1.f}, p[3] = {0.f, 0.f, 4.f};
    MirrorPlane m = DeriveReflectionPlane(n, p);
    CHECK(feq(m.d, 4.f));
}
