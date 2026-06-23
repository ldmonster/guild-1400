// Wave 27 P5 — COMBAT / INTRIGUE (attack, sabotage, theft, crime) vertical slice.
// See slice_combat.h.
//
// The whole hostile-action loop: load -> classify a click on a rival unit/person ->
// REAL command (attack opcode-80 via BuildAttackPacket 0x488a4c, OR a crime via
// ExAddStraftat 0x498f44) -> REAL apply mutating the folded g_objects order pad /
// g_crimeTable + g_relationMatrix -> run a real game-day -> prove the combat/crime
// state evolved and the run is deterministic. Every link is a CALL into an
// already-reconstructed sibling (or a faithful in-place port of ExAddStraftat's
// record-write core); this file defines no new world state.
//
// REUSED (extern, never redefined — ODR):
//   app::MountRealGameAssets / RealCityPath              (app/real_boot.h)
//   io::LoadWorld / VfsShutdown                          (io/save_world_load.h, io/vfs.h)
//   sim::BuildAttackPacket / RequestBuildOp80 / CommandQueue
//                                            (sim/combat_packets.h, sim/command.h)
//   sim::BuildingFindById / g_objects / ObjectRec        (sim/entity.h, sim/types.h)
//   world::StraftatFindFreeSlot / g_crimeTable           (world/crime.h, world/law_types.h)
//   world::RelationGet / RelationSet / g_relationMatrix  (world/relation.h)
//   world::CityInitParameterTable                        (world/city.h)
//   play::RunGameDay / SeedGameDay / GameDayState        (play/game_day.h)
//   play::HashFullWorld                                  (play/world_digest.h)
//   sim::ResetEntityArrays                               (sim/entity.h)
//   crt::Srand                                           (crt/rand.h)
#include "play/slice_combat.h"

#include <cstring>
#include <vector>

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save_person.h"   // kPlantBytes / kKindPlant
#include "io/vfs.h"
#include "play/game_day.h"
#include "play/world_digest.h"
#include "sim/command.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/building_lifecycle.h"  // g_buildingPersons
// The full set of live world tables HashFullWorld folds — zeroed before a load so the
// slice's hashes are a pure function of (loaded city + seed). (Same set
// world_digest.cpp folds; mirrors playable_slice.cpp / slice_council.cpp.)
#include "sim/building.h"
#include "sim/building_create.h"
#include "sim/building_production.h"
#include "sim/actionqueue.h"
#include "sim/command_apply5.h"
#include "sim/command_apply6.h"
#include "world/city.h"
#include "world/law.h"
#include "world/event.h"
#include "world/office.h"
#include "world/office_assign.h"
#include "world/crime.h"
#include "world/relation.h"
#include "crt/rand.h"

namespace guild::play {

// Bounded relation snapshot read. world::RelationGet (gilde.exe 0x5942fc) indexes
// the flat 768x768 grid as g_relationMatrix[768*a + b] with NO internal bound, so a
// degenerate out-of-range click id (e.g. a stale/garbage victim/perpetrator id) is a
// genuine OOB read. ApplyCrimeCommand already gates the WRITE on kRelationDim; the
// slice's before/after SNAPSHOTS must gate the READ identically. For in-range pairs
// this returns exactly RelationGet (goldens unchanged); an out-of-range pair has no
// stored relation, so we report the unbound default 0 (the zero-initialized matrix
// cell value) without touching memory. (RelationGet/RelationSet themselves lack the
// bound — DOCUMENTED for the world/relation owner; not editable here.)
namespace {
int RelationGetSafe(i32 a, i32 b) {
    if (a < 0 || b < 0 || a >= world::kRelationDim || b >= world::kRelationDim)
        return 0;
    return world::RelationGet(a, b);
}
} // namespace

// The order-pad offsets the op-80 apply writes (same as input_command.cpp).
namespace {
constexpr int kAppliedKindOff  = 0x60;  // order-kind byte
constexpr int kAppliedDestXOff = 0x64;  // destination tile X (dword)
constexpr int kAppliedDestZOff = 0x68;  // destination tile Z (dword)
constexpr u8  kOrderKindAttack = 2;     // LOBYTE(v15[1]) for attack (kOrderPacketAttack)
}  // namespace

// ===========================================================================
// ATTACK packet build — drive the REAL sim::BuildAttackPacket over a standalone
// CommandQueue. The CombatOrderContext models the heightmap/object-def lookups the
// original calls; we feed it the deterministic projections so the packet is built
// (the original early-outs on a null target/attacker or a failed world->tile).
// ===========================================================================
CombatPacket BuildCombatPacket(const CombatInteraction& it) {
    CombatPacket pkt;
    if (it.action != HostileAction::kAttack)
        return pkt;
    if (it.targetId == 0 || it.attackerId == 0)
        return pkt;   // BuildAttackPacket early-outs on null target/attacker

    sim::CommandQueue q;
    q.Init();
    q.set_standalone(true);

    sim::CombatOrderHandle h;
    h.slotKey   = 1;          // *handle  (FindOrAllocSlot key, non-zero)
    h.op80Owner = it.attackerId;

    sim::CombatOrderContext ctx{};
    // VIBE_Combat_FindObjectDef: report a default melee weapon class (0 -> the
    // generic attack branch, which packs target+attacker straight in).
    ctx.findObjectDef = [](i32, i16& wc, u8& wsc) { wc = 0; wsc = 0; return true; };
    // VIBE_Heightmap_WorldToTileWithHeight: project to the attack tile.
    ctx.worldToTile = [&it](float, float, float, i32& tx, i32& tz) {
        tx = it.attackTileX; tz = 0; return true;
    };
    // VIBE_Combat_FindActiveTarget: not used by the generic (weaponClass 0) branch.
    ctx.findActiveTarget = [](i32, bool& ht, bool& hs) { ht = true; hs = true; };
    ctx.findOrAllocSlot  = [](i32, i32) { return true; };

    i32 ring = sim::BuildAttackPacket(q, h, it.targetId, it.attackerId,
                                      it.attackParam, /*secondaryByteIn123=*/false, ctx);
    if (ring < 0)
        return pkt;   // BuildAttackPacket early-out (-1)

    pkt.opcode    = 80;
    pkt.orderKind = kOrderKindAttack;
    pkt.target    = it.targetId;
    pkt.attacker  = it.attackerId;
    pkt.ringSlot  = ring;
    pkt.built     = true;
    return pkt;
}

// ===========================================================================
// Op-80 ATTACK apply dispatch hook (the lockstep handler -> pad write).
// ===========================================================================
namespace {
const CombatApplyHooks* g_combatHooks = nullptr;
}  // namespace

void SetCombatApplyHooks(const CombatApplyHooks* hooks) { g_combatHooks = hooks; }

int ApplyCombatPacket(const CombatPacket& pkt, const CombatInteraction& it) {
    if (!pkt.built || pkt.opcode != 80)
        return 0;

    // Resolve the target's g_objects slot: via the hook (tests can override) else
    // the REAL BuildingFindById id scan.
    sim::ObjectRec* rec = nullptr;
    if (g_combatHooks && g_combatHooks->resolveSlot) {
        int slot = g_combatHooks->resolveSlot(pkt.target, g_combatHooks->ctx);
        if (slot >= 0 && slot < sim::kObjectCapacity)
            rec = &sim::g_objects[slot];
    } else {
        rec = sim::BuildingFindById(pkt.target);
    }
    if (!rec)
        return 0;

    // Write (kind, destX, destZ) into the target's record pad — the observable
    // "the unit took the attack order" mutation (same pad the op-80 handler folds).
    u8* base = reinterpret_cast<u8*>(rec);
    base[kAppliedKindOff] = pkt.orderKind;
    i32 destX = it.attackTileX;
    i32 destZ = 0;
    std::memcpy(base + kAppliedDestXOff, &destX, sizeof destX);
    std::memcpy(base + kAppliedDestZOff, &destZ, sizeof destZ);
    return 1;
}

// ===========================================================================
// gilde.exe 0x498f44 — VIBE_Command_ExAddStraftat: the CRIME apply, record-write
// core. Grab a free crime slot, copy the staged 45-byte crime record into
// g_crimeTable[slot], stamp the crime id, and sour the perpetrator->victim relation.
//   v8 = crime id (dword_632240); slot = VIBE_Straftat_FindFreeSlot();
//   qmemcpy(&dword_11BC760 + 45*slot, a1+16, 0x2D);   // copy 45-byte record
//   *(int*)(&dword_11BC760 + 45*slot) = v8;           // stamp id at +0
//   string: taeter = *(a1+38) (+22), opfer = *(a1+34) (+18)
// The original's side cascade (VIBE_Beweis_Add evidence, VIBE_City_AddCrimeToGrid,
// VIBE_History_NotifyCrimeAdded) runs over handler-filter scans (VIBE_He_*) that
// have no headless analogue; we model the CORE folded-table mutation (g_crimeTable
// record + the relation hit g_relationMatrix) and SAY SO in the brief report.
// ===========================================================================
int ApplyCrimeCommand(const CombatInteraction& it, i32 crimeId) {
    if (it.action != HostileAction::kCrime)
        return -1;

    int slot = world::StraftatFindFreeSlot();   // VIBE_Straftat_FindFreeSlot 0x4c3390
    if (slot < 0)
        return -1;

    // Build + copy the 45-byte crime record (the a1+16 staging block) into the table.
    world::CrimeRecord& cr = world::g_crimeTable[slot];
    cr = world::CrimeRecord{};                 // staged block starts zeroed
    cr.id          = crimeId;                  // *(+0) = v8 (the stamped crime id)
    cr.perpetrator = it.perpetratorId;         // +22 (taeter)
    cr.wanted      = 1;                         // +26 wanted counter (one open offense)
    cr.location    = it.crimeLocation;         // +28 location byte
    cr.target      = it.victimId;              // +33 target/victim id
    cr.provenState = 1;                         // +37 (1 == proven/active; != 0 occupied)
    // opfer (+18) lives in pad4[18-4 .. ]; stamp the victim id at the +18 slot too so
    // the record carries the "opfer" the sprintf reports (pad4 covers +4..+21).
    std::memcpy(reinterpret_cast<u8*>(&cr) + 18, &it.victimId, sizeof(i32));

    // The crime sours the perpetrator's standing toward the victim (the relation
    // matrix the AI/grid consult). Bump the folded g_relationMatrix cell downward.
    if (it.perpetratorId >= 0 && it.victimId >= 0 &&
        it.perpetratorId < world::kRelationDim && it.victimId < world::kRelationDim &&
        it.perpetratorId != it.victimId) {
        int before = world::RelationGet(it.victimId, it.perpetratorId);
        int after = before - 10;
        if (after < -127) after = -127;
        world::RelationSet(it.victimId, it.perpetratorId, after);
    }
    return slot;
}

// ===========================================================================
// Shared helpers (mirror slice_council.cpp / playable_slice.cpp).
// ===========================================================================
namespace {

// Zero EVERY live world table HashFullWorld folds, to a clean blank slate. Called
// BEFORE io::LoadWorld so the loaded-city tables repopulate identically each run and
// the combat/crime tables start blank, making the whole-world hash reproducible
// across reruns in one process. (Brief: RunGameDay dirties g_crime/g_relation, so
// zero+repopulate.)
void ZeroWorldGlobals() {
    using std::memset;
    memset(sim::g_objects, 0, sizeof(sim::g_objects));
    memset(sim::g_persons, 0, sizeof(sim::g_persons));
    memset(sim::g_personIds, 0, sizeof(sim::g_personIds));
    memset(sim::g_sceneNodes, 0, sizeof(sim::g_sceneNodes));
    memset(sim::g_buildingPersons, 0, sizeof(sim::g_buildingPersons));
    memset(sim::g_buildingTypes, 0, sizeof(sim::g_buildingTypes));
    sim::g_buildingTypesLoaded = false;
    sim::g_buildingNextId = 0;
    memset(sim::g_sceneTypes, 0, sizeof(sim::g_sceneTypes));
    sim::g_sceneTypesLoaded = false;
    memset(sim::g_sceneTypeRemap, 0, sizeof(sim::g_sceneTypeRemap));
    memset(sim::g_prodStore, 0, sizeof(sim::g_prodStore));
    memset(sim::g_prodSchedules, 0, sizeof(sim::g_prodSchedules));
    memset(world::g_cities, 0, sizeof(world::g_cities));
    memset(world::g_goods, 0, sizeof(world::g_goods));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;
    memset(&sim::g_sysGameTime, 0, sizeof(sim::g_sysGameTime));
    memset(&sim::g_tickClock, 0, sizeof(sim::g_tickClock));
    sim::g_tickSubCounter = 0;
    sim::g_gameTick = 0;
    sim::g_currentPlayer = 0;
    sim::g_sysActivePlayer = 0;
    // --- politics + the combat/crime tables (the focus of this slice) ---
    memset(world::g_lawTable, 0, sizeof(world::g_lawTable));
    memset(world::g_eventTable, 0, sizeof(world::g_eventTable));
    world::g_eventTableCount = 0;
    world::g_missionLcgState = 0;
    memset(world::g_officeHolders, 0, sizeof(world::g_officeHolders));
    memset(world::g_crimeTable, 0, sizeof(world::g_crimeTable));
    memset(world::g_relationMatrix, 0, sizeof(world::g_relationMatrix));
}

// Seed the economy parameter table + RNG to a deterministic baseline.
void ResetWorldBaseline(std::uint32_t baselineSeed) {
    crt::Srand(baselineSeed);
    world::CityInitParameterTable(100.0f);
    world::g_capDivisor = 0.0f;
}

// Stabilize the kind-30 plantmap heap-pointer column (a fresh heap ptr per load).
void NormalizePlantPointers(std::uint32_t objectCount) {
    using namespace guild::sim;
    static std::vector<guild::u8> plantScratch(io::kPlantBytes, 0);
    guild::u8* scratch = plantScratch.data();
    for (std::uint32_t i = 0; i < objectCount && i < (std::uint32_t)kObjectCapacity; ++i) {
        guild::u8* r = reinterpret_cast<guild::u8*>(&g_objects[i]);
        if (r[0] != io::kKindPlant)
            continue;
        std::memcpy(r + 113, &scratch, sizeof scratch);
    }
}

// Seat a synthetic ATTACK target object into g_objects slot 0 (alive, id == targetId)
// so the real BuildingFindById resolves it and the op-80 apply has a pad to write.
void SeatAttackTarget(const CombatInteraction& it) {
    sim::ObjectRec& o = sim::g_objects[0];
    o = sim::ObjectRec{};
    o.alive = 1;                 // non-zero == live slot
    o.id    = it.targetId;       // BuildingFindById matches on id
}

// Install the slice's combat apply resolver pinning the attack target to slot 0.
i32 g_sliceTargetId = -1;
int SliceResolveSlot(i32 targetId, void*) {
    return (targetId == g_sliceTargetId) ? 0 : -1;
}
void InstallCombatResolver(const CombatInteraction& it) {
    g_sliceTargetId = it.targetId;
    static CombatApplyHooks hooks;
    hooks.resolveSlot = &SliceResolveSlot;
    hooks.ctx = nullptr;
    SetCombatApplyHooks(&hooks);
}

// A deterministic crime id (the original draws dword_632240; we use a fixed seed-
// derived id so the slice is byte-reproducible across reruns).
i32 SliceCrimeId(std::uint32_t seed) { return static_cast<i32>(1000 + (seed & 0xFFFF)); }

// Run the CLICK->COMMAND hostile step over the live world. Fills the command fields
// of `r` recording the folded field before/after.
void RunCombatCommand(CombatSliceResult& r, const CombatInteraction& it,
                      std::uint32_t seed) {
    r.action = it.action;
    if (it.action == HostileAction::kAttack) {
        r.targetSlot    = 0;   // SeatAttackTarget put it in slot 0
        u8* base = reinterpret_cast<u8*>(&sim::g_objects[0]);
        r.padKindBefore = base[kAppliedKindOff];

        CombatPacket pkt = BuildCombatPacket(it);
        r.attackBuilt     = pkt.built;
        r.attackOpcode    = pkt.opcode;
        r.attackOrderKind = pkt.orderKind;
        r.attackRingSlot  = pkt.ringSlot;

        int applied = ApplyCombatPacket(pkt, it);
        r.attackApplied = (applied == 1);
        r.padKindAfter  = base[kAppliedKindOff];
    } else if (it.action == HostileAction::kCrime) {
        r.relationBefore = RelationGetSafe(it.victimId, it.perpetratorId);
        i32 cid = SliceCrimeId(seed);
        int slot = ApplyCrimeCommand(it, cid);
        r.crimeSlot     = slot;
        r.crimeId       = cid;
        r.crimePerp     = it.perpetratorId;
        r.crimeVictim   = it.victimId;
        r.relationAfter = RelationGetSafe(it.victimId, it.perpetratorId);
    }
}

}  // namespace

// ===========================================================================
// RunCombatSlice — the whole hostile-action loop over a REAL city.
// ===========================================================================
CombatSliceResult RunCombatSlice(shim::IFileSystem* fs, const std::string& gameDir,
                                 const std::string& cityName,
                                 const CombatInteraction& it,
                                 std::uint32_t seed) {
    CombatSliceResult r;
    if (!fs)
        return r;

    // --- LOAD (mount real assets + io::LoadWorld into the live arrays) --------
    app::RealGameAssets assets =
        app::MountRealGameAssets(fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    if (!assets.vfsBound) {
        io::VfsShutdown();
        return r;
    }
    ZeroWorldGlobals();
    sim::ResetEntityArrays();
    const std::string cityPath = app::RealCityPath(cityName);
    io::WorldState world{};
    r.loaded = io::LoadWorld(cityPath.c_str(), world);
    if (!r.loaded) {
        io::VfsShutdown();
        return r;
    }
    r.personCount = world.cityRecCount;
    r.objectCount = world.objectCount;
    NormalizePlantPointers(world.objectCount);

    // --- deterministic baseline + the seated target / resolver ---------------
    ResetWorldBaseline(seed);
    if (it.action == HostileAction::kAttack) {
        SeatAttackTarget(it);
        InstallCombatResolver(it);
    }

    // --- HashFullWorld after load (anchor RNG so the hash is reproducible) ----
    crt::Srand(seed);
    r.hashAfterLoad = HashFullWorld();

    // --- CLICK -> REAL COMMAND -> REAL APPLY ---------------------------------
    RunCombatCommand(r, it, seed);
    crt::Srand(seed);
    r.hashAfterCommand = HashFullWorld();

    // --- GAME-DAY (the REAL per-day cascade; seeded for determinism) ----------
    crt::Srand(seed);
    GameDayState st = SeedGameDay(seed);
    GameDayDeltas d = RunGameDay(seed, st);
    r.daySteps = d.stepsRun;
    crt::Srand(seed);
    r.hashAfterDay = HashFullWorld();

    io::VfsShutdown();
    return r;
}

// ===========================================================================
// RunCombatStepsSynthetic — the same step sequence on a synthetic live world.
// ===========================================================================
int RunCombatStepsSynthetic(std::uint32_t seed, const CombatInteraction& it,
                            CombatStepHash* out, int cap,
                            i32* outBefore, i32* outAfter) {
    if (!out || cap < 3)
        return 0;

    // --- kSeed: blank slate + a deterministic baseline + the target ----------
    ZeroWorldGlobals();
    sim::ResetEntityArrays();
    ResetWorldBaseline(seed);
    if (it.action == HostileAction::kAttack) {
        SeatAttackTarget(it);
        InstallCombatResolver(it);
    }
    crt::Srand(seed);
    std::uint64_t hSeed = HashFullWorld();
    if (outBefore) {
        if (it.action == HostileAction::kAttack)
            *outBefore = reinterpret_cast<u8*>(&sim::g_objects[0])[kAppliedKindOff];
        else
            *outBefore = RelationGetSafe(it.victimId, it.perpetratorId);
    }

    // --- kCommand: build + apply the real hostile command --------------------
    {
        if (it.action == HostileAction::kAttack) {
            CombatPacket pkt = BuildCombatPacket(it);
            ApplyCombatPacket(pkt, it);
        } else {
            ApplyCrimeCommand(it, SliceCrimeId(seed));
        }
    }
    crt::Srand(seed);
    std::uint64_t hCmd = HashFullWorld();
    if (outAfter) {
        if (it.action == HostileAction::kAttack)
            *outAfter = reinterpret_cast<u8*>(&sim::g_objects[0])[kAppliedKindOff];
        else
            *outAfter = RelationGetSafe(it.victimId, it.perpetratorId);
    }

    // --- kDay: the real per-day cascade --------------------------------------
    {
        crt::Srand(seed);
        GameDayState st = SeedGameDay(seed);
        RunGameDay(seed, st);
    }
    crt::Srand(seed);
    std::uint64_t hDay = HashFullWorld();

    out[0] = {CombatStep::kSeed,    hSeed, false};
    out[1] = {CombatStep::kCommand, hCmd,  hCmd != hSeed};
    out[2] = {CombatStep::kDay,     hDay,  hDay != hCmd};
    return 3;
}

}  // namespace guild::play
