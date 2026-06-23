#include "test.h"

// =============================================================================
// Unit tier for the wave-4 LIVING CITY session wiring (asset-free):
//
//   * play::InstallSessionPersonsMovePlacement / UpdateSessionPersons3DPositions
//     (src/play/session_persons3d.*) — the documented placementOverride handoff:
//     a person with a live movement tile (wire_npc_movement record pad
//     +0x6C/+0x70, state +0x74 — the WalkStep @0x4093b0 advance) draws at
//     render::TileToWorld @0x5c65d4 over the terrain heightmap (REAL ground
//     height from the heights grid); everyone else keeps the CAPTURED
//     entrance-dummy seat (SpawnAtBuildingEntrance @0x57c8f0 default).
//
//   * play::SessionNpcDailyAssign (src/play/session_npc_daily.*) — the
//     day-start daily-routine pass: the REAL director step
//     (sim::NpcDaily_DailyRoutineStep @0x4e7e88, He state 0) over the REAL
//     leaf bridge (play::InstallRealNpcActions) dispatches persons whose home
//     is a production building (Building_IsProductionKind over the live type
//     byte) and the pass binds their movement destination from the target
//     building's REAL bound placement via WorldToTileWithHeight @0x5c6644.
// =============================================================================
#include "play/city_view3d.h"
#include "play/session_npc_daily.h"
#include "play/session_persons3d.h"
#include "play/wire_npc_actions.h"
#include "play/wire_npc_movement.h"
#include "render/heightmap.h"
#include "sim/character_query.h"
#include "sim/command_apply6.h"   // g_tickClock (the world-clock image)
#include "sim/entity.h"
#include "sim/map.h"
#include "sim/npc_daily.h"        // DailyTurnBit masks (the +456 column)
#include "sim/person.h"

#include <cstring>
#include <vector>

using namespace guild;
using play::CityPlacement;
using play::CityView3D;
using play::SceneObjectInst;

namespace {

SceneObjectInst Node(const char* name, int parent, float x, float y, float z,
                     u32 ownerId = 0) {
    SceneObjectInst n;
    n.name = name;
    n.type = 3;
    n.parent = parent;
    n.pos[0] = x; n.pos[1] = y; n.pos[2] = z;
    n.ownerId = ownerId;
    return n;
}

// A live person passing the 0x4f8e60 gate, with the daily-director columns
// (sim/npc_daily.h): +356 activeA, +364 homeBld, +368 workBld, +388 destBld.
void MakeLivePerson(int slot, i32 id, i32 homeBld, i32 workBld, i32 destBld) {
    sim::Person& p = sim::g_persons[slot];
    std::memset(&p, 0, sizeof(p));
    p.marker = 0;
    p.kind = 5;
    p.id = id;
    sim::g_personIds[slot] = id;     // the parallel id column FindRecordById scans
    p.isPlayer = 100;                                   // +8 live byte
    sim::PersonSetByte(&p, 0x165, 0x02);                // profession (dieb_MANN2)
    sim::PersonSetByte(&p, 356, 1);                     // activeA (byte_12CEA74)
    sim::PersonSetDword(&p, 364, homeBld);              // homeBld dword_12CEA7C
    sim::PersonSetDword(&p, 368, workBld);              // workBld dword_12CEA80
    sim::PersonSetDword(&p, 388, destBld);              // destBld dword_12CEA94
}

// A live world object/building record (g_objects): type byte @+0, id @+1.
void MakeLiveBuilding(int slot, i32 id, u8 type) {
    std::memset(&sim::g_objects[slot], 0, sizeof(sim::ObjectRec));
    sim::g_objects[slot].alive = type;
    std::memcpy(reinterpret_cast<u8*>(&sim::g_objects[slot]) + 1, &id, 4);
}

void ResetWorld() {
    sim::ResetEntityArrays();
    sim::ResetCharacterQuery();
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        sim::g_persons[i].marker = -1;
    std::memset(&sim::g_tickClock, 0, sizeof sim::g_tickClock);
    play::UninstallNpcMovement();
    play::ClearSessionPersonsMovePlacement();
    play::ResetNpcMovementTallies();
}

// The shared synthetic rig: a 16x16 heightmap (scale 10, originY 3, one raised
// height byte so the REAL ground height is observable), an open walkable grid
// with the blocked border, two owner-bound buildings (A id 41 production
// type 11 at tile (2,2); B id 77 type 14 at tile (9,9)) each with a door
// dummy, and a live person homed at A working at B.
struct Rig {
    render::Heightmap hm{};
    std::vector<u8> heights;
    std::vector<u8> entries;
    CityView3D view;

    bool Build() {
        const int n = 16;
        heights.assign((std::size_t)n * n, 0);
        heights[5 * n + 5] = 100;            // tile (5,5) raised (real ground)
        entries.assign((std::size_t)n * n * sim::kTileEntryStride, 0);
        for (int i = 0; i < n * n; ++i)
            entries[(std::size_t)i * sim::kTileEntryStride] = 1;
        std::memset(&hm, 0, sizeof hm);
        hm.size = n;
        hm.heights = heights.data();
        hm.entries = entries.data();
        hm.originX = 0.0f; hm.originY = 3.0f; hm.originZ = 0.0f;
        hm.scaleX = 10.0f; hm.scaleY = 0.25f; hm.scaleZ = 10.0f;
        sim::MapGrid mg{n, entries.data()};
        for (int i = 0; i < n; ++i) {
            sim::MapSetCellAt(mg, i, 0, sim::kCellBlocked);
            sim::MapSetCellAt(mg, i, n - 1, sim::kCellBlocked);
            sim::MapSetCellAt(mg, 0, i, sim::kCellBlocked);
            sim::MapSetCellAt(mg, n - 1, i, sim::kCellBlocked);
        }
        play::SetNpcMovementGrid(mg);
        play::InstallNpcMovement();

        std::vector<SceneObjectInst> nodes;
        nodes.push_back(Node("city", -1, 0, 0, 0));               // 0
        nodes.push_back(Node("gb_HOME", 0, 25, 0, 25, 41));       // 1: tile (2,2)
        nodes.push_back(Node("dummy_TUER", 1, 1, 0, 1));          // 2
        nodes.push_back(Node("gb_WORK", 0, 95, 0, 95, 77));       // 3: tile (9,9)
        nodes.push_back(Node("dummy_TUER", 3, 1, 0, 1));          // 4
        if (!view.LoadCityFromNodes(nodes))
            return false;

        MakeLiveBuilding(0, 41, /*type=*/11);    // production kind (11)
        MakeLiveBuilding(1, 77, /*type=*/14);
        return view.BindWorldObjects() == 2;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The movement placement chain: captured seat fallback, live-tile placement at
// the REAL ground height, the cheap no-move frame, the rebind-on-move frame.
// ---------------------------------------------------------------------------
TEST(SessionLivingCity, MovePlacementFollowsLiveTile) {
    ResetWorld();
    Rig rig;
    CHECK(rig.Build());
    MakeLivePerson(2, 900, /*homeBld=*/41, /*workBld=*/77, /*destBld=*/41);

    // Default bind: the person seats at its home's entrance dummy (26,0,26).
    play::SessionPersons3DOptions po;
    po.mountAnims = false;
    play::SessionPersons3DStatus st = play::WireSessionPersons3D(rig.view, nullptr, po);
    CHECK_EQ(1, st.bound);
    CHECK_EQ(26.0f, rig.view.boundPersons()[0].place.pos[0]);
    CHECK_EQ(26.0f, rig.view.boundPersons()[0].place.pos[2]);

    // Install the movement placement: seats captured, hook installed.
    CHECK_EQ(1, play::InstallSessionPersonsMovePlacement(rig.view, po, &rig.hm));

    // No movement -> the per-frame check is a no-op (no rebind, same seat).
    CHECK_EQ(0, play::UpdateSessionPersons3DPositions(rig.view, po));
    CHECK_EQ(26.0f, rig.view.boundPersons()[0].place.pos[0]);

    // A rebind WITHOUT movement keeps the captured entrance-dummy seat (the
    // fallback half of the override).
    st = play::RebindSessionPersons3D(rig.view, po);
    CHECK_EQ(1, st.bound);
    CHECK_EQ(26.0f, rig.view.boundPersons()[0].place.pos[0]);
    CHECK_EQ(26.0f, rig.view.boundPersons()[0].place.pos[2]);

    // Give the person a destination (the order/daily binding) and step the
    // real path-follow once: the per-frame check sees the moved tile and
    // rebinds; the person now draws at TileToWorld(curTile).
    CHECK(play::SetEntityDestination(900, /*dest*/ 9, 9, /*start*/ 2, 2));
    CHECK_EQ(1, play::StepNpcMovement());
    play::NpcMovePos mp = play::GetEntityMovePos(900);
    CHECK(mp.active);
    CHECK(mp.curX != 2 || mp.curZ != 2);     // advanced one waypoint
    CHECK(play::UpdateSessionPersons3DPositions(rig.view, po) >= 1);
    float w[3];
    CHECK(render::TileToWorld(&rig.hm, mp.curX, mp.curZ, w));
    CHECK_EQ(w[0], rig.view.boundPersons()[0].place.pos[0]);
    CHECK_EQ(w[1], rig.view.boundPersons()[0].place.pos[1]);
    CHECK_EQ(w[2], rig.view.boundPersons()[0].place.pos[2]);

    // Walk to arrival: the person ends at the destination tile and STAYS there
    // (arrived persons keep the tile placement, not the old seat).
    for (int i = 0; i < 64 && play::GetEntityMovePos(900).active; ++i)
        play::StepNpcMovement();
    mp = play::GetEntityMovePos(900);
    CHECK(!mp.active);
    CHECK_EQ(9, mp.curX);
    CHECK_EQ(9, mp.curZ);
    CHECK(play::UpdateSessionPersons3DPositions(rig.view, po) >= 1);
    CHECK(render::TileToWorld(&rig.hm, 9, 9, w));
    CHECK_EQ(w[0], rig.view.boundPersons()[0].place.pos[0]);
    CHECK_EQ(w[2], rig.view.boundPersons()[0].place.pos[2]);
    // Steady state afterwards: no further rebinds.
    CHECK_EQ(0, play::UpdateSessionPersons3DPositions(rig.view, po));

    play::UnwireSessionPersons3D(rig.view);
    play::ClearSessionPersonsMovePlacement();
    play::UninstallNpcMovement();
}

// ---------------------------------------------------------------------------
// The REAL ground height: a tile with a nonzero height byte places the person
// at heights[..]*scaleY + originY (TileToWorld @0x5c65d4), not at y=0.
// ---------------------------------------------------------------------------
TEST(SessionLivingCity, MovePlacementUsesRealGroundHeight) {
    ResetWorld();
    Rig rig;
    CHECK(rig.Build());
    MakeLivePerson(2, 900, 41, 77, 41);

    play::SessionPersons3DOptions po;
    po.mountAnims = false;
    play::WireSessionPersons3D(rig.view, nullptr, po);
    play::InstallSessionPersonsMovePlacement(rig.view, po, &rig.hm);

    // Park the person ON the raised tile (5,5): height byte 100 over
    // scaleY 0.25 + originY 3 => world y = 28.
    CHECK(play::SetEntityDestination(900, 5, 5, 5, 5));
    play::StepNpcMovement();                  // immediate arrival (start == dest)
    CHECK(play::UpdateSessionPersons3DPositions(rig.view, po) >= 1);
    CHECK_EQ(28.0f, rig.view.boundPersons()[0].place.pos[1]);
    CHECK_EQ(50.0f, rig.view.boundPersons()[0].place.pos[0]);   // 5*10 + 0

    play::UnwireSessionPersons3D(rig.view);
    play::ClearSessionPersonsMovePlacement();
    play::UninstallNpcMovement();
}

// ---------------------------------------------------------------------------
// SessionNpcDailyAssign: the REAL director step (He state 0, morning window)
// dispatches the production-homed person and the pass binds its movement
// destination at the work building's tile; deterministic across reruns.
// ---------------------------------------------------------------------------
TEST(SessionLivingCity, DailyAssignDispatchesAndBindsDestination) {
    ResetWorld();
    Rig rig;
    CHECK(rig.Build());
    // Person at production home 41, works at 77, currently "in" 41 (destBld).
    MakeLivePerson(2, 900, 41, 77, 41);
    // A second person with NO home: never dispatched (rule 8 — no invention).
    MakeLivePerson(3, 901, 0, 77, 41);

    play::SessionPersons3DOptions po;
    po.mountAnims = false;
    play::WireSessionPersons3D(rig.view, nullptr, po);
    play::InstallSessionPersonsMovePlacement(rig.view, po, &rig.hm);

    play::SessionNpcDailyResult dr = play::SessionNpcDailyAssign(rig.view, &rig.hm);
    CHECK_EQ(1, dr.dispatched);             // only the homed person
    CHECK_EQ(1, dr.assigned);

    // The dispatch bit landed in the +456 turn-bits column (the director's
    // real writeback) and the movement binding points at the work building's
    // tile (9,9) from the person's seat tile (2,2).
    u32 bits = 0;
    std::memcpy(&bits, reinterpret_cast<u8*>(&sim::g_persons[2]) + 456, 4);
    CHECK((bits & (u32)sim::kDailyDispWork) != 0);
    play::NpcMovePos mp = play::GetEntityMovePos(900);
    CHECK(mp.found);
    CHECK(mp.active);
    CHECK_EQ(9, mp.destX);
    CHECK_EQ(9, mp.destZ);
    CHECK_EQ(2, mp.curX);                   // start = the bound seat's tile
    CHECK_EQ(2, mp.curZ);
    play::NpcMovePos mp2 = play::GetEntityMovePos(901);
    CHECK(!mp2.active);                     // the homeless person stays put

    // A SECOND pass the same morning re-dispatches nothing NEW (the 0x100000
    // work-dispatch bit gates the sweep) — destinations are stable.
    dr = play::SessionNpcDailyAssign(rig.view, &rig.hm);
    CHECK_EQ(0, dr.dispatched);
    CHECK_EQ(0, dr.assigned);

    // Determinism: a full reset + rerun lands the identical binding.
    u32 firstBits = bits;
    ResetWorld();
    Rig rig2;
    CHECK(rig2.Build());
    MakeLivePerson(2, 900, 41, 77, 41);
    MakeLivePerson(3, 901, 0, 77, 41);
    play::SessionPersons3DOptions po2;
    po2.mountAnims = false;
    play::WireSessionPersons3D(rig2.view, nullptr, po2);
    play::InstallSessionPersonsMovePlacement(rig2.view, po2, &rig2.hm);
    dr = play::SessionNpcDailyAssign(rig2.view, &rig2.hm);
    CHECK_EQ(1, dr.dispatched);
    CHECK_EQ(1, dr.assigned);
    play::NpcMovePos mpB = play::GetEntityMovePos(900);
    CHECK_EQ(mp.destX, mpB.destX);
    CHECK_EQ(mp.destZ, mpB.destZ);
    CHECK_EQ(mp.curX, mpB.curX);
    CHECK_EQ(mp.curZ, mpB.curZ);
    u32 bits2 = 0;
    std::memcpy(&bits2, reinterpret_cast<u8*>(&sim::g_persons[2]) + 456, 4);
    CHECK_EQ(firstBits, bits2);

    play::UnwireSessionPersons3D(rig2.view);
    play::ClearSessionPersonsMovePlacement();
    play::UninstallNpcMovement();
}

// ---------------------------------------------------------------------------
// W10-SIM hardening: SessionNpcDailyAssign with NO terrain. A null heightmap (or
// a zero/negative-size one) is the documented data-absent guard: the pass returns
// an empty result without touching the (absent) tile arrays — no OOB, no dispatch.
// ---------------------------------------------------------------------------
TEST(SessionLivingCity, DailyAssignNoTerrainIsSafe) {
    ResetWorld();
    Rig rig;
    CHECK(rig.Build());
    MakeLivePerson(2, 900, 41, 77, 41);

    play::SessionPersons3DOptions po;
    po.mountAnims = false;
    play::WireSessionPersons3D(rig.view, nullptr, po);

    // Null heightmap -> guarded early-out (empty result).
    play::SessionNpcDailyResult dr = play::SessionNpcDailyAssign(rig.view, nullptr);
    CHECK_EQ(0, dr.dispatched);
    CHECK_EQ(0, dr.assigned);

    // A zero-size heightmap is equally rejected (hm->size <= 0).
    render::Heightmap empty{};
    std::memset(&empty, 0, sizeof empty);
    empty.size = 0;
    dr = play::SessionNpcDailyAssign(rig.view, &empty);
    CHECK_EQ(0, dr.dispatched);
    CHECK_EQ(0, dr.assigned);

    play::UnwireSessionPersons3D(rig.view);
    play::UninstallNpcMovement();
}

// ---------------------------------------------------------------------------
// End to end over the synthetic rig: daily assign -> per-tick steps -> the
// person's draw seat crosses the city to the work building's door tile.
// ---------------------------------------------------------------------------
TEST(SessionLivingCity, AssignedPersonWalksToWorkAcrossSteps) {
    ResetWorld();
    Rig rig;
    CHECK(rig.Build());
    MakeLivePerson(2, 900, 41, 77, 41);

    play::SessionPersons3DOptions po;
    po.mountAnims = false;
    play::WireSessionPersons3D(rig.view, nullptr, po);
    play::InstallSessionPersonsMovePlacement(rig.view, po, &rig.hm);
    play::SessionNpcDailyAssign(rig.view, &rig.hm);

    float prevX = rig.view.boundPersons()[0].place.pos[0];
    float prevZ = rig.view.boundPersons()[0].place.pos[2];
    int posChanges = 0;
    for (int step = 0; step < 64; ++step) {
        if (play::StepNpcMovement() == 0 && !play::GetEntityMovePos(900).active)
            break;
        if (play::UpdateSessionPersons3DPositions(rig.view, po) > 0) {
            const auto& bp = rig.view.boundPersons()[0];
            CHECK(bp.place.pos[0] != prevX || bp.place.pos[2] != prevZ);
            prevX = bp.place.pos[0];
            prevZ = bp.place.pos[2];
            ++posChanges;
        }
    }
    CHECK(posChanges >= 7);                       // (2,2) -> (9,9) tile walk
    play::NpcMovePos mp = play::GetEntityMovePos(900);
    CHECK(!mp.active);                            // arrived
    CHECK_EQ(9, mp.curX);
    CHECK_EQ(9, mp.curZ);
    const auto& t = play::GetNpcMovementTallies();
    CHECK(t.entitiesMoved >= 7);
    CHECK_EQ(1, t.arrivals);

    play::UnwireSessionPersons3D(rig.view);
    play::ClearSessionPersonsMovePlacement();
    play::UninstallNpcMovement();
}
