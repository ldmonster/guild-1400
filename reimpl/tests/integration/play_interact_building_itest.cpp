// tests/integration/play_interact_building_itest.cpp — INTEGRATION: a scripted
// world-view click on a SEEDED building resolves it (REAL scene_pick ->
// GameObjectResolveEntityById), opens the building dialog FSM, drives a menu choice
// that BUILDS a real opcode-26 building command, ENQUEUES it through the REAL
// sim::CommandQueue codec (assert opcode/len/fields in the ring), and APPLYING it
// mutates the seeded building record (sim::BuildingRec over g_objects).
#include "test.h"

#include "play/interact_building.h"
#include "play/scene_pick.h"
#include "sim/command.h"
#include "sim/entity.h"
#include "sim/building_types.h"

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

// Seed one live building in g_objects (alive byte @+0 + id dword @+1), and turn on
// the scene-array loaded guard the resolver checks. Returns the slot's BuildingRec
// view. NOTE: the BuildingRec overlay's typeIndex (+0/+1) ALIASES the ObjectRec
// alive(+0)/id(+1) columns the resolver keys on, so we do NOT write typeIndex — the
// dialog KIND code is supplied via kindOf, not read from the record's +0 word. The
// price/stock/upgrade fields the apply mutates sit past the alive/id columns and are
// safe to read/write through the overlay.
sim::BuildingRec* SeedBuilding(int slot, i32 id) {
    sim::ResetEntityArrays();
    sim::g_sceneArrayLoaded = true;          // resolver bail guard

    sim::ObjectRec& o = sim::g_objects[slot];
    o.alive = 1;                             // non-free slot
    o.id    = id;
    return reinterpret_cast<sim::BuildingRec*>(&o);
}

} // namespace

// A scripted click lands on the seeded building, the dialog reaches the action, and
// a real opcode-26 command is enqueued + applied, mutating the building's price.
TEST(PlayInteractBuildingItest, ClickResolvesBuildingEmitsAndAppliesCommand) {
    SetBuildingDialogHooks(nullptr);         // inert defaults (real-record apply)

    const i32 kId = 4242;
    sim::BuildingRec* b = SeedBuilding(/*slot=*/3, kId);
    CHECK(b != nullptr);

    // Read the price scalar before (BuildingRec +122 qualityScalar).
    i32 priceBefore = 0;
    std::memcpy(&priceBefore, &b->qualityScalar, sizeof priceBefore);

    // City view: eye at origin, 1 px/unit. An object at world (10,0,5) projects to
    // screen (10.875, 5.875). The pick object reports the seeded id.
    float eye[3] = {0.0f, 0.0f, 0.0f};
    CityViewCamera cam = MakeCityViewCamera(eye, /*pixelsPerUnit=*/1.0f, 256, 256);

    ScenePickObject objs[1];
    objs[0].id = kId;
    objs[0].pos[0] = 10.0f; objs[0].pos[1] = 0.0f; objs[0].pos[2] = 5.0f;

    sim::CommandQueue q;
    q.Init();
    q.set_standalone(true);
    InstallBuildingCommandHandler(q);

    // A carpenter dialog (kind 133); raise-price menu choice. kindOf supplies the
    // building KIND code the dialog switch reads.
    auto kindOf = [](i32) { return 133; };
    u32 sendBefore = q.send_count();

    BuildingClickResult r =
        IssueBuildingClick(q, cam, /*sx=*/10.875f, /*sy=*/5.875f, objs, 1,
                           /*pickRadius=*/4.0f, kindOf, BuildingMenuItem::kRaisePrice);

    // (1) the click resolved the seeded building (REAL scene_pick + resolver).
    CHECK_EQ(r.pickIndex, 0);
    CHECK_EQ(r.pickId, kId);
    CHECK_EQ(r.resolveKind, 1);              // kind 1 == object/building
    CHECK_EQ(r.buildingKind, 133);
    CHECK(r.group == BuildingActionGroup::kCarpenter);

    // (2) the dialog opened and reached the ACTION state.
    CHECK(r.opened);
    CHECK(r.finalState == BuildingDialogState::kAction);

    // (3) a real building command was classified + enqueued through the real codec.
    CHECK(r.command.issued);
    CHECK_EQ((int)r.command.opcode, 26);
    CHECK_EQ(r.command.field, (i32)kFieldPrice);
    CHECK(r.enqueued);
    CHECK(r.ringSlot >= 0);
    CHECK_EQ(q.send_count(), sendBefore + 1);

    // Inspect the staged packet in the ring: opcode 26, computed len 28, and the
    // three QueueRequestArgs26 dwords (id/field/value) at +0x10/+0x14/+0x18.
    sim::CommandPacket& staged = q.ring_slot((u32)r.ringSlot);
    CHECK_EQ((int)staged.opcode(), 26);
    CHECK_EQ((int)staged.len(), 28);          // ComputePacketSize(26) == 28
    CHECK_EQ((i32)staged.get32(kCmdBuildingIdOff), kId);
    CHECK_EQ((i32)staged.get32(kCmdFieldOff), (i32)kFieldPrice);
    CHECK_EQ((i32)staged.get32(kCmdValueOff), 1);

    // (4) applying mutated the seeded building record: price += 1.
    CHECK(r.applied);
    i32 priceAfter = 0;
    std::memcpy(&priceAfter, &b->qualityScalar, sizeof priceAfter);
    CHECK_EQ(priceAfter, priceBefore + 1);

    std::printf("[interact_building][itest] kind=133 op=26 field=%d ring=%d price %d->%d\n",
                (int)kFieldPrice, r.ringSlot, priceBefore, priceAfter);
}

// Stock restock + sell deltas mutate the building's fill level; the command applies
// through the same codec path.
TEST(PlayInteractBuildingItest, RestockAndSellMutateStock) {
    SetBuildingDialogHooks(nullptr);

    const i32 kId = 9001;
    sim::BuildingRec* b = SeedBuilding(/*slot=*/0, kId);
    b->fillLevel = 5;

    float eye[3] = {0.0f, 0.0f, 0.0f};
    CityViewCamera cam = MakeCityViewCamera(eye, 1.0f, 256, 256);
    ScenePickObject objs[1];
    objs[0].id = kId; objs[0].pos[0] = 3.0f; objs[0].pos[1] = 0.0f; objs[0].pos[2] = 8.0f;

    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    InstallBuildingCommandHandler(q);
    auto kindOf = [](i32) { return 116; };   // smith production hub

    BuildingClickResult restock =
        IssueBuildingClick(q, cam, 3.875f, 8.875f, objs, 1, 4.0f, kindOf,
                           BuildingMenuItem::kRestock);
    CHECK(restock.applied);
    CHECK_EQ(restock.command.field, (i32)kFieldStock);
    CHECK_EQ((int)b->fillLevel, 6);

    BuildingClickResult sell =
        IssueBuildingClick(q, cam, 3.875f, 8.875f, objs, 1, 4.0f, kindOf,
                           BuildingMenuItem::kSell);
    CHECK(sell.applied);
    CHECK_EQ(sell.command.delta, -1);
    CHECK_EQ((int)b->fillLevel, 5);
}

// A click over empty space resolves nothing -> no dialog, no command.
TEST(PlayInteractBuildingItest, EmptySpaceClickIssuesNothing) {
    SetBuildingDialogHooks(nullptr);
    const i32 kId = 77;
    SeedBuilding(/*slot=*/1, kId);

    float eye[3] = {0.0f, 0.0f, 0.0f};
    CityViewCamera cam = MakeCityViewCamera(eye, 1.0f, 256, 256);
    ScenePickObject objs[1];
    objs[0].id = kId; objs[0].pos[0] = 10.0f; objs[0].pos[1] = 0.0f; objs[0].pos[2] = 10.0f;

    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    InstallBuildingCommandHandler(q);
    auto kindOf = [](i32) { return 133; };

    // Click far from the only object (radius 4, object at ~10.875/10.875).
    BuildingClickResult r =
        IssueBuildingClick(q, cam, 100.0f, 100.0f, objs, 1, 4.0f, kindOf,
                           BuildingMenuItem::kRaisePrice);
    CHECK_EQ(r.pickIndex, -1);
    CHECK(!r.opened);
    CHECK(!r.command.issued);
    CHECK(!r.enqueued);
    CHECK_EQ(q.send_count(), (u32)0);
}

// Entry-denied dialog: the FSM blocks before any command is built.
TEST(PlayInteractBuildingItest, EntryDeniedBlocksCommand) {
    BuildingDialogHooks hooks;
    hooks.checkEntryAllowed = [](i32, int) { return false; };
    SetBuildingDialogHooks(&hooks);

    const i32 kId = 555;
    SeedBuilding(/*slot=*/2, kId);

    float eye[3] = {0.0f, 0.0f, 0.0f};
    CityViewCamera cam = MakeCityViewCamera(eye, 1.0f, 256, 256);
    ScenePickObject objs[1];
    objs[0].id = kId; objs[0].pos[0] = 1.0f; objs[0].pos[1] = 0.0f; objs[0].pos[2] = 1.0f;

    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    InstallBuildingCommandHandler(q);
    auto kindOf = [](i32) { return 133; };

    BuildingClickResult r =
        IssueBuildingClick(q, cam, 1.875f, 1.875f, objs, 1, 4.0f, kindOf,
                           BuildingMenuItem::kRaisePrice);
    CHECK_EQ(r.pickId, kId);                  // resolved fine
    CHECK(!r.opened);                          // but entry denied
    CHECK(r.finalState == BuildingDialogState::kEntryDenied);
    CHECK(!r.command.issued);
    CHECK_EQ(q.send_count(), (u32)0);

    SetBuildingDialogHooks(nullptr);
}
