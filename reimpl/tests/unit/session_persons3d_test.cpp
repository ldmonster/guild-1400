#include "test.h"

// Unit tier for the wave-3 PERSON binding in the 3D city view:
//   * play::FindEntranceDummyNode — the VIBE_Character_SpawnAtBuildingEntrance
//     @0x57c8f0 entrance-dummy resolution (pre-order subtree walk, the
//     dummy_EINGANG -> dummy_TUER preference, the StrCmpNoCase compare) with
//     the VIBE_Object_IsNearDoorAlt @0x4b0ee8 building-node fallback,
//   * play::CityView3D::BindPersons — the live-person gate (VIBE_Person_
//     IsValidActiveRecord @0x4f8e60), the anchor columns (+0x16C homeBld
//     dword_12CEA7C / +0x170 workBld dword_12CEA80, sim/npc_daily.h), the
//     composed dummy world seat, the REAL model resolver default
//     (VIBE_Office_ResolveStaffModel @0x57c1e8) and the named-gap counters,
//   * play::WireSessionPersons3D / Update / Rebind / Unwire — the session
//     contract over synthetic persons, all asset-free.
#include "play/city_view3d.h"
#include "play/session_persons3d.h"
#include "sim/character_query.h"
#include "sim/entity.h"
#include "sim/person.h"

#include <cstring>

using namespace guild;
using play::CityPlacement;
using play::CityView3D;
using play::SceneObjectInst;

namespace {

SceneObjectInst Node(const char* name, int parent, float x, float y, float z,
                     u32 ownerId = 0) {
    SceneObjectInst n;
    n.name = name;
    n.type = 3;            // dummy/locator
    n.parent = parent;
    n.pos[0] = x; n.pos[1] = y; n.pos[2] = z;
    n.ownerId = ownerId;
    return n;
}

// A live person passing the full 0x4f8e60 gate (marker != -1, +8 != 0, kind < 10).
void MakeLivePerson(int slot, i32 id, u8 kind, u8 gender, u8 profession,
                    i32 homeBld, i32 workBld = 0) {
    sim::Person& p = sim::g_persons[slot];
    std::memset(&p, 0, sizeof(p));
    p.marker = 0;
    p.kind = kind;
    p.id = id;
    p.isPlayer = 100;          // the +8 live byte Person_CreateAndSpawn stamps
    p.gender = gender;
    sim::PersonSetByte(&p, 0x165, profession);          // profession byte
    sim::PersonSetDword(&p, 0x16C, homeBld);            // homeBld dword_12CEA7C
    sim::PersonSetDword(&p, 0x170, workBld);            // workBld dword_12CEA80
}

void ResetWorld() {
    sim::ResetEntityArrays();
    sim::ResetCharacterQuery();
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        sim::g_persons[i].marker = -1;
}

} // namespace

// ---------------------------------------------------------------------------
// FindEntranceDummyNode: pre-order subtree resolution, EINGANG > TUER, the
// case-insensitive compare, miss -> -1.
// ---------------------------------------------------------------------------
TEST(SessionPersons3D, FindEntranceDummyResolvesSubtreePreOrder) {
    std::vector<SceneObjectInst> scene;
    scene.push_back(Node("city", -1, 0, 0, 0));            // 0
    scene.push_back(Node("gb_HAUS", 0, 100, 0, 50, 41));   // 1 building
    scene.push_back(Node("deco", 1, 1, 0, 1));             // 2 child of building
    scene.push_back(Node("DUMMY_tuer", 2, 3, 0, 4));       // 3 deep descendant
    scene.push_back(Node("dummy_TUER", 0, 9, 0, 9));       // 4 NOT under building

    // The deep descendant is found (case-insensitive), not the outside node.
    CHECK_EQ(3, play::FindEntranceDummyNode(scene, 1));

    // A dummy_EINGANG in the subtree is preferred even when it appears LATER
    // in pre-order than the dummy_TUER (the 0x57c8f0 tag preference).
    scene.push_back(Node("dummy_EINGANG", 1, 5, 0, 5));    // 5
    CHECK_EQ(5, play::FindEntranceDummyNode(scene, 1));

    // No dummy in the subtree -> -1 (callers fall back to the building node,
    // the 0x4b0ee8 behaviour). Node 4 is itself a dummy_TUER but has none
    // BELOW it (the walk searches descendants, as FindByHandle does from the
    // building root).
    CHECK_EQ(-1, play::FindEntranceDummyNode(scene, 4));
    // Out-of-range building index -> -1.
    CHECK_EQ(-1, play::FindEntranceDummyNode(scene, -1));
    CHECK_EQ(-1, play::FindEntranceDummyNode(scene, 99));
}

// ---------------------------------------------------------------------------
// BindPersons: the homeBld anchor column places the person at the building's
// entrance dummy (composed world position), the REAL staff-model resolver runs
// (golden table row), and the no-anchor person is counted unplaced (named gap).
// ---------------------------------------------------------------------------
TEST(SessionPersons3D, BindPersonsAnchorsAtEntranceDummy) {
    ResetWorld();

    std::vector<SceneObjectInst> nodes;
    nodes.push_back(Node("city", -1, 0, 0, 0));            // 0
    nodes.push_back(Node("gb_HAUS", 0, 100, 0, 50, 41));   // 1 owner-id 41
    nodes.push_back(Node("dummy_TUER", 1, 3, 0, 4));       // 2 door dummy

    CityView3D view;
    CHECK(view.LoadCityFromNodes(nodes));

    // profession 0x02 (male) == "dieb_MANN2" in the recovered 0x63DAC8 table.
    MakeLivePerson(2, 900, 5, 0, 0x02, /*homeBld=*/41);
    // No anchor columns at all -> NOT placed (never an invented position).
    MakeLivePerson(3, 901, 5, 0, 0x02, /*homeBld=*/0);

    CHECK_EQ(1, view.BindPersons());
    CHECK_EQ(1, view.personsUnplaced());
    CHECK_EQ(1u, (unsigned)view.boundPersons().size());

    const CityView3D::BoundPerson& bp = view.boundPersons()[0];
    CHECK_EQ(900, bp.id);
    CHECK_EQ(2, bp.slot);
    CHECK_EQ(41, bp.anchorBuildingId);
    CHECK_EQ(2, bp.dummySceneIndex);
    // The composed dummy world seat: parent (100,0,50) + local (3,0,4).
    CHECK_EQ(103.0f, bp.place.pos[0]);
    CHECK_EQ(0.0f, bp.place.pos[1]);
    CHECK_EQ(54.0f, bp.place.pos[2]);
    // Zero spawn rotation (dword_577A78 default).
    CHECK_EQ(0.0f, bp.place.euler[1]);
    // The REAL resolver row (recovered byte-exact from 0x63DAC8).
    CHECK(bp.model == "dieb_MANN2");
    // No Objects.BIN mounted -> the member is the model named gap, counted.
    CHECK(bp.member.empty());
    CHECK_EQ(1, view.personModelUnresolved());
    CHECK(!bp.posed);

    // A frame renders cleanly: the placed person has no drawable mesh, so no
    // person instances are drawn and nothing crashes.
    CityView3D::Options opt;
    opt.fbW = 32; opt.fbH = 24;
    CityView3D::Result r = view.RenderFrame(play::CityCamera3D{}, opt);
    CHECK_EQ(0, r.personInstances);
    CHECK_EQ(0, r.personPosed);

    ResetWorld();
}

// ---------------------------------------------------------------------------
// The workBld (+0x170) column is the fallback anchor when homeBld is 0, and a
// building without a door dummy seats the person at the building node itself
// (the 0x4b0ee8 fallback).
// ---------------------------------------------------------------------------
TEST(SessionPersons3D, WorkBldFallbackAndBuildingNodeSeat) {
    ResetWorld();

    std::vector<SceneObjectInst> nodes;
    nodes.push_back(Node("city", -1, 0, 0, 0));             // 0
    nodes.push_back(Node("gb_WERK", 0, -20, 0, 30, 77));    // 1 — NO door child

    CityView3D view;
    CHECK(view.LoadCityFromNodes(nodes));

    MakeLivePerson(0, 910, 6, 1, 0x05, /*homeBld=*/0, /*workBld=*/77);

    CHECK_EQ(1, view.BindPersons());
    const CityView3D::BoundPerson& bp = view.boundPersons()[0];
    CHECK_EQ(77, bp.anchorBuildingId);
    CHECK_EQ(-1, bp.dummySceneIndex);          // no dummy -> building node
    CHECK_EQ(-20.0f, bp.place.pos[0]);
    CHECK_EQ(30.0f, bp.place.pos[2]);
    // profession 0x05 female == "handwerkerin_FRAU"... the FEMALE table row for
    // code 5 (the recovered 0x63DF00 table; the legacy e2e exercises the same).
    CHECK(!bp.model.empty());

    ResetWorld();
}

// ---------------------------------------------------------------------------
// The 0x4f8e60 record gate: free slot (marker -1), non-person kind (>= 10) and
// a dead +8 byte are all skipped.
// ---------------------------------------------------------------------------
TEST(SessionPersons3D, LivePersonGateFiltersRecords) {
    ResetWorld();

    std::vector<SceneObjectInst> nodes;
    nodes.push_back(Node("gb_HAUS", -1, 0, 0, 0, 41));
    CityView3D view;
    CHECK(view.LoadCityFromNodes(nodes));

    MakeLivePerson(0, 920, 5, 0, 0, 41);                 // valid
    MakeLivePerson(1, 921, 12, 0, 0, 41);                // kind >= 10 -> skipped
    MakeLivePerson(2, 922, 5, 0, 0, 41);
    sim::g_persons[2].isPlayer = 0;                      // +8 == 0 -> skipped
    MakeLivePerson(3, 923, 5, 0, 0, 41);
    sim::g_persons[3].marker = -1;                       // free slot -> skipped

    CHECK_EQ(1, view.BindPersons());
    CHECK_EQ(920, view.boundPersons()[0].id);
    CHECK_EQ(0, view.personsUnplaced());                 // the others never gate in

    // maxPersons caps the bind.
    MakeLivePerson(4, 924, 5, 0, 0, 41);
    MakeLivePerson(5, 925, 5, 0, 0, 41);
    CHECK_EQ(2, view.BindPersons(2));
    CHECK_EQ(2u, (unsigned)view.boundPersons().size());

    ResetWorld();
}

// ---------------------------------------------------------------------------
// The session contract: Wire installs the option hooks WITHOUT clobbering the
// building/object hooks, Rebind re-runs against the current sim, Unwire
// restores the persons-free view.
// ---------------------------------------------------------------------------
TEST(SessionPersons3D, WireUpdateRebindUnwireContract) {
    ResetWorld();

    std::vector<SceneObjectInst> nodes;
    nodes.push_back(Node("gb_HAUS", -1, 10, 0, 20, 41));
    CityView3D view;
    CHECK(view.LoadCityFromNodes(nodes));

    // A pre-existing object hook must survive the person wiring.
    bool objectHookAlive = false;
    play::CityView3DHooks pre;
    pre.resolveObjectPlacement = [&objectHookAlive](const sim::ObjectRec&,
                                                    CityPlacement&) {
        objectHookAlive = true;
        return false;
    };
    view.SetHooks(std::move(pre));

    MakeLivePerson(0, 930, 5, 0, 0, /*homeBld=*/0);      // no record anchor

    // anchorProvider replaces the +0x16C/+0x170 column read.
    play::SessionPersons3DOptions po;
    po.mountAnims = false;
    po.anchorProvider = [](const sim::Person& p) -> i32 {
        return p.id == 930 ? 41 : 0;
    };
    play::SessionPersons3DStatus st = play::WireSessionPersons3D(view, nullptr, po);
    CHECK_EQ(1, st.bound);
    CHECK_EQ(0, st.unplaced);
    CHECK(!st.animsMounted);
    CHECK_EQ(1, st.modelUnresolved);          // no archives in the unit tier
    CHECK_EQ(0, st.posed);
    CHECK_EQ(10.0f, view.boundPersons()[0].place.pos[0]);

    // The object hook is still installed (BindWorldObjects routes through it).
    sim::g_objects[0].alive = 1;
    sim::g_objects[0].id = 41;
    view.BindWorldObjects();
    CHECK(objectHookAlive);

    // Update with nothing poseable is a safe no-op.
    CHECK_EQ(0, play::UpdateSessionPersons3D(view, 8.0f));

    // A full placement override wins over the anchor default.
    po.placementOverride = [](const sim::Person&, CityPlacement& out) {
        out.pos[0] = -5.0f; out.pos[1] = 1.0f; out.pos[2] = 7.0f;
        return true;
    };
    st = play::WireSessionPersons3D(view, nullptr, po);
    CHECK_EQ(1, st.bound);
    CHECK_EQ(-5.0f, view.boundPersons()[0].place.pos[0]);

    // Rebind follows the live sim: a second person appears.
    MakeLivePerson(1, 931, 5, 1, 0, 0);
    st = play::RebindSessionPersons3D(view, po);
    CHECK_EQ(2, st.bound);

    // Unwire: the view is persons-free again and renders as before.
    play::UnwireSessionPersons3D(view);
    CHECK_EQ(0u, (unsigned)view.boundPersons().size());
    CityView3D::Options opt;
    opt.fbW = 32; opt.fbH = 24;
    CityView3D::Result r = view.RenderFrame(play::CityCamera3D{}, opt);
    CHECK_EQ(0, r.personInstances);

    ResetWorld();
}

// ---------------------------------------------------------------------------
// W10-SIM hardening edges: degenerate person populations.
// ---------------------------------------------------------------------------

// ZERO persons: wire / update / rebind / unwire over an empty person array all
// produce empty, crash-free results (no iteration over an unbound roster).
TEST(SessionPersons3D, ZeroPersonsWireUpdateRebindUnwire) {
    ResetWorld();                          // every slot marker == -1 (no live person)
    std::vector<SceneObjectInst> nodes;
    nodes.push_back(Node("gb_HAUS", -1, 0, 0, 0, 41));
    CityView3D view;
    CHECK(view.LoadCityFromNodes(nodes));

    play::SessionPersons3DOptions po;
    po.mountAnims = false;
    play::SessionPersons3DStatus st = play::WireSessionPersons3D(view, nullptr, po);
    CHECK_EQ(0, st.bound);
    CHECK_EQ(0, st.unplaced);
    CHECK_EQ(0, st.posed);
    CHECK_EQ(0u, (unsigned)view.boundPersons().size());

    CHECK_EQ(0, play::UpdateSessionPersons3D(view, 16.0f));   // nothing to advance
    st = play::RebindSessionPersons3D(view, po);
    CHECK_EQ(0, st.bound);
    play::UnwireSessionPersons3D(view);
    CHECK_EQ(0u, (unsigned)view.boundPersons().size());

    // A frame over zero persons renders clean.
    CityView3D::Options opt; opt.fbW = 16; opt.fbH = 16;
    CityView3D::Result r = view.RenderFrame(play::CityCamera3D{}, opt);
    CHECK_EQ(0, r.personInstances);
    ResetWorld();
}

// MAX persons: fill the whole person capacity, all anchored to one building, and
// bind without a cap (binds the full live roster) then with a small cap. Drives
// the bind loop across the full kPersonCapacity bound (ASAN exercises the array).
TEST(SessionPersons3D, MaxPersonsBindCapAndFull) {
    ResetWorld();
    std::vector<SceneObjectInst> nodes;
    nodes.push_back(Node("city", -1, 0, 0, 0));
    nodes.push_back(Node("gb_HAUS", 0, 100, 0, 50, 41));    // owner-id 41
    nodes.push_back(Node("dummy_TUER", 1, 3, 0, 4));
    CityView3D view;
    CHECK(view.LoadCityFromNodes(nodes));

    for (int i = 0; i < sim::kPersonCapacity; ++i)
        MakeLivePerson(i, 1000 + i, 5, (u8)(i & 1), 0x02, /*homeBld=*/41);

    // Full bind: every live person binds (all anchored -> none unplaced).
    const int bound = view.BindPersons();
    CHECK_EQ(sim::kPersonCapacity, bound);
    CHECK_EQ(sim::kPersonCapacity, (int)view.boundPersons().size());
    CHECK_EQ(0, view.personsUnplaced());

    // Capped bind: at most N regardless of the live count.
    CHECK_EQ(8, view.BindPersons(8));
    CHECK_EQ(8u, (unsigned)view.boundPersons().size());
    // Cap larger than the population yields the population.
    CHECK_EQ(sim::kPersonCapacity, view.BindPersons(sim::kPersonCapacity + 100));

    ResetWorld();
}

// A person with NO anchor (no home/work columns, no provider) is never given an
// invented seat: it counts as unplaced and binds zero, and the move-placement
// path (MovePlacement with no seat captured) leaves it unplaced too.
TEST(SessionPersons3D, NoAnchorPersonStaysUnplaced) {
    ResetWorld();
    std::vector<SceneObjectInst> nodes;
    nodes.push_back(Node("gb_HAUS", -1, 0, 0, 0, 41));
    CityView3D view;
    CHECK(view.LoadCityFromNodes(nodes));

    MakeLivePerson(0, 940, 5, 0, 0x02, /*homeBld=*/0, /*workBld=*/0);  // no anchor

    play::SessionPersons3DOptions po;
    po.mountAnims = false;
    play::SessionPersons3DStatus st = play::WireSessionPersons3D(view, nullptr, po);
    CHECK_EQ(0, st.bound);
    CHECK_EQ(1, st.unplaced);
    CHECK_EQ(0u, (unsigned)view.boundPersons().size());
    play::UnwireSessionPersons3D(view);
    ResetWorld();
}
