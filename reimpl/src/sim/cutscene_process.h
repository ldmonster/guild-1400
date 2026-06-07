#pragma once
// ===========================================================================
// cutscene_process.{h,cpp} — the deferred Cutscene "active-processing" step
// machine (gilde.exe, namespace guild::sim).
// ===========================================================================
//
// This module completes the cutscene substrate (slot table + RNG in
// cutscene.{h,cpp}) with the per-frame active-processing driver that the master
// frame loop calls:
//
//   VIBE_Cutscene_ProcessActive   (0x4ac31c)  — per-frame scan of the 96 slots;
//       per slot: alive-gate, state-flag skip, two GameTime windows (a "ready"
//       window advanced by -60s and a "timeout"/cleanup window), per-type
//       prepare/step dispatch, and slot teardown.
//   VIBE_Cutscene_ExecMainFunc    (0x4ab55c)  — the heavyweight per-cutscene
//       executor (its body is render/voice/command/fade-coupled; we translate
//       the deterministic skeleton: seed the cutscene RNG from the slot, run the
//       per-type "main func", and clear the participant table). The GUI/render/
//       fade/window leaves are routed through a hook so the skeleton is testable.
//   VIBE_Cutscene_RunForMaster    (0x4ac680)  — run every active slot whose
//       participant list contains the local master, exec it, then tear it down.
//   VIBE_Cutscene_PrepareReady    (0x4ac1b0, skeleton) — master/participant
//       readiness check; returns 1 ready / 0 not-yet / -1 master-invalid.
//   VIBE_Cutscene_CheckMaster     (0x4ac0c8, skeleton).
//   VIBE_Cutscene_ActorHasParticipant (0x4abec8).
//   VIBE_Cutscene_InitParticipantTable / AllParticipantsDone / AllParticipantsReady
//       (0x4aa9b4 / 0x4aaa74 / 0x4aaab8) — the per-participant parallel state
//       columns (dword_11AB000 family, stride 215 dwords).
//   VIBE_Cutscene_BuildSpeechPacket (0x4abf04) — build the cmd28 speech packet
//       (the deterministic field assembly; the cmd emit is a hook).
//   VIBE_Cutscene_InitCommandTable (0x4acefc) — installs the per-type step table
//       (recovered byte-for-byte below).
//   VIBE_Cutscene_AddActorToSlot / RemoveActorFromSlot (0x4b0844 / 0x4b08bc) —
//       the Person "+140" actor-slot 8-entry list ops.
//
// RECOVERED TABLE — the per-type cutscene step table (gilde.exe base
// dword_11AE5C0 @0x11AE5C0, stride 20 bytes == 5 dwords, 12 types). Two parallel
// columns the engine reads: the "main func" column at +0 (dword_11AE5C0, called
// by ExecMainFunc as fn[5*type]) and the "prepare/step" column at +16
// (dword_11AE5D0, called by ProcessActive as fn[5*type]). The intervening +4/+8
// dwords and +12 byte hold per-type secondary fns / flags. Filled at runtime by
// InitCommandTable; recovered from that initializer:
//
//   type  main(+0)                 next(+4)              f8(+8)                 b(+12) step(+16)
//   0     PlayTobyScene            NullSub               0                      0      0
//   1     Office_RunCouncilSession PrepareSuccessorChoiceA PrepareSuccessorChoiceB 0    0
//   2     Office_RunCourtTrial     BuildTortureChoiceForm EvaluateSessionDecision 0    BuildElectionForm
//   3     Combat_RunBattleSetup    BeginBattleOrCacheState 0                     1      BuildBattleInfoText
//   4     Cutscene_Duel            ShowDuelWindow        RollDuelOutcomeTier     0      Duel_CheckParticipants
//   5     Cutscene_Execution       0                     0                      0      Duel_BuildMessages
//   6     Cutscene_Wedding         0                     0                      0      CheckMarriageEligible
//   7     Cutscene_Birth           0                     0                      0      CheckBirthParticipants
//   8     Cutscene_Death           0                     0                      0      CheckDeathTimer
//   9     Cutscene_Bankruptcy      0                     0                      0      0
//   10    Cutscene_Auction         LeaseWindow           LeaseAutoResolve        1      BroadcastMessage
//   11    Cutscene_Salon           0                     0                      0      0
//
// The "step" column (+16) is what ProcessActive dispatches each frame to decide
// whether a slot is ready/finished; type 5/6/7/8 use it as a participant-/timer-
// check predicate, returning nonzero when the cutscene should fire. The body of
// each per-type fn is owned elsewhere (office/combat/cutscene*), so here the
// table is injectable: the host installs real fns; tests install predicates.
//
// All GUI/render/voice/fade/window/command leaves are routed through
// CutsceneProcHooks (mock in tests). Mutations (the cmd-emit, the participant
// rearm-deltas, the op88 timeout request) go through the hook too.
#include "guild/common/types.h"
#include "sim/types.h"        // GameTime
#include "sim/gametime.h"     // GameTimeAdvance / GameTimeCompare
#include "sim/cutscene.h"     // CutsceneSlot / CutsceneTable / CutsceneRng
#include <array>
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Per-type cutscene step table. 12 types, 5 fn/flag slots each (20-byte stride).
// We expose the two function-pointer columns the engine actually dispatches:
//   main[type]  (col +0)  — run by ExecMainFunc
//   step[type]  (col +16) — run by ProcessActive (prepare/ready predicate)
// plus the +12 flag byte. The host installs the real per-type fns; tests install
// stand-ins. A step fn returns nonzero when the slot is "ready to exec".
// ---------------------------------------------------------------------------
constexpr int kCutsceneTypeCount = 12;

using CutsceneTypeFn = int (*)(CutsceneSlot* slot);

struct CutsceneTypeEntry {
    CutsceneTypeFn main;   // +0   heavyweight per-cutscene executor
    CutsceneTypeFn next;   // +4   secondary/chained fn (unused by the driver)
    CutsceneTypeFn f8;     // +8   tertiary fn
    u8             flag;   // +12  per-type flag byte
    CutsceneTypeFn step;   // +16  per-frame prepare/ready predicate
};

class CutsceneTypeTable {
public:
    CutsceneTypeTable() { entries_.fill(CutsceneTypeEntry{}); }
    CutsceneTypeEntry&       operator[](int t)       { return entries_[t]; }
    const CutsceneTypeEntry& operator[](int t) const { return entries_[t]; }
    void Clear() { entries_.fill(CutsceneTypeEntry{}); }
private:
    std::array<CutsceneTypeEntry, kCutsceneTypeCount> entries_;
};

// ---------------------------------------------------------------------------
// Leaf hooks — the cross-cluster side effects the active-processing path makes.
// Tests install a recording mock; nullptr installs an inert default.
//   VIBE_Command_RequestBuildOp88(id)        — request "cutscene timed out".
//   VIBE_Command_QueueRequestBuffer28(...)   — emit a cmd28 speech packet.
//   VIBE_Cutscene_PrepareReady's command leaves are modelled by `prepareReady`.
// ---------------------------------------------------------------------------
struct CutsceneProcHooks {
    // RequestBuildOp88(slotId) — "this cutscene's ready window expired with no
    // step fn; ask the host to drive it." Called from ProcessActive.
    void (*requestBuildOp88)(i32 slotId);
    // QueueRequestBuffer28(slot, person, lenTotal, lenB, text) — speech packet.
    // Returns the packet handle. Called from BuildSpeechPacket.
    i32 (*queueSpeech28)(const CutsceneSlot* slot, i32 person,
                         u32 lenTotal, u32 lenB, const char* text);
    // PrepareReady override: when set, ProcessActive uses this instead of the
    // built-in skeleton. Returns 1 ready / 0 not-yet / -1 master-invalid.
    int (*prepareReady)(CutsceneSlot* slot);
};

void SetCutsceneProcHooks(const CutsceneProcHooks* hooks);
const CutsceneProcHooks& GetCutsceneProcHooks();

// ---------------------------------------------------------------------------
// Driver context — the global state ProcessActive reads. The original pulls
// these from scattered globals (the local master person id, the global clock,
// the universe mode flags). We bundle them so the driver is a pure function of
// (table, clock, master, flags) over the slot table.
// ---------------------------------------------------------------------------
struct CutsceneContext {
    CutsceneTable*     table   = nullptr;  // the 96-slot table (cutscene.h)
    CutsceneTypeTable* types   = nullptr;  // per-type step table
    CutsceneRng*       rng     = nullptr;  // cutscene-local LCG (seeded per slot)
    GameTime           clock{};            // qword_13CE852 — the global clock
    i32  localMaster = -1;                 // dword_12CE914[134*word_63CC5C]
    i32  localSlotId = -1;                 // dword_12CEB18[134*word_63CC5C]
    u16  modeFlags   = 0;                  // word_63C740 (bit2 cmd4, bit3 cmd8)
    u8   modeFlagsB  = 0;                  // byte_63CC28 (bit3)
    // alive-actor scan callback: returns the kind byte of person `id`, or -1 if
    // the person record is absent. The original calls Person_FindRecordById then
    // reads its +2 kind byte; we indirect it so the driver needs no Person array.
    u8 (*personKind)(i32 id) = nullptr;    // returns 0xFF if absent
    // bookkeeping the driver fills
    int execCount = 0;                     // # of slots ExecMainFunc'd this pass
};

// gilde.exe 0x4ac31c — VIBE_Cutscene_ProcessActive.
// One per-frame pass over the 96-slot table. For each alive slot (partCount@+48
// != 0) whose state flags (+38) don't have bit0/bit2 set:
//   * copy the slot's "ready" GameTime (+24) and advance it by -60 seconds (the
//     "fire 60s early" window);
//   * fill the slot master (+12) from the local master if unset and a mode flag
//     is on;
//   * if the slot is owned by the local master, not started (+40 == 0), and the
//     clock is past the -60s window, run PrepareReady:
//       -1 -> re-arm the ready window by +5s (and +9h if the type code >= 0x16),
//        0 -> re-arm by +30s,
//       else -> mark started, run the per-type step fn; if it returns 0, request
//        op88 and move on;
//   * if there is no step fn and the clock is past the slot's timeout window
//     (+24 unmodified), set the slot's "finished" flag (+4);
//   * participant/master cleanup (teardown of orphaned/late slots);
//   * if the clock is past the timeout window and the slot is finished, run
//     ExecMainFunc and tear the slot down.
// Returns the number of slots executed this pass (the original returns 0 in v17
// — we additionally expose execCount in the context for tests).
int CutsceneProcessActive(CutsceneContext& ctx);

// gilde.exe 0x4ab55c — VIBE_Cutscene_ExecMainFunc (deterministic skeleton).
// Sets the slot's state-flag bit2 (+38 |= 4), seeds the cutscene RNG from the
// slot's seed word (+0x78, dword index 30), runs the per-type main fn, then
// resets the participant table for the slot. Returns the main fn's result (0 if
// none). The full body's render/fade/window/command work is the host's job.
int CutsceneExecMainFunc(CutsceneContext& ctx, CutsceneSlot* slot);

// gilde.exe 0x4ac680 — VIBE_Cutscene_RunForMaster. Repeatedly pick the lowest-id
// active slot; if it lists `masterId` as a participant, exec it; tear it down.
// Returns 1 if any slot was executed. (Bounded by table size to avoid spin.)
int CutsceneRunForMaster(CutsceneContext& ctx, i32 masterId);

// gilde.exe 0x4abec8 — VIBE_Cutscene_ActorHasParticipant. True iff `personId` is
// in the slot's participant list (+52, partCount@+48 entries) AND that person
// resolves (ctx.personKind != 0xFF). `personKind` may be null -> resolves always.
bool CutsceneActorHasParticipant(const CutsceneContext& ctx,
                                 i32 personId, const CutsceneSlot* slot);

// gilde.exe 0x4b0844 — VIBE_Cutscene_AddActorToSlot. The Person "+140" 8-entry
// actor-slot list: find the slot whose [140] == masterEntityId; if absent, find
// the first free (-1) slot and write masterEntityId there. `actorList` points at
// the +140 dword of the actor record (8 dwords, stride 4). Returns the resolved
// list-entry index (0..7) it settled on.
int CutsceneAddActorToSlot(i32 masterEntityId, i32* actorList);

// gilde.exe 0x4b08bc — VIBE_Cutscene_RemoveActorFromSlot. Clear the actor-slot
// entry whose value == masterEntityId (set it to -1). Returns the index.
int CutsceneRemoveActorFromSlot(i32 masterEntityId, i32* actorList);

// ---------------------------------------------------------------------------
// Per-participant parallel state (gilde.exe dword_11AB000 family, stride 215
// dwords == 860 bytes, 16 participants). The columns ProcessActive/ExecMainFunc
// read: [+0] enabled (dword_11AB000), [+4] ready (dword_11AB004), [+8] done
// (dword_11AB008), [+12] active (dword_11AB00C), [+16] personId (dword_11AB010).
// ---------------------------------------------------------------------------
constexpr int kCutsceneParticipants  = 16;
constexpr int kCutsceneParticipantCols = 5;   // enabled/ready/done/active/person

struct CutsceneParticipantState {
    i32 enabled;   // +0   dword_11AB000
    i32 ready;     // +4   dword_11AB004
    i32 done;      // +8   dword_11AB008
    i32 active;    // +12  dword_11AB00C
    i32 personId;  // +16  dword_11AB010 (-1 == empty)
};

class CutsceneParticipants {
public:
    CutsceneParticipants() { Clear(); }
    void Clear() { for (auto& p : rows_) p = CutsceneParticipantState{0,0,0,0,-1}; }

    // gilde.exe 0x4aa9b4 — VIBE_Cutscene_InitParticipantTable. Reset all rows;
    // copy the slot's participant ids (+52) into the personId column. If `slot`
    // is null, mark every row empty (personId = -1). `count` = slot->partCount.
    void Init(const CutsceneSlot* slot);

    // gilde.exe 0x4aaa74 — VIBE_Cutscene_AllParticipantsDone. Over the first
    // `count` rows: false if any enabled, not-done, present row remains.
    bool AllDone(int count) const;

    // gilde.exe 0x4aaab8 — VIBE_Cutscene_AllParticipantsReady. Over the first
    // `count` rows: false if any enabled, present, resolving, not-ready row
    // remains. `resolves(id)` mirrors Person_FindRecordById != 0.
    bool AllReady(int count, bool (*resolves)(i32 id)) const;

    CutsceneParticipantState&       operator[](int i)       { return rows_[i]; }
    const CutsceneParticipantState& operator[](int i) const { return rows_[i]; }

private:
    CutsceneParticipantState rows_[kCutsceneParticipants];
};

// gilde.exe 0x4acefc — VIBE_Cutscene_InitCommandTable. Install the per-type step
// table recovered above. We take the per-type fns as a struct so the host can
// supply the real office/combat/cutscene fns; the byte-for-byte slot mapping
// (which fn lands in main/next/f8/step and the flag bytes) is reproduced here.
struct CutsceneTypeFns {
    // type 0
    CutsceneTypeFn playTobyScene = nullptr, nullSub = nullptr;
    // type 1
    CutsceneTypeFn councilSession = nullptr, successorA = nullptr, successorB = nullptr;
    // type 2
    CutsceneTypeFn courtTrial = nullptr, tortureForm = nullptr, sessionDecision = nullptr,
                   electionForm = nullptr;
    // type 3
    CutsceneTypeFn battleSetup = nullptr, beginBattle = nullptr, battleInfoText = nullptr;
    // type 4
    CutsceneTypeFn duel = nullptr, showDuelWindow = nullptr, rollDuelTier = nullptr,
                   duelCheckParticipants = nullptr;
    // type 5
    CutsceneTypeFn execution = nullptr, duelBuildMessages = nullptr;
    // type 6
    CutsceneTypeFn wedding = nullptr, checkMarriage = nullptr;
    // type 7
    CutsceneTypeFn birth = nullptr, checkBirth = nullptr;
    // type 8
    CutsceneTypeFn death = nullptr, checkDeath = nullptr;
    // type 9
    CutsceneTypeFn bankruptcy = nullptr;
    // type 10
    CutsceneTypeFn auction = nullptr, leaseWindow = nullptr, leaseAutoResolve = nullptr,
                   broadcastMessage = nullptr;
    // type 11
    CutsceneTypeFn salon = nullptr;
};
void InitCutsceneTypeTable(CutsceneTypeTable& table, const CutsceneTypeFns& fns);

} // namespace guild::sim
