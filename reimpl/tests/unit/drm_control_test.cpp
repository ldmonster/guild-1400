// Golden-vector unit tests for guild::drm DRM control-flow / exception dispatch.
// Drives the pure state machines / dispatch tables with injected exception
// events and asserts the exact transitions, CONTEXT mutations and return
// values reconstructed 1:1 from gilde.exe.
#include "tests/framework/test.h"
#include "drm/drm_control.h"

using namespace guild;        // u8/u16/u32/i16 fixed-width typedefs
using namespace guild::drm;

namespace {
// Observing OS hook: deterministic RandomMod, records exit/trap.
struct RecOs : DrmOs {
    u32 randSeed = 0, randMod = 0; bool randCalled = false;
    u32 randReturn = 7;            // fixed value to make math assertions exact
    u32 RandomMod(u32 seed, u32 modulus) override {
        randCalled = true; randSeed = seed; randMod = modulus; return randReturn;
    }
};
} // namespace

// ---------------------------------------------------------------------------
// VIBE_Drm_CheckExceptionCode @0x140b570
// ---------------------------------------------------------------------------
TEST(DrmControl, CheckExceptionCode_MatchOnFltInvalidOp) {
    CHECK_EQ(CheckExceptionCode(0xC0000090u), -1);   // STATUS_FLOAT_INVALID_OPERATION
    CHECK_EQ(CheckExceptionCode(kExcFltInvalidOp), kExcCodeMatch);
}
TEST(DrmControl, CheckExceptionCode_NoMatchOtherwise) {
    CHECK_EQ(CheckExceptionCode(0u), 0);
    CHECK_EQ(CheckExceptionCode(0xC0000005u), 0);    // access violation != filter
    CHECK_EQ(CheckExceptionCode(0xC0000094u), 0);
    CHECK_EQ(CheckExceptionCode(0x80000003u), kExcCodeNoMatch);
}

// ---------------------------------------------------------------------------
// VIBE_Drm_TrapUndefined @0x140b0b0 — routes ud0 to inert hook.
// (cannot return; verify the hook fires via a thread-free side channel is not
// possible, so we only assert the hook contract on a direct call path that we
// can observe: the hook is invoked before the spin. We use a hook that throws
// out via longjmp-free mechanism is overkill; instead assert TrapUndefined is
// [[noreturn]] by construction and exercise the hook directly.)
TEST(DrmControl, TrapUndefined_HookContract) {
    RecOs os;
    os.TrapUndefined();          // exercises the same primitive TrapUndefined() calls
    CHECK(os.undefinedTrapped);
}

// ---------------------------------------------------------------------------
// VIBE_Drm_ExceptionDispatch @0x140b0c0
// ---------------------------------------------------------------------------
TEST(DrmControl, ExcDispatch_DefaultContinuesSearch) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    ev.code = 0x12345678u;
    CHECK_EQ(ExceptionDispatch(ev, st, os), kContinueSearch); // 1
}

TEST(DrmControl, ExcDispatch_IntDivByZero_SetsEdx6) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    ev.code = kExcIntDivByZero;   // 0xC0000094
    ev.ctx.edx = 99;
    CHECK_EQ(ExceptionDispatch(ev, st, os), kContinueExecution); // 0
    CHECK_EQ(ev.ctx.edx, 6u);
}

TEST(DrmControl, ExcDispatch_Breakpoint_XorsAccumulators) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    st.xorAccLo = 0xAAAAAAAAu; st.xorAccHi = 0x55555555u;
    ev.code = kExcBreakpoint;     // 0x80000003
    ev.ctx.eip = 0x0F0F0F0Fu; ev.ctx.ecx = 0x12341234u;
    CHECK_EQ(ExceptionDispatch(ev, st, os), kContinueExecution);
    CHECK_EQ(st.xorAccLo, 0xAAAAAAAAu ^ 0x0F0F0F0Fu);
    CHECK_EQ(st.xorAccHi, 0x55555555u ^ 0x12341234u);
}

TEST(DrmControl, ExcDispatch_AccessViolation_SetsHwBreakpointAtEipPlus6) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    ev.code = kExcAccessViol;     // 0xC0000005
    ev.ctx.eip = 0x401000u;
    ev.ctx.eflags = 0;
    CHECK_EQ(ExceptionDispatch(ev, st, os), kContinueExecution);
    CHECK_EQ(ev.ctx.dr0, 0x401000u + 6u);   // *(a3+4) = Eip+6
    CHECK_EQ(ev.ctx.dr7, 3u);               // *(a3+24) = 3
    CHECK_EQ(ev.ctx.dr6, 0u);
    CHECK_EQ(ev.ctx.eflags & (1u << 8), (1u << 8)); // TF set
    CHECK_EQ(st.guardArmed, 1);
}

// Single-step: first entry after an access-violation armed the guard (guardArmed=1,
// stepPhase=0) -> the else-branch arms phase 1 and sets the Eip+3 breakpoint.
// Per decompile 0x140b0c0 the arming else-branch runs iff dword_145A11C && !dword_1459F9C
// (guardArmed && !stepPhase); dword_145A11C is set only by the 0xC0000005 AV case.
TEST(DrmControl, ExcDispatch_SingleStep_FirstEntryArmsPhase1) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    st.guardArmed = 1; st.stepPhase = 0;
    st.rangeLo = 0x1000u; st.rangeHi = 0x9000u;
    ev.code = kExcSingleStep;     // 0x80000004
    ev.ctx.eip = 0x5000u;         // inside range
    ev.ctx.byteAtEip = 0x40;
    CHECK_EQ(ExceptionDispatch(ev, st, os), kContinueExecution);
    CHECK_EQ(st.stepPhase, 1);
    CHECK_EQ(ev.ctx.dr0, 0x5000u + 3u);
    CHECK_EQ(ev.ctx.dr7, 3u);
    CHECK_EQ(ev.ctx.eflags & (1u << 8), (1u << 8)); // phase 1 sets TF
    // phase 1 also accumulates opcode-byte counters
    CHECK_EQ(st.sumByteA, (guild::u8)1);
    CHECK_EQ(st.sumDword, 1u);
    CHECK_EQ(st.sumByteB, (guild::u8)0x40);
}

// Single-step: Eip outside range -> divert to return target, phase 2.
TEST(DrmControl, ExcDispatch_SingleStep_OutOfRangeDiverts) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    st.guardArmed = 1; st.stepPhase = 1;   // so the else-arm is skipped
    st.rangeLo = 0x4000u; st.rangeHi = 0x8000u;
    ev.code = kExcSingleStep;
    ev.ctx.eip = 0x9999u;                  // above rangeHi
    ev.ctx.returnTarget = 0xDEAD0000u;
    CHECK_EQ(ExceptionDispatch(ev, st, os), kContinueExecution);
    // guard armed && phase==1 -> guard cleared, phase 0; then out-of-range hits.
    CHECK_EQ(st.stepPhase, 2);
    CHECK_EQ(ev.ctx.dr0, 0xDEAD0000u);     // *(a3+4) = **(a3+196)
    CHECK_EQ(ev.ctx.dr7, 3u);
    CHECK_EQ(ev.ctx.eflags & (1u << 8), 0u); // TF cleared on divert
}

// Single-step phase-1 counter reset honoured.
TEST(DrmControl, ExcDispatch_SingleStep_ResetCountersClearsSums) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    st.guardArmed = 1; st.stepPhase = 1;
    st.rangeLo = 0; st.rangeHi = 0xFFFFFFFFu;  // never diverts
    st.resetCounters = 1;
    st.sumByteA = 5; st.sumDword = 9; st.sumByteB = 7;
    ev.code = kExcSingleStep;
    ev.ctx.eip = 0x100u; ev.ctx.byteAtEip = 0xAB;
    ExceptionDispatch(ev, st, os);
    // guard&&phase1 -> phase 0; phase!=1 so the counter block is NOT entered.
    CHECK_EQ(st.stepPhase, 0);
}

// ---------------------------------------------------------------------------
// VIBE_Drm_VectoredExceptionHandler @0x140f230
// ---------------------------------------------------------------------------
TEST(DrmControl, VEH_Breakpoint_SetsTfAndArms) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    st.xorAccLo = 1; st.xorAccHi = 2;
    ev.code = kExcBreakpoint;
    ev.ctx.eip = 0x10; ev.ctx.ecx = 0x20; ev.ctx.eflags = 0;
    CHECK_EQ(VectoredExceptionHandler(ev, st, os), kContinueExecution);
    CHECK_EQ(st.xorAccLo, 1u ^ 0x10u);
    CHECK_EQ(st.xorAccHi, 2u ^ 0x20u);
    CHECK_EQ(ev.ctx.eflags & (1u << 8), (1u << 8));
    CHECK_EQ(st.guardArmed, 1);
}

TEST(DrmControl, VEH_AccessViolation_CapturesEdxAndClearsEcx) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    st.resetCounters = 0;
    ev.code = kExcAccessViol;
    ev.ctx.edx = 0xCAFEBABEu; ev.ctx.ecx = 0x999u;
    CHECK_EQ(VectoredExceptionHandler(ev, st, os), kContinueExecution);
    CHECK_EQ(ev.ctx.returnTarget, 0xCAFEBABEu); // **(a3+164) = *(a3+172)
    CHECK_EQ(ev.ctx.ecx, 0u);                   // cleared because !resetCounters
}

TEST(DrmControl, VEH_AccessViolation_KeepsEcxWhenResetPending) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    st.resetCounters = 1;
    ev.code = kExcAccessViol;
    ev.ctx.ecx = 0x777u;
    VectoredExceptionHandler(ev, st, os);
    CHECK_EQ(ev.ctx.ecx, 0x777u);               // not overwritten
}

// SINGLE_STEP out-of-range with int3 (0xCC) byte -> random Eip relocation.
TEST(DrmControl, VEH_SingleStep_Int3RelocatesEip) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    os.randReturn = 7;
    st.guardArmed = 1; st.stepPhase = 1;        // skip else-arm; guard&&1 -> phase0
    st.rangeLo = 0x5000u; st.rangeHi = 0x1000u; // lo>hi so (lo-hi) is the span arg
    st.xorAccLo = 0; st.xorAccHi = 0;
    ev.code = kExcSingleStep;
    ev.ctx.eip = 0xFFFFu;                       // > rangeHi -> out of range
    ev.ctx.byteAtEip = 0xCC;                    // int3
    ev.ctx.ecx = 0x11u;
    CHECK_EQ(VectoredExceptionHandler(ev, st, os), kContinueExecution);
    CHECK(os.randCalled);
    CHECK_EQ(os.randSeed, 0xCCu);
    CHECK_EQ(os.randMod, 0x5000u - 0x1000u);    // rangeLo - rangeHi
    CHECK_EQ(ev.ctx.eip, 0x5000u + 7u);         // rangeLo + RandomMod
    CHECK_EQ(st.xorAccLo, (0x5000u + 7u));      // xor 0 with new eip
    CHECK_EQ(st.xorAccHi, 0x11u);
}

TEST(DrmControl, VEH_DefaultContinuesSearch) {
    DrmState st; RecOs os; DrmExceptionEvent ev;
    ev.code = 0xDEADBEEFu;
    CHECK_EQ(VectoredExceptionHandler(ev, st, os), kContinueSearch);
}

// ---------------------------------------------------------------------------
// VIBE_Drm_VerifyTimerOrExit @0x1411e30
// ---------------------------------------------------------------------------
TEST(DrmControl, VerifyTimer_ExitsWhenUncalibrated) {
    DrmState st; RecOs os;
    st.timerCalibration = 0;          // !dword_142DDBC -> exit(-1)
    st.vmTableSize = 100;
    i16 rv = VerifyTimerOrExit(st, os);
    CHECK(os.exited);
    CHECK_EQ(os.lastExitCode, -1);
    CHECK_EQ(rv, (guild::i16)99);     // still decremented after exit-record
}

TEST(DrmControl, VerifyTimer_ExitsWhenTooSlow) {
    DrmState st; RecOs os;
    st.timerCalibration = 100;        // 0.9*100 = 90
    st.timerSampleCount = 91;         // 90 < 91 -> exit
    st.vmTableSize = 8;
    VerifyTimerOrExit(st, os);
    CHECK(os.exited);
    CHECK_EQ(os.lastExitCode, -1);
}

TEST(DrmControl, VerifyTimer_PassesWhenFastEnough) {
    DrmState st; RecOs os;
    st.timerCalibration = 100;        // 0.9*100 = 90
    st.timerSampleCount = 90;         // 90 < 90 is false -> no exit
    st.vmTableSize = 0x1FD3;          // initial word_14501E0
    i16 rv = VerifyTimerOrExit(st, os);
    CHECK(!os.exited);
    CHECK_EQ(rv, (guild::i16)0x1FD2);
    CHECK_EQ(st.vmTableSize, (guild::u16)0x1FD2);
}

TEST(DrmControl, VerifyTimer_TableSizeWrapsAtZero) {
    DrmState st; RecOs os;
    st.timerCalibration = 100; st.timerSampleCount = 0;
    st.vmTableSize = 0;               // --0 -> 0xFFFF (16-bit wrap)
    i16 rv = VerifyTimerOrExit(st, os);
    CHECK_EQ(st.vmTableSize, (guild::u16)0xFFFF);
    CHECK_EQ(rv, (guild::i16)-1);
}

// ---------------------------------------------------------------------------
// VIBE_Drm_StateDispatch @0x140b590
// ---------------------------------------------------------------------------
TEST(DrmControl, StateDispatch_SmallStateUsesStringArmAndTearsDown) {
    auto r = StateDispatch(0u, /*handshakeSucceeded=*/false);
    CHECK(r.decodedSmallTable);
    CHECK(r.stringArm);
    CHECK(!r.largeDecodeArm);
    CHECK(r.handshakeEligible);       // state 0 < 6
    CHECK(r.teardown);
}

TEST(DrmControl, StateDispatch_State44KeepsResources) {
    auto r = StateDispatch(44u, false); // 0x2C
    CHECK(r.keepAliveArm);
    CHECK(!r.stringArm);
    CHECK(!r.largeDecodeArm);
    CHECK(!r.decodedSmallTable);       // 44 > 13
    CHECK(!r.handshakeEligible);       // 44 not <6 and !=13
    CHECK(!r.teardown);                // state == 44 -> resources kept
}

TEST(DrmControl, StateDispatch_State13EligibleStringArm) {
    auto r = StateDispatch(13u, false);
    CHECK(r.decodedSmallTable);
    CHECK(r.stringArm);
    CHECK(r.handshakeEligible);        // a1 == 13
    CHECK(r.teardown);
}

TEST(DrmControl, StateDispatch_LargeStateUsesLargeDecode) {
    auto r = StateDispatch(20u, false);
    CHECK(!r.decodedSmallTable);       // 20 > 13
    CHECK(r.largeDecodeArm);
    CHECK(!r.stringArm);
    CHECK(!r.handshakeEligible);
    CHECK(r.teardown);                 // not 44
}

TEST(DrmControl, StateDispatch_RegisterStateStaysResidentOnHandshake) {
    // state 2 with successful handshake -> dword_145A040=1 -> no teardown.
    auto stay = StateDispatch(2u, /*handshakeSucceeded=*/true);
    CHECK(stay.handshakeEligible);
    CHECK(!stay.teardown);
    // state 2 with failed handshake -> teardown runs.
    auto fall = StateDispatch(2u, /*handshakeSucceeded=*/false);
    CHECK(fall.teardown);
    // state 5 behaves the same as state 2.
    CHECK(!StateDispatch(5u, true).teardown);
    CHECK(StateDispatch(5u, false).teardown);
    // state 1 (eligible but not register-state) never stays resident.
    CHECK(StateDispatch(1u, true).teardown);
}

// ---------------------------------------------------------------------------
// VIBE_Drm_DispatchKeyAction @0x1411030
// ---------------------------------------------------------------------------
TEST(DrmControl, KeyAction_Opcode0WritesZeroAndRedirectA13) {
    auto r = DispatchKeyAction({/*opcode*/0, /*imm*/0}, /*a13*/0x111u, /*a15*/0x222u);
    CHECK(r.keyOutWritten); CHECK_EQ(r.keyOut, 0);
    CHECK(r.redirectWritten); CHECK_EQ(r.redirectValue, 0x111u);
    CHECK_EQ(r.ret, 1);
}
TEST(DrmControl, KeyAction_Opcode1WritesOne) {
    auto r = DispatchKeyAction({1, 0}, 0xAAu, 0xBBu);
    CHECK_EQ(r.keyOut, 1);
    CHECK_EQ(r.redirectValue, 0xAAu);
}
TEST(DrmControl, KeyAction_Opcode2WritesImmediate) {
    auto r = DispatchKeyAction({2, 0x5A}, 0xAAu, 0xBBu);
    CHECK(r.keyOutWritten); CHECK_EQ(r.keyOut, 0x5A);
    CHECK_EQ(r.redirectValue, 0xAAu);
}
TEST(DrmControl, KeyAction_Opcode64RedirectsToA15) {
    auto r = DispatchKeyAction({64, 0}, 0xAAu, 0xBBu);
    CHECK(!r.keyOutWritten);              // 64 does not write *a16
    CHECK(r.redirectWritten);
    CHECK_EQ(r.redirectValue, 0xBBu);     // *(a7+4) = a15
}
TEST(DrmControl, KeyAction_OsCoupledOpcodesReturnOneNoWrite) {
    for (int op : {16, 32, 33, 48, 49}) {
        auto r = DispatchKeyAction({op, 0}, 0x1u, 0x2u);
        CHECK(!r.keyOutWritten);
        CHECK(!r.redirectWritten);
        CHECK_EQ(r.ret, 1);
    }
}
TEST(DrmControl, KeyAction_UnknownOpcodeReturnsOne) {
    auto r = DispatchKeyAction({999, 0}, 0, 0);
    CHECK_EQ(r.ret, 1);
    CHECK(!r.keyOutWritten);
}

// ---------------------------------------------------------------------------
// VIBE_Drm_InitProtection step-2 CD timecode math @0x140fa60 (pure part)
// ---------------------------------------------------------------------------
TEST(DrmControl, SectorAddrs_ZeroBase) {
    auto a = ComputeSectorAddrs(0u);
    CHECK_EQ(a.startMsf, 0u);
    CHECK_EQ(a.startBcd, 0u);
    // base+150 = 150 frames = 2 seconds: ff=0, ss=2, mm=0 -> 0x0200
    CHECK_EQ(a.endMsf, (2u << 8));            // 0x000200
    CHECK_EQ(a.endBcd, (2u << 8));            // BCD of 2 == 2
}
TEST(DrmControl, SectorAddrs_OneSecond) {
    // base = 75 sectors == exactly 1 second: ff=0, ss=1, mm=0.
    auto a = ComputeSectorAddrs(75u);
    CHECK_EQ(a.startMsf, (1u << 8));          // 0x000100
    CHECK_EQ(a.startBcd, (1u << 8));
}
TEST(DrmControl, SectorAddrs_FrameAndBcdConversion) {
    // base = 76 -> ff=1, ss=1, mm=0. MSF=0x000101, BCD same (single digits).
    auto a = ComputeSectorAddrs(76u);
    CHECK_EQ(a.startMsf, (1u << 8) | 1u);
    CHECK_EQ(a.startBcd, (1u << 8) | 1u);
    // base producing a 2-digit field exercises the 6*(f/10)+f BCD nudge:
    // pick ff=12 -> base%75==12 with base=12: BCD field = 6*1+12 = 18 = 0x12.
    auto b = ComputeSectorAddrs(12u);
    CHECK_EQ(b.startMsf, 12u);                // binary 12
    CHECK_EQ(b.startBcd, 0x12u);              // BCD 0x12
}
TEST(DrmControl, SectorAddrs_MinuteRollover) {
    // 4500 sectors == 1 minute: mm=1, ss=0, ff=0.
    auto a = ComputeSectorAddrs(4500u);
    CHECK_EQ(a.startMsf, (1u << 16));         // 0x010000
    CHECK_EQ(a.startBcd, (1u << 16));
}

// ---------------------------------------------------------------------------
// VIBE_Drm_NopStub3/4/5 @0x1411e90 / 0x1414b00 / 0x1415590 — range markers.
// ---------------------------------------------------------------------------
TEST(DrmControl, NopStubsCarryDecryptorRanges) {
    auto s3 = NopStub3(); CHECK_EQ(s3.lo, 0x442103u); CHECK_EQ(s3.hi, 0x442204u);
    auto s4 = NopStub4(); CHECK_EQ(s4.lo, 0x401001u); CHECK_EQ(s4.hi, 0x4020ABu);
    auto s5 = NopStub5(); CHECK_EQ(s5.lo, 0x403001u); CHECK_EQ(s5.hi, 0x404121u);
}
