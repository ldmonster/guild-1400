#pragma once
// character_state — the flag / state ACCESSORS and the per-turn classification
// RULES of the Character cluster (gilde.exe). These are small, pure predicate /
// flag functions: they read the object/owner record's type byte, owner, AI-control
// flags and the network turn-stride, and they manage the live-actor redraw flag
// (+140 bit 0) across the active scene. Faithful 1:1 ports; the only render leaf
// (the +140&1 "refresh this actor" effect) is routed through a hook.
//
// Translated functions (this TU):
//   VIBE_Character_IsActiveType          0x45263c   (type ∈ {2,3,4,5})
//   VIBE_Character_IsActiveTypeForTurn   0x452660   (+ network turn-stride gate)
//   VIBE_Character_IsObjectForTurn       0x453228
//   VIBE_Character_IsOwnerForTurn        0x453260
//   VIBE_Character_IsAiControllableForTurn 0x4532d0
//   VIBE_Character_IsIdle                0x43d9f4   (action head == 0)
//   VIBE_Character_IsSitting             0x43ddd8   (+140 & 0x10)
//   VIBE_Character_ProcessFlaggedLocal   0x40204c   (apply pivot for flagged)
//   VIBE_Character_RefreshFlaggedLocal   0x40208c   (re-show flagged actors)
#include "guild/common/types.h"

namespace guild::sim {

struct LiveActor;  // character_query.h

// ===========================================================================
// Object / owner record view (the "person"/object the turn-rules classify). The
// rules read these by byte offset on a 169-byte object record OR a 536-byte
// person record; we model just the touched fields. (a1+2 type, a1+4 id, a1+356
// "is-master" byte, a1+364 owner-object ptr, a1+39 owner-player word.)
// ===========================================================================
struct TurnObject {
    u8  type;          // +2   record-type byte (2/3/4/5 == "active" actor)
    int id;            // +4   record id (used for the network turn-stride modulo)
    u8  isMaster;      // +356 player-owned ("Meister") flag
    int ownerKindByte; // owner-object's +0 kind byte (IsAiControllable: 7 excluded)
    int ownerId;       // owner-object's id (turn-stride modulo key)
    u16 ownerPlayer;   // owner-object's +39 owner-player word (0xFFFF == none)
    bool hasOwner;     // +364 owner pointer present
};

// ===========================================================================
// Network turn-stride state. The "*ForTurn" rules decide whether THIS peer owns
// the record this turn: id % turnStride == myTurnSlot. Mirrors the engine globals:
//   byte_63CC1D  turn stride (number of peers; 0 == single, no gate)
//   dword_764CE0 standalone flag (-1 == not networked: every record is mine)
//   dword_764CF4 my turn slot (networked)
//   dword_63CC20 my turn slot (standalone-with-stride)
// ===========================================================================
struct TurnState {
    u8  stride;        // byte_63CC1D
    int standalone;    // dword_764CE0 (-1 == standalone)
    int myTurnSlot;    // dword_764CF4
    int localTurnSlot; // dword_63CC20
};
extern TurnState g_turn;  // the active turn-stride state
void SetTurnState(const TurnState& ts);

// ===========================================================================
// Flag/state hook — the one render leaf the flagged-local refresh pass calls.
// ===========================================================================
struct CharStateHooks {
    // VIBE_Object_SetPivotVector(mesh, &actor+84): re-pivot a redraw-flagged actor.
    // ProcessFlaggedLocal calls it for every flagged actor in the active scene.
    void (*applyPivot)(LiveActor* a);
    // VIBE_Character_ApplyVisibilityState(actor): re-show a flagged actor.
    // RefreshFlaggedLocal calls it for flagged actors that are not mid-talk.
    void (*applyVisibility)(LiveActor* a);
};
void SetCharStateHooks(const CharStateHooks* hooks);
const CharStateHooks& GetCharStateHooks();

// ===========================================================================
// Type / turn classification rules.
// ===========================================================================

// gilde.exe 0x45263c — VIBE_Character_IsActiveType. True iff type ∈ {2,3,4,5}.
bool IsActiveType(const TurnObject* o);

// gilde.exe 0x452660 — VIBE_Character_IsActiveTypeForTurn. IsActiveType AND the
// network turn-stride gate (id % stride == my slot), with the standalone /
// networked slot selection. With stride 0 the gate is open.
bool IsActiveTypeForTurn(const TurnObject* o);

// gilde.exe 0x453228 — VIBE_Character_IsObjectForTurn. True if standalone (-1),
// OR type == 6 (always-local), OR id % stride == my slot.
bool IsObjectForTurn(const TurnObject* o);

// gilde.exe 0x453260 — VIBE_Character_IsOwnerForTurn. type must be <= 1; then the
// turn-stride key is the OWNER person's id (via owner-player word), defaulting to
// the object's own id. True if standalone or key % stride == my slot.
bool IsOwnerForTurn(const TurnObject* o);

// gilde.exe 0x4532d0 — VIBE_Character_IsAiControllableForTurn. type ∈ {1,2} and
// isMaster set; the owner person's kind must not be 7 (player); then standalone OR
// kind == 6 OR ownerId % stride == my slot.
bool IsAiControllableForTurn(const TurnObject* o);

// gilde.exe 0x43d9f4 — VIBE_Character_IsIdle. True iff the actor has no action head
// (+296 == 0). (The engine reports an error and returns 0 for a null handle.)
bool IsIdle(const LiveActor* a);

// gilde.exe 0x43ddd8 — VIBE_Character_IsSitting. True iff (+140 & 0x10) is set.
bool IsSitting(const LiveActor* a);

// ===========================================================================
// Flagged-local passes (apply the +140 bit-0 redraw flag across the active scene).
// ===========================================================================

// gilde.exe 0x40204c — VIBE_Character_ProcessFlaggedLocal. For each live actor in
// the active universe with the redraw flag (+140 & 1) set, applies the pivot
// vector (render leaf via the hook). Returns the number of actors processed.
int ProcessFlaggedLocal();

// gilde.exe 0x40208c — VIBE_Character_RefreshFlaggedLocal. For each live actor in
// the active universe with the redraw flag set: if it is mid-talk (action head
// type == 45) it just clears the flag; otherwise it re-applies the visibility
// state (render leaf) and then clears the flag. Returns the number cleared.
int RefreshFlaggedLocal();

// ===========================================================================
// Work-season eligibility (the clean DATA rule extracted from
// VIBE_Character_UpdateWorkScripts 0x4b9a5c). Production/mining/search/training
// work scripts only run when the current month-of-year falls inside the actor's
// seasonal work window. The window edges are two 16-entry float tables indexed by
// the season class (the 0..15 byte the work-script pass derives from the day):
//   flt_6476FC @0x6476FC : per-season window MIN (month, inclusive)
//   flt_64770C @0x64770C : per-season window MAX (month, exclusive)
// The gate is: monthOfYear >= MIN[season] && monthOfYear < MAX[season].
// (Exactly the `v55 < flt_64770C[v7] && v55 >= flt_6476FC[v7]` test, with
// v55 == (double)WORD2(qword_13CE852) == the current month-of-year.)
// ===========================================================================
extern const float kWorkSeasonMin[16];  // flt_6476FC
extern const float kWorkSeasonMax[16];  // flt_64770C
bool IsInWorkSeason(int seasonClass, float monthOfYear);

} // namespace guild::sim
