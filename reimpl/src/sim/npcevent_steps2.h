#pragma once
// NpcEventSteps2 — the second batch of large NpcEvent step state machines for the
// Guild simulation (gilde.exe, VIBE_NpcEvent_* family). These are the "guild boss"
// timed-event coroutines that npcevent_steps.cpp deferred:
//
//   * ProtectionMoneyStep / ExtortionStep — the protection-racket / inspection
//     shakedown drivers: the racketeer NPC walks to a shop, runs a method roll
//     (gesture byte, room-worth percentage), then either pays out (op16) or
//     messages success/failure and frees.
//   * PatrolStep — the guild-patrol coroutine: a phase machine (states -2..5) that
//     starts/ends a "Patrol" action for up to 6 guild members, optionally triggers
//     a brawl (op39 fight packet drafting up to 6 attackers + 6 defenders), and
//     re-arms via a cutscene-slot wait.
//   * GatherGuildMembersStep — the "summon to the guild meeting" event: ranks up to
//     8 wealthy/eligible members (Person_SumCurrencyHeld vs the +176 threshold),
//     builds an op39 gather packet, and messages every guild master.
//   * AwardTitleStep — the title/citizenship award dialog: gated to the active
//     player, builds an EventPanel, renders the rich-text + voice fanfare, applies
//     the title via a delta packet, and waits for the dialog button.
//
// These follow the SAME translation contract as npcevent_steps.{h,cpp}: the He
// record (he.h) state machine — phase switch, timer advances (+82 GameTime),
// counters and RNG draws — is translated 1:1; cross-cluster leaf side effects
// (command emit, render/text/voice, person/object array sweeps, event panels)
// are reached through the NpcEventHooks2 bridge below so each step is exercisable
// in isolation. Where the original walks a GLOBAL person/object/type array inline
// (the 768-person currency sweep, the object-array brawl draft) that sweep is a
// host leaf — only the observable He-record mutations are reproduced exactly.
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Leaf hooks for the second batch. As with NpcEventHooks, a null member installs
// an inert default (queries report "absent", effects are no-ops). The shared
// NpcLeafHooks (npcaction.h) still supplies freeHandlerEntry / queueRequestEntity29
// / packetStatus; this struct adds the racket/patrol/gather/award leaves.
// ===========================================================================
struct NpcEventHooks2 {
    // --- entity / person resolution (same semantics as NpcEventHooks) ---
    // VIBE_Person_QueryBegin(&clock,1,1,id) -> object handle (0 == absent). The
    // steps read object fields through objectField below.
    i32 (*queryBegin)(i32 id);
    // VIBE_Person_FindRecordById(id) -> Person handle (0 == absent).
    i32 (*findPerson)(i32 id);
    // Field read off a Person handle (raw record offset). Returns 0 if handle == 0.
    i32 (*personField)(i32 personHandle, int byteOffset);
    // Field read off an object handle (the QueryBegin result). Returns 0 if 0.
    //   +1  : object id (dword)   +39 : owner/faction person-index word.
    i32 (*objectField)(i32 objectHandle, int byteOffset);

    // --- building / economy queries ---
    // VIBE_Building_GetUpgradeLevel(object) -> upgrade tier (drives the inspection
    // fail probability in ExtortionStep).
    int (*buildingUpgradeLevel)(i32 objectHandle);
    // VIBE_BuildingValue_ComputeRoomWorth(object, pct, person) -> worth value.
    i32 (*computeRoomWorth)(i32 objectHandle, int pct, i32 personHandle);
    // VIBE_Person_SumCurrencyHeld(personSlot) -> total cash a person holds (the
    // GatherGuildMembers wealth gate). personSlot is the iterator index passed
    // through opaque to the host.
    i32 (*sumCurrencyHeld)(i32 personSlot);
    // VIBE_Object_FindByHandle / scan for a brawl-eligible object near the patrol
    // (PatrolStep): nonzero if a fight should be staged.
    int (*patrolBrawlEligible)(HeRecord* h);

    // --- character / command emit leaves ---
    // VIBE_Character_ChangePlayerAction(object, 0, record, methodWord).
    void (*changePlayerAction)(i32 objectHandle, i32 record, u16 methodWord);
    i32 (*queueRequestSingle49)(i32 id);                              // op49
    i32 (*queueRequestNamedObject53)(i32 a, i32 b, int c, int d, int e, const char* tag); // op53
    i32 (*queueRequestArgs25)(i32 id, int a, int b, int c, int d);    // op25
    i32 (*queueRequest16)(i32 a, i32 b, int amount, int d);           // op16 (payout)
    i32 (*queueRequest39)(const void* packet);                        // op39 (fight/gather)
    // VIBE_Command begin/append/queue delta (AwardTitleStep title apply).
    void (*applyTitleDelta)(i32 personHandle, i32 ownerObjectId, i32 newTitle);

    // --- packet status / sequence (re-arm gating) ---
    i32 (*packetStatus)(i32 handle);   // 0 pending else applied
    i32 (*packetSeq)(i32 handle);      // applied entity handle (0 == none)
    // VIBE_Cutscene_FindSlotById(seqId) -> nonzero while the cutscene runs.
    int (*cutsceneActive)(i32 seqId);

    // --- messaging / text leaves (return ignored) ---
    void (*sendEntityMessage)(i32 from, i32 to, const char* body, int code, const char* tag);
    void (*sendQuickjumpMessage)(i32 from, i32 to, const char* body, int code, i32 jumpId, const char* tag);

    // --- AwardTitle GUI / voice leaves ---
    // VIBE_GameTime_Compare(personTime, &clock): >0 / ==1 cutoff used by the gate.
    int (*compareAwardTime)(i32 personHandle);
    int (*eventPanelCreate)(HeRecord* h);   // returns nonzero panel handle (->+116 dword[29])
    void (*eventPanelDestroy)(HeRecord* h);
    void (*renderAwardText)(HeRecord* h, i32 person, int variant);  // VIBE_Text_RenderRichString cluster
    void (*playAwardVoice)(HeRecord* h, i32 person, int variant);   // VIBE_Voice_PlayQueuedSample cluster
    // The dialog "result" the panel reported this tick (dword_75BF38; 1210 confirm).
    i32 (*awardDialogResult)();
    // Whether this step is the active player's selected NPC (word_63CC5C gate +
    // byte_63CC40). 1 == proceed.
    int (*awardActivePlayerGate)(HeRecord* h);
};

void SetNpcEventHooks2(const NpcEventHooks2* hooks);
const NpcEventHooks2& GetNpcEventHooks2();

// ===========================================================================
// Translated step functions. Each takes the He record and returns the original's
// eax (a packet handle, a phase id, or the FreeHandlerEntry passthrough).
// ===========================================================================

// gilde.exe 0x4d3c50 — VIBE_NpcEvent_ProtectionMoneyStep.
//   Protection-racket coroutine (phases 1..4, plus -1/-2 teardown). Phase 1 starts
//   the "Schutzgeld" action (op49/op53) and arms phase 2 (+10 min). Phase 2 either
//   waits on the live-actor (+296) or rolls the method: a gesture-byte branch (6/7)
//   does an op28 slot-reset and a longer wait, otherwise a RandomModulo(16)+2 wait
//   and a probability roll vs the room safety set the +184 outcome (1 pay / 0|2
//   refuse). Phase 3 pays out (op25/op16, optional kickback to +188) and messages;
//   phase 4 reports the result, ends the action and frees. State<-1 or a missing
//   person/owner frees. Returns the cmd29 / message handle.
i32 NpcEvent_ProtectionMoneyStep(HeRecord* h);

// gilde.exe 0x4d4460 — VIBE_NpcEvent_ExtortionStep.
//   Business-inspection ("BetriebsPr") shakedown (phases 0..2). Phase 0 starts the
//   action (op49/op53) and arms phase 1 (+10 min). Phase 1: a +4 min wait; if the
//   actor is busy stays; else computes a fail probability from the building upgrade
//   level, scans for a contraband object (QueryFind 0,2,7,4,29 matching the shop),
//   and on a clean/triggered result emits the fine (op16) plus success/failure
//   messages; arms phase 2. Phase 2 ends the action (op49/op53) and frees. A
//   missing person/owner or phase -2 frees. Returns the record / free passthrough.
i32 NpcEvent_ExtortionStep(HeRecord* h);

// gilde.exe 0x4d49b8 — VIBE_NpcEvent_PatrolStep.
//   Guild-patrol coroutine (phases -2..5). Phase 0 starts a "Patrol" action for up
//   to 6 members (+140 slots) and arms a +5 min hold; phase 1 waits for an active
//   patroller (else +10 min and phase 2); phase 2 enforces the 24h cooldown (+68),
//   finds a rival patrol (He_FindFirstHandlerByFilter), and on a RandomModulo(2)
//   accept drafts an op39 brawl (6 attackers + 6 defenders) -> phase 3, else a
//   hair-gesture leaf and re-hold; phase 3 waits the fight packet then -> phase 4;
//   phase 4 waits the cutscene slot; phase 5 ends every member's action and frees.
//   Phase -2 (not yet spawned) reroutes to phase 5; phase -1 frees. Re-arms cmd29.
i32 NpcEvent_PatrolStep(HeRecord* h);

// gilde.exe 0x4d5308 — VIBE_NpcEvent_GatherGuildMembersStep.
//   "Summon to the guild meeting" event (phases 0/1, -1 free). Phase 0 messages
//   every live guild master (broadcast 3617/1418) and arms phase 1 (+1 min). Phase 1
//   ranks up to 8 wealthy eligible members: a first pass takes the +176-threshold
//   guild masters, a second pass insertion-sorts the wealthiest non-master members
//   by Person_SumCurrencyHeld, merges them, and if any were gathered emits an op39
//   gather packet; arms a +24h re-hold via cmd29(-1). Returns the cmd29 handle.
i32 NpcEvent_GatherGuildMembersStep(HeRecord* h);

// gilde.exe 0x4d57a0 — VIBE_NpcEvent_AwardTitleStep.
//   Title / citizenship award dialog. Gated to the active player's selected NPC
//   (word_63CC5C == +8) and a pre-22:00 cutoff. Phase 0 builds the EventPanel and,
//   per "self vs other player" and the title kind, renders the rich-text strings
//   and plays the fanfare/voice samples; if it is the player's own award it also
//   applies the title via a delta packet (op22). Phase 1 waits for the panel
//   confirm (1210) / cancel and frees. A missing target / past-cutoff / closed panel
//   frees. Returns the panel-render result / free passthrough.
i32 NpcEvent_AwardTitleStep(HeRecord* h);

// ===========================================================================
// Registration. Same address-keyed table form as RegisterNpcEvents().
// ===========================================================================
int RegisterNpcEvents2();
i32 (*NpcEvent2_TableEntry(int address))(HeRecord*);

} // namespace guild::sim
