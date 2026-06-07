#pragma once
// charaction_brawl — the BRAWL (street-fight) combat damage-resolution step
// (namespace guild::sim). MODULE: combat damage resolution over a He handler
// record. Sibling of VIBE_CharAction_GroupInteractStep @0x4d19c0 (charaction_misc.h)
// — same +112 He state-machine convention, same hook-routed cross-cluster leaves.
//
//   VIBE_CharAction_BrawlStep  @0x4d201c  (the He-record brawl coroutine)
//
// SCOPE — the deterministic brawl combat RULE that resolves repeated unarmed
// "Pruegel" (beating) blows between two NPCs, accumulating damage until a
// knockout. Driven each tick over the handler record:
//   * state +112 == -2 / -1  -> terminal: free the handler entry.
//   * gated on the busy flag (+120 & 4 clear) AND no in-flight packet
//     (+132 == -1, or its packet status has resolved):
//       - state 0 (a landed blow): resolve the victim Person record; apply the
//         two mood/relation deltas (the aggressor's two mood bytes), bump the HIT
//         COUNTER (+186) and, on the 5th blow, flip state -> 1 (final blow); add
//         24 to the action-time word (+86); then register the negated-AP DAMAGE
//         event against the victim. If the victim is gone, restore the saved pose
//         and re-queue the entity-29 request.
//       - state 1 (the knockout blow): resolve the victim; emit the "x beats y"
//         entity message + bump the victim family's defeat counter (+24 dword);
//         restore the saved pose and re-queue the entity-29 request.
//
// The damage MATH (the hit-count -> knockout gate at 5, the 24-tick action
// cadence, the negated AP magnitude) is translated 1:1. The cross-cluster leaves
// (Person lookup / family record / relation-mood adjust / AP-event register /
// entity message / packet status / free / re-queue) are routed through
// BrawlHooks, mirroring the GroupInteractHooks pattern, so the integration test
// can wire the REAL siblings (person_relations, npcaction, meister_events,
// handler_entry, command_builders) and the unit test can golden-vector the rule.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Brawl He-record fields (byte offsets into the handler record; +112 state
// machine shared with the GroupInteract sibling).
//   +86   action-time word (u16; += 24 each landed blow — the swing cadence).
//   +112  state (dword: -2/-1 terminal-free, 0 landed-blow, 1 knockout-blow).
//   +120  flag byte (bit2 / &4 busy gate).
//   +132  in-flight entity-request packet handle (dword; -1 == none).
//   +172  victim person id (dword; FindRecordById key).
//   +180  AP damage magnitude (dword; the event registers -value).
//   +181  aggressor mood byte #1 (HIBYTE of the dword at +181).
//   +182  aggressor mood byte #2 (HIBYTE of the dword at +182).
//   +186  HIT COUNTER (byte; ++ each blow; the 5th flips state -> 1).
//   +82..+95  saved-pose region (restored on a re-queue: qword@+82, dword@+90,
//             word@+94 — copied from the global saved-pose snapshot).
// ---------------------------------------------------------------------------
inline i32& Brawl_State(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 112); }
inline u8&  Brawl_Flags(HeRecord* h)   { return *reinterpret_cast<u8*>(HeBytes(h) + 120); }
inline i32& Brawl_Packet(HeRecord* h)  { return *reinterpret_cast<i32*>(HeBytes(h) + 132); }
inline i32& Brawl_VictimId(HeRecord* h){ return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline i32& Brawl_ApDamage(HeRecord* h){ return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline u8&  Brawl_HitCount(HeRecord* h){ return *reinterpret_cast<u8*>(HeBytes(h) + 186); }
inline u16& Brawl_ActionWord(HeRecord* h){ return *reinterpret_cast<u16*>(HeBytes(h) + 86); }

// The two aggressor mood bytes are the HIGH byte of the dword at +181 / +182.
// (HIBYTE(*(_DWORD*)(v4+181)) == byte at +184; HIBYTE(*(_DWORD*)(v4+182)) ==
//  byte at +185.) Exposed as helpers so the rule reads them exactly as the binary.
inline i8 Brawl_MoodKind1(HeRecord* h) { return static_cast<i8>(HeBytes(h)[181 + 3]); }
inline i8 Brawl_MoodKind2(HeRecord* h) { return static_cast<i8>(HeBytes(h)[182 + 3]); }

// ---------------------------------------------------------------------------
// The brawl-step cross-cluster leaves (mockable; the integration test wires the
// real siblings, the unit test wires recording stubs).
// ---------------------------------------------------------------------------
struct BrawlHooks {
    // VIBE_Command_GetPacketStatusById(handle): the in-flight packet's status
    // (nonzero == resolved). Only consulted when +132 != -1.
    int   (*packetStatus)(i32 handle);
    // VIBE_He_FreeHandlerEntry(h): release the handler record (terminal states).
    void  (*freeHandlerEntry)(HeRecord* h);
    // VIBE_Person_FindRecordById(id): the victim Person record (or null). The rule
    // only needs to know whether it exists and its "alive/id" word for the event.
    void* (*findPersonById)(i32 id);
    // The aggressor Person record (word_12CE910[268 * he+8]) — passed to the
    // relation-mood adjust. Resolved by the host from the record's +8 index.
    void* (*aggressorRecord)(HeRecord* h);
    // VIBE_Npc_AdjustRelationByMood(aggressor, kind): apply one mood/relation delta.
    void  (*adjustRelationByMood)(void* aggressor, i8 kind);
    // VIBE_MeisterAi_RegisterApEvent(victimWord, -apDamage, 0, msgBuf): the DAMAGE
    // event. `victimWord` is the victim record's id/alive word (*(_WORD*)victim).
    void  (*registerApEvent)(u16 victimWord, i32 negAp);
    // VIBE_He_SendEntityMessage + family defeat-counter bump on the knockout blow:
    // emit the "x beats y" message for the victim and ++victimFamily[+24]. Modeled
    // as one host call (victimWord identifies the victim record).
    void  (*sendDefeatMessage)(u16 victimWord);
    // VIBE_Person_GetFamilyRecord(victim) -> family record; ++*(family+24*4). Folded
    // into sendDefeatMessage above for the host, but exposed for the unit test to
    // observe the counter bump distinctly.
    void  (*bumpVictimFamilyDefeats)(void* victim);
    // Restore the saved-pose snapshot into the record (+82 qword/+90 dword/+94 word)
    // then VIBE_Command_QueueRequestEntity29(-1, h). One host call.
    void  (*restorePoseAndRequeue)(HeRecord* h);
};
void SetBrawlHooks(const BrawlHooks* hooks);
const BrawlHooks& GetBrawlHooks();

// The brawl-step outcome (what the rule resolved this tick), for golden testing.
enum class BrawlOutcome {
    Freed,         // terminal state -> handler entry freed
    Busy,          // gated out (busy flag set, or packet still in flight)
    BlowLanded,    // state 0: a blow landed (mood + AP damage applied)
    BlowMissedVictimGone, // state 0 but victim record gone -> pose restore + requeue
    Knockout,      // state 1: the final blow (defeat message + family bump)
    KnockoutVictimGone,   // state 1 but victim gone -> pose restore + requeue
    Idle,          // no actionable state this tick
};

// gilde.exe 0x4d201c — VIBE_CharAction_BrawlStep.
//   if (state == -2 || state == -1) return FreeHandlerEntry(h);   // terminal
//   if ((flags & 4) == 0 && (packet == -1 || PacketStatus(packet) != 0)) {
//       packet = -1;
//       if (state != 0) { if (state == 1) { ...knockout... } }
//       else { ...landed blow... }
//   }
// Mutates the record (+112/+132/+186/+86) and routes the leaves through hooks.
// Returns the resolved outcome.
BrawlOutcome BrawlStep(HeRecord* h);

// The knockout gate, exposed for golden testing: a brawl ends (state -> 1) once
// the aggressor has landed 5 blows. `hitCountAfterIncrement` is +186 post-bump.
constexpr u8 kBrawlKnockoutHits = 5;
inline i32 BrawlNextState(u8 hitCountAfterIncrement) {
    return hitCountAfterIncrement >= kBrawlKnockoutHits ? 1 : 0;
}

// The per-blow action-time cadence added to +86 each landed blow.
constexpr u16 kBrawlActionTicks = 24;

} // namespace guild::sim
