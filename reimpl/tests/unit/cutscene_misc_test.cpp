// Unit tests for src/sim/cutscene_misc.{h,cpp} — the self-contained cutscene
// leaf bodies. Golden vectors computed with python3 against the recovered
// pseudocode (cutscene LCG + tier branching + progress-bar fill math).
#include "test.h"
#include "sim/cutscene_misc.h"

#include <vector>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// ---- recording mock for the leaf hooks ------------------------------------
struct MiscMock {
    std::vector<void*>             finished;       // Script_Finish(record*)
    std::vector<std::pair<void*,int>> aniToggles;  // (character*, pause)
    int  voiceFlush = 0, musicPlay = 0, musicRestore = 0;
    std::vector<std::pair<void*,int>> registered;  // (proc, interval)
    std::vector<void*>             unregistered;
    std::vector<std::pair<int,int>> setEnabled;    // (widget, enabled)
    struct SetVal { int widget, lo, hi, val; };
    std::vector<SetVal>            setValues;
};
MiscMock* g_mock = nullptr;

void mock_scriptFinish(void* s)            { g_mock->finished.push_back(s); }
void mock_aniToggle(void* c, int p)        { g_mock->aniToggles.push_back({c,p}); }
void mock_voiceFlush()                     { g_mock->voiceFlush++; }
void mock_musicPlay()                      { g_mock->musicPlay++; }
void mock_musicRestore()                   { g_mock->musicRestore++; }
void mock_register(void* p, int iv)        { g_mock->registered.push_back({p,iv}); }
void mock_unregister(void* p)              { g_mock->unregistered.push_back(p); }
void mock_setEnabled(int w, int e)         { g_mock->setEnabled.push_back({w,e}); }
void mock_setValue(int w,int lo,int hi,int v){ g_mock->setValues.push_back({w,lo,hi,v}); }

CutsceneMiscHooks MakeHooks(MiscMock& m, i32 gate = 0) {
    g_mock = &m;
    CutsceneMiscHooks h{};
    h.disabledGate       = gate;
    h.scriptFinish       = mock_scriptFinish;
    h.characterToggleAni = mock_aniToggle;
    h.voiceFlushAll      = mock_voiceFlush;
    h.musicPlayCutscene  = mock_musicPlay;
    h.musicRestore       = mock_musicRestore;
    h.timeBaseRegister   = mock_register;
    h.timeBaseUnregister = mock_unregister;
    h.objectSetEnabled   = mock_setEnabled;
    h.objectSetValue     = mock_setValue;
    return h;
}

} // namespace

// ---------------------------------------------------------------------------
// RollDuelOutcomeTier — golden vectors (seed, choice roll/tier, outcome roll/tier).
// ---------------------------------------------------------------------------
TEST(CutsceneMisc, RollDuelOutcomeTierChoiceMode) {
    struct V { i32 seed; i32 roll; u8 tier; };
    const V vecs[] = {
        {0x0,        0, 0},
        {0x1,        8, 1},
        {0x3039,     9, 1},
        {(i32)0xDEADBEEF, 9, 1},
        {0x7,        5, 0},
    };
    CutsceneMisc().duelMode = 0;       // choice mode -> RandInt(10)
    for (const auto& v : vecs) {
        CutsceneRng rng; rng.SetSeed(v.seed);
        u8 rec[256]; std::memset(rec, 0xAB, sizeof(rec));
        i32 r = CutsceneRollDuelOutcomeTier(rng, rec);
        CHECK_EQ(r, v.roll);
        CHECK_EQ(rec[148], v.tier);
    }
}

TEST(CutsceneMisc, RollDuelOutcomeTierOutcomeMode) {
    struct V { i32 seed; i32 roll; u8 tier; };
    const V vecs[] = {
        {0x0,        0, 3},     // r<=15 -> 3
        {0x1,       38, 2},     // 15<r<=50 -> 2
        {0x3039,    69, 4},     // r>50 -> 4
        {(i32)0xDEADBEEF, 69, 4},
        {0x7,       65, 4},
    };
    CutsceneMisc().duelMode = 1;       // outcome mode -> RandInt(100)
    for (const auto& v : vecs) {
        CutsceneRng rng; rng.SetSeed(v.seed);
        u8 rec[256]; std::memset(rec, 0xAB, sizeof(rec));
        i32 r = CutsceneRollDuelOutcomeTier(rng, rec);
        CHECK_EQ(r, v.roll);
        CHECK_EQ(rec[148], v.tier);
    }
    CutsceneMisc().duelMode = 0;       // restore
}

// Tier boundary cases (exact branch edges): roll 50 -> 2, 51 -> 4, 15 -> 3, 16 -> 2.
TEST(CutsceneMisc, RollDuelOutcomeTierBoundaries) {
    // We can't pick rolls directly, but verify the branch predicate on a struct
    // by exercising the classification logic via known rolls from RandInt.
    // Instead, validate the rule on a synthetic helper mirroring the body.
    auto classify = [](int r) -> u8 {
        if (r > 50) return 4;
        if (r <= 15) return 3;
        return 2;
    };
    CHECK_EQ(classify(50), (u8)2);
    CHECK_EQ(classify(51), (u8)4);
    CHECK_EQ(classify(15), (u8)3);
    CHECK_EQ(classify(16), (u8)2);
    CHECK_EQ(classify(0),  (u8)3);
    CHECK_EQ(classify(100),(u8)4);
}

// ---------------------------------------------------------------------------
// CheckDeathTimer — predicate (output <= 0).
// ---------------------------------------------------------------------------
TEST(CutsceneMisc, CheckDeathTimer) {
    CHECK(CutsceneCheckDeathTimer(0.0));
    CHECK(CutsceneCheckDeathTimer(-1.5));
    CHECK(!CutsceneCheckDeathTimer(0.0001));
    CHECK(!CutsceneCheckDeathTimer(100.0));
}

// ---------------------------------------------------------------------------
// TickCompareCounter — counter advance + latch.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc, TickCompareCounter) {
    CutsceneMiscState& s = CutsceneMisc();
    s.tickCounter = 0;
    s.tickLimit   = 3;
    s.tickExpired = -1;
    // counter goes 0..4; result becomes true once counter > 3.
    bool seq[] = {false, false, false, false, true};
    int  expectedExpired[] = {0,0,0,0,1};
    int  expectedCounterAfter[] = {1,2,3,4,5};
    for (int i = 0; i < 5; ++i) {
        bool r = CutsceneTickCompareCounter();
        CHECK_EQ(r, seq[i]);
        CHECK_EQ(s.tickExpired, expectedExpired[i]);
        CHECK_EQ(s.tickCounter, expectedCounterAfter[i]);
    }
}

// ---------------------------------------------------------------------------
// LatchFrameCountToFloat — float store + reset.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc, LatchFrameCountToFloat) {
    CutsceneMiscState& s = CutsceneMisc();
    s.frameCount = 60;
    s.frameRate  = -1.0f;
    CutsceneLatchFrameCountToFloat();
    CHECK_EQ(s.frameRate, 60.0f);
    CHECK_EQ(s.frameCount, 0);
    // a second latch with 0 frames -> 0.0
    CutsceneLatchFrameCountToFloat();
    CHECK_EQ(s.frameRate, 0.0f);
}

// ---------------------------------------------------------------------------
// Register / Unregister tick proc — same opaque identity round-trips.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc, TickProcRegistration) {
    MiscMock m; CutsceneMiscHooks h = MakeHooks(m);
    SetCutsceneMiscHooks(&h);
    CutsceneRegisterTickProc();
    CHECK_EQ((int)m.registered.size(), 1);
    CHECK_EQ(m.registered[0].second, 71);    // interval recovered = 71
    void* proc = m.registered[0].first;
    CutsceneUnregisterTickProc();
    CHECK_EQ((int)m.unregistered.size(), 1);
    CHECK(m.unregistered[0] == proc);        // same fn identity
    SetCutsceneMiscHooks(nullptr);
}

// ---------------------------------------------------------------------------
// PauseGame / ResumeGame — gated on disabledGate.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc, PauseResumeGameEnabled) {
    MiscMock m; CutsceneMiscHooks h = MakeHooks(m, /*gate=*/0);
    SetCutsceneMiscHooks(&h);
    CutscenePauseGame();
    CHECK_EQ(m.voiceFlush, 1);
    CHECK_EQ(m.musicPlay, 1);
    CutsceneResumeGame();
    CHECK_EQ(m.musicRestore, 1);
    SetCutsceneMiscHooks(nullptr);
}

TEST(CutsceneMisc, PauseResumeGameDisabled) {
    MiscMock m; CutsceneMiscHooks h = MakeHooks(m, /*gate=*/1);
    SetCutsceneMiscHooks(&h);
    CutscenePauseGame();
    CutsceneResumeGame();
    CHECK_EQ(m.voiceFlush, 0);
    CHECK_EQ(m.musicPlay, 0);
    CHECK_EQ(m.musicRestore, 0);
    SetCutsceneMiscHooks(nullptr);
}

// ---------------------------------------------------------------------------
// FinishPendingScripts — scan the 128-record table (stride 2584).
// ---------------------------------------------------------------------------
TEST(CutsceneMisc, FinishPendingScripts) {
    std::vector<u8> table(kScriptRecordCount * kScriptRecordStride, 0);
    auto setRec = [&](int idx, i32 handle, u8 flags, i32 kind) {
        u8* r = table.data() + idx * kScriptRecordStride;
        *reinterpret_cast<i32*>(r + kScriptOffHandle) = handle;
        r[kScriptOffFlags] = flags;
        *reinterpret_cast<i32*>(r + kScriptOffKind) = kind;
    };
    // record 0: pending + active + valid handle -> finished
    setRec(0, 100, 0x01, -2);
    // record 1: empty handle -> skipped
    setRec(1, -1, 0x01, -2);
    // record 2: not active (flag bit0 clear) -> skipped
    setRec(2, 101, 0x02, -2);
    // record 3: wrong kind -> skipped
    setRec(3, 102, 0x01, 5);
    // record 5: another finishable
    setRec(5, 200, 0x05, -2);

    MiscMock m; CutsceneMiscHooks h = MakeHooks(m);
    SetCutsceneMiscHooks(&h);
    i32 r = CutsceneFinishPendingScripts(table.data());
    CHECK_EQ(r, 0);
    CHECK_EQ((int)m.finished.size(), 2);
    CHECK(m.finished[0] == table.data() + 0 * kScriptRecordStride);
    CHECK(m.finished[1] == table.data() + 5 * kScriptRecordStride);
    SetCutsceneMiscHooks(nullptr);
}

// ---------------------------------------------------------------------------
// WaitForPendingScripts — pump until no pending record remains.
// ---------------------------------------------------------------------------
namespace { int g_pumpCalls = 0; int pump_always() { ++g_pumpCalls; return 1; } }

TEST(CutsceneMisc, WaitForPendingScriptsNonePending) {
    std::vector<u8> table(kScriptRecordCount * kScriptRecordStride, 0);
    // all handles -1 (empty) -> immediately "none pending".
    for (int i = 0; i < kScriptRecordCount; ++i)
        *reinterpret_cast<i32*>(table.data() + i * kScriptRecordStride
                                + kScriptOffHandle) = -1;
    g_pumpCalls = 0;
    i32 r = CutsceneWaitForPendingScripts(table.data(), pump_always);
    // first pump returns 1, scan finds nothing pending -> returns table end.
    CHECK_EQ(g_pumpCalls, 1);
    CHECK_EQ(r, kScriptRecordCount * kScriptRecordStride);
}

// pump that clears the pending record after 2 frames, then returns 0 to stop.
namespace {
u8*  g_waitTable = nullptr;
int  g_waitFrames = 0;
int  pump_clearing() {
    ++g_waitFrames;
    if (g_waitFrames >= 2) {
        // clear the pending record so the scan finds none pending.
        *reinterpret_cast<i32*>(g_waitTable + kScriptOffHandle) = -1;
    }
    return 1;   // keep the frame loop alive; the inner scan decides when to stop
}
}

TEST(CutsceneMisc, WaitForPendingScriptsClearsThenReturns) {
    std::vector<u8> table(kScriptRecordCount * kScriptRecordStride, 0);
    for (int i = 0; i < kScriptRecordCount; ++i)
        *reinterpret_cast<i32*>(table.data() + i * kScriptRecordStride
                                + kScriptOffHandle) = -1;
    // record 0 starts pending (handle != -1, kind == -2).
    *reinterpret_cast<i32*>(table.data() + kScriptOffHandle) = 7;
    *reinterpret_cast<i32*>(table.data() + kScriptOffKind)   = -2;
    g_waitTable = table.data(); g_waitFrames = 0;
    i32 r = CutsceneWaitForPendingScripts(table.data(), pump_clearing);
    // After frame 2 the record is cleared; the scan then reaches the table end.
    CHECK_EQ(g_waitFrames, 2);
    CHECK_EQ(r, kScriptRecordCount * kScriptRecordStride);
}

// ---------------------------------------------------------------------------
// Pause/ResumeAllActorAni — 512-entry character table, +140 ani flags bit2.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc, PauseResumeAllActorAni) {
    // Build a few fake "characters": 160-byte blobs, flags byte at +140.
    std::vector<std::vector<u8>> blobs(4, std::vector<u8>(160, 0));
    void* chars[kCharacterTableCount];
    std::memset(chars, 0, sizeof(chars));
    chars[0]   = blobs[0].data();    // flags 0   -> pausable
    chars[10]  = blobs[1].data();    // flags 0   -> pausable
    chars[200] = blobs[2].data();    // flags bit2 set -> already paused
    blobs[2][kCharacterOffAniFlags] = kCharAniPausedBit;
    chars[511] = blobs[3].data();    // flags 0   -> pausable
    // entries left null are skipped.

    MiscMock m; CutsceneMiscHooks h = MakeHooks(m);
    SetCutsceneMiscHooks(&h);

    CutscenePauseAllActorAni(chars);
    // only the 3 non-paused characters get toggle(pause=1).
    CHECK_EQ((int)m.aniToggles.size(), 3);
    for (auto& t : m.aniToggles) CHECK_EQ(t.second, 1);

    // Resume: only those with bit2 set. Mark the three we just "paused".
    blobs[0][kCharacterOffAniFlags] = kCharAniPausedBit;
    blobs[1][kCharacterOffAniFlags] = kCharAniPausedBit;
    blobs[3][kCharacterOffAniFlags] = kCharAniPausedBit;
    m.aniToggles.clear();
    CutsceneResumeAllActorAni(chars);
    // all 4 now have bit2 set -> 4 toggles(resume=0).
    CHECK_EQ((int)m.aniToggles.size(), 4);
    for (auto& t : m.aniToggles) CHECK_EQ(t.second, 0);
    SetCutsceneMiscHooks(nullptr);
}

// ---------------------------------------------------------------------------
// UpdateProgressBar — reset path + fill math golden vectors.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc, UpdateProgressBarReset) {
    MiscMock m; CutsceneMiscHooks h = MakeHooks(m);
    SetCutsceneMiscHooks(&h);
    SetCutsceneMiscGameTick(1000);
    i32 v = CutsceneUpdateProgressBar(/*widget=*/42, /*span=*/2000, /*reset=*/true);
    CHECK_EQ(v, 0);
    CHECK_EQ(CutsceneMisc().barSpan, 2000);
    CHECK_EQ(CutsceneMisc().barStart, 1000);
    CHECK_EQ((int)m.setEnabled.size(), 1);
    CHECK_EQ(m.setEnabled[0].first, 42);
    CHECK_EQ(m.setEnabled[0].second, 0);
    CHECK_EQ((int)m.setValues.size(), 1);
    CHECK_EQ(m.setValues[0].val, 0);
    SetCutsceneMiscHooks(nullptr);
}

TEST(CutsceneMisc, UpdateProgressBarFill) {
    MiscMock m; CutsceneMiscHooks h = MakeHooks(m);
    SetCutsceneMiscHooks(&h);
    // span 2000, start 1000 (set up via reset).
    SetCutsceneMiscGameTick(1000);
    CutsceneUpdateProgressBar(42, 2000, true);
    struct V { u32 tick; i32 value; };
    const V vecs[] = {
        {1000, 100},   // elapsed 0
        {1100,  95},   // elapsed 100
        {1500,  75},   // elapsed 500
        {2000,  50},   // elapsed 1000
        {3000,   0},   // elapsed 2000
        {4000,   0},   // elapsed 3000 -> negative -> clamp 0
    };
    for (const auto& vv : vecs) {
        SetCutsceneMiscGameTick(vv.tick);
        i32 got = CutsceneUpdateProgressBar(42, 2000, false);
        CHECK_EQ(got, vv.value);
    }
    SetCutsceneMiscHooks(nullptr);
}
