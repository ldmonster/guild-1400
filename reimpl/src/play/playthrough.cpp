// Wave 28 PLAY P7 seed — multi-day SCRIPTED PLAYTHROUGH harness. See playthrough.h.
//
// The multi-day integrator: load -> seat the records the script needs -> for each
// game-day apply the day's scripted actions (via the REAL P5 slice apply entry
// points) then advance ONE real game-day (RunGameDay) -> record a per-day witness
// (HashFullWorld + real observables). Proves the run EVOLVES day to day, REPRODUCES
// byte-for-byte on a rerun, and DIVERGES with a different seed.
//
// Every gameplay link is a CALL into an already-reconstructed sibling; this file
// defines NO new gameplay and edits no existing .cpp. It only orchestrates and seeds
// the live records the slices' public applies operate on.
//
// REUSED (extern, never redefined — ODR):
//   app::MountRealGameAssets / RealCityPath              (app/real_boot.h)
//   io::LoadWorld / VfsShutdown                          (io/save_world_load.h, io/vfs.h)
//   play::ClassifyMarketInteraction / InstallMarketCommandHandler /
//         ReadBuildingTreasury / ReadBuildingStock       (play/slice_market.h)
//   play::BuildCouncilPacket / ApplyCouncilPacket / SetCouncilApplyHooks
//                                                        (play/slice_council.h)
//   play::ClassifyChurchInteraction / InstallChurchCommandHandler /
//         SeatChurchObject / ReadObjectMoney             (play/slice_church.h)
//   play::BuildCombatPacket / ApplyCombatPacket / SetCombatApplyHooks /
//         ApplyCrimeCommand                              (play/slice_combat.h)
//   play::RunGameDay / SeedGameDay / GameDayState        (play/game_day.h)
//   play::HashFullWorld                                  (play/world_digest.h)
//   sim::CommandQueue / BuildTradePacket(via classify)   (sim/command.h)
//   sim::g_objects / ResetEntityArrays / BuildingFindById(sim/entity.h, sim/types.h)
//   world::OfficeAssignToCandidate substrate / g_officeHolders / g_crimeTable /
//         CityInitParameterTable / Building_ComputeMarketPrice
//   crt::Srand                                           (crt/rand.h)
#include "play/playthrough.h"

#include <cstring>
#include <vector>

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save_person.h"   // kPlantBytes / kKindPlant (plant-pointer normalization)
#include "io/vfs.h"
#include "play/slice_market.h"
#include "play/slice_council.h"
#include "play/slice_church.h"
#include "play/slice_combat.h"
#include "play/game_day.h"
#include "play/world_digest.h"
#include "play/determinism.h"        // Fnv1a64
#include "sim/command.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/building_types.h"      // BuildingRec / fillLevel
#include "sim/building_production.h" // Building_ComputeMarketPrice / SceneTypeDefAt / g_sceneTypes
#include "sim/building_lifecycle.h"  // g_buildingPersons (base digest)
// The full set of live world tables HashFullWorld folds — zeroed before a load so the
// run's hashes are a pure function of (loaded city + script + seed). (Same set
// world_digest.cpp folds; mirrors playable_slice.cpp / slice_council.cpp.)
#include "sim/building.h"
#include "sim/building_create.h"
#include "sim/actionqueue.h"         // g_gameTick
#include "sim/command_apply5.h"      // g_sysGameTime / g_sysActivePlayer / g_currentPlayer
#include "sim/command_apply6.h"      // g_tickClock / g_tickSubCounter
#include "world/city.h"              // g_cities / g_goods / CityInitParameterTable / g_cityTotalMoney
#include "world/law.h"
#include "world/event.h"
#include "world/office.h"            // g_officeHolders
#include "world/office_assign.h"     // OfficePersonRec / OfficeAssignToCandidate
#include "world/crime.h"             // g_crimeTable / kCrimeCount
#include "world/relation.h"
#include "crt/rand.h"

namespace guild::play {

namespace {

// Crime-record provenState offset (+37): a slot is "occupied" when provenState != 0
// (world/crime.h: StraftatFindFreeSlot scans for provenState == 0).
constexpr int kCrimeProvenStateOff = 37;

// ===========================================================================
// Determinism scaffolding (the play-layer memory gotchas, shared with the slices).
// ===========================================================================

// Zero EVERY live world table HashFullWorld folds, to a clean blank slate. Called
// BEFORE io::LoadWorld / synthetic seeding so the loaded-city tables repopulate
// identically each run and every folded table the city/economy day dirties starts
// blank in every run -> the whole-world hash is a pure function of (city + script +
// seed) across reruns in one process. (Same exact set world_digest.cpp folds.)
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

// Stabilize the kind-30 plantmap heap-pointer column (a fresh heap ptr per load,
// which would make the raw-byte digest non-reproducible). Mirrors
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

// ===========================================================================
// Live-record seating — place the entities the scripted actions operate on into the
// live arrays (keyed off the script's ids). These mirror the per-slice test seeders
// exactly; the harness only PLACES the records, the slices' real applies mutate them.
// ===========================================================================

// Seat a market/contor building (kind-2) into the first free g_objects slot with a
// starting stock + zeroed treasury, and a priceable scene-type for `ware` so the REAL
// Building_ComputeMarketPrice returns a non-zero value. Idempotent per (buildingId).
// (Matches the play_slice_market test seeder.)
void SeatMarketBuilding(i32 buildingId, i16 ware) {
    using namespace guild::sim;
    if (BuildingFindById(buildingId))
        return;   // already seated
    for (int i = 0; i < kObjectCapacity; ++i) {
        if (g_objects[i].alive != 0)
            continue;
        ObjectRec& o = g_objects[i];
        o.alive = 1;
        o.id    = buildingId;
        auto* b = reinterpret_cast<BuildingRec*>(&o);
        b->fillLevel = 500;                  // a starting stock so a sell can remove
        i32 zero = 0;
        std::memcpy(reinterpret_cast<u8*>(&o) + kTreasuryFieldOff, &zero, sizeof zero);
        break;
    }
    // A priceable raw-good scene-type (category 23 -> the simple priced branch).
    g_sceneTypesLoaded = true;
    SceneTypeDef* td = SceneTypeDefAt(ware);
    if (td && td->kind == 0) {
        td->kind        = 23;
        td->baseValue   = 1000;
        td->divisor     = 10;
        td->cachedPrice = 0;
    }
}

// Seat ONE vacant council seat into g_officeHolders slot 0 (the slice's convention):
// type == officeType, city == -1 (vacant), rank 0 (< 4 assignable), state 3
// (electable). Returns nothing; slot 0 is the council seat the script applies to.
void SeatCouncilSeat(u8 officeType, u8 holderKey) {
    using namespace guild::world;
    OfficeHolder& s = g_officeHolders[0];
    s.holder    = holderKey;
    s.city      = -1;
    s.type      = officeType;
    s.rank      = 0;
    s.state     = 3;
    s.secondary = -1;
}

// The council applicant record the inert-default op-68 apply hook resolves. A live,
// not-yet-candidate person whose id == the applicant id. One per harness run (the
// script's council actions all stand for the same player character).
world::OfficePersonRec g_pcApplicant;
i32 g_pcApplicantId = -1;
world::OfficePersonRec* PcApplicantFind(i32 id, void*) {
    return (id == g_pcApplicantId) ? &g_pcApplicant : nullptr;
}
void InstallCouncilApplicant(i32 applicantId) {
    g_pcApplicant = world::OfficePersonRec{};
    g_pcApplicant.ownerId   = applicantId;
    g_pcApplicant.office360 = 0;     // not yet a candidate
    g_pcApplicant.valid     = true;
    g_pcApplicantId = applicantId;
    static CouncilApplyHooks hooks;
    hooks.find = &PcApplicantFind;
    hooks.ctx  = nullptr;
    SetCouncilApplyHooks(&hooks);
}

// The combat attack-target resolver: the script seats the target as a live object and
// pins the op-80 apply at its slot. Installed once per run; resolves whichever target
// id the harness seated (a per-run single-target store, matching slice_combat).
i32 g_combatTargetId = -1;
int g_combatTargetSlot = -1;
int CombatTargetResolve(i32 targetId, void*) {
    return (targetId == g_combatTargetId) ? g_combatTargetSlot : -1;
}
void SeatCombatTarget(i32 targetId) {
    using namespace guild::sim;
    if (BuildingFindById(targetId)) {
        // already seated; just (re)bind the resolver to its slot
        for (int i = 0; i < kObjectCapacity; ++i) {
            if (g_objects[i].alive != 0 && g_objects[i].id == targetId) {
                g_combatTargetId = targetId; g_combatTargetSlot = i; break;
            }
        }
    } else {
        for (int i = 0; i < kObjectCapacity; ++i) {
            if (g_objects[i].alive != 0) continue;
            g_objects[i] = ObjectRec{};
            g_objects[i].alive = 1;
            g_objects[i].id    = targetId;
            g_combatTargetId = targetId; g_combatTargetSlot = i;
            break;
        }
    }
    static CombatApplyHooks hooks;
    hooks.resolveSlot = &CombatTargetResolve;
    hooks.ctx = nullptr;
    SetCombatApplyHooks(&hooks);
}

// ===========================================================================
// Action appliers — each calls the matching slice's PUBLIC apply entry point on the
// already-loaded live world. NONE re-implement gameplay; they only marshal the
// scripted action into the slice's command/apply and let the real apply mutate.
// Returns true iff the action's command was issued/applied (a real mutation fired).
// ===========================================================================

bool ApplyMarketAction(const PlaythroughAction& a, std::uint32_t /*seed*/) {
    SeatMarketBuilding(a.targetId, a.ware);
    MarketInteraction mi;
    mi.side         = a.buy ? MarketSide::kBuy : MarketSide::kSell;
    mi.ware         = a.ware;
    mi.qty          = a.qty;
    mi.buildingId   = a.targetId;
    mi.buildingKind = 2;
    mi.player       = (u8)(a.actorId & 0xFF);

    MarketCommand cmd = ClassifyMarketInteraction(mi);   // real price oracle
    if (!cmd.issued)
        return false;

    // Build + enqueue + apply through the REAL CommandQueue opcode-17 codec (the same
    // path RunMarketSlice uses, minus its bundled game-day — the harness advances the
    // day itself so the command effect is isolated from the day cascade).
    sim::CommandQueue q;
    q.Init();
    q.set_standalone(true);
    InstallMarketCommandHandler(q);   // sets the opcode-17 handler + inert money/stock hooks
    SetMarketApplyHooks(nullptr);
    // Stage the opcode-17 trade packet from the classified command using the
    // documented QueueRequest17 layout (slice_market.h kTrade*Off), then enqueue +
    // exec through the REAL CommandQueue codec so the opcode-17 handler applies the
    // treasury + stock move. (RunMarketSlice bundles a game-day; the harness advances
    // the day itself, so it drives the command-only path here.)
    sim::CommandPacket pkt{};
    pkt.bytes[0] = kTradeCmdOpcode;                                 // 17
    pkt.put32(kTradeSellerOff, (u32)cmd.seller);                    // a1 seller
    pkt.put32(kTradeBuyerOff,  (u32)cmd.buyer);                     // a2 buyer (-1)
    {
        u16 proto = (u16)cmd.proto;
        std::memcpy(pkt.bytes + kTradeProtoOff, &proto, sizeof proto);  // a4 word
    }
    pkt.bytes[kTradePlayerOff] = cmd.player;                        // a5 byte
    pkt.put32(kTradeQtyOff,   (u32)cmd.qty);                        // a3 qty
    pkt.put32(kTradePriceOff, (u32)cmd.unitPrice);                 // a6 price
    i32 slot = q.EnqueuePacket(pkt);
    if (slot < 0)
        return false;
    q.FlushSendQueue();
    q.ExecCommands();   // -> the opcode-17 handler -> treasury + stock move
    return true;
}

bool ApplyCouncilAction(const PlaythroughAction& a, std::uint32_t /*seed*/) {
    SeatCouncilSeat(a.officeType, a.holderKey);
    InstallCouncilApplicant(a.actorId);
    CouncilInteraction it;
    it.action      = CouncilAction::kApplyCandidacy;
    it.applicantId = a.actorId;
    it.holderKey   = a.holderKey;
    it.officeType  = a.officeType;
    CouncilPacket pkt = BuildCouncilPacket(it);     // REAL opcode-68 builder
    int applied = ApplyCouncilPacket(pkt);          // REAL OfficeAssignToCandidate
    return applied == 1;
}

bool ApplyChurchAction(const PlaythroughAction& a, std::uint32_t /*seed*/) {
    // Seat the church (recipient) + donor account if absent, with starting money so a
    // debit has something to move. The church is `targetId`, donor is `actorId`.
    if (!sim::BuildingFindById(a.targetId))
        SeatChurchObject(a.targetId, /*money=*/0);
    if (!sim::BuildingFindById(a.actorId))
        SeatChurchObject(a.actorId, /*money=*/1000000);

    ChurchInteraction it;
    it.action       = ChurchAction::kDonate;
    it.churchId     = a.targetId;
    it.donorAccount = a.actorId;
    it.amount       = a.amount;
    it.currencyType = 0;
    ChurchCommand cmd = ClassifyChurchInteraction(it);
    if (!cmd.issued)
        return false;

    sim::CommandQueue q;
    q.Init();
    q.set_standalone(true);
    InstallChurchCommandHandler(q);                  // REAL opcode-15 apply + money leaves
    sim::CommandPacket pkt = cmd.Encode();
    i32 slot = q.EnqueuePacket(pkt);
    if (slot < 0)
        return false;
    q.FlushSendQueue();
    q.ExecCommands();   // -> ExRemapObjectPair -> currency donor->church
    return true;
}

bool ApplyCombatAction(const PlaythroughAction& a, std::uint32_t /*seed*/) {
    SeatCombatTarget(a.targetId);
    CombatInteraction it;
    it.action      = HostileAction::kAttack;
    it.attackerId  = a.actorId;
    it.targetId    = a.targetId;
    it.attackTileX = 7;
    it.attackParam = 1;
    CombatPacket pkt = BuildCombatPacket(it);        // REAL BuildAttackPacket (op-80)
    int applied = ApplyCombatPacket(pkt, it);        // op-80 pad write
    return applied == 1;
}

bool ApplyCrimeAction(const PlaythroughAction& a, std::uint32_t seed) {
    CombatInteraction it;
    it.action        = HostileAction::kCrime;
    it.perpetratorId = a.actorId;
    it.victimId      = a.targetId;
    it.crimeLocation = 0;
    it.crimeType     = (CrimeType)a.crimeType;
    // A deterministic, day+action-derived crime id so each crime gets a distinct id
    // but the run reproduces (the original draws a global counter; we derive it).
    i32 crimeId = (i32)(2000 + ((seed + (u32)a.onDay * 31u + (u32)a.actorId) & 0xFFFF));
    int slot = ApplyCrimeCommand(it, crimeId);       // REAL ExAddStraftat core
    return slot >= 0;
}

bool ApplyAction(const PlaythroughAction& a, std::uint32_t seed) {
    switch (a.kind) {
        case PlaythroughActionKind::kMarket:  return ApplyMarketAction(a, seed);
        case PlaythroughActionKind::kCouncil: return ApplyCouncilAction(a, seed);
        case PlaythroughActionKind::kChurch:  return ApplyChurchAction(a, seed);
        case PlaythroughActionKind::kCombat:  return ApplyCombatAction(a, seed);
        case PlaythroughActionKind::kCrime:   return ApplyCrimeAction(a, seed);
        default: return false;
    }
}

// Count occupied crime-table slots (provenState != 0). Mirrors the free-slot scan.
int CountActiveCrimes() {
    using namespace guild::world;
    int n = 0;
    for (int i = 0; i < kCrimeCount; ++i) {
        u8 st = reinterpret_cast<u8*>(&g_crimeTable[i])[kCrimeProvenStateOff];
        if (st != 0) ++n;
    }
    return n;
}

// Pick a representative "tracked treasury" id from the script: the first market or
// church target (so the witness treasury field follows a real building the run
// mutates). Returns 0 if none. Also returns a tracked ware (price witness).
void PickWitnessTargets(const PlaythroughScript& script, i32& outBuildingId, i16& outWare) {
    outBuildingId = 0; outWare = 0;
    for (const auto& a : script.actions) {
        if (a.kind == PlaythroughActionKind::kMarket) {
            if (outBuildingId == 0) outBuildingId = a.targetId;
            if (outWare == 0)       outWare = a.ware;
        } else if (a.kind == PlaythroughActionKind::kChurch && outBuildingId == 0) {
            outBuildingId = a.targetId;
        }
    }
}

// The shared day loop: apply scripted actions + advance the day + record the witness.
// `witnessBuilding`/`witnessWare` are the tracked observables (may be 0). Mutates the
// live world; fills `r.witnesses`. `seed` re-roots the day RNG; the per-day hash is
// Srand(seed)-anchored so it is reproducible across reruns in one process.
void RunDayLoop(PlaythroughResult& r, const PlaythroughScript& script,
                std::uint32_t seed, i32 witnessBuilding, i16 witnessWare) {
    // The whole-world hash right after load/seed (day -1 baseline).
    crt::Srand(seed);
    r.hashAfterLoad = HashFullWorld();

    GameDayState st = SeedGameDay(seed);

    for (int day = 0; day < script.days; ++day) {
        int applied = 0;
        for (const auto& a : script.actions) {
            if (a.onDay != day) continue;
            if (ApplyAction(a, seed)) ++applied;
        }
        r.actionsApplied += applied;

        // Advance ONE real game-day (the real per-day cascade). RunGameDay re-seeds
        // with (seed + state.day) internally so each day's RNG stream is distinct but
        // reproducible (the world keeps evolving, not converging).
        RunGameDay(seed, st);
        ++r.daysRun;

        // Record the per-day witness. Anchor the RNG right before HashFullWorld (the
        // base digest folds the live CRT RNG state).
        DayWitness w;
        w.day            = day;
        w.actionsApplied = applied;
        if (witnessBuilding != 0)
            w.treasury = ReadBuildingTreasury(witnessBuilding);
        if (witnessWare != 0)
            w.price = (int)sim::Building_ComputeMarketPrice(witnessWare, 100);
        w.officeRank = world::g_officeHolders[0].rank;
        w.crimeCount = CountActiveCrimes();
        w.cityMoney  = world::g_cityTotalMoney;
        crt::Srand(seed);
        w.hash = HashFullWorld();
        r.witnesses.push_back(w);
    }
}

} // namespace

// ===========================================================================
// PlaythroughResult::runSignature — fold every day's hash + observable fields into
// one 64-bit number so two runs compare with a single equality.
// ===========================================================================
std::uint64_t PlaythroughResult::runSignature() const {
    Fnv1a64 h;
    h.scalar(hashAfterLoad);
    for (const auto& w : witnesses) {
        h.scalar(w.hash);
        h.scalar(w.treasury);
        h.scalar((i32)w.price);
        h.scalar(w.officeRank);
        h.scalar((i32)w.crimeCount);
        h.scalar(w.cityMoney);
    }
    return h.value();
}

// ===========================================================================
// RunScriptedPlaythrough — the multi-day scripted playthrough over a REAL city.
// ===========================================================================
PlaythroughResult RunScriptedPlaythrough(shim::IFileSystem* fs,
                                         const std::string& gameDir,
                                         const std::string& cityName,
                                         const PlaythroughScript& script,
                                         std::uint32_t seed) {
    PlaythroughResult r;
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

    // --- deterministic baseline ---------------------------------------------
    ResetWorldBaseline(seed);

    i32 witnessBuilding = 0; i16 witnessWare = 0;
    PickWitnessTargets(script, witnessBuilding, witnessWare);

    // --- the multi-day scripted loop ----------------------------------------
    RunDayLoop(r, script, seed, witnessBuilding, witnessWare);

    io::VfsShutdown();
    return r;
}

// ===========================================================================
// RunScriptedPlaythroughSynthetic — the same multi-day sequence on a synthetic world.
// ===========================================================================
PlaythroughResult RunScriptedPlaythroughSynthetic(std::uint32_t seed, int persons,
                                                  const PlaythroughScript& script) {
    using namespace guild::sim;
    PlaythroughResult r;

    // --- SEED a synthetic live world (no assets) ----------------------------
    ZeroWorldGlobals();
    ResetEntityArrays();
    ResetWorldBaseline(seed);

    // A few synthetic people so the entity digest has a non-trivial substrate.
    for (int i = 0; i < persons && i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, kPersonStride);
        g_persons[i].id = 5000 + i;
        g_personIds[i]  = g_persons[i].id;
    }
    g_personArrayLoaded = persons > 0;
    g_sceneArrayLoaded  = true;
    r.loaded = true;
    r.personCount = (std::uint32_t)persons;
    r.objectCount = 0;

    i32 witnessBuilding = 0; i16 witnessWare = 0;
    PickWitnessTargets(script, witnessBuilding, witnessWare);

    RunDayLoop(r, script, seed, witnessBuilding, witnessWare);
    return r;
}

} // namespace guild::play
