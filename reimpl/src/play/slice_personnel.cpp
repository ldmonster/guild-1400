// Wave 27 PLAY P5 — PERSONNEL / FAMILY vertical slice implementation.
// See slice_personnel.h. Every validation/economy/sim link is a CALL into an
// already-reconstructed sibling; this file defines no new world state beyond the
// per-run apply hook (with a faithful inert default).
//
// REUSED (extern, never redefined — ODR):
//   sim::RecruitCheckRecruitProximity        (sim/recruit.h)
//   sim::RecruitComputeRecruitmentCost       (sim/recruit_cost.h)
//   sim::PersonFindRecordById / g_persons    (sim/entity.h)
//   sim::PersonFindEmploymentRelation        (sim/person_personnel2.h)
//   sim::PersonGet*/Set* accessors           (sim/person.h)
//   play::RunEconomyTurn / SeedEconomyTurnState  (play/turn_economy.h)
//   play::HashFullWorld                          (play/world_digest.h)
//   crt::Srand                                    (crt/rand.h)
#include "play/slice_personnel.h"

#include <cstring>

#include "sim/entity.h"
#include "sim/person.h"
#include "sim/recruit.h"
#include "sim/recruit_cost.h"
#include "sim/person_personnel2.h"
#include "sim/types.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "crt/rand.h"

// The full set of folded world tables ZeroWorldGlobals blanks (mirrors
// playable_slice.cpp) so the synthetic slice's hashes are a pure function of
// (seed + click) and reproducible across reruns in one process.
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

namespace {

// --- the active apply hooks (inert-by-default reconstruction) ----------------
const PersonnelApplyHooks* g_hooks = nullptr;

// Default HIRE apply: bind candidate's employer field (+92) to the recruiter and
// debit `fee` from the candidate cash word (+0x0A). This is the exact observable
// mutation a confirmed hire produces — the invariant CheckRecruitProximity reads
// (candidate +92 == recruiter) — and is folded by HashFullWorld (g_persons).
void DefaultApplyHire(i32 recruiterId, i32 candidateId, int fee) {
    using namespace guild::sim;
    Person* cand = PersonFindRecordById(candidateId);
    if (!cand)
        return;
    PersonSetDword(cand, kPnEmployerOff, recruiterId);
    i16 cash = PersonGetWord(cand, kPnCashOff);
    cash = static_cast<i16>(cash - fee);
    PersonSetWord(cand, kPnCashOff, cash);
}

// Default MARRY apply: write the reciprocal relation ids into each partner's
// relation slot 0 (+92), binding them into a household. Both records are folded.
void DefaultApplyMarry(i32 aId, i32 bId) {
    using namespace guild::sim;
    Person* a = PersonFindRecordById(aId);
    Person* b = PersonFindRecordById(bId);
    if (a) PersonSetDword(a, kPnEmployerOff, bId);
    if (b) PersonSetDword(b, kPnEmployerOff, aId);
}

void RunApplyHire(i32 r, i32 c, int fee) {
    if (g_hooks && g_hooks->applyHire) g_hooks->applyHire(r, c, fee);
    else DefaultApplyHire(r, c, fee);
}
void RunApplyMarry(i32 a, i32 b) {
    if (g_hooks && g_hooks->applyMarry) g_hooks->applyMarry(a, b);
    else DefaultApplyMarry(a, b);
}

// Zero EVERY live world table HashFullWorld folds (exactly the set in
// playable_slice.cpp's ZeroWorldGlobals) so a synthetic run's hash is reproducible.
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
// Slot 0 is the MASTER/recruiter (kind 6, a live actor that can employ); the rest
// are candidate workers (kind < 10, live actors, unbound employer = -1).
void SeedSyntheticPersons(std::uint32_t seed, int persons) {
    using namespace guild::sim;
    ZeroWorldGlobals();
    ResetEntityArrays();
    crt::Srand(seed);
    for (int i = 0; i < persons && i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, kPersonStride);
        Person& p = g_persons[i];
        p.marker = (i16)i;                 // occupied (!= -1); FindRecordById gate
        // Ids are STABLE (5000+i) so the click's recruiter/candidate ids resolve
        // regardless of seed; the seed is folded into the cash word instead so the
        // world hash still tracks the seed (HashFullWorld folds g_persons raw bytes)
        // without perturbing the id-based lookups the click depends on.
        i32 id = 5000 + i;
        PersonSetDword(&p, kPfId, id);
        g_personIds[i] = id;               // FindRecordById matches on this column
        PersonSetByte(&p, kPfIsPlayer, 1); // +8 live actor (proximity gate)
        PersonSetByte(&p, kPfKind, (u8)(i == 0 ? 6 : 1));  // 6 == master/employer
        // Cash folds the seed (low bits) + slot so the digest tracks the seed; kept
        // well above the max recruitment fee (25) so the post-hire debit stays > 0.
        PersonSetWord(&p, kPfCash,
                      (i16)(200 + 50 * i + (i32)((seed >> 3) & 0x3F)));
        PersonSetDword(&p, kPfRelationBase, -1);           // employer unbound (-1)
        PersonSetDword(&p, kPfSuperiorId, -1);             // +0x60 link unbound
    }
    g_personArrayLoaded = persons > 0;
    g_sceneArrayLoaded  = true;
}

} // namespace

void SetPersonnelApplyHooks(const PersonnelApplyHooks* hooks) { g_hooks = hooks; }

void PersonnelZeroWorldGlobals() { ZeroWorldGlobals(); }

PersonnelKind ClassifyPersonnel(PersonnelAction action) {
    switch (action) {
        case PersonnelAction::kHire:  return kPnHire;   // RunHireConfirmDialog kind 2
        case PersonnelAction::kMarry: return kPnMarry;  // courtship command
        default:                      return kPnNone;
    }
}

// ===========================================================================
// IssuePersonnelClick — resolve, validate, cost, and apply one interaction.
// ===========================================================================
PersonnelOrder IssuePersonnelClick(const PersonnelClick& click) {
    using namespace guild::sim;
    PersonnelOrder o;
    o.recruiterId = click.recruiterId;
    o.candidateId = click.candidateId;
    o.kind = ClassifyPersonnel(click.action);
    if (o.kind == kPnNone || click.candidateId == 0)
        return o;

    if (click.action == PersonnelAction::kHire) {
        // --- REAL rules core: eligibility + fee --------------------------
        o.proximity = RecruitCheckRecruitProximity(click.recruiterId,
                                                   click.candidateId);
        o.fee = RecruitComputeRecruitmentCost(click.recruiterId,
                                              click.candidateId);
        // The hire is "issued" once the dialog confirm builds the command (the
        // staging block with kind byte 2 + candidate id). It only enqueues when
        // the candidate is eligible (proximity >= 0; -1024.. are reject codes).
        o.issued = (o.proximity >= 0);
        if (o.issued) {
            RunApplyHire(click.recruiterId, click.candidateId, o.fee);
            o.applied = true;
        }
    } else { // kMarry — the FAMILY action (reciprocal courtship commands)
        // Both partners must resolve to a live person record.
        Person* a = PersonFindRecordById(click.recruiterId);
        Person* b = PersonFindRecordById(click.candidateId);
        o.issued = (a != nullptr && b != nullptr);
        o.proximity = o.issued ? 1 : -1024;
        if (o.issued) {
            RunApplyMarry(click.recruiterId, click.candidateId);
            o.applied = true;
        }
    }

    // --- post-apply readbacks of the folded person fields ---------------
    if (Person* cand = PersonFindRecordById(click.candidateId)) {
        o.candEmployerAfter = PersonGetDword(cand, kPnEmployerOff);
        o.candCashAfter     = (int)PersonGetWord(cand, kPnCashOff);
        // FindEmploymentRelation returns 0 once a kind-6/7 master references the
        // candidate — here the master's relation gets the binding via the +92
        // write on the candidate; we also report the candidate's own employer.
        o.employmentAfter   = PersonFindEmploymentRelation(cand);
    }
    return o;
}

// ===========================================================================
// RunPersonnelStepsSynthetic — three-step hash sequence on a synthetic world.
// ===========================================================================
int RunPersonnelStepsSynthetic(std::uint32_t seed, int persons,
                               const PersonnelClick& click, std::uint32_t econSeed,
                               PersonnelStepHash* out, int cap) {
    if (!out || cap < 3)
        return 0;

    // --- kSeed ---
    SeedSyntheticPersons(seed, persons);
    crt::Srand(econSeed);
    std::uint64_t hSeed = HashFullWorld();

    // --- kCommand (apply the personnel interaction) ---
    SetPersonnelApplyHooks(nullptr);   // use the inert-default field mutation
    IssuePersonnelClick(click);
    crt::Srand(econSeed);              // re-anchor RNG: the hash folds RNG state
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

    out[0] = {PersonnelStep::kSeed,    hSeed, false};
    out[1] = {PersonnelStep::kCommand, hCmd,  hCmd != hSeed};
    out[2] = {PersonnelStep::kDay,     hDay,  hDay != hCmd};
    return 3;
}

// ===========================================================================
// RunPersonnelSliceSynthetic — the whole personnel slice on a synthetic world.
// ===========================================================================
PersonnelSliceResult RunPersonnelSliceSynthetic(std::uint32_t seed, int persons,
                                                const PersonnelClick& click,
                                                std::uint32_t econSeed) {
    PersonnelSliceResult r;

    SeedSyntheticPersons(seed, persons);
    r.seeded = persons > 0;
    crt::Srand(econSeed);
    r.hashAfterSeed = HashFullWorld();

    // --- the personnel command ---
    SetPersonnelApplyHooks(nullptr);
    r.order = IssuePersonnelClick(click);
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
