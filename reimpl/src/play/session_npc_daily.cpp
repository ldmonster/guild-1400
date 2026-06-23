// =============================================================================
// guild::play — session_npc_daily implementation. See the header for the chain
// (BeginPlayerRound @0x533188 -> NpcDaily_DailyRoutineStep @0x4e7e88 -> the
// wire_npc_actions real-leaf bridge -> the wire_npc_movement path-follow) and
// the named provider stand-ins. Everything routes through PUBLIC sibling APIs;
// no owned module is edited.
// =============================================================================
#include "play/session_npc_daily.h"

#include "play/city_view3d.h"        // CityView3D::boundObjects/boundPersons
#include "play/wire_npc_actions.h"   // InstallRealNpcActions + target provider
#include "play/wire_npc_movement.h"  // SetEntityDestination (the movement bridge)
#include "render/heightmap.h"        // WorldToTileWithHeight @0x5c6644
#include "sim/building.h"            // g_buildingTypes (dword_13CE294, 589-stride)
#include "sim/command_apply6.h"      // g_tickClock (qword_13CE852 world clock)
#include "sim/entity.h"              // g_persons, BuildingFindById
#include "sim/he.h"                  // HeRecord + He_State/He_SavedTime accessors
#include "sim/npc_daily.h"           // NpcDaily_DailyRoutineStep + turn-bit masks
#include "sim/npcaction.h"           // NpcClock / SetNpcClock (the clock image)
#include "sim/types.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace guild::play {

namespace {

// The view the provider closures resolve building placements through while a
// pass runs (function pointers can carry no captures). Process-static, set for
// the duration of SessionNpcDailyAssign only.
CityView3D* g_view = nullptr;

// ---------------------------------------------------------------------------
// Raw person-record column reads (the exact byte offsets the director's
// columns live at — see sim/npc_daily.h).
// ---------------------------------------------------------------------------
inline i32 PersonColI32(int slot, int off) {
    i32 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(&sim::g_persons[slot]) + off,
                sizeof v);
    return v;
}
inline u32 PersonColU32(int slot, int off) {
    u32 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(&sim::g_persons[slot]) + off,
                sizeof v);
    return v;
}

// The building id's REAL bound city placement (the owner-id scene-node match
// BindWorldObjects performs — RebuildModelByOwner @0x5a8140). Null when the
// building has no bound placement in the live view.
const CityView3D::BoundObject* FindBoundBuilding(i32 id) {
    if (!g_view || id == 0)
        return nullptr;
    for (const CityView3D::BoundObject& b : g_view->boundObjects())
        if (b.id == id)
            return &b;
    return nullptr;
}

// ---------------------------------------------------------------------------
// The target provider (the render/entity-coupled searches the FINDINGS name —
// every slot derives from the REAL live records; see the header).
// ---------------------------------------------------------------------------

// findCarryTarget / findInteractionTarget: the person's workBld column (+368,
// dword_12CEA80) when it resolves to a real bound placement.
int PvFindTarget(int i, i32* outU, i32* outObj) {
    if (i < 0 || i >= sim::kPersonCapacity)
        return 0;
    const i32 work = PersonColI32(i, 368);
    if (work <= 0 || !FindBoundBuilding(work))
        return 0;
    *outU = work;
    *outObj = work;
    return 1;
}

// destDoorIds: the destBld column (+388, dword_12CEA94) as the "already there"
// comparison pair (the building's +44/+48 door dwords ride the unreconstructed
// building runtime; the id pair preserves the equality semantics).
bool PvDestDoorIds(int i, i32* out44, i32* out48) {
    if (i < 0 || i >= sim::kPersonCapacity)
        return false;
    const i32 dest = PersonColI32(i, 388);
    if (dest == 0)
        return false;
    *out44 = dest;
    *out48 = dest;
    return true;
}

// homeHasMesh: the home building (+364) has an owner-matched scene node (the
// observable of the original's homeBld->+97 mesh-root probe).
bool PvHomeHasMesh(int i) {
    if (i < 0 || i >= sim::kPersonCapacity)
        return false;
    const i32 home = PersonColI32(i, 364);
    const CityView3D::BoundObject* b = FindBoundBuilding(home);
    return b != nullptr && b->sceneIndex >= 0;
}

// ownerKind: byte_12CE912[536 * homeBld->+39] — the home building record's
// word +39 indexes the owner person's record; its kind byte is +2.
u8 PvOwnerKind(int i) {
    if (i < 0 || i >= sim::kPersonCapacity)
        return 0;
    const i32 home = PersonColI32(i, 364);
    sim::ObjectRec* rec = sim::BuildingFindById(home);
    if (!rec)
        return 0;
    u16 ownerSlot = 0;
    std::memcpy(&ownerSlot, reinterpret_cast<const u8*>(rec) + 39, 2);
    if (ownerSlot >= (u16)sim::kPersonCapacity)
        return 0;
    return reinterpret_cast<const u8*>(&sim::g_persons[ownerSlot])[2];
}

// aiPlayerClass: *(byte*)(dword_13CE294 + 589 * homeBld type) — the loaded
// A_Geb type record's first byte (state-1 social gating only).
u8 PvAiPlayerClass(int i) {
    if (i < 0 || i >= sim::kPersonCapacity || !sim::g_buildingTypesLoaded)
        return 0;
    const i32 home = PersonColI32(i, 364);
    sim::ObjectRec* rec = sim::BuildingFindById(home);
    if (!rec || rec->alive >= sim::kBuildingTypeCapacity)
        return 0;
    return reinterpret_cast<const u8*>(&sim::g_buildingTypes[rec->alive])[0];
}

// workDistanceOk: DISASM-VERIFIED gate @0x4e80b2 — the original compares the
// 3D bone-chain distance between homeBld's scene node and the node of
// Building_FindById(charPtr->+44) (the char's CURRENT building) against
// 7000.0f (0x45DAC000; `jge` skip), and PASSES when either node is missing
// (v69 stays 0.0). This provider derives the pair from the live records
// (home column +364 vs the carry-target/work column +368 — the documented
// provider seam; the char's +44 rides the unreconstructed char runtime) and
// mirrors the missing-node pass-through.
bool PvWorkDistanceOk(int i) {
    if (i < 0 || i >= sim::kPersonCapacity)
        return false;
    const CityView3D::BoundObject* home = FindBoundBuilding(PersonColI32(i, 364));
    const CityView3D::BoundObject* work = FindBoundBuilding(PersonColI32(i, 368));
    if (!home || !work)
        return true;   // 0x4e800c/0x4e8012: no node -> v69 == 0.0 -> within range
    const float dx = home->place.pos[0] - work->place.pos[0];
    const float dy = home->place.pos[1] - work->place.pos[1];
    const float dz = home->place.pos[2] - work->place.pos[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz) < 7000.0f;
}

// characterBudgetOk: the live-actor cap (VIBE_Character_CountByOwner(0,0) < 32)
// over the view's bound person roster.
bool PvCharacterBudgetOk() {
    return g_view && (int)g_view->boundPersons().size() < 32;
}

// State-1 social leaves: the candidate gather (@0x4e8383 Person_QueryBegin
// cluster) and the tavern pick (@0x4e7c3c) are not derivable from the live
// records — report none (the evening pass still emits the go-home moves).
i32 PvCurrencyHeld(int) { return 0; }
int PvPickTavern(int, i32*, i32*) { return 0; }
int PvCandidateCount() { return 0; }

const NpcActionsTargetProvider kProvider = {
    /* findTarget        */ PvFindTarget,
    /* destDoorIds       */ PvDestDoorIds,
    /* homeHasMesh       */ PvHomeHasMesh,
    /* ownerKind         */ PvOwnerKind,
    /* aiPlayerClass     */ PvAiPlayerClass,
    /* workDistanceOk    */ PvWorkDistanceOk,
    /* characterBudgetOk */ PvCharacterBudgetOk,
    /* currencyHeld      */ PvCurrencyHeld,
    /* pickTavern        */ PvPickTavern,
    /* candidateCount    */ PvCandidateCount,
};

} // namespace

SessionNpcDailyResult SessionNpcDailyAssign(CityView3D& view,
                                            const render::Heightmap* hm) {
    SessionNpcDailyResult r;
    if (!hm || hm->size <= 0)
        return r;

    // 1. The director reads the clock through sim::NpcClock(); sync it from the
    //    WORLD clock qword_13CE852 (sim::g_tickClock — the record SessionTick's
    //    opcode-30 commits write; zeroed-clock sessions read hour 0, still
    //    inside every season's morning window).
    sim::SetNpcClock(sim::g_tickClock);

    // 2. Snapshot the +456 turn-bit column so the promotion sweep below can see
    //    exactly which persons THIS director step dispatched.
    std::vector<u32> bitsBefore((std::size_t)sim::kPersonCapacity, 0);
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        if (sim::g_persons[i].marker == -1)
            continue;
        bitsBefore[(std::size_t)i] = PersonColU32(i, 456);
    }

    // 3. The REAL director step over the REAL leaves: InstallRealNpcActions
    //    routes the column sweep / turn-bit writeback / production probe /
    //    command builds at the live arrays; the provider supplies the
    //    render-coupled searches (header stand-ins). He state 0 == the morning
    //    work-dispatch sweep BeginPlayerRound runs.
    const bool wasInstalled = RealNpcActionsInstalled();
    InstallRealNpcActions();
    SetNpcActionsTargetProvider(&kProvider);
    g_view = &view;

    sim::HeRecord rec{};
    sim::He_State(&rec) = 0;
    sim::He_SavedTime(&rec) = sim::NpcClock();
    sim::NpcDaily_DailyRoutineStep(&rec);

    SetNpcActionsTargetProvider(nullptr);
    if (!wasInstalled)
        UninstallRealNpcActions();

    // 4. Promotion sweep: every person whose dispatch bit was SET by this step
    //    (disasm-verified bits: work = 0x1000, social = 0x800 — BYTE1 of +456)
    //    gets its movement destination bound (the destination<->entity binding
    //    wire_npc_movement.h documents). Destination tile = the target
    //    building's real placement through WorldToTileWithHeight @0x5c6644;
    //    start tile = the person's bound seat (entrance dummy), else the home
    //    building's placement (they are at home at day start).
    const u32 dispMask = (u32)sim::kDailyDispWork | (u32)sim::kDailyDispSocial;
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        if (sim::g_persons[i].marker == -1)
            continue;
        const u32 now = PersonColU32(i, 456);
        const u32 gained = (now & dispMask) & ~bitsBefore[(std::size_t)i];
        if (!gained)
            continue;
        ++r.dispatched;

        // The dispatched target: the provider's carry target (workBld column).
        i32 u = 0, obj = 0;
        if (!PvFindTarget(i, &u, &obj))
            continue;
        const CityView3D::BoundObject* tgt = FindBoundBuilding(obj);
        if (!tgt)
            continue;

        // Start point: the person's bound seat, else its home building node.
        const float* startPos = nullptr;
        const i32 personId = sim::g_persons[i].id;
        for (const CityView3D::BoundPerson& bp : view.boundPersons()) {
            if (bp.id == personId) { startPos = bp.place.pos; break; }
        }
        if (!startPos) {
            const CityView3D::BoundObject* home =
                FindBoundBuilding(PersonColI32(i, 364));
            if (home)
                startPos = home->place.pos;
        }
        if (!startPos)
            continue;   // no evidenced start position -> not promoted (rule 8)

        int tx = 0, tz = 0, sx = 0, sz = 0;
        if (!render::WorldToTileWithHeight(hm, tgt->place.pos, &tx, &tz, nullptr))
            continue;
        if (!render::WorldToTileWithHeight(hm, startPos, &sx, &sz, nullptr))
            continue;
        if (SetEntityDestination(personId, tx, tz, sx, sz))
            ++r.assigned;
    }

    g_view = nullptr;
    return r;
}

} // namespace guild::play
