// Golden-vector unit tests for the reconstructed combat/physics pure-math
// leaves (gilde.exe 0x48b744 InitDefaultParameters, 0x5d8e80 Physics_Update).
#include "tests/framework/test.h"

#include "sim/combat_recon_balance.h"

#include <array>
#include <cmath>

using namespace guild;
using namespace guild::sim;

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// Expected post-processed balance table (cumulative thresholds in [0,1] after
// the 0.01 scale + cumulative pass + dedup pass). Computed independently with
// float32 arithmetic from the decompiled default constants.
const float kGolden[kCombatBalanceFloats] = {
    0.1f, 0.2f, 0.f, 0.f, 0.5f, 1.f,   // row 0
    0.4f, 0.6f, 0.f, 0.f, 0.8f, 1.f,   // row 1
    0.8f, 1.f,  0.f, 0.f, 0.f,  0.f,   // row 2 (raw col0=80: ecx=0x42A00000 preserved across SetGrayColorThunk @0x48b79f)
    0.7f, 1.f,  0.f, 0.f, 0.f,  0.f,   // row 3
    0.5f, 1.f,  0.f, 0.f, 0.f,  0.f,   // row 4
    0.1f, 0.2f, 0.f, 0.f, 0.5f, 1.f,   // row 5
    0.8f, 1.f,  0.f, 0.f, 0.f,  0.f,   // row 6
    0.8f, 1.f,  0.f, 0.f, 0.f,  0.f,   // row 7
    0.6f, 1.f,  0.f, 0.f, 0.f,  0.f,   // row 8
    0.5f, 1.f,  0.f, 0.f, 0.f,  0.f,   // row 9
    0.05f, 0.f, 0.1f, 0.f, 0.3f, 1.f,  // row 10
    0.5f,  0.f, 0.6f, 0.f, 0.8f, 1.f,  // row 11
    0.8f,  0.f, 1.f,  0.f, 0.f,  0.f,  // row 12
    0.7f,  0.f, 1.f,  0.f, 0.f,  0.f,  // row 13
    0.5f,  0.f, 1.f,  0.f, 0.f,  0.f,  // row 14
    0.2f,  0.f, 1.f,  0.f, 0.f,  0.f,  // row 15
    0.3f,  0.f, 1.f,  0.f, 0.f,  0.f,  // row 16
    0.4f,  0.f, 1.f,  0.f, 0.f,  0.f,  // row 17
    0.6f,  0.f, 1.f,  0.f, 0.f,  0.f,  // row 18
    0.7f,  0.f, 1.f,  0.f, 0.f,  0.f,  // row 19
    0.2f, 0.f, 0.f, 0.f, 0.3f, 1.f,    // row 20
    0.3f, 0.f, 0.f, 0.f, 0.5f, 1.f,    // row 21
    1.f,  0.f, 0.f, 0.f, 0.f,  0.f,    // row 22
    1.f,  0.f, 0.f, 0.f, 0.f,  0.f,    // row 23
    1.f,  0.f, 0.f, 0.f, 0.f,  0.f,    // row 24
    0.2f, 0.f, 0.f, 0.f, 0.3f, 1.f,    // row 25
    0.3f, 0.f, 0.f, 0.f, 0.5f, 1.f,    // row 26
    1.f,  0.f, 0.f, 0.f, 0.f,  0.f,    // row 27
    1.f,  0.f, 0.f, 0.f, 0.f,  0.f,    // row 28
    1.f,  0.f, 0.f, 0.f, 0.f,  0.f,    // row 29
};

} // namespace

TEST(CombatReconBalance, InitReturnValue) {
    // 0x48bb6c/0x48bb70: each of the 6 (v12=0..5) iterations runs
    // `result = v11 + 120` then `v11 += 120`, with v11 init 120. The final pass
    // (v12 == 5) has v11 == 720 entering, so result == 840.
    // (Verified against live decompile 0x48b744: 120->240->360->480->600->720->840.)
    std::array<float, kCombatBalanceFloats> t{};
    int rv = InitDefaultParameters(t.data());
    CHECK_EQ(rv, 840);
}

TEST(CombatReconBalance, InitGoldenTable) {
    auto t = InitDefaultParametersTable();
    for (int i = 0; i < kCombatBalanceFloats; ++i)
        CHECK(approx(t[i], kGolden[i]));
}

TEST(CombatReconBalance, InitColumnsAreCumulativeMonotone) {
    // Within each bucket row, the nonzero cumulative columns are >= previous.
    auto t = InitDefaultParametersTable();
    for (int row = 0; row < 30; ++row) {
        float last = 0.f;
        for (int c = 0; c < kCombatBalanceCols; ++c) {
            float v = t[row * kCombatBalanceCols + c];
            if (v != 0.f) {
                CHECK(v >= last - 1e-6f);
                last = v;
            }
        }
        // Fully-populated rows (every value branch present) reach the 1.0 total;
        // partial default rows (e.g. raw [0,20,0,0,0,0]) stop earlier. Either
        // way no cumulative value exceeds 1.0.
        CHECK(last <= 1.f + 1e-6f);
    }
}

TEST(CombatReconBalance, InitDedupZerosDuplicateColumns) {
    // Row 0 raw scaled+cumulative is [.1,.2,.2,.2,.5,1]; dedup zeroes cols 2,3.
    auto t = InitDefaultParametersTable();
    CHECK(approx(t[0], 0.1f));
    CHECK(approx(t[1], 0.2f));
    CHECK_EQ(t[2], 0.0f);
    CHECK_EQ(t[3], 0.0f);
    CHECK(approx(t[4], 0.5f));
    CHECK(approx(t[5], 1.0f));
}

// --- VIBE_Physics_Update ---------------------------------------------------

static PhysicsSpriteState MakeState(u32 bank, u32 gateB, i16 velY, i16 velX,
                                    u8 shape) {
    PhysicsSpriteState s{};
    s.bank = bank;
    s.gateB = gateB;
    s.velY = velY;
    s.velX = velX;
    s.shapeIndex = shape;
    return s;
}

TEST(CombatReconBalance, PhysicsEmptyBankReturnsByteOffset) {
    ResetPhysicsRenderProbes();
    SetPhysicsRenderHooks(PhysicsRenderHooks{}); // default inert hooks
    std::array<PhysicsSpriteState, 4> tbl{};
    // slot 3 has bank==0 -> returns 17*3 == 51, no hooks fired.
    int rv = Physics_Update(3, /*a2*/ 99, /*a3*/ 0x12340000, tbl.data());
    CHECK_EQ(rv, 51);
    CHECK_EQ(LastAnimationBasicCall().called, false);
    CHECK_EQ(LastVelocityApplyCall().called, false);
}

TEST(CombatReconBalance, PhysicsAnimationOnlyWhenGateBClear) {
    ResetPhysicsRenderProbes();
    SetPhysicsRenderHooks(PhysicsRenderHooks{});
    std::array<PhysicsSpriteState, 2> tbl{};
    // velY = 3 -> v6 = (3<<16)|HIWORD(a3); v6>>16 == 3 (low half ignored by >>16).
    // velX = 5 -> v7 = 5<<16; v7>>16 == 5.
    tbl[1] = MakeState(/*bank*/ 0xABCD, /*gateB*/ 0, /*velY*/ 3, /*velX*/ 5,
                       /*shape*/ 7);
    int rv = Physics_Update(1, /*a2*/ 42, /*a3*/ 0xBEEF0000, tbl.data());
    CHECK_EQ(rv, 1); // DefaultAnimationBasic returns 1
    CHECK_EQ(LastVelocityApplyCall().called, false); // gateB clear
    const auto& a = LastAnimationBasicCall();
    CHECK_EQ(a.called, true);
    CHECK_EQ(a.x, 3);
    CHECK_EQ(a.y, 5);
    CHECK_EQ(a.bank, 0xABCDu);
    CHECK_EQ(a.arg, 42);
    CHECK_EQ((int)a.shape, 7);
}

TEST(CombatReconBalance, PhysicsVelocityWhenGateBSet) {
    ResetPhysicsRenderProbes();
    SetPhysicsRenderHooks(PhysicsRenderHooks{});
    std::array<PhysicsSpriteState, 1> tbl{};
    tbl[0] = MakeState(/*bank*/ 0x10, /*gateB*/ 1, /*velY*/ -2, /*velX*/ 4,
                       /*shape*/ 9);
    int rv = Physics_Update(0, /*a2*/ 7, /*a3*/ 0, tbl.data());
    CHECK_EQ(rv, 1);
    const auto& vel = LastVelocityApplyCall();
    CHECK_EQ(vel.called, true);
    // Velocity_Apply args are (v6>>16)+6, (v7>>16)+6 => (-2+6, 4+6) = (4, 10).
    CHECK_EQ(vel.x, 4);
    CHECK_EQ(vel.y, 10);
    CHECK_EQ(vel.bank, 0x10u);
    CHECK_EQ(vel.arg, 7);
    CHECK_EQ((int)vel.shape, 9);
    // Animation_Basic uses raw v6>>16, v7>>16 = (-2, 4).
    const auto& anim = LastAnimationBasicCall();
    CHECK_EQ(anim.called, true);
    CHECK_EQ(anim.x, -2);
    CHECK_EQ(anim.y, 4);
}

TEST(CombatReconBalance, PhysicsSignedVelocityArithmeticShift) {
    ResetPhysicsRenderProbes();
    SetPhysicsRenderHooks(PhysicsRenderHooks{});
    std::array<PhysicsSpriteState, 1> tbl{};
    // A negative velY must arithmetic-shift back to its signed value through >>16.
    tbl[0] = MakeState(0x1, /*gateB*/ 0, /*velY*/ -100, /*velX*/ -7, /*shape*/ 0);
    Physics_Update(0, 0, /*a3 low-half noise*/ 0xFFFF0000, tbl.data());
    const auto& a = LastAnimationBasicCall();
    CHECK_EQ(a.x, -100);
    CHECK_EQ(a.y, -7);
}

TEST(CombatReconBalance, PhysicsCustomHookOverride) {
    ResetPhysicsRenderProbes();
    PhysicsRenderHooks h{};
    h.animationBasic = [](int, int, u32, int, u8) -> int { return 0xCAFE; };
    SetPhysicsRenderHooks(h);
    std::array<PhysicsSpriteState, 1> tbl{};
    tbl[0] = MakeState(0x1, 0, 0, 0, 0);
    int rv = Physics_Update(0, 0, 0, tbl.data());
    CHECK_EQ(rv, 0xCAFE);
    SetPhysicsRenderHooks(PhysicsRenderHooks{}); // restore default
}

// Wave-12 hardening: the bitfield assembly `HIWORD(v) = velocity` was a
// signed-left-shift of a negative velocity (UB, flagged by UBSAN). The fix
// assembles the high half in unsigned space then reinterprets; the later signed
// `>> 16` recovers the velocity. Pin both i16 extremes at the boundary.
TEST(CombatReconBalance, PhysicsExtremeSignedVelocityNoUB) {
    ResetPhysicsRenderProbes();
    SetPhysicsRenderHooks(PhysicsRenderHooks{});
    std::array<PhysicsSpriteState, 1> tbl{};
    // velY = INT16_MIN (-32768), velX = INT16_MAX (32767).
    tbl[0] = MakeState(0x2, /*gateB*/ 1, /*velY*/ -32768, /*velX*/ 32767,
                       /*shape*/ 3);
    Physics_Update(0, /*a2*/ 1, /*a3*/ 0xCAFE0000, tbl.data());
    const auto& a = LastAnimationBasicCall();
    CHECK_EQ(a.x, -32768);          // sign preserved through >>16
    CHECK_EQ(a.y, 32767);
    const auto& v = LastVelocityApplyCall();
    CHECK_EQ(v.x, -32768 + 6);
    CHECK_EQ(v.y, 32767 + 6);
}

// Boundary index: a slot at the highest populated index of the caller table is
// read at exactly table[a1] (the engine's fixed dword_1406420 stride), with no
// over-read past it. (Per-rule the index bound is the caller's contract — the
// original addresses a fixed global; a negative/oversized a1 is the binary's own
// fault, documented in progress/harden-combat-wave12.md.)
TEST(CombatReconBalance, PhysicsMaxValidIndexReadsExactSlot) {
    ResetPhysicsRenderProbes();
    SetPhysicsRenderHooks(PhysicsRenderHooks{});
    std::array<PhysicsSpriteState, 8> tbl{};   // valid indices 0..7
    tbl[7] = MakeState(/*bank*/ 0x55, /*gateB*/ 0, /*velY*/ 1, /*velX*/ 2, 4);
    int rv = Physics_Update(7, /*a2*/ 9, /*a3*/ 0, tbl.data());
    CHECK_EQ(rv, 1);
    const auto& a = LastAnimationBasicCall();
    CHECK_EQ(a.bank, 0x55u);
    CHECK_EQ(a.x, 1);
    CHECK_EQ(a.y, 2);
}
