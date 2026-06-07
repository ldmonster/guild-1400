// tests/unit/wire_npc_actions_test.cpp — UNIT tests for the REAL NPC daily-
// schedule / action-dispatch bridge (play::wire_npc_actions). Proves
// InstallRealNpcActions() swaps the inert NpcDailyHooks for real leaves: a
// synthetic NPC's scheduled action now produces its REAL effect (a turn-bits
// write-back into the live g_persons +456 column + a real command packet built on
// the bridge queue) vs. the inert default's no-op (no persons swept, nothing
// emitted, no record mutated).
#include "test.h"

#include "play/wire_npc_actions.h"
#include "sim/npc_daily.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock
#include "sim/entity.h"      // g_persons, g_objects, ResetEntityArrays
#include "sim/he.h"
#include "sim/types.h"

#include <cstring>

using namespace guild;
using guild::play::InstallRealNpcActions;
using guild::play::UninstallRealNpcActions;
using guild::play::RealNpcActionsInstalled;
using guild::play::SetNpcActionsTargetProvider;
using guild::play::NpcActionsTargetProvider;
using guild::play::NpcActionsQueue;
using guild::play::ResetNpcActionsQueue;
using guild::play::GetNpcActionsTallies;
using guild::play::ResetNpcActionsTallies;

namespace {

// Write a Person column at a raw byte offset (matches the director's reads).
template <typename T>
void PutCol(int i, int off, T v) {
    std::memcpy(reinterpret_cast<u8*>(&sim::g_persons[i]) + off, &v, sizeof(T));
}

// A live production worker in slot 0 whose home building is a production type.
// Person columns: marker(+0)!=-1, activeB(+357)=1, homeBld(+364)=HOME_ID,
// destBld(+388)=DEST_ID, turnBits(+456)=0, id(+4)=PERSON_ID.
constexpr i32 kHomeId   = 4242;
constexpr i32 kDestId   = 99;
constexpr i32 kPersonId = 7;

void SetupOneProductionWorker() {
    sim::ResetEntityArrays();
    std::memset(&sim::g_persons[0], 0, sizeof(sim::Person));
    PutCol<i16>(0, 0x00, 0);          // marker (not the -1 free sentinel)
    PutCol<u8>(0, 357, 1);            // activeB live
    PutCol<i32>(0, 364, kHomeId);     // homeBld
    PutCol<i32>(0, 388, kDestId);     // destBld
    PutCol<u32>(0, 456, 0u);          // turnBits
    PutCol<i32>(0, 4, kPersonId);     // id
    // free every other slot so the sweep finds exactly one worker.
    for (int i = 1; i < sim::kPersonCapacity; ++i)
        PutCol<i16>(i, 0x00, -1);

    // The home building: a PRODUCTION kind (11) so Building_IsProductionKind is
    // true -> the move/string "go to work" branch fires.
    sim::g_objects[0].alive = 11;     // production type byte @+0
    sim::g_objects[0].id    = kHomeId;
}

// Deterministic provider: findTarget returns (universe,obj) that DIFFER from the
// dest door ids, so the "already there" early-out does not fire.
int PvFind(int, i32* u, i32* o) { *u = 100; *o = 200; return 1; }
bool PvDoor(int, i32* a, i32* b) { *a = 1; *b = 1; return true; }

NpcActionsTargetProvider MakeProvider() {
    NpcActionsTargetProvider p{};
    p.findTarget   = PvFind;
    p.destDoorIds  = PvDoor;
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// Inert default: with the bridge NOT installed, the director sees no persons and
// mutates nothing — even with a fully set-up live population.
// ---------------------------------------------------------------------------
TEST(WireNpcActions, InertDefaultIsNoOp) {
    SetupOneProductionWorker();
    sim::SetNpcDailyHooks(nullptr);    // explicit inert default
    NpcActionsTargetProvider p = MakeProvider();
    SetNpcActionsTargetProvider(&p);
    ResetNpcActionsQueue();
    ResetNpcActionsTallies();
    sim::SetNpcClock(sim::GameTime{ /*day*/0, /*hour*/6, /*minute*/0, /*second*/0 });

    sim::HeRecord rec{};
    sim::He_State(&rec) = 0;
    sim::NpcDaily_DailyRoutineStep(&rec);

    // Inert: nothing swept, nothing built, turn-bits untouched.
    CHECK_EQ((int)GetNpcActionsTallies().rowsRead, 0);
    CHECK_EQ((int)NpcActionsQueue().send_count(), 0);
    u32 bits = 0;
    std::memcpy(&bits, reinterpret_cast<u8*>(&sim::g_persons[0]) + 456, sizeof(bits));
    CHECK_EQ((int)bits, 0);
    SetNpcActionsTargetProvider(nullptr);
}

// ---------------------------------------------------------------------------
// Real bridge: InstallRealNpcActions() routes the director at the real leaves.
// The production worker is swept off the live g_persons array, the production
// probe resolves the real building, and the morning work dispatch fires:
//   * real command packets are BUILT on the bridge queue (op77 + chrmove + str47),
//   * the +456 turn-bits column is written back with kDailyDispWork (the real
//     in-line per-person state mutation).
// ---------------------------------------------------------------------------
TEST(WireNpcActions, RealDispatchSweepsLivePersonsAndMutates) {
    SetupOneProductionWorker();
    NpcActionsTargetProvider p = MakeProvider();
    SetNpcActionsTargetProvider(&p);
    ResetNpcActionsQueue();
    ResetNpcActionsTallies();
    sim::SetNpcClock(sim::GameTime{ 0, 6, 0, 0 });   // hour 6 < workStart(8)

    InstallRealNpcActions();
    CHECK(RealNpcActionsInstalled());

    sim::HeRecord rec{};
    sim::He_State(&rec) = 0;
    sim::NpcDaily_DailyRoutineStep(&rec);

    // Live array was actually swept.
    CHECK(GetNpcActionsTallies().rowsRead >= sim::kPersonCapacity);
    // Production probe ran and resolved the real building.
    CHECK(GetNpcActionsTallies().prodProbes >= 1);
    // Real command packets were built: op77 + chrmove + str47 = 3.
    CHECK_EQ((int)GetNpcActionsTallies().commandsBuilt, 3);
    CHECK_EQ((int)NpcActionsQueue().send_count(), 3);
    // The +456 turn-bits column was written back with the dispatch bit.
    u32 bits = 0;
    std::memcpy(&bits, reinterpret_cast<u8*>(&sim::g_persons[0]) + 456, sizeof(bits));
    CHECK((bits & sim::kDailyDispWork) != 0);
    CHECK(GetNpcActionsTallies().turnBitsWrites >= 1);

    UninstallRealNpcActions();
    SetNpcActionsTargetProvider(nullptr);
    CHECK(!RealNpcActionsInstalled());
}

// ---------------------------------------------------------------------------
// Install/uninstall is the inert<->real swap on the shared sim setter, and
// uninstall fully restores the no-op behaviour.
// ---------------------------------------------------------------------------
TEST(WireNpcActions, InstallSwapsHooksAndUninstallRestores) {
    SetupOneProductionWorker();
    NpcActionsTargetProvider p = MakeProvider();
    SetNpcActionsTargetProvider(&p);
    sim::SetNpcClock(sim::GameTime{ 0, 6, 0, 0 });

    // After install: a sweep mutates.
    InstallRealNpcActions();
    ResetNpcActionsQueue();
    ResetNpcActionsTallies();
    { sim::HeRecord rec{}; sim::He_State(&rec) = 0; sim::NpcDaily_DailyRoutineStep(&rec); }
    CHECK(GetNpcActionsTallies().commandsBuilt > 0);

    // After uninstall: re-setup a clean population, a sweep is a no-op.
    UninstallRealNpcActions();
    SetupOneProductionWorker();
    ResetNpcActionsQueue();
    ResetNpcActionsTallies();
    { sim::HeRecord rec{}; sim::He_State(&rec) = 0; sim::NpcDaily_DailyRoutineStep(&rec); }
    CHECK_EQ((int)GetNpcActionsTallies().commandsBuilt, 0);
    CHECK_EQ((int)NpcActionsQueue().send_count(), 0);
    SetNpcActionsTargetProvider(nullptr);
}
