#include "test.h"
#include "render/sprite_scale.h"
#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

// =============================================================================
// Golden vectors for the WORLD-SPACE billboard projection arm of
// gilde.exe 0x5AC970 VIBE_Particle_UpdateBillboards. The math is fixed by the
// disassembly:
//   invZ    = 1/cz
//   screenX = projScaleX*cx*invZ + centerX
//   screenY = projScaleY*cy*invZ + centerY
//   distSq  = cx*cx + cy*cy + cz*cz
//   alpha   = distSq<=fadeMinSq ? 255 : 255 - min(255,(sqrt(distSq)-near)*scale)
// Expected values below are hand-derived from those formulas (independent of
// the implementation source) so the test pins the exact behaviour.
// =============================================================================

static BillboardVertex MakeVtx(float x, float y, float z, u8 flags) {
    BillboardVertex v;                 // raw[] is value-initialised to 0
    v.cx() = x; v.cy() = y; v.cz() = z; v.flags() = flags;
    v.colorSrc() = 0xAABBCCDD;
    v.byteSrc()  = 0xFC;               // >>2 == 0x3F
    return v;
}

static BillboardParams MakeParams(bool fade) {
    BillboardParams p;
    p.projScaleX = 320.0f;
    p.projScaleY = 240.0f;
    p.centerX    = 400.0f;             // 800x600 viewport centre
    p.centerY    = 300.0f;
    p.fadeMinSq  = 100.0f;             // distSq<=100 -> full alpha
    p.fadeNear   = 10.0f;
    p.fadeScale  = 4.0f;
    p.enabled    = true;
    p.depthFade  = fade;
    return p;
}

// --- core projection: a vertex straight ahead lands at the viewport centre ----
TEST(BillboardProject, CenteredVertex) {
    BillboardParams p = MakeParams(/*fade=*/false);
    BillboardVertex v = MakeVtx(0.0f, 0.0f, 5.0f, 0x80);
    ProjectBillboardVertices(&v, 1, p);

    CHECK(v.invZ() == 1.0f / 5.0f);
    CHECK(v.screenX() == 400.0f);            // 320*0*invZ + 400
    CHECK(v.screenY() == 300.0f);            // 240*0*invZ + 300
    // colorOut (+0x40) is a verbatim copy of colorSrc (+0x44). Note byteSrc
    // (+0x46) physically aliases the high half of that colour word in the real
    // record, so MakeVtx's byteSrc=0xFC overwrites byte 0x46: 0xAABBCCDD -> 0xAAFCCCDD.
    CHECK_EQ(v.colorOut(), 0xAAFCCCDDu);     // +0x40 = +0x44 (with byteSrc alias)
}

// --- perspective scale: closer (smaller z) pushes offset further from centre --
TEST(BillboardProject, PerspectiveScale) {
    BillboardParams p = MakeParams(false);
    BillboardVertex nr = MakeVtx(1.0f, 1.0f, 2.0f, 0x80);
    BillboardVertex fr = MakeVtx(1.0f, 1.0f, 8.0f, 0x80);
    ProjectBillboardVertices(&nr, 1, p);
    ProjectBillboardVertices(&fr, 1, p);

    CHECK(nr.screenX() == 560.0f);           // 320*1/2 + 400
    CHECK(nr.screenY() == 420.0f);           // 240*1/2 + 300
    CHECK(fr.screenX() == 440.0f);           // 320*1/8 + 400
    CHECK(fr.screenY() == 330.0f);           // 240*1/8 + 300
    CHECK((nr.screenX() - p.centerX) > (fr.screenX() - p.centerX));
}

// --- dead vertices (bit7 clear) are untouched -------------------------------
TEST(BillboardProject, SkipsDeadVertex) {
    BillboardParams p = MakeParams(false);
    BillboardVertex v = MakeVtx(1.0f, 1.0f, 2.0f, 0x00);   // not live
    v.screenX() = -777.0f; v.screenY() = -777.0f; v.invZ() = -777.0f;
    ProjectBillboardVertices(&v, 1, p);
    CHECK(v.screenX() == -777.0f);
    CHECK(v.screenY() == -777.0f);
    CHECK(v.invZ()    == -777.0f);
}

// --- depth fade: near -> full alpha; mid -> 255 - clamp; far -> 0 ------------
TEST(BillboardProject, DepthFade) {
    BillboardParams p = MakeParams(/*fade=*/true);

    // distSq = 9+9+9 = 27 <= 100 -> alpha 255
    BillboardVertex nearV = MakeVtx(3.0f, 3.0f, 3.0f, 0x80);
    ProjectBillboardVertices(&nearV, 1, p);
    CHECK_EQ((int)nearV.alphaOut(), 255);

    // cx=cy=0, cz=20 -> distSq=400 (>100). sqrt=20. (20-10)*4=40 -> 255-40=215
    BillboardVertex midV = MakeVtx(0.0f, 0.0f, 20.0f, 0x80);
    ProjectBillboardVertices(&midV, 1, p);
    CHECK_EQ((int)midV.alphaOut(), 215);

    // cz=100 -> distSq=10000, sqrt=100, (100-10)*4=360 -> clamp 255 -> 255-255=0
    BillboardVertex farV = MakeVtx(0.0f, 0.0f, 100.0f, 0x80);
    ProjectBillboardVertices(&farV, 1, p);
    CHECK_EQ((int)farV.alphaOut(), 0);
}

// --- disabled (byte_649D70==0) path: project + byteOut, no alpha --------------
TEST(BillboardProject, DisabledPath) {
    BillboardParams p = MakeParams(true);
    p.enabled = false;                       // billboards globally off
    BillboardVertex v = MakeVtx(0.0f, 0.0f, 4.0f, 0x80);
    v.alphaOut() = 0x5A;                      // must stay untouched
    ProjectBillboardVertices(&v, 1, p);
    CHECK(v.invZ() == 0.25f);
    CHECK(v.screenX() == 400.0f);
    CHECK_EQ((int)v.byteOut(), 0x3F);         // 0xFC >> 2
    CHECK_EQ((int)v.alphaOut(), 0x5A);        // NOT written in disabled path
}

// --- truncation toward zero (frndint with RC=truncate, stored as a byte) ------
TEST(BillboardProject, AlphaTruncatesTowardZero) {
    BillboardParams p = MakeParams(true);
    p.fadeNear  = 0.0f;
    p.fadeScale = 1.0f;
    p.fadeMinSq = 0.0f;
    // cz=10.3 -> distSq=106.09, sqrt=10.3 ; alpha = 255-10.3 = 244.7 -> 244
    BillboardVertex v = MakeVtx(0.0f, 0.0f, 10.3f, 0x80);
    ProjectBillboardVertices(&v, 1, p);
    CHECK_EQ((int)v.alphaOut(), 244);
}

// =============================================================================
// WAVE-10 HARDENING — degenerate projection inputs (ASAN+UBSAN). cz==0 / cz<0 are
// IEEE float divides the original also performs (fdiv), so inf/negative results
// are the faithful 1:1 behavior — these pin that no UB/OOB occurs, NOT a guard.
// =============================================================================

// cz == 0: invZ = 1/0 = +inf (IEEE); screenX/Y are inf*scale + center (no UB).
TEST(BillboardProject, HardenDepthZeroProducesInf) {
    BillboardParams p = MakeParams(/*fade=*/false);
    BillboardVertex v = MakeVtx(1.0f, 1.0f, 0.0f, 0x80);
    ProjectBillboardVertices(&v, 1, p);
    CHECK(std::isinf(v.invZ()));
    CHECK(v.invZ() > 0.0f);
    CHECK(std::isinf(v.screenX()));   // projScaleX*cx*inf + center
}

// cz < 0 (vertex behind the camera): invZ negative, projection mirrors through
// the centre. The original divides identically; just ensure finite/no-crash.
TEST(BillboardProject, HardenBehindCamera) {
    BillboardParams p = MakeParams(false);
    BillboardVertex v = MakeVtx(2.0f, 4.0f, -8.0f, 0x80);
    ProjectBillboardVertices(&v, 1, p);
    CHECK(v.invZ() == 1.0f / -8.0f);
    CHECK(v.screenX() == 320.0f * 2.0f * (1.0f / -8.0f) + 400.0f);
    CHECK(v.screenY() == 240.0f * 4.0f * (1.0f / -8.0f) + 300.0f);
}

// Depth-fade with cz == 0 (origin vertex): invZ = 1/0 = +inf, yet distSq =
// 0+0+0 = 0 <= fadeMinSq, so the fade takes the full-alpha (255) branch. This pins
// that the inf invZ does NOT poison the (separate) distSq fade computation.
TEST(BillboardProject, HardenDepthFadeAtZeroDepth) {
    BillboardParams p = MakeParams(/*fade=*/true);
    BillboardVertex v = MakeVtx(0.0f, 0.0f, 0.0f, 0x80);
    ProjectBillboardVertices(&v, 1, p);
    CHECK(std::isinf(v.invZ()));       // 1/0 -> inf (faithful fdiv)
    CHECK_EQ((int)v.alphaOut(), 255);  // distSq 0 <= fadeMinSq -> full alpha
}

// Depth-fade at a huge depth: distSq is enormous, sqrt -> large, t clamps to 255,
// alpha = 255 - 255 = 0 (the far clamp). Pins the upper clamp with no NaN/UB.
TEST(BillboardProject, HardenDepthFadeFarClampsToZero) {
    BillboardParams p = MakeParams(/*fade=*/true);
    BillboardVertex v = MakeVtx(0.0f, 0.0f, 1.0e6f, 0x80);  // distSq ~1e12
    ProjectBillboardVertices(&v, 1, p);
    CHECK_EQ((int)v.alphaOut(), 0);    // t clamps to 255 -> alpha 0
}

// "max scale": a vertex extremely close to the camera (tiny positive cz) yields a
// huge but finite projected coordinate (the depth-scale blowup). No overflow trap.
TEST(BillboardProject, HardenMaxScaleTinyDepth) {
    BillboardParams p = MakeParams(false);
    BillboardVertex v = MakeVtx(1.0f, 1.0f, 1e-30f, 0x80);
    ProjectBillboardVertices(&v, 1, p);
    CHECK(std::isfinite(v.invZ()) || std::isinf(v.invZ()));
    // screenX = 320 * 1 * (1/1e-30) + 400 = ~3.2e32 (finite float, or inf if it
    // overflows the float range) — either way no crash / no UB.
    CHECK(v.screenX() > 1.0e30f);
}

// Zero-count / empty spans must be safe no-ops across all three arms + the quad
// pass + the tint broadcast.
TEST(BillboardProject, HardenZeroCountNoOp) {
    BillboardParams p = MakeParams(true);
    ProjectBillboardVertices(nullptr, 0, p);      // no project
    p.enabled = false;
    ProjectBillboardVertices(nullptr, 0, p);      // disabled arm, count 0
    BillboardApplyEffectTint(nullptr, 0, 8, 0, 0, 0);
    BillboardQuadVisibilityPass(nullptr, 0);
    CHECK(true);                                   // reached without crash
}

// =============================================================================
// Per-quad back-face / visibility pass (0x5ACAE0).
// =============================================================================
static BillboardQuad MakeQuad(BillboardVertex* a, BillboardVertex* b,
                              BillboardVertex* c, u8 flags, u8 flags2) {
    BillboardQuad q;
    q.v0 = a; q.v1 = b; q.v2 = c; q.flags = flags; q.flags2 = flags2;
    return q;
}

TEST(BillboardQuad, ForceKeepSetsBit6) {
    BillboardVertex a, b, c;
    BillboardQuad q = MakeQuad(&a, &b, &c, 0x80 | 0x10, 0x00);
    BillboardQuadVisibilityPass(&q, 1);
    CHECK((q.flags & 0x40) != 0);            // bit6 set
    CHECK((q.flags & 0x80) != 0);            // bit7 retained
}

TEST(BillboardQuad, NotVisibleIgnored) {
    BillboardVertex a, b, c;
    BillboardQuad q = MakeQuad(&a, &b, &c, 0x10, 0x00);   // bit7 clear
    BillboardQuadVisibilityPass(&q, 1);
    CHECK_EQ((int)q.flags, 0x10);            // untouched
}

TEST(BillboardQuad, BackFaceWindingCull) {
    // (v0X-v2X)*(v0Y-v1Y) > (v0X-v1X)*(v0Y-v2Y) -> front-facing -> cull (clear bit7)
    BillboardVertex v0, v1, v2;
    v0.screenX() = 0;  v0.screenY() = 0;
    v1.screenX() = 10; v1.screenY() = 0;
    v2.screenX() = 0;  v2.screenY() = 10;
    // lhs = (0-0)*(0-0)=0 ; rhs = (0-10)*(0-10)=100 ; 0>100? no -> NOT culled
    BillboardQuad keep = MakeQuad(&v0, &v1, &v2, 0x80, 0x00);
    BillboardQuadVisibilityPass(&keep, 1);
    CHECK((keep.flags & 0x80) != 0);

    BillboardVertex w0, w1, w2;
    w0.screenX() = 0;  w0.screenY() = 0;
    w1.screenX() = 0;  w1.screenY() = 10;    // v1
    w2.screenX() = 10; w2.screenY() = 0;     // v2
    // lhs = (0-10)*(0-10)=100 ; rhs = (0-0)*(0-0)=0 ; 100>0 -> CULL (clear bit7)
    BillboardQuad cull = MakeQuad(&w0, &w1, &w2, 0x80, 0x00);
    BillboardQuadVisibilityPass(&cull, 1);
    CHECK((cull.flags & 0x80) == 0);

    // never-cull (flags2 bit2) keeps it even when front-facing
    BillboardQuad nev = MakeQuad(&w0, &w1, &w2, 0x80, 0x04);
    BillboardQuadVisibilityPass(&nev, 1);
    CHECK((nev.flags & 0x80) != 0);
}

// =============================================================================
// Node-level EFFECT-TINT arm (0x5ACAB0). When the node type byte +0x215 >= 5 the
// original packs a 0x00RRGGBB colour and broadcasts it into colorOut (+0x40) of
// EVERY vertex (no per-vertex live test). nodeType 8 is the fixed 0x1F1FFF; 5..7
// pack from three node floats, each truncated toward zero to a byte.
// Golden values derived from the disasm pack: R=trunc(f5C)<<16, G=trunc(f60)<<8,
// B=trunc(f64); and the literal `mov esi, 1F1FFFh`.
// =============================================================================

// --- the colour computation (independent of the broadcast) ------------------
TEST(BillboardTint, ColorBelowFiveNoTint) {
    u32 c = 0xDEADBEEF;
    for (u8 t = 0; t < 5; ++t) {
        CHECK(!BillboardEffectTintColor(t, 11.0f, 22.0f, 33.0f, c));
        CHECK_EQ(c, 0xDEADBEEFu);            // outColor left untouched
    }
}

TEST(BillboardTint, ColorTypeEightIsConstant) {
    u32 c = 0;
    // nodeType 8 ignores the floats and uses the literal 0x1F1FFF.
    CHECK(BillboardEffectTintColor(8, 1.0f, 2.0f, 3.0f, c));
    CHECK_EQ(c, 0x1F1FFFu);                   // mov esi, 1F1FFFh
    // decode: R=0x1F, G=0x1F, B=0xFF
    CHECK_EQ((int)((c >> 16) & 0xFF), 0x1F);
    CHECK_EQ((int)((c >> 8)  & 0xFF), 0x1F);
    CHECK_EQ((int)( c        & 0xFF), 0xFF);
}

TEST(BillboardTint, ColorPackedFromFloats) {
    u32 c = 0;
    // R=200, G=128, B=64 -> 0x00C88040
    CHECK(BillboardEffectTintColor(5, 200.0f, 128.0f, 64.0f, c));
    CHECK_EQ(c, 0x00C88040u);
    // type 6 and 7 pack the same way (only !=8 matters in the else arm)
    CHECK(BillboardEffectTintColor(6, 16.0f, 32.0f, 48.0f, c));
    CHECK_EQ(c, 0x00102030u);
    CHECK(BillboardEffectTintColor(7, 255.0f, 0.0f, 1.0f, c));
    CHECK_EQ(c, 0x00FF0001u);
}

TEST(BillboardTint, ColorTruncatesTowardZeroAndNarrows) {
    u32 c = 0;
    // truncate toward zero: 200.9 -> 200, 0.9 -> 0
    CHECK(BillboardEffectTintColor(5, 200.9f, 0.9f, 127.5f, c));
    CHECK_EQ(c, 0x00C8007Fu);                 // 200, 0, 127
    // 8-bit narrowing: 256.0 -> (u8)256 == 0; 257.0 -> 1 (only low byte survives)
    CHECK(BillboardEffectTintColor(5, 256.0f, 257.0f, 511.0f, c));
    CHECK_EQ(c, 0x00000100u | 0xFFu);         // R=0, G=1, B=255 -> 0x000001FF
    CHECK_EQ(c, 0x000001FFu);
}

// --- the broadcast loop -----------------------------------------------------
TEST(BillboardTint, BroadcastOverwritesAllColorOut) {
    BillboardVertex v[3];
    for (auto& x : v) x.colorOut() = 0x11223344;  // project-arm copy to be overwritten
    BillboardApplyEffectTint(v, 3, /*nodeType=*/8, 0, 0, 0);
    for (auto& x : v) CHECK_EQ(x.colorOut(), 0x1F1FFFu);
}

TEST(BillboardTint, BroadcastIgnoresLiveBit) {
    // The tint loop is unconditional over the count — even bit7-clear (dead)
    // vertices get the tint written (unlike the project arm which skips them).
    BillboardVertex v[2];
    v[0].flags() = 0x80; v[1].flags() = 0x00;     // one live, one dead
    v[0].colorOut() = 0; v[1].colorOut() = 0;
    BillboardApplyEffectTint(v, 2, /*nodeType=*/5, 10.0f, 20.0f, 30.0f);
    CHECK_EQ(v[0].colorOut(), 0x000A141Eu);       // 10,20,30
    CHECK_EQ(v[1].colorOut(), 0x000A141Eu);       // dead vertex tinted too
}

TEST(BillboardTint, BroadcastNoOpBelowFive) {
    BillboardVertex v[2];
    v[0].colorOut() = 0xCAFEBABE; v[1].colorOut() = 0xCAFEBABE;
    BillboardApplyEffectTint(v, 2, /*nodeType=*/4, 1.0f, 2.0f, 3.0f);
    CHECK_EQ(v[0].colorOut(), 0xCAFEBABEu);       // nodeType < 5 -> untouched
    CHECK_EQ(v[1].colorOut(), 0xCAFEBABEu);
}
