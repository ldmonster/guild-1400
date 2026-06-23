#include "render/node_lod.h"
#include "util/coord.h"
#include "tests/framework/test.h"

#include <cmath>
#include <vector>

// =============================================================================
// NodeLodSelect — golden tests for the per-distance / forced-LOD frame pick
// VIBE_Mesh_SelectLodFrame @0x5adb6c (the per-frame mesh LOD selection the
// universe/object scene-walk runs before projecting an object).
//
// The threshold math is reconstructed 1:1 from the decompile/disasm + the exact
// table bytes recovered via get_bytes:
//   flt_62807C == 0xBF800000 == -1.0   (the "max LOD" bias: lodCount + (-1))
//   distance LOD = trunc( sqrt(|obj-cam|^2) * lodCount * fovScale )  [trunc==fdiv
//     toward zero via VIBE_Coord_ConvertX @0x5c6b08, modeled by util::ConvertX]
//   near (lodCount > truncLod): index = trunc(max(lod, 0))
//   far  (else):                index = lodCount - 1
//   forced (renderFlags & 0x30 || !world): index = ((u8)(4*flags)>>6)-1, clamped
//
// Each golden vector recomputes the expectation with the SAME integer/float ops
// the binary performs (no library helpers that could round differently), so the
// CHECK_EQ is a true byte-faithful oracle of the threshold boundaries.
// =============================================================================
namespace {

using namespace guild::render;
using guild::i32;
using guild::u8;

// A drawable object with `lodCount` poly-valid LOD frames (frame+8 / frame+12
// nonzero) so SelectLodFrame's frame-validity gate always passes.
struct ObjFixture {
    std::vector<LodFrame> frames;
    LodObject obj;
    explicit ObjFixture(int lodCount, int currentFrame = -1) {
        frames.assign(lodCount, LodFrame{ /*polyCount*/ 7, /*polyCap*/ 13 });
        obj.lodCount = (u8)lodCount;
        obj.drawDataReady = true;
        obj.frames = frames.data();
        obj.currentFrameIndex = currentFrame;
    }
};

// The exact distance-branch oracle (mirrors 0x5adc1e..0x5adce9 verbatim).
i32 OracleDistanceIndex(const float pos[3], const float cam[3], int lodCount,
                        float fovScale) {
    float dx = pos[0] - cam[0];
    float dy = pos[1] - cam[1];
    float dz = pos[2] - cam[2];
    // fild word of lodCount (the (float)(u8)count load), then fmul .. fmul flt.
    double lod = std::sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz)
               * (double)lodCount * (double)fovScale;
    int truncLod = (int)guild::util::ConvertX(lod);
    double v12;
    if (lodCount > truncLod) {        // cmp eax, var_18 ; jg loc_5ADCEE
        v12 = lod;
        if (v12 < 0.0) v12 = 0.0;     // fldz ; fcomp ; jbe -> clamp
    } else {
        v12 = (double)lodCount + (-1.0);   // fadd flt_62807C
    }
    return (int)guild::util::ConvertX(v12);
}

// ---- forced LOD branch -----------------------------------------------------

TEST(NodeLodSelect, ForcedLodIndexFromFlagBits) {
    LodView view; view.worldPresent = false;   // !dword_13FCD1C -> forced branch

    // renderFlags bit layout: ((u8)(4*flags) >> 6) - 1 selects the index.
    // flags=0x10 -> 4*0x10=0x40 -> >>6 = 1 -> v4 = 0.
    // flags=0x20 -> 4*0x20=0x80 -> >>6 = 2 -> v4 = 1.
    // flags=0x30 -> 4*0x30=0xC0 -> >>6 = 3 -> v4 = 2.
    struct Case { u8 flags; int expect; };
    Case cases[] = { {0x10, 0}, {0x20, 1}, {0x30, 2} };
    for (auto c : cases) {
        ObjFixture f(/*lodCount*/ 4);
        f.obj.renderFlags = c.flags;
        bool setCull = false;
        i32 idx = SelectLodFrame(f.obj, view, &setCull);
        CHECK_EQ(idx, c.expect);
    }
}

TEST(NodeLodSelect, ForcedLodClampsToLodCountMinusOne) {
    LodView view; view.worldPresent = false;
    // flags=0x30 wants index 2, but only 2 frames exist -> clamp to lodCount-1=1.
    ObjFixture f(/*lodCount*/ 2);
    f.obj.renderFlags = 0x30;
    i32 idx = SelectLodFrame(f.obj, view, nullptr);
    CHECK_EQ(idx, 1);
}

TEST(NodeLodSelect, ForcedZeroSelectorUnderflowsToMaxLod) {
    // (4*flags)>>6 == 0 -> v4 = 0u - 1 = 0xFFFFFFFF (unsigned underflow), so the
    // clamp `(lodCount-1) < v4` fires -> v4 = lodCount-1. Use flags with bit 0x10
    // set (forced branch) but whose 0x30 bits give a 0 selector is impossible, so
    // exercise the underflow via the no-world forced path with flags=0x10 against
    // a multi-LOD object: 0x10 -> selector 1 -> index 0 (covered above). The pure
    // underflow case is flags whose top-two-of-low-six bits are 0; the engine only
    // reaches the forced branch when (flags & 0x30)!=0 OR no world, so drive the
    // underflow through the no-world branch with flags=0x00.
    LodView view; view.worldPresent = false;
    ObjFixture f(/*lodCount*/ 5);
    f.obj.renderFlags = 0x00;     // 4*0=0 -> >>6=0 -> 0u-1 underflow -> clamp
    i32 idx = SelectLodFrame(f.obj, view, nullptr);
    CHECK_EQ(idx, 4);             // lodCount - 1
}

// ---- distance branch boundaries -------------------------------------------

TEST(NodeLodSelect, DistanceNearMidFarBoundaries) {
    LodView view;
    view.worldPresent = true;
    view.camPos[0] = view.camPos[1] = view.camPos[2] = 0.0f;
    view.fovScale = 0.01f;

    struct Case { float x; int lodCount; };
    Case cases[] = {
        {  10.0f, 4 },   // lod=0.4  -> near  -> 0
        {  50.0f, 4 },   // lod=2.0  -> near  -> 2
        { 100.0f, 4 },   // lod=4.0  -> far   -> 3
        {1000.0f, 3 },   // lod=30   -> far   -> 2
        {   0.0f, 4 },   // lod=0    -> near  -> 0
    };
    for (auto c : cases) {
        ObjFixture f(c.lodCount);
        f.obj.pos[0] = c.x; f.obj.pos[1] = 0; f.obj.pos[2] = 0;
        f.obj.renderFlags = 0x00;       // not forced; world present -> distance
        float pos[3] = { c.x, 0.0f, 0.0f };
        int expect = OracleDistanceIndex(pos, view.camPos, c.lodCount, view.fovScale);
        i32 idx = SelectLodFrame(f.obj, view, nullptr);
        CHECK_EQ(idx, expect);
    }
}

TEST(NodeLodSelect, DistanceMatchesOracleOverSweep) {
    LodView view;
    view.worldPresent = true;
    view.camPos[0] = 3.0f; view.camPos[1] = -2.0f; view.camPos[2] = 1.0f;
    view.fovScale = 0.0075f;
    // Sweep object distances in all three axes; assert the reconstruction equals
    // the verbatim integer/float oracle at every step (boundary-sensitive).
    for (int i = 0; i < 40; ++i) {
        ObjFixture f(/*lodCount*/ 6);
        f.obj.renderFlags = 0x00;
        f.obj.pos[0] = (float)(i * 7) - 5.0f;
        f.obj.pos[1] = (float)(i * 3);
        f.obj.pos[2] = (float)(-i * 2) + 4.0f;
        float pos[3] = { f.obj.pos[0], f.obj.pos[1], f.obj.pos[2] };
        int expect = OracleDistanceIndex(pos, view.camPos, 6, view.fovScale);
        i32 idx = SelectLodFrame(f.obj, view, nullptr);
        CHECK_EQ(idx, expect);
        CHECK(idx >= 0 && idx < 6);
    }
}

// ---- guards / side-effects -------------------------------------------------

TEST(NodeLodSelect, NoDrawDataReturnsMinusOne) {
    LodView view; view.worldPresent = false;
    ObjFixture f(/*lodCount*/ 3);
    f.obj.drawDataReady = false;          // !*(drawData) -> return 0 (null frame)
    CHECK_EQ(SelectLodFrame(f.obj, view, nullptr), -1);

    ObjFixture g(/*lodCount*/ 0);         // *(drawData+2316)==0 -> return 0
    g.obj.drawDataReady = true;
    CHECK_EQ(SelectLodFrame(g.obj, view, nullptr), -1);
}

TEST(NodeLodSelect, ZeroPolyFrameReturnsMinusOne) {
    LodView view; view.worldPresent = false;
    ObjFixture f(/*lodCount*/ 2);
    f.obj.renderFlags = 0x30;             // forced -> index 1 (clamped)
    f.frames[1] = LodFrame{ /*polyCount*/ 0, /*polyCap*/ 5 };  // frame+8 == 0
    CHECK_EQ(SelectLodFrame(f.obj, view, nullptr), -1);

    f.frames[1] = LodFrame{ /*polyCount*/ 5, /*polyCap*/ 0 };  // frame+12 == 0
    CHECK_EQ(SelectLodFrame(f.obj, view, nullptr), -1);
}

TEST(NodeLodSelect, CullBitSetWhenFrameChanges) {
    LodView view; view.worldPresent = false;

    // currentFrameIndex already equals the pick (forced 0x20 -> index 1): no set.
    ObjFixture same(/*lodCount*/ 3, /*currentFrame*/ 1);
    same.obj.renderFlags = 0x20;          // selector 2 -> index 1
    bool cull = true;
    CHECK_EQ(SelectLodFrame(same.obj, view, &cull), 1);
    CHECK_EQ(cull, false);

    // currentFrameIndex differs from the pick -> +528 |= 0x40 fires (cull=true).
    ObjFixture diff(/*lodCount*/ 3, /*currentFrame*/ 0);
    diff.obj.renderFlags = 0x20;          // index 1 != current 0
    cull = false;
    CHECK_EQ(SelectLodFrame(diff.obj, view, &cull), 1);
    CHECK_EQ(cull, true);

    // forceRebuild (byte_64A068) fires the set even when the frame is unchanged.
    ObjFixture forced(/*lodCount*/ 3, /*currentFrame*/ 1);
    forced.obj.renderFlags = 0x20;        // index 1 == current 1
    view.forceRebuild = true;
    cull = false;
    CHECK_EQ(SelectLodFrame(forced.obj, view, &cull), 1);
    CHECK_EQ(cull, true);
}

// =============================================================================
// WAVE-10 HARDENING — degenerate / boundary coverage (ASAN+UBSAN).
// =============================================================================

// (w1) Distance exactly 0 (object at camera) -> lod = 0 -> near branch -> index 0.
//      sqrt(0) is well-defined; no UB; the in-range frame at index 0 validates.
TEST(NodeLodSelect, HardenDistanceZero) {
    LodView view;
    view.worldPresent = true;
    view.camPos[0] = 5; view.camPos[1] = 5; view.camPos[2] = 5;
    view.fovScale = 0.5f;
    ObjFixture f(/*lodCount*/ 4);
    f.obj.renderFlags = 0x00;
    f.obj.pos[0] = 5; f.obj.pos[1] = 5; f.obj.pos[2] = 5;   // exactly at the camera
    i32 idx = SelectLodFrame(f.obj, view, nullptr);
    CHECK_EQ(idx, 0);
}

// (w2) Large distance -> far branch -> index clamps to lodCount-1; the chosen
//      frame index never runs off the frames[] array (bounds-safe). Uses a sane
//      fovScale*dist so the truncated LOD stays representable as int (the engine's
//      ConvertX->int is only defined while lod is in int range; we test the clamp,
//      not int-overflow of the distance term).
TEST(NodeLodSelect, HardenHugeDistanceClampsInBounds) {
    LodView view;
    view.worldPresent = true;
    view.camPos[0] = view.camPos[1] = view.camPos[2] = 0.0f;
    view.fovScale = 1.0f;
    for (int lodCount = 1; lodCount <= 8; ++lodCount) {
        ObjFixture f(lodCount);
        f.obj.renderFlags = 0x00;
        f.obj.pos[0] = 1.0e6f;                 // far -> lod huge but < INT_MAX
        // Cross-check against the verbatim oracle, then assert the final clamp.
        float pos[3] = {1.0e6f, 0.0f, 0.0f};
        int expect = OracleDistanceIndex(pos, view.camPos, lodCount, view.fovScale);
        i32 idx = SelectLodFrame(f.obj, view, nullptr);
        CHECK_EQ(idx, expect);
        CHECK_EQ(idx, lodCount - 1);           // far branch -> clamped, in-bounds
        CHECK(idx >= 0 && idx < lodCount);
    }
}

// (w3) Forced-flag bits sweep: every (flags & 0x30) combination resolves to an
//      in-bounds index for a 1-LOD object (the lodCount-1 clamp must hold for the
//      0x10/0x20/0x30 selectors AND the 0x00 underflow), so frames[index] is safe.
TEST(NodeLodSelect, HardenForcedFlagBitsStayInBounds) {
    LodView view; view.worldPresent = false;   // forced branch
    for (int lodCount = 1; lodCount <= 3; ++lodCount) {
        for (u8 fb : {(u8)0x00, (u8)0x10, (u8)0x20, (u8)0x30}) {
            ObjFixture f(lodCount);
            f.obj.renderFlags = fb;
            i32 idx = SelectLodFrame(f.obj, view, nullptr);
            CHECK(idx >= 0 && idx < lodCount); // never OOB into frames[]
        }
    }
}

// (w4) Null frames pointer with drawDataReady && lodCount>0 (an impossible engine
//      state) must NOT deref frames[] -> the memory-safety guard returns -1.
TEST(NodeLodSelect, HardenNullFramesGuard) {
    LodView view; view.worldPresent = false;
    LodObject obj;
    obj.drawDataReady = true;
    obj.lodCount = 4;
    obj.frames = nullptr;                       // would OOB without the guard
    obj.renderFlags = 0x30;
    CHECK_EQ(SelectLodFrame(obj, view, nullptr), -1);
}

TEST(NodeLodSelect, MaxLodBiasConstantIsMinusOne) {
    // flt_62807C == -1.0: a far object lands at lodCount-1 (lodCount + (-1.0)).
    LodView view;
    view.worldPresent = true;
    view.camPos[0] = view.camPos[1] = view.camPos[2] = 0.0f;
    view.fovScale = 1.0f;                  // any positive distance -> far branch
    for (int lodCount = 1; lodCount <= 5; ++lodCount) {
        ObjFixture f(lodCount);
        f.obj.renderFlags = 0x00;
        f.obj.pos[0] = 1000.0f;           // huge lod -> lodCount > truncLod is false
        i32 idx = SelectLodFrame(f.obj, view, nullptr);
        CHECK_EQ(idx, lodCount - 1);
    }
}

} // namespace
