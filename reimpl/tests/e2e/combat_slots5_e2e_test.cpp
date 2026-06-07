#include "test.h"

// End-to-end: drive a single combat "encounter" frame across combat_slots5's
// functions, wiring the leaves through one recording hooks struct (the inert
// defaults are the no-op live wiring). The flow:
//   1. RefreshHealthBars rebuilds the per-unit HP windows.
//   2. FindUnitById resolves the local unit; FindNearestEnemyTarget picks its
//      victim (nearest by node distance).
//   3. The unit is a thrown-weapon user on the local side -> DropBombAction
//      queues a target request-22 against that victim.
//   4. The winning side plays the outro cutscene; the music selection for the
//      *intro* of the next encounter is rolled from a deterministic RNG.
#include "sim/combat_slots5.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct EncounterRec {
    int  hpWindowsCreated = 0;
    bool bombQueued = false;
    bool outroRan = false;
    IntroTrack nextIntro = IntroTrack::kSeuche;
    int  introRoll = -1;
} g_enc;

int CreateHpWin(int, void*) { ++g_enc.hpWindowsCreated; return 1; }
void Queue22(void* t) { g_enc.bombQueued = (t != nullptr); }
void* Load(const char*) { return reinterpret_cast<void*>(1); }
void RunMain(void*) {}
void RunWait(void*) {}
void StopAmbient(int) {}
void PlayTrack(IntroTrack t) { g_enc.nextIntro = t; }

// Deterministic RNG: returns a queued sequence so the flow is reproducible.
std::vector<int> g_rolls;
std::size_t g_rollIdx = 0;
int Rng(int n) {
    if (g_rollIdx >= g_rolls.size()) return 0;
    int v = g_rolls[g_rollIdx++];
    return (n > 0) ? (v % n) : 0;
}

CombatSlots5Hooks MakeHooks() {
    CombatSlots5Hooks h{};
    h.createHealthBarWindow = &CreateHpWin;
    h.queueTargetRequest22  = &Queue22;
    h.scriptLoad            = &Load;
    h.scriptRunMain         = &RunMain;
    h.scriptRunWaitLoop     = &RunWait;
    h.stopAmbientTrack      = &StopAmbient;
    h.playMusicTrack        = &PlayTrack;
    h.randomModulo          = &Rng;
    return h;
}

} // namespace

TEST(CombatSlots5E2E, EncounterFrameFlow) {
    g_enc = EncounterRec{};
    g_rolls = {85};        // next-intro roll -> kAmKuehlen
    g_rollIdx = 0;
    CombatSlots5Hooks h = MakeHooks();
    SetCombatSlots5Hooks(&h);

    // --- 1. health bars ----------------------------------------------------
    std::vector<int>   wins = {-1, 5, -1, 7};
    std::vector<bool>  show = {true, true, false, true};   // 3 shown
    std::vector<void*> recs(4, nullptr);
    int created = RefreshHealthBars(0, wins, show, recs);
    CHECK_EQ(created, 3);
    CHECK_EQ(g_enc.hpWindowsCreated, 3);

    // --- 2. resolve actor + victim ----------------------------------------
    std::vector<UnitSlotView> slots(4);
    for (int i = 0; i < 4; ++i) { slots[i].entityId = static_cast<i16>(i + 1); slots[i].aiId = 200 + i; }
    int actor = FindUnitById(slots, 202);
    CHECK_EQ(actor, 2);

    float self[3] = {0.0f, 0.0f, 0.0f};
    std::vector<EnemyCandidate> enemies;
    {
        EnemyCandidate far_{};  far_.defType=5; far_.level=3; far_.cap=10; far_.hasNode=true;
        far_.worldPos[0]=20.0f; far_.index=900; enemies.push_back(far_);
        EnemyCandidate near_{}; near_.defType=5; near_.level=3; near_.cap=10; near_.hasNode=true;
        near_.worldPos[0]=3.0f; near_.worldPos[2]=4.0f; near_.index=901; enemies.push_back(near_);
    }
    int victim = FindNearestEnemyTarget(enemies, true, self, {});
    CHECK_EQ(victim, 901);    // nearest (dist 5 < 20)

    // --- 3. drop-bomb action gate -----------------------------------------
    int victimHandle = victim;     // a stand-in non-null target handle
    BombActionInput in{};
    in.isLocalSide = true; in.hasActiveTarget = true; in.weaponClass = 2;
    BombActionOutcome bomb = DropBombAction(in, &victimHandle);
    CHECK(bomb.spawnedBomb);
    CHECK(bomb.queuedRequest);
    CHECK(g_enc.bombQueued);

    // --- 4. resolve encounter cutscenes -----------------------------------
    g_enc.outroRan = PlayOutroCutscene(55, nullptr);
    CHECK(g_enc.outroRan);

    g_enc.introRoll = PlayIntroCutscene(true, nullptr);
    CHECK_EQ(g_enc.introRoll, 85);
    CHECK(g_enc.nextIntro == IntroTrack::kAmKuehlen);

    SetCombatSlots5Hooks(nullptr);
}
