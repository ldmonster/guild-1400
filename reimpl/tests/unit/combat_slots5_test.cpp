#include "test.h"

#include "sim/combat_slots5.h"

#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A deterministic stand-in RNG for the fallback / intro paths.
int g_rngReturn = 0;
int RngStub(int /*n*/) { return g_rngReturn; }

CombatSlots5Hooks WithRng(int v) {
    g_rngReturn = v;
    CombatSlots5Hooks h{};   // inert
    h.randomModulo = &RngStub;
    return h;
}

EnemyCandidate MkNode(int idx, int dt, int lvl, int cap, bool flag, float x, float y, float z) {
    EnemyCandidate c{};
    c.defType = static_cast<u8>(dt);
    c.level = static_cast<u8>(lvl);
    c.cap = static_cast<u16>(cap);
    c.flagBit0 = flag;
    c.hasNode = true;
    c.worldPos[0] = x; c.worldPos[1] = y; c.worldPos[2] = z;
    c.index = idx;
    return c;
}
EnemyCandidate MkNoNode(int idx, int dt, int lvl, int cap) {
    EnemyCandidate c{};
    c.defType = static_cast<u8>(dt);
    c.level = static_cast<u8>(lvl);
    c.cap = static_cast<u16>(cap);
    c.hasNode = false;
    c.index = idx;
    return c;
}

} // namespace

// --------------------------------------------------------------------------
// FindUnitById  (0x486430)
// --------------------------------------------------------------------------
TEST(CombatSlots5_FindUnitById, ReturnsMatchingIndex) {
    std::vector<UnitSlotView> t(8);
    for (int i = 0; i < 8; ++i) { t[i].entityId = static_cast<i16>(i + 1); t[i].aiId = 1000 + i; }
    CHECK_EQ(FindUnitById(t, 1000), 0);
    CHECK_EQ(FindUnitById(t, 1007), 7);
    CHECK_EQ(FindUnitById(t, 1003), 3);
}

TEST(CombatSlots5_FindUnitById, EmptySlotSkippedEvenIfIdMatches) {
    std::vector<UnitSlotView> t(4);
    t[0].entityId = -1;   t[0].aiId = 42;   // empty -> must NOT match
    t[1].entityId = 9;    t[1].aiId = 42;   // real match
    t[2].entityId = 10;   t[2].aiId = 7;
    t[3].entityId = 11;   t[3].aiId = 8;
    CHECK_EQ(FindUnitById(t, 42), 1);
}

TEST(CombatSlots5_FindUnitById, NoMatchReturnsMinusOne) {
    std::vector<UnitSlotView> t(4);
    for (int i = 0; i < 4; ++i) { t[i].entityId = static_cast<i16>(i + 1); t[i].aiId = i; }
    CHECK_EQ(FindUnitById(t, 999), -1);
}

TEST(CombatSlots5_FindUnitById, ScanCappedAt32Slots) {
    std::vector<UnitSlotView> t(40);
    for (int i = 0; i < 40; ++i) { t[i].entityId = static_cast<i16>(i + 1); t[i].aiId = i; }
    // index 35 is beyond the 32-slot window -> not found.
    CHECK_EQ(FindUnitById(t, 35), -1);
    CHECK_EQ(FindUnitById(t, 31), 31);
}

// --------------------------------------------------------------------------
// FindNearestEnemyTarget  (0x57e50c)
// --------------------------------------------------------------------------
TEST(CombatSlots5_FindNearest, PicksNearestByNodeDistance) {
    float self[3] = {0.0f, 0.0f, 0.0f};
    std::vector<EnemyCandidate> c = {
        MkNode(100, 5, 3, 10, false, 10.0f, 0.0f, 0.0f),  // dist 10
        MkNode(101, 5, 3, 10, false, 3.0f, 4.0f, 0.0f),   // dist 5
        MkNode(102, 5, 3, 10, false, 1.0f, 2.0f, 2.0f),   // dist 3 -> winner
        MkNoNode(103, 72, 3, 10),                          // gated: defType>=72
        MkNode(104, 5, 10, 10, false, 0.2f, 0.0f, 0.0f),  // gated: level !< cap
        MkNode(105, 5, 3, 10, true, 0.3f, 0.0f, 0.0f),    // gated: flagBit0
    };
    CombatSlots5Hooks h = WithRng(0);
    SetCombatSlots5Hooks(&h);
    CHECK_EQ(FindNearestEnemyTarget(c, true, self, {}), 102);
    SetCombatSlots5Hooks(nullptr);
}

TEST(CombatSlots5_FindNearest, RatioBranchPicksMostHurt) {
    // self has no node -> every candidate routes through the (level/cap) ratio branch.
    float self[3] = {0.0f, 0.0f, 0.0f};
    std::vector<EnemyCandidate> c = {
        MkNoNode(200, 5, 2, 10),   // ratio .2
        MkNoNode(201, 5, 8, 10),   // ratio .8 -> winner
        MkNoNode(202, 5, 5, 10),   // ratio .5
    };
    CHECK_EQ(FindNearestEnemyTarget(c, false, self, {}), 201);
}

TEST(CombatSlots5_FindNearest, FallbackRandomWhenNonePrimary) {
    float self[3] = {0.0f, 0.0f, 0.0f};
    std::vector<EnemyCandidate> none;     // no primary candidate
    std::vector<int> fb = {7, 8, 9, 10};
    CombatSlots5Hooks h = WithRng(2);     // pick index 2 -> value 9
    SetCombatSlots5Hooks(&h);
    CHECK_EQ(FindNearestEnemyTarget(none, true, self, fb), 9);
    SetCombatSlots5Hooks(nullptr);
}

TEST(CombatSlots5_FindNearest, FallbackEmptyReturnsMinusOne) {
    float self[3] = {0.0f, 0.0f, 0.0f};
    std::vector<EnemyCandidate> none;
    CHECK_EQ(FindNearestEnemyTarget(none, true, self, {}), -1);
}

// --- Wave-12 hardening: fallback roll boundary clamps -----------------------

// A roll returned >= count (or negative) must be clamped to the valid [0,count)
// range before indexing fallbackIndices — no out-of-bounds read.
TEST(CombatSlots5_FindNearest, FallbackRollClampedHighAndLow) {
    float self[3] = {0.0f, 0.0f, 0.0f};
    std::vector<EnemyCandidate> none;
    std::vector<int> fb = {40, 41, 42};      // count 3, valid indices 0..2

    // Roll too high (99) -> clamp to count-1 == index 2 -> value 42.
    CombatSlots5Hooks hi = WithRng(99);
    SetCombatSlots5Hooks(&hi);
    CHECK_EQ(FindNearestEnemyTarget(none, true, self, fb), 42);

    // Roll negative (-5) -> clamp to 0 -> value 40.
    CombatSlots5Hooks lo = WithRng(-5);
    SetCombatSlots5Hooks(&lo);
    CHECK_EQ(FindNearestEnemyTarget(none, true, self, fb), 40);
    SetCombatSlots5Hooks(nullptr);
}

// Single-element fallback: any roll resolves to the one entry.
TEST(CombatSlots5_FindNearest, FallbackSingleElement) {
    float self[3] = {0.0f, 0.0f, 0.0f};
    std::vector<EnemyCandidate> none;
    std::vector<int> fb = {77};
    CombatSlots5Hooks h = WithRng(123);      // clamped to index 0
    SetCombatSlots5Hooks(&h);
    CHECK_EQ(FindNearestEnemyTarget(none, true, self, fb), 77);
    SetCombatSlots5Hooks(nullptr);
}

// --------------------------------------------------------------------------
// SelectIntroTrack / PlayIntroCutscene  (0x48ac70 / 0x48ac0c)
// --------------------------------------------------------------------------
TEST(CombatSlots5_IntroTrack, BucketLadder) {
    CHECK(SelectIntroTrack(0)  == IntroTrack::kKrieg);
    CHECK(SelectIntroTrack(19) == IntroTrack::kKrieg);
    CHECK(SelectIntroTrack(20) == IntroTrack::kRuesteEuch);
    CHECK(SelectIntroTrack(39) == IntroTrack::kRuesteEuch);
    CHECK(SelectIntroTrack(40) == IntroTrack::kFlucht);
    CHECK(SelectIntroTrack(59) == IntroTrack::kFlucht);
    CHECK(SelectIntroTrack(60) == IntroTrack::kSeuche);
    CHECK(SelectIntroTrack(79) == IntroTrack::kSeuche);
    CHECK(SelectIntroTrack(80) == IntroTrack::kAmKuehlen);
    CHECK(SelectIntroTrack(99) == IntroTrack::kAmKuehlen);
}

namespace {
IntroTrack g_played = IntroTrack::kSeuche;
bool g_didPlay = false;
void PlayTrackRec(IntroTrack t) { g_played = t; g_didPlay = true; }
bool g_loadedScript = false;
void* ScriptLoadRec(const char*) { g_loadedScript = true; return reinterpret_cast<void*>(1); }
bool g_ranMain = false, g_ranWait = false;
void RunMainRec(void*) { g_ranMain = true; }
void RunWaitRec(void*) { g_ranWait = true; }
}

TEST(CombatSlots5_Intro, MusicEnabledRollsAndPlaysBucket) {
    g_didPlay = false; g_loadedScript = false; g_ranMain = false; g_ranWait = false;
    CombatSlots5Hooks h = WithRng(85);   // -> kAmKuehlen
    h.playMusicTrack = &PlayTrackRec;
    h.scriptLoad = &ScriptLoadRec;
    h.scriptRunMain = &RunMainRec;
    h.scriptRunWaitLoop = &RunWaitRec;
    SetCombatSlots5Hooks(&h);
    int roll = PlayIntroCutscene(true, nullptr);
    CHECK_EQ(roll, 85);
    CHECK(g_didPlay);
    CHECK(g_played == IntroTrack::kAmKuehlen);
    CHECK(g_loadedScript);
    CHECK(g_ranMain);
    CHECK(g_ranWait);
    SetCombatSlots5Hooks(nullptr);
}

TEST(CombatSlots5_Intro, MusicDisabledSkipsRoll) {
    g_didPlay = false;
    CombatSlots5Hooks h = WithRng(5);
    h.playMusicTrack = &PlayTrackRec;
    SetCombatSlots5Hooks(&h);
    int roll = PlayIntroCutscene(false, nullptr);
    CHECK_EQ(roll, -1);
    CHECK(!g_didPlay);
    SetCombatSlots5Hooks(nullptr);
}

// --------------------------------------------------------------------------
// PlayOutroCutscene  (0x48ace8)
// --------------------------------------------------------------------------
namespace {
int g_stoppedTrack = 0;
void StopAmbientRec(int id) { g_stoppedTrack = id; }
}
TEST(CombatSlots5_Outro, StopsAmbientAndRunsScript) {
    g_stoppedTrack = 0; g_loadedScript = false; g_ranMain = false; g_ranWait = false;
    CombatSlots5Hooks h{};
    h.stopAmbientTrack = &StopAmbientRec;
    h.scriptLoad = &ScriptLoadRec;
    h.scriptRunMain = &RunMainRec;
    h.scriptRunWaitLoop = &RunWaitRec;
    SetCombatSlots5Hooks(&h);
    bool ran = PlayOutroCutscene(77, nullptr);
    CHECK(ran);
    CHECK_EQ(g_stoppedTrack, 77);
    CHECK(g_ranMain);
    CHECK(g_ranWait);
    SetCombatSlots5Hooks(nullptr);
}

TEST(CombatSlots5_Outro, NoAmbientWhenZeroAndNoScriptReturnsFalse) {
    g_stoppedTrack = 0;
    CombatSlots5Hooks h{};
    h.stopAmbientTrack = &StopAmbientRec;
    // no scriptLoad hook -> load returns null -> false
    SetCombatSlots5Hooks(&h);
    CHECK(!PlayOutroCutscene(0, nullptr));
    CHECK_EQ(g_stoppedTrack, 0);    // ambient id 0 -> not stopped
    SetCombatSlots5Hooks(nullptr);
}

// --------------------------------------------------------------------------
// StartCutscene  (0x4876d8)
// --------------------------------------------------------------------------
namespace {
int g_destroyed = -999;
void FormDestroyRec(int id) { g_destroyed = id; }
bool g_boundsOn = true;
bool ScreenBoundsRec(const void*, int* xy) { xy[0] = 100; xy[1] = 50; return g_boundsOn; }
int FinalizeRec(int x, int y, const char*) { return x * 1000 + y; } // encode args
}
TEST(CombatSlots5_StartCutscene, OnScreenDestroysOldAndFinalizes) {
    g_destroyed = -999; g_boundsOn = true;
    CombatSlots5Hooks h{};
    h.formDestroy = &FormDestroyRec;
    h.objectScreenBounds = &ScreenBoundsRec;
    h.gameTickFinalize = &FinalizeRec;
    SetCombatSlots5Hooks(&h);
    int out = -123;
    int form = StartCutscene(42, nullptr, &out);
    CHECK_EQ(g_destroyed, 42);             // old form destroyed
    CHECK_EQ(form, (100 + 48) * 1000 + (50 - 30));   // x+48, y-30
    CHECK_EQ(out, form);
    SetCombatSlots5Hooks(nullptr);
}

TEST(CombatSlots5_StartCutscene, OffScreenReturnsMinusOne) {
    g_boundsOn = false;
    CombatSlots5Hooks h{};
    h.objectScreenBounds = &ScreenBoundsRec;
    h.gameTickFinalize = &FinalizeRec;
    SetCombatSlots5Hooks(&h);
    int out = 5;
    CHECK_EQ(StartCutscene(-1, nullptr, &out), -1);
    CHECK_EQ(out, -1);
    SetCombatSlots5Hooks(nullptr);
}

// --------------------------------------------------------------------------
// RefreshHealthBars  (0x4872a0)
// --------------------------------------------------------------------------
namespace {
int g_removeCount = 0;
void RemoveRec(int) { ++g_removeCount; }
int g_createCount = 0;
int CreateRec(int, void*) { ++g_createCount; return 1; }
}
TEST(CombatSlots5_RefreshHealthBars, RemovesActiveThenRecreatesShown) {
    g_removeCount = 0; g_createCount = 0;
    CombatSlots5Hooks h{};
    h.windowRemoveIfActive = &RemoveRec;
    h.createHealthBarWindow = &CreateRec;
    SetCombatSlots5Hooks(&h);

    std::vector<int> wins = {10, -1, 20, -1, 30};   // 3 active
    std::vector<bool> show = {true, false, true, true, false};  // 3 shown
    std::vector<void*> recs(5, nullptr);
    int created = RefreshHealthBars(0, wins, show, recs);

    CHECK_EQ(g_removeCount, 3);
    CHECK_EQ(created, 3);
    CHECK_EQ(g_createCount, 3);
    for (int w : wins) CHECK_EQ(w, -1);   // all cells reset to -1
    SetCombatSlots5Hooks(nullptr);
}

// --------------------------------------------------------------------------
// DropBombAction / ThrowBombAction  (0x48cdc8 / 0x48ce88)
// --------------------------------------------------------------------------
namespace {
bool g_queued = false;
void Queue22Rec(void*) { g_queued = true; }
bool g_spawnedThrown = false;
int ThrownRec(int, int, int) { g_spawnedThrown = true; return 777; }
}
TEST(CombatSlots5_DropBomb, GatesOnLocalSideAndWeaponClass) {
    g_queued = false;
    CombatSlots5Hooks h{};
    h.queueTargetRequest22 = &Queue22Rec;
    SetCombatSlots5Hooks(&h);

    int target = 1;
    BombActionInput in{};
    in.isLocalSide = true; in.hasActiveTarget = true; in.weaponClass = 2;
    BombActionOutcome o = DropBombAction(in, &target);
    CHECK(o.spawnedBomb);        // unconditional
    CHECK(o.playedVoice);        // unconditional
    CHECK(o.queuedRequest);      // gate satisfied
    CHECK(g_queued);

    // weapon class 0 -> no queue (but still spawns bomb + voice)
    g_queued = false;
    in.weaponClass = 0;
    BombActionOutcome o2 = DropBombAction(in, &target);
    CHECK(o2.spawnedBomb);
    CHECK(!o2.queuedRequest);
    CHECK(!g_queued);

    // not local side -> no queue
    g_queued = false;
    in.weaponClass = 1; in.isLocalSide = false;
    BombActionOutcome o3 = DropBombAction(in, &target);
    CHECK(!o3.queuedRequest);
    SetCombatSlots5Hooks(nullptr);
}

TEST(CombatSlots5_ThrowBomb, AlwaysSpawnsThrownBomb) {
    g_queued = false; g_spawnedThrown = false;
    CombatSlots5Hooks h{};
    h.queueTargetRequest22 = &Queue22Rec;
    h.spawnThrownBomb = &ThrownRec;
    SetCombatSlots5Hooks(&h);

    int target = 1;
    BombActionInput in{};
    in.isLocalSide = false; in.hasActiveTarget = true;   // not local -> no queue
    BombActionOutcome o = ThrowBombAction(in, &target);
    CHECK(!o.queuedRequest);
    CHECK(o.spawnedThrownBomb);          // always spawns
    CHECK_EQ(o.thrownBombResult, 777);
    CHECK(g_spawnedThrown);

    // local side + target -> queue AND spawn
    g_queued = false;
    in.isLocalSide = true;
    BombActionOutcome o2 = ThrowBombAction(in, &target);
    CHECK(o2.queuedRequest);
    CHECK(g_queued);
    CHECK(o2.spawnedThrownBomb);
    SetCombatSlots5Hooks(nullptr);
}

// --------------------------------------------------------------------------
// Scene-graph string-match predicates  (0x48b350 / 0x48b5a4 / 0x48b488)
// --------------------------------------------------------------------------
TEST(CombatSlots5_Callbacks, EscapeMatchesCaseInsensitive) {
    CHECK(EscapeTileMatches("sp_ESCAPE"));
    CHECK(EscapeTileMatches("SP_escape"));   // StrCmpNoCase fold
    CHECK(!EscapeTileMatches("sp_ESCAPEX")); // length differs
    CHECK(!EscapeTileMatches("sp_CONQUER"));
    CHECK(!EscapeTileMatches(nullptr));
}

TEST(CombatSlots5_Callbacks, ConquerMatchesFirstTenBytes) {
    CHECK(ConquerObjectMatches("sp_CONQUER"));
    CHECK(ConquerObjectMatches("sp_CONQUER_42"));   // prefix-only (10 bytes)
    CHECK(!ConquerObjectMatches("sp_CONQUE"));      // too short
    CHECK(!ConquerObjectMatches("sp_conquer"));     // case-sensitive (StrncmpN)
}

TEST(CombatSlots5_Callbacks, WareMatchesPrefixAndDistinctId) {
    CHECK(WareObjectMatches("WARE_07", 7, 3));     // prefix ok, ids differ
    CHECK(!WareObjectMatches("WARE_07", 7, 7));    // same ware id -> skip
    CHECK(!WareObjectMatches("ITEM_07", 7, 3));    // wrong prefix
    CHECK(!WareObjectMatches(nullptr, 1, 2));
}

// --------------------------------------------------------------------------
// RegisterFlagCallback  (0x4897bc)
// --------------------------------------------------------------------------
namespace {
int g_walkInvoked = 0;
int FlagCb(const char*, void*) { return 0; }
void WalkRec(int (*)(const char*, void*), void*) { ++g_walkInvoked; }
}
TEST(CombatSlots5_RegisterFlag, InertDefaultNoWalk) {
    SetCombatSlots5Hooks(nullptr);
    CHECK_EQ(RegisterFlagCallback(&FlagCb, nullptr), 0);
}
TEST(CombatSlots5_RegisterFlag, InstalledHookWalks) {
    g_walkInvoked = 0;
    CombatSlots5Hooks h{};
    h.sceneWalkAndInvoke = &WalkRec;
    SetCombatSlots5Hooks(&h);
    RegisterFlagCallback(&FlagCb, nullptr);
    CHECK_EQ(g_walkInvoked, 1);
    SetCombatSlots5Hooks(nullptr);
}
