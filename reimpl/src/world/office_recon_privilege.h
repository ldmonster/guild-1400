#pragma once
// office_recon_privilege.h — pure decision logic extracted 1:1 from the political
// office (Amt) / privilege (Privilegien) panel + session functions of gilde.exe.
//
// The functions in this cluster (VIBE_Privilege_Panel*, VIBE_Office_*Session*,
// VIBE_Office_ShowCandidateList*) are, in the original, GUI frame-loop dialogs:
// they build a Form (VIBE_GameTick_Finalize / VIBE_Form_*), spin
// VIBE_GameLogic_RunFrameLoop, and on the confirm/cancel button enqueue a lockstep
// network command (VIBE_Command_*). Those subsystems (Form/HUD/Vulkan, the
// command queue, the live word_12CE910 person array) are coupled leaves; per the
// project rules they are surfaced here as inert-default hooks.
//
// What is faithfully reconstructed here (the load-bearing, testable rules buried
// inside those dialogs) are the *pure decision predicates and dispatch codes*:
//
//   * The privilege availability gates (the `(flags & mask)` and category-match
//     idioms that decide whether a privilege action is allowed at all).
//   * The blackmail success roll (RandomModulo(8) <= matchCount).
//   * The shared confirm/cancel event-id dispatch (1210 / 1155) and the resulting
//     dword_631614 state code each panel writes.
//   * The evidence-detail aggregation flags (any-active, self-target suppression)
//     and the BuildEvidenceEntry return codes.
//   * The candidate-list paging math (row stride / capped count).
//
// Reused records: this module does NOT redefine any office/person record. It uses
// guild::world::OfficePersonRec (office_assign.h) for the held-office-type fields
// where a person view is needed.
//
// Functions touched (addr -> symbol -> what is reconstructed vs. deferred):
//   0x561400 VIBE_Privilege_PanelLawScroll      gate+dispatch  (GUI deferred)
//   0x561878 VIBE_Privilege_PanelInstillFear    dispatch       (GUI deferred)
//   0x5625b4 VIBE_Privilege_PanelCharm          gate+dispatch  (GUI deferred)
//   0x560c1c VIBE_Privilege_BlackmailConfirm    roll+gate+result(GUI deferred)
//   0x565b88 VIBE_Privilege_PanelEvidenceDetails aggregation   (GUI deferred)
//   0x5663b8 VIBE_Privilege_PanelEvidenceDetailsAlt (== EvidenceDetails shape)
//   0x56589c VIBE_Privilege_BuildEvidenceEntry  return codes   (entity scan deferred)
//   0x555f8c VIBE_Office_ShowCandidateListWithRoles  paging math (HUD deferred)
//   0x5564ac/0x5566dc/0x556858/0x556a2c           (same paging shape)
//   0x49d910 VIBE_Office_RenderSessionTimer      timer split    (render deferred)
//   0x49d9e0 VIBE_Office_DestroySessionActors    iteration      (Character deferred)
#include "guild/common/types.h"
#include "world/office_assign.h"  // OfficePersonRec (reused, NOT redefined)

namespace guild::world {

// ===========================================================================
// Privilege flag/category gates.
// ===========================================================================

// gilde.exe 0x561400 (VIBE_Privilege_PanelLawScroll head) and 0x561700
// (SendSimpleCmd): a privilege action against a target is blocked when the
// target's +457 flag byte has bit 2 (0x4) set. (Original: shows msg 6527 and
// returns 0.) Returns true when the action is ALLOWED (bit clear).
constexpr u32 kPrivBit457Immune = 0x4;  // person+457 & 4
inline bool PrivLawScrollAllowed(u8 targetFlags457) {
    return (targetFlags457 & kPrivBit457Immune) == 0; /*0x561419*/
}

// gilde.exe 0x561434 — PanelLawScroll text-variant select:
//   v5 = (*(person+9) != 0) + 6504  ->  6504 when +9 == 0, else 6505.
inline int PrivLawScrollTextId(u8 personByte9) {
    return (personByte9 != 0) + 6504; /*0x561434*/
}

// gilde.exe 0x5625ea — VIBE_Privilege_PanelCharm eligibility gate.
// The actor (the active player's selected person, dword_12CE914[...]+2) and the
// target a1 each carry a held-office "category" in the high byte of their record
// dword at +2 (i.e. record byte +5). When the two categories are EQUAL the charm
// is refused (original shows msg 6577/6578|6579 and returns 0). Returns true iff
// the charm may proceed (categories differ).
//   actorCat == LOBYTE was already the category byte (+5 of the record).
inline bool PrivCharmAllowed(u8 actorOfficeCat, u8 targetOfficeCat) {
    return actorOfficeCat != targetOfficeCat; /*0x5625ea*/
}

// gilde.exe 0x5625ec — PanelCharm "already affected" text select. When the
// actor's status low byte (LOBYTE(dword_12CE919[...])) is non-zero the dialog
// shows sub-id 6579, else 6578 (both under header 6577). Returns the sub-id.
inline int PrivCharmRefusalTextId(u8 actorStatusLow) {
    return actorStatusLow ? 6579 : 6578; /*0x5625ec*/
}

// ===========================================================================
// Blackmail (VIBE_Privilege_BlackmailConfirm, 0x560c1c).
// ===========================================================================

// gilde.exe 0x560c46 — BlackmailConfirm subject gate. The dialog only proceeds
// when the subject record exists, the target exists, and the subject's kind byte
// (record +2) is 6 or 7 (an office-holder kind). Returns true iff it proceeds.
inline bool PrivBlackmailSubjectOk(bool subjectValid, bool targetValid,
                                   u8 subjectKindByte) {
    if (!subjectValid || !targetValid)
        return false;                                  /*0x560c46*/
    return subjectKindByte == 6 || subjectKindByte == 7;
}

// gilde.exe 0x560d18..0x560d36 — the blackmail success roll fired on the confirm
// button (event 1210): success iff RandomModulo(8) <= matchCount, where
// matchCount is the count returned by VIBE_He_FindMatchingEntityIds (the number
// of incriminating-evidence entities found for the subject). The RNG draw is
// injected so the rule is testable deterministically.
//   roll in 0..7 (the value of RandomModulo(8)); success when roll <= matchCount.
inline bool PrivBlackmailRollSucceeds(int roll, int matchCount) {
    return roll <= matchCount; /*0x560d36*/
}

// gilde.exe 0x560e89 — after a successful blackmail the dialog inspects the
// subject's +456 flag dword: if bit 8 (0x100) is set it pops the "now holds
// office" confirmation (msg 6490). Returns true iff that follow-up fires.
constexpr u32 kPrivBit456HoldsOffice = 0x100; // person+456 & 0x100
inline bool PrivBlackmailHoldsOffice(u32 subjectFlags456) {
    return (subjectFlags456 & kPrivBit456HoldsOffice) != 0; /*0x560e89*/
}

// gilde.exe — BlackmailConfirm return value: 1 on a successful blackmail, 0 on
// failure / cancel. (v8 latched 1 in the success arm, 0 otherwise.)
inline int PrivBlackmailResult(bool success) { return success ? 1 : 0; }

// ===========================================================================
// Shared confirm/cancel dispatch (the dword_75BF38 button id + dword_631614
// state code every privilege panel frame-loop uses).
// ===========================================================================
// In each panel:
//   button id 1210 ("OK") -> run the action, then set the panel's done-state.
//   button id 1155 ("cancel") -> set state 2 (cancelled).
// The done-state each panel sets differs:
//   BlackmailConfirm: 1            (0x560e90 / 0x560d92)
//   PanelLawScroll  : 3 on success (0x5616bd), 2 on cancel
//   PanelInstillFear: 4 on success (0x561a32), 2 on cancel
//   PanelCharm      : 4 on success (0x562786), 2 on cancel
//   EvidenceDetails : 3 on confirm (0x565f1b), 1 on window-close (0x565edd)
constexpr int kPrivBtnConfirm = 1210; // dword_75BF38 == 1210
constexpr int kPrivBtnCancel  = 1155; // dword_75BF38 == 1155

enum class PrivPanel { LawScroll, InstillFear, Charm, BlackmailConfirm };

// The done-state code (dword_631614) a panel writes on a SUCCESSFUL confirm.
inline int PrivPanelConfirmState(PrivPanel p) {
    switch (p) {
        case PrivPanel::BlackmailConfirm: return 1; /*0x560e90*/
        case PrivPanel::LawScroll:        return 3; /*0x5616bd*/
        case PrivPanel::InstillFear:      return 4; /*0x561a32*/
        case PrivPanel::Charm:            return 4; /*0x562786*/
    }
    return 1;
}
// The cancel-state code (always 2) shared by the panels' 1155 branch.
constexpr int kPrivPanelCancelState = 2; // dword_631614 = 2

// ===========================================================================
// Evidence detail panel (VIBE_Privilege_PanelEvidenceDetails, 0x565b88) +
// BuildEvidenceEntry (0x56589c).
// ===========================================================================

// gilde.exe 0x565bb4 — EvidenceDetails: if the target id word == 0xFFFF the panel
// returns 0 (no target). Returns true iff the panel proceeds.
inline bool PrivEvidenceHasTarget(u16 targetIdWord) {
    return targetIdWord != 0xFFFF; /*0x565bc2*/
}

// gilde.exe 0x565bfe — if the evidence-match count is < 1 the panel returns 96
// ("no evidence"). Returns the early-out code, or 0 to continue.
constexpr int kPrivEvidenceNoTarget = 96; // matchCount < 1
inline int PrivEvidencePrecheck(int matchCount) {
    return matchCount < 1 ? kPrivEvidenceNoTarget : 0; /*0x565bfe*/
}

// gilde.exe 0x565c64 — while copying each evidence row (45-byte stride) the panel
// ORs in `(row.field37 == 1)` to a single "any-actionable-evidence" flag. Given
// the per-row field37 values, returns the aggregated flag (matches the loop's
// v28 |= (field37 == 1)).
inline bool PrivEvidenceAnyActionable(const i32* field37, int count) {
    int v28 = 0;
    for (int i = 0; i < count; ++i)
        v28 |= (field37[i] == 1); /*0x565c64*/
    return v28 != 0;
}

// gilde.exe 0x565c7e — the actor cannot use evidence against itself: when the
// inspecting person == the target person, the actionable flag is forced false
// (the confirm button is then disabled at 0x565f88). Returns the final flag.
inline bool PrivEvidenceConfirmEnabled(bool anyActionable, bool actorIsTarget) {
    return actorIsTarget ? false : anyActionable; /*0x565c82 / 0x565f88*/
}

// gilde.exe 0x56589c — VIBE_Privilege_BuildEvidenceEntry return codes:
//   16 : an entry was built / no judge needed (0x5658f2, 0x565adf)
//   64 : a required witness entity could not be found (0x565a6b / 0x565ae8)
// `judgeFound` models the early "found a kind-15 (judge) person" break; when no
// judge exists the original returns 16 immediately. `witnessesFound` models the
// two VIBE_ObjectSearch_FindOneByPaletteRange hits.
// WAVE-23: the FULL function body (judge scan + record build + witness search +
// command emission) is now reconstructed in world/privilege_panels_b.cpp
// (PrivBuildEvidenceEntry); this predicate is REUSED there for the exact return
// codes. The "entity scan deferred" note at the top of this file is obsolete.
constexpr int kPrivEvidenceBuilt   = 16; // command enqueued / no judge
constexpr int kPrivEvidenceNoWitness = 64; // a FindOneByPaletteRange missed
inline int PrivBuildEvidenceResult(bool judgeFound, bool witnessesFound) {
    if (!judgeFound)
        return kPrivEvidenceBuilt;                 /*0x5658f2: no judge -> 16*/
    return witnessesFound ? kPrivEvidenceBuilt     /*0x565adf*/
                          : kPrivEvidenceNoWitness; /*0x565a6b / 0x565ae8*/
}

// ===========================================================================
// Candidate-list paging math (VIBE_Office_ShowCandidateListWithRoles 0x555f8c
// and the three sibling layouts 0x5564ac/0x5566dc/0x556858/0x556a2c).
// ===========================================================================

// gilde.exe 0x555f96 — the list functions return 0 immediately when count == 0.
inline bool PrivCandidateListHasRows(unsigned count) {
    return count != 0; /*0x555f96*/
}

// gilde.exe 0x556020 / 0x55608c — per-row vertical layout. Row i is placed at
//   x = 75 ; y = 112*i + 40 ; the running label-y starts at 70 and steps +112.
// kCandidateRowStride is the per-row pixel step the dialog also stores into the
// page descriptor (dword_67EDE4[..] = 112 at 0x5560eb).
constexpr int kCandidateRowStride = 112; // 112*v6 + 40, label y step 112
constexpr int kCandidateRowX      = 75;  // *(v8+16) = 75
constexpr int kCandidateRowY0     = 40;  // + 40
inline int PrivCandidateRowY(unsigned row) {
    return kCandidateRowStride * static_cast<int>(row) + kCandidateRowY0;
}

// ===========================================================================
// Office session-actor management (VIBE_Office_DestroySessionActors 0x49d9e0,
// VIBE_Office_RenderSessionTimer 0x49d910).
// ===========================================================================

// The session actor table is dword_11AAFC0[16] (64 bytes / 4 = 16 slots). The
// model resolution + Character create/destroy are coupled leaves (handled via a
// hook below). The pure controls reconstructed here are the slot count and the
// session-timer split.
constexpr int kOfficeSessionActorSlots = 16; // 64-byte table / 4

// gilde.exe 0x49d911 — VIBE_Office_RenderSessionTimer formats an elapsed time as
// minutes : seconds : milliseconds from a 14x-scaled tick delta:
//   elapsed = 14 * (nowTick - startTick)        (ms domain)
//   minutes = elapsed / 60000
//   seconds = elapsed / 1000 % 60
//   millis  = elapsed % 1000
// Returns the three components (the actual "%2i : %2i : %3i ms" sprintf + render
// is deferred to the render hook).
struct SessionTimeSplit { i32 minutes; i32 seconds; i32 millis; };
inline SessionTimeSplit OfficeSessionTimeSplit(u32 nowTick, u32 startTick) {
    u32 elapsed = 14u * (nowTick - startTick);     /*0x49d942 etc.*/
    SessionTimeSplit s;
    s.minutes = static_cast<i32>(elapsed / 60000u);     // /0xEA60
    s.seconds = static_cast<i32>(elapsed / 1000u % 60u);// /0x3E8 %0x3C
    s.millis  = static_cast<i32>(elapsed % 1000u);      // %0x3E8
    return s;
}

// gilde.exe 0x49d9e0 — VIBE_Office_DestroySessionActors. Walks the 16-slot actor
// table; for each non-null slot (and only while the global abort flag dword_6315BC
// is clear) it destroys the actor. The actual VIBE_Character_Destroy is a coupled
// render leaf, surfaced as a hook. Returns the number of actors destroyed.
//   Faithful control flow: the original's inner `while` advances past null slots
//   AND skips everything once dword_6315BC is set (early-return). We model
//   `abortFlag` as the dword_6315BC value sampled at entry.
using OfficeDestroyActorHook = void (*)(i32 actorHandle, void* ctx);
int OfficeDestroySessionActors(const i32 actors[kOfficeSessionActorSlots],
                               bool abortFlag,
                               OfficeDestroyActorHook hook, void* ctx);

} // namespace guild::world
