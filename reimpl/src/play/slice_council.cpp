// Wave 27 P5 — COURT / COUNCIL / OFFICE (politics) vertical slice. See
// slice_council.h.
//
// The whole politics loop: load -> seat a vacant council office -> click
// "Amtsbewerbung" (apply-for-candidacy) -> REAL opcode-68 command (RequestBuildOp68
// 0x495454) -> REAL apply (OfficeAssignToCandidate 0x47e4e0) mutating the folded
// g_officeHolders rank field -> run a real game-day -> prove the politics state
// evolved and the run is deterministic. Every link is a CALL into an
// already-reconstructed sibling; this file defines no new world state.
//
// REUSED (extern, never redefined — ODR):
//   app::MountRealGameAssets / RealCityPath            (app/real_boot.h)
//   io::LoadWorld / VfsShutdown                        (io/save_world_load.h, io/vfs.h)
//   world::OfficeAssignToCandidate / OfficeHolderTableReset / g_officeHolders
//                                                      (world/office_assign.h, office.h)
//   world::CityInitParameterTable                      (world/city.h)
//   play::RunGameDay / SeedGameDay / GameDayState      (play/game_day.h)
//   play::HashFullWorld                                (play/world_digest.h)
//   sim::ResetEntityArrays / g_objects / g_persons     (sim/entity.h)
//   crt::Srand                                          (crt/rand.h)
#include "play/slice_council.h"

#include <cstring>
#include <vector>

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save_person.h"   // kPlantBytes / kKindPlant (plant-pointer normalization)
#include "io/vfs.h"
#include "play/game_day.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/building_lifecycle.h"  // g_buildingPersons (base digest)
// The full set of live world tables HashFullWorld folds — zeroed before a load so the
// slice's hashes are a pure function of (loaded city + seed), independent of any
// prior run that dirtied these globals in this process. (Same set world_digest.cpp
// folds; mirrors playable_slice.cpp::ZeroWorldGlobals.)
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

// ===========================================================================
// CouncilPacket::encode — the on-wire image RequestBuildOp68 (0x495454) lays out.
// ===========================================================================
void CouncilPacket::encode(u8 out[8]) const {
    out[0] = opcode;                 // v3[0] = 68
    // qmemcpy(v4, a1, 4): the applicant id dword (v17 = *(person+4)).
    std::memcpy(out + 1, &applicant, 4);
    out[5] = holderA;                // a1+4 byte 0 (v18)
    out[6] = holderB;                // a1+4 byte 1 (v19)
    out[7] = officeType;             // a1+4 byte 2 (v20)
}

// ===========================================================================
// gilde.exe 0x495454 — VIBE_Command_RequestBuildOp68: build the candidacy packet.
// The original packs the byte-buffer the candidacy emit (ApplyForCandidacy 0x47e1b8)
// fills: { applicant id (v17), holderA (v18), holderB (v19), officeType (v20) } and
// stamps opcode 68. Classification: a kApplyCandidacy interaction with a valid
// office type yields a built packet; anything else is unbuilt.
// ===========================================================================
CouncilPacket BuildCouncilPacket(const CouncilInteraction& it) {
    CouncilPacket pkt;
    if (it.action != CouncilAction::kApplyCandidacy)
        return pkt;                          // unbuilt (opcode 0)
    if (it.officeType >= world::kOfficeDefCount)
        return pkt;                          // invalid office type

    pkt.opcode    = 68;                       // v3[0] = 68
    pkt.applicant = it.applicantId;           // v17 = *(person+4)
    pkt.holderA   = it.holderKey;             // v18
    pkt.holderB   = 0xFF;                      // v19 == none (single-slot path)
    pkt.officeType = it.officeType;            // v20
    pkt.built     = true;
    return pkt;
}

// ===========================================================================
// Op-68 apply dispatch hook (the lockstep handler -> OfficeAssignToCandidate).
// ===========================================================================
namespace {

// A single-record store backing the inert default: resolves exactly the applicant
// id the default hook is asked for (the live engine resolves via
// VIBE_Person_FindRecordById; tests can override with a richer store).
struct HookPersonStore : world::OfficePersonStore {
    world::OfficePersonRec* (*find)(i32, void*) = nullptr;
    void* ctx = nullptr;
    world::OfficePersonRec* Find(i32 id) override {
        return find ? find(id, ctx) : nullptr;
    }
};

const CouncilApplyHooks* g_hooks = nullptr;

} // namespace

void SetCouncilApplyHooks(const CouncilApplyHooks* hooks) { g_hooks = hooks; }

int ApplyCouncilPacket(const CouncilPacket& pkt) {
    if (!pkt.built || pkt.opcode != 68)
        return 0;
    if (!g_hooks || !g_hooks->find)
        return 0;   // no resolver -> the real apply cannot find the applicant record

    // Decode the packet into the AssignToCandidate request (the exact mapping
    // OfficeAssignToCandidate reads: candidate id, keyA, keyB, officeType).
    world::AssignRequest req;
    req.candidateId = pkt.applicant;
    req.keyA        = pkt.holderA;
    req.keyB        = pkt.holderB;
    req.officeType  = pkt.officeType;

    HookPersonStore store;
    store.find = g_hooks->find;
    store.ctx  = g_hooks->ctx;

    // The REAL opcode-68 apply: marks the applicant's +360 candidacy field and bumps
    // the matched g_officeHolders slot's rank counter.
    return world::OfficeAssignToCandidate(req, store);
}

// ===========================================================================
// Shared helpers (mirror playable_slice.cpp for hash reproducibility).
// ===========================================================================
namespace {

// Zero EVERY live world table HashFullWorld folds, to a clean blank slate. Called
// BEFORE io::LoadWorld so the loaded-city tables are repopulated identically each run
// and the politics tables (law/office/crime/relation) start blank in every run,
// making the whole-world hash reproducible across reruns in one process. (Same set
// as playable_slice.cpp::ZeroWorldGlobals; the brief calls out that the politics
// tables must be zeroed because RunGameDay dirties them.)
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
    // --- the politics tables (the focus of this slice) ---
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

// Stabilize the kind-30 plantmap heap-pointer column (it is a fresh heap ptr per
// load, which would make the raw-byte digest non-reproducible). Mirrors
// playable_slice.cpp::NormalizePlantPointers.
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

// Seat ONE vacant council office into g_officeHolders matching `it`, in slot 0:
//   holder == it.holderKey, type == it.officeType, city == -1 (vacant),
//   state == 3 (electable), rank == 0 (< 4, so a candidacy can seat).
// This is the council seat the player clicks "Amtsbewerbung" on. Returns the slot.
int SeatVacantOffice(const CouncilInteraction& it) {
    using namespace guild::world;
    OfficeHolder& s = g_officeHolders[0];
    s.holder    = it.holderKey;
    s.city      = -1;   // vacant assignment (CanRunForOffice / AssignToCandidate gate)
    s.type      = it.officeType;
    s.rank      = 0;    // < 4 -> assignable
    s.state     = 3;    // electable
    s.secondary = -1;
    return 0;
}

// The applicant person record the inert default apply hook resolves. A live,
// not-yet-candidate (office360 == 0) person whose id == the slice's applicant id.
world::OfficePersonRec g_sliceApplicant;
i32 g_sliceApplicantId = -1;

world::OfficePersonRec* SliceApplicantFind(i32 id, void*) {
    return (id == g_sliceApplicantId) ? &g_sliceApplicant : nullptr;
}

// Install the slice's default candidacy resolver around `it` (the applicant record
// the op-68 apply mutates).
void InstallSliceApplicant(const CouncilInteraction& it) {
    g_sliceApplicant = world::OfficePersonRec{};
    g_sliceApplicant.ownerId   = it.applicantId;
    g_sliceApplicant.office360 = 0;     // not yet a candidate
    g_sliceApplicant.valid     = true;
    g_sliceApplicantId = it.applicantId;

    static CouncilApplyHooks hooks;
    hooks.find = &SliceApplicantFind;
    hooks.ctx  = nullptr;
    SetCouncilApplyHooks(&hooks);
}

// Run the CLICK->COMMAND politics step over the live world: build the op-68 packet,
// apply the real OfficeAssignToCandidate, recording the folded g_officeHolders rank
// field before/after. Fills the command fields of `r`.
void RunCouncilCommand(CouncilSliceResult& r, const CouncilInteraction& it) {
    using namespace guild::world;
    int slot = 0;   // SeatVacantOffice put the seat in slot 0
    r.slotIndex      = slot;
    r.rankBefore     = g_officeHolders[slot].rank;
    r.candidacyBefore = g_sliceApplicant.office360;

    CouncilPacket pkt = BuildCouncilPacket(it);
    r.commandBuilt     = pkt.built;
    r.commandOpcode    = pkt.opcode;
    r.commandApplicant = pkt.applicant;
    r.commandOfficeType = pkt.officeType;

    int applied = ApplyCouncilPacket(pkt);
    r.commandApplied = (applied == 1);
    r.rankAfter      = g_officeHolders[slot].rank;
    r.candidacyAfter = g_sliceApplicant.office360;
}

} // namespace

// ===========================================================================
// RunCouncilSlice — the whole politics loop over a REAL city.
// ===========================================================================
CouncilSliceResult RunCouncilSlice(shim::IFileSystem* fs, const std::string& gameDir,
                                   const std::string& cityName,
                                   const CouncilInteraction& it,
                                   std::uint32_t seed) {
    CouncilSliceResult r;
    if (!fs)
        return r;

    // --- step: LOAD (mount real assets + io::LoadWorld into the live arrays) --
    app::RealGameAssets assets =
        app::MountRealGameAssets(fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    if (!assets.vfsBound) {
        io::VfsShutdown();
        return r;
    }
    ZeroWorldGlobals();              // blank slate (incl. the politics tables)
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

    // --- deterministic baseline + the seated council seat + the applicant ----
    ResetWorldBaseline(seed);
    SeatVacantOffice(it);
    InstallSliceApplicant(it);

    // --- HashFullWorld after load (anchor RNG so the hash is reproducible) ----
    crt::Srand(seed);
    r.hashAfterLoad = HashFullWorld();

    // --- step: CLICK -> OPCODE-68 COMMAND -> REAL APPLY ----------------------
    RunCouncilCommand(r, it);
    crt::Srand(seed);
    r.hashAfterCommand = HashFullWorld();

    // --- step: GAME-DAY (the REAL per-day cascade; seeded for determinism) ----
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
// RunCouncilStepsSynthetic — the same step sequence on a synthetic live world.
// ===========================================================================
int RunCouncilStepsSynthetic(std::uint32_t seed, const CouncilInteraction& it,
                             CouncilStepHash* out, int cap,
                             i32* outRankBefore, i32* outRankAfter) {
    if (!out || cap < 3)
        return 0;

    // --- kSeed: blank slate + a deterministic baseline + the vacant seat -----
    ZeroWorldGlobals();
    sim::ResetEntityArrays();
    ResetWorldBaseline(seed);
    SeatVacantOffice(it);
    InstallSliceApplicant(it);
    crt::Srand(seed);
    std::uint64_t hSeed = HashFullWorld();
    if (outRankBefore) *outRankBefore = world::g_officeHolders[0].rank;

    // --- kCommand: build op-68 + apply the real OfficeAssignToCandidate ------
    {
        CouncilPacket pkt = BuildCouncilPacket(it);
        ApplyCouncilPacket(pkt);
    }
    crt::Srand(seed);
    std::uint64_t hCmd = HashFullWorld();
    if (outRankAfter) *outRankAfter = world::g_officeHolders[0].rank;

    // --- kDay: the real per-day cascade --------------------------------------
    {
        crt::Srand(seed);
        GameDayState st = SeedGameDay(seed);
        RunGameDay(seed, st);
    }
    crt::Srand(seed);
    std::uint64_t hDay = HashFullWorld();

    out[0] = {CouncilStep::kSeed,    hSeed, false};
    out[1] = {CouncilStep::kCommand, hCmd,  hCmd != hSeed};
    out[2] = {CouncilStep::kDay,     hDay,  hDay != hCmd};
    return 3;
}

} // namespace guild::play
