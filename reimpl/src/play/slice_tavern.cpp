// Wave 27 PLAY P5 — TAVERN / SOCIAL vertical slice implementation.
// See slice_tavern.h. Every validation/sim link is a CALL into an already-
// reconstructed sibling; this file defines no new world state beyond the per-run
// apply hook (with a faithful inert default).
//
// REUSED (extern, never redefined — ODR):
//   sim::PersonFindRecordById / g_persons    (sim/entity.h)
//   sim::PersonGet*/Set* accessors           (sim/person.h)
//   sim::PersonFindEmploymentRelation        (sim/person_personnel2.h)
//   play::RunEconomyTurn / SeedEconomyTurnState  (play/turn_economy.h)
//   play::HashFullWorld                          (play/world_digest.h)
//   crt::Srand                                    (crt/rand.h)
#include "play/slice_tavern.h"

#include <cstring>

#include "sim/entity.h"
#include "sim/person.h"
#include "sim/person_personnel2.h"
#include "sim/types.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "crt/rand.h"

// The full set of folded world tables ZeroWorldGlobals blanks (mirrors
// slice_personnel.cpp / playable_slice.cpp).
#include "sim/building.h"
#include "sim/building_create.h"
#include "sim/building_production.h"
#include "sim/building_lifecycle.h"
#include "sim/actionqueue.h"
#include "sim/command_apply5.h"
#include "sim/command_apply6.h"
#include "world/city.h"
#include "world/law.h"
#include "world/event.h"
#include "world/office.h"
#include "world/crime.h"
#include "world/relation.h"

namespace guild::play {

// ===========================================================================
// TavernPacket::encode — the on-wire image RequestBuildOp83 (0x495954) lays out.
//   byte[0]   = 83
//   dword@+16 = tag ("buy ")
//   dword@+20 = playerId
//   dword@+24 = locationId
//   dword@+28 = targetId
//   dword@+32 = price
// ===========================================================================
void TavernPacket::encode(u8 out[36]) const {
    std::memset(out, 0, 36);
    out[0] = opcode;
    auto put = [&](int off, u32 v) {
        out[off + 0] = (u8)(v & 0xFF);
        out[off + 1] = (u8)((v >> 8) & 0xFF);
        out[off + 2] = (u8)((v >> 16) & 0xFF);
        out[off + 3] = (u8)((v >> 24) & 0xFF);
    };
    put(16, tag);
    put(20, (u32)playerId);
    put(24, (u32)locationId);
    put(28, (u32)targetId);
    put(32, (u32)price);
}

namespace {

// --- the active apply hooks (inert-by-default reconstruction) ----------------
const TavernApplyHooks* g_hooks = nullptr;

// Default RECRUIT apply: bind the target NPC's relation slot 0 (+0x5C) and superior
// (+0x60) to the buyer (the op-36 Pair36 binding records exactly this {npcId,
// playerId} pair), and debit `price` from the buyer's cash word (+0x0A) (the op-16
// QueueRequest16 transfer). All folded by HashFullWorld (g_persons raw bytes).
void DefaultApplyRecruit(i32 playerId, i32 targetId, int price) {
    using namespace guild::sim;
    Person* tgt = PersonFindRecordById(targetId);
    if (tgt) {
        PersonSetDword(tgt, kTvRelationOff, playerId);  // bind master (op-36)
        PersonSetDword(tgt, kTvSuperiorOff, playerId);  // bind superior link
    }
    Person* buyer = PersonFindRecordById(playerId);
    if (buyer) {
        i16 cash = PersonGetWord(buyer, kTvCashOff);
        cash = static_cast<i16>(cash - price);          // debit the price (op-16)
        PersonSetWord(buyer, kTvCashOff, cash);
    }
}

void RunApplyRecruit(i32 p, i32 t, int price) {
    if (g_hooks && g_hooks->applyRecruit) g_hooks->applyRecruit(p, t, price);
    else DefaultApplyRecruit(p, t, price);
}

// Zero EVERY live world table HashFullWorld folds (exactly the set in
// slice_personnel.cpp / playable_slice.cpp) so a synthetic run's hash is reproducible.
void ZeroWorldGlobals() {
    using std::memset;
    using namespace guild::sim;
    using namespace guild::world;
    memset(g_objects, 0, sizeof(g_objects));
    memset(g_persons, 0, sizeof(g_persons));
    memset(g_personIds, 0, sizeof(g_personIds));
    memset(g_sceneNodes, 0, sizeof(g_sceneNodes));
    memset(g_buildingPersons, 0, sizeof(g_buildingPersons));
    memset(g_buildingTypes, 0, sizeof(g_buildingTypes));
    g_buildingTypesLoaded = false;
    g_buildingNextId = 0;
    memset(g_sceneTypes, 0, sizeof(g_sceneTypes));
    g_sceneTypesLoaded = false;
    memset(g_sceneTypeRemap, 0, sizeof(g_sceneTypeRemap));
    memset(g_prodStore, 0, sizeof(g_prodStore));
    memset(g_prodSchedules, 0, sizeof(g_prodSchedules));
    memset(g_cities, 0, sizeof(g_cities));
    memset(g_goods, 0, sizeof(g_goods));
    g_capDivisor = 0.0f;
    g_cityTotalMoney = 0.0f;
    g_cityTotalGoods = 0.0f;
    memset(&g_sysGameTime, 0, sizeof(g_sysGameTime));
    memset(&g_tickClock, 0, sizeof(g_tickClock));
    g_tickSubCounter = 0;
    g_gameTick = 0;
    g_currentPlayer = 0;
    g_sysActivePlayer = 0;
    memset(g_lawTable, 0, sizeof(g_lawTable));
    memset(g_eventTable, 0, sizeof(g_eventTable));
    g_eventTableCount = 0;
    g_missionLcgState = 0;
    memset(g_officeHolders, 0, sizeof(g_officeHolders));
    memset(g_crimeTable, 0, sizeof(g_crimeTable));
    memset(g_relationMatrix, 0, sizeof(g_relationMatrix));
}

// Seed a small deterministic world of persons directly into the live sim arrays.
// Slot 0 is the BUYER/guild-master (kind 10, a live actor); the rest are dark-corner
// NPCs (kind 1, live actors, unbound relation = -1).
void SeedSyntheticPersons(std::uint32_t seed, int persons) {
    using namespace guild::sim;
    ZeroWorldGlobals();
    ResetEntityArrays();
    crt::Srand(seed);
    for (int i = 0; i < persons && i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, kPersonStride);
        Person& p = g_persons[i];
        p.marker = (i16)i;                 // occupied (!= -1); FindRecordById gate
        // Ids STABLE (6000+i) so the click's player/target ids resolve regardless
        // of seed; the seed is folded into the cash word instead so the world hash
        // tracks the seed without perturbing the id-based lookups the click needs.
        i32 id = 6000 + i;
        PersonSetDword(&p, kPfId, id);
        g_personIds[i] = id;               // FindRecordById matches on this column
        PersonSetByte(&p, kPfIsPlayer, 1); // +8 live actor
        PersonSetByte(&p, kPfKind, (u8)(i == 0 ? 10 : 1));  // 10 == player/master
        // Cash folds the seed (low bits) + slot so the digest tracks the seed; kept
        // well above the max price so the post-buy debit stays positive.
        PersonSetWord(&p, kPfCash,
                      (i16)(500 + 50 * i + (i32)((seed >> 3) & 0x3F)));
        PersonSetDword(&p, kPfRelationBase, -1);           // relation unbound (-1)
        PersonSetDword(&p, kPfSuperiorId, -1);             // +0x60 link unbound
    }
    g_personArrayLoaded = persons > 0;
    g_sceneArrayLoaded  = true;
}

} // namespace

void SetTavernApplyHooks(const TavernApplyHooks* hooks) { g_hooks = hooks; }

void TavernZeroWorldGlobals() { ZeroWorldGlobals(); }

// ===========================================================================
// BuildTavernPacket — classify + build the op-83 dark-corner-buy command packet.
// Mirrors VIBE_Location_TavernDarkCornerBuy's staging block + CheckResourceAmount
// affordability gate (the buyer must be able to afford the price).
// ===========================================================================
TavernPacket BuildTavernPacket(const TavernClick& click) {
    using namespace guild::sim;
    TavernPacket pkt;
    pkt.tag        = kTavernBuyTag;
    pkt.playerId   = click.playerId;
    pkt.locationId = click.locationId;
    pkt.targetId   = click.targetId;
    pkt.price      = click.price;
    if (click.action != TavernAction::kRecruitThug)
        return pkt;                        // unbuilt (opcode 0)
    if (click.targetId == 0 || click.playerId == 0)
        return pkt;
    // CheckResourceAmount gate: the buyer must afford the price.
    Person* buyer = PersonFindRecordById(click.playerId);
    if (!buyer)
        return pkt;
    int cash = (int)PersonGetWord(buyer, kTvCashOff);
    if (click.price > cash)
        return pkt;                        // can't afford -> not built
    pkt.opcode = 83;
    pkt.built  = true;
    return pkt;
}

// ===========================================================================
// IssueTavernClick — resolve, gate, build, and apply one interaction.
// ===========================================================================
TavernOrder IssueTavernClick(const TavernClick& click) {
    using namespace guild::sim;
    TavernOrder o;
    o.playerId = click.playerId;
    o.targetId = click.targetId;
    o.price    = click.price;

    TavernPacket pkt = BuildTavernPacket(click);
    o.opcode = pkt.opcode;
    o.issued = pkt.built;
    if (!o.issued)
        return o;

    // --- before readbacks (the folded person fields) ---
    if (Person* tgt = PersonFindRecordById(click.targetId))
        o.targetRelationBefore = PersonGetDword(tgt, kTvRelationOff);
    if (Person* buyer = PersonFindRecordById(click.playerId))
        o.buyerCashBefore = (int)PersonGetWord(buyer, kTvCashOff);

    // --- the apply (op-83 dark-corner-buy effect) ---
    RunApplyRecruit(click.playerId, click.targetId, click.price);
    o.applied = true;

    // --- after readbacks ---
    if (Person* tgt = PersonFindRecordById(click.targetId)) {
        o.targetRelationAfter = PersonGetDword(tgt, kTvRelationOff);
        o.targetSuperiorAfter = PersonGetDword(tgt, kTvSuperiorOff);
        // FindEmploymentRelation returns 0 once a master references the target —
        // here the target's relation gets the binding via the +0x5C write.
        o.employmentAfter = PersonFindEmploymentRelation(tgt);
    }
    if (Person* buyer = PersonFindRecordById(click.playerId))
        o.buyerCashAfter = (int)PersonGetWord(buyer, kTvCashOff);
    return o;
}

// ===========================================================================
// RunTavernStepsSynthetic — three-step hash sequence on a synthetic world.
// ===========================================================================
int RunTavernStepsSynthetic(std::uint32_t seed, int persons,
                            const TavernClick& click, std::uint32_t econSeed,
                            TavernStepHash* out, int cap) {
    if (!out || cap < 3)
        return 0;

    // --- kSeed ---
    SeedSyntheticPersons(seed, persons);
    crt::Srand(econSeed);
    std::uint64_t hSeed = HashFullWorld();

    // --- kCommand (apply the tavern interaction) ---
    SetTavernApplyHooks(nullptr);          // use the inert-default field mutation
    IssueTavernClick(click);
    crt::Srand(econSeed);                   // re-anchor RNG: the hash folds RNG state
    std::uint64_t hCmd = HashFullWorld();

    // --- kDay (the real economy passes + RNG consumption) ---
    {
        crt::Srand(econSeed);
        EconomyTurnState st = SeedEconomyTurnState();
        st.day = 0;
        RunEconomyTurn(st);
    }
    crt::Srand(econSeed);
    std::uint64_t hDay = HashFullWorld();

    out[0] = {TavernStep::kSeed,    hSeed, false};
    out[1] = {TavernStep::kCommand, hCmd,  hCmd != hSeed};
    out[2] = {TavernStep::kDay,     hDay,  hDay != hCmd};
    return 3;
}

// ===========================================================================
// RunTavernSliceSynthetic — the whole tavern slice on a synthetic world.
// ===========================================================================
TavernSliceResult RunTavernSliceSynthetic(std::uint32_t seed, int persons,
                                          const TavernClick& click,
                                          std::uint32_t econSeed) {
    TavernSliceResult r;

    SeedSyntheticPersons(seed, persons);
    r.seeded = persons > 0;
    crt::Srand(econSeed);
    r.hashAfterSeed = HashFullWorld();

    // --- the tavern command ---
    SetTavernApplyHooks(nullptr);
    r.order = IssueTavernClick(click);
    crt::Srand(econSeed);
    r.hashAfterCommand = HashFullWorld();

    // --- one game-day ---
    {
        crt::Srand(econSeed);
        EconomyTurnState st = SeedEconomyTurnState();
        st.day = 0;
        EconomyTurnDeltas d = RunEconomyTurn(st);
        r.economyPasses = d.passesRun;
    }
    crt::Srand(econSeed);
    r.hashAfterDay = HashFullWorld();

    return r;
}

} // namespace guild::play
