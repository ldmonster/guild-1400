#pragma once
// Wave 27 PLAY P5 — PERSONNEL / FAMILY vertical slice (namespace guild::play).
//
// A faithful click -> dialog/HUD -> REAL command -> sim effect -> render slice for
// the personnel/family system, mirroring playable_slice.cpp (which drives the unit-
// ORDER path). Where playable_slice issues a CONQUER order on an object record, this
// module issues a PERSONNEL interaction on a *person* record:
//
//   HIRE   — the player picks a candidate person in the staff/recruit window and
//            confirms. The real hire-confirm dialog
//            (VIBE_Recruit_RunHireConfirmDialog @0x55d990) on the CONFIRM button
//            (widget id 1210) stages a 28-byte command block with order-kind byte
//            v19 = 2 carrying the candidate id (dword_12CE914[134*word_63CC5C]) and
//            enqueues it via VIBE_Command_QueueRequestSlotReset28 @0x4948c8 ->
//            StagePendingBlock(0xF8) -> EnqueuePacket. The fee is computed first by
//            VIBE_Recruit_ComputeRecruitmentCost @0x55d674, and eligibility by
//            VIBE_Recruit_CheckRecruitProximity @0x55d5c0 (employer field at +92).
//
//   MARRY  — a FAMILY action: VIBE_NpcAction_BeginMarriage @0x5687b0 queues the two
//            reciprocal courtship/relation commands (A->B, B->A) binding two persons
//            into a household.
//
// GROUNDING (decompiled this wave):
//   * VIBE_Recruit_RunHireConfirmDialog @0x55d990 — staging block kind byte = 2,
//     candidate id = dword_12CE914[134*word_63CC5C]; QueueRequestSlotReset28.
//   * VIBE_Command_QueueRequestSlotReset28 @0x4948c8 — block[0]=28 length tag,
//     StagePendingBlock(0xF8,...), EnqueuePacket.
//   * VIBE_Recruit_CheckRecruitProximity @0x55d5c0 — employer field = +92 (relation
//     slot 0); -1 == unbound; reads recruiter/candidate +8 (live-actor) gate.
//   * VIBE_Person_FindEmploymentRelation @0x58d95c — a candidate is "employed" once
//     a kind-6/7 master references its id in a relation slot (+92/+96/+100).
//
// The opcode-28 (kind-2 hire) APPLY path and the courtship-command apply are not
// reconstructed in the translated slice, so they are supplied as an installable
// hook with a faithful INERT-by-default reconstruction defined in
// slice_personnel.cpp (the input_command / CutsceneMiscHooks pattern): the default
// binds the candidate's employer field (+92) to the recruiter — the exact invariant
// CheckRecruitProximity enforces — and debits the recruitment fee from the
// candidate's cash word (+0x0A). For MARRY it writes the reciprocal relation ids.
//
// REAL reconstructed siblings WIRED (called directly, never redefined):
//   sim::RecruitCheckRecruitProximity      (recruit.cpp)
//   sim::RecruitComputeRecruitmentCost     (recruit_cost.cpp)
//   sim::PersonFindRecordById              (entity.cpp)
//   sim::PersonFindEmploymentRelation      (person_personnel2.cpp)
//   sim::PersonGetDword/SetDword/GetWord/SetWord/GetByte  (person.cpp)
//   play::RunEconomyTurn / SeedEconomyTurnState           (turn_economy.cpp)
//   play::HashFullWorld / SnapshotFullWorld               (world_digest.cpp)
//   crt::Srand                                            (crt/rand.h)
// INERT hook (defined here, faithful default): the kind-2 hire / courtship apply.
//
// Additive: no edits to playable_slice.cpp / input_command.cpp / recruit*.cpp.
#include <cstdint>
#include <functional>

#include "guild/common/types.h"

namespace guild::sim { struct Person; }

namespace guild::play {

// ===========================================================================
// The personnel interaction the slice replays. A click in the staff/recruit/
// family window resolves to one of these against a target person.
// ===========================================================================
enum class PersonnelAction {
    kNone  = 0,   // nothing issued
    kHire  = 1,   // staff-hire confirm  (RunHireConfirmDialog kind byte = 2)
    kMarry = 2,   // family/marry action (BeginMarriage courtship commands)
};

// The order-KIND byte the personnel command carries (staging block byte). Mirrors
// the unit-ORDER kind byte in input_command.h (OrderKind); the WIRE opcode of the
// enqueued packet is the 28-byte slot-reset block (StagePendingBlock 0xF8). For a
// HIRE the staged kind byte is 2 (RunHireConfirmDialog v19 = 2). MARRY rides the
// courtship command (QueueRequestCoord27, coord = -40).
enum PersonnelKind : u8 {
    kPnNone   = 0,
    kPnHire   = 2,    // RunHireConfirmDialog staging kind byte
    kPnMarry  = 27,   // courtship/relation command (modeled kind tag)
};

// ===========================================================================
// One scripted personnel interaction.
// ===========================================================================
struct PersonnelClick {
    PersonnelAction action     = PersonnelAction::kHire;
    i32             recruiterId = 0;   // the master/recruiter person id (employer)
    i32             candidateId = 0;   // the candidate / partner person id
};

// ===========================================================================
// The resolved outcome of one personnel interaction.
// ===========================================================================
struct PersonnelOrder {
    bool          issued      = false;  // the interaction produced a command
    PersonnelKind kind        = kPnNone;
    i32           recruiterId  = 0;
    i32           candidateId  = 0;

    // --- validation / economy (the real rules core) ---
    int  proximity   = 0;     // RecruitCheckRecruitProximity result (1 == eligible)
    int  fee         = 0;     // RecruitComputeRecruitmentCost (0..25)

    // --- applied effect (post-apply readbacks of the folded person fields) ---
    bool applied        = false;  // the apply hook ran (employer/relation written)
    i32  candEmployerAfter = 0;   // candidate +92 after apply (== recruiterId on hire)
    int  candCashAfter     = 0;   // candidate cash word (+0x0A) after fee debit
    int  employmentAfter   = 0;   // PersonFindEmploymentRelation(candidate): 0 == bound
};

// ===========================================================================
// PersonnelApplyHooks — the kind-2 hire / courtship apply, not reconstructed in
// the translated slice; installable hook with a faithful inert default in the .cpp.
// ===========================================================================
struct PersonnelApplyHooks {
    // Apply a decoded HIRE: bind candidate's employer field (+92) to the recruiter
    // and debit `fee` from candidate cash (+0x0A). Default does exactly that.
    std::function<void(i32 recruiterId, i32 candidateId, int fee)> applyHire;
    // Apply a decoded MARRY: write the reciprocal relation ids (A->B, B->A) into
    // the two persons' relation slot 0 (+92). Default does exactly that.
    std::function<void(i32 aId, i32 bId)> applyMarry;
};
// Install the active apply hooks (nullptr restores the inert default).
void SetPersonnelApplyHooks(const PersonnelApplyHooks* hooks);

// Zero EVERY live world table HashFullWorld folds (the same set playable_slice.cpp
// blanks) so a run's hashes are a pure function of (loaded city + seed),
// reproducible across reruns in one process. Call BEFORE io::LoadWorld / seeding.
// Exposed so the GUARDED real-asset e2e can reset the folded economy tables a prior
// RunEconomyTurn dirtied before reloading the city.
void PersonnelZeroWorldGlobals();

// Person-record offsets the apply hook reads/writes (byte offsets into the 536-byte
// record; documented so tests read them back). Same fields the real rules core uses.
enum PersonnelOffset : int {
    kPnEmployerOff = 0x5C,  // relation slot 0 (employer id); -1 == unbound
    kPnCashOff     = 0x0A,  // cash-on-hand word
};

// ===========================================================================
// ClassifyPersonnel — the golden: a PersonnelAction -> its command kind byte.
// Mirrors the dialog/HUD dispatch (hire-confirm kind 2 vs courtship command).
// ===========================================================================
PersonnelKind ClassifyPersonnel(PersonnelAction action);

// ===========================================================================
// IssuePersonnelClick — resolve a personnel interaction into a REAL command,
// validate + cost it via the real rules core, and apply it so the live person
// records mutate. Operates on the live g_persons array. Returns the outcome.
// ===========================================================================
PersonnelOrder IssuePersonnelClick(const PersonnelClick& click);

// ===========================================================================
// The full personnel slice over a SYNTHETIC live world.
// ===========================================================================
struct PersonnelSliceResult {
    bool seeded = false;
    PersonnelOrder order{};

    // --- the determinism oracle (four world hashes, fold order = loop order) ---
    std::uint64_t hashAfterSeed    = 0;  // HashFullWorld() right after seeding
    std::uint64_t hashAfterCommand = 0;  // ... after the personnel command applied
    std::uint64_t hashAfterDay     = 0;  // ... after one game-day

    int economyPasses = 0;

    bool commandChangedWorld() const { return hashAfterSeed != hashAfterCommand; }
    bool worldChanged() const { return hashAfterSeed != hashAfterDay; }
    bool ok() const {
        return seeded && order.issued && order.applied && commandChangedWorld();
    }
};

// Drive the whole personnel slice on a synthetic live world: seed `persons` people
// (a master/recruiter @ slot 0 + workers), issue `click`, run one economy day.
// Deterministic in (seed, econSeed, persons, click). Used by the unit test.
PersonnelSliceResult RunPersonnelSliceSynthetic(std::uint32_t seed, int persons,
                                                const PersonnelClick& click,
                                                std::uint32_t econSeed);

// ===========================================================================
// Step sequencing (mirrors playable_slice's SliceStep), so the unit test can
// assert each step's pre/post HashFullWorld() behaves as the loop requires.
// ===========================================================================
enum class PersonnelStep {
    kSeed    = 0,   // world populated (synthetic persons)
    kCommand = 1,   // apply the personnel command (mutates a person record)
    kDay     = 2,   // run the economy day (mutates economy + RNG)
};
struct PersonnelStepHash {
    PersonnelStep step;
    std::uint64_t hashAfter = 0;
    bool          mutated   = false;
};
// Measure the three steps' hashes on a synthetic world. Returns the step count.
int RunPersonnelStepsSynthetic(std::uint32_t seed, int persons,
                               const PersonnelClick& click, std::uint32_t econSeed,
                               PersonnelStepHash* out, int cap);

} // namespace guild::play
