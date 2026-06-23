#pragma once
// character_recon_tavern — faithful 1:1 reconstruction of the one PURE record /
// slot-scan rule of the VIBE_Character cluster (gilde.exe).
//
// Translated functions (this TU):
//   VIBE_Character_FindTavernTargetSlot   0x4d5c60
//
// The rest of the VIBE_Character cluster (FadeOutSlots, SetAllFreezeState,
// AttachTransport, UpdateTransportAttach, ReleaseMorphAni, the Cmd* script-command
// handlers, RegisterScriptCommands, RefreshAllFlags, EnsureObjectAvatar/
// EnsureBuildingAvatar, PreloadSceneAnimations, SyncTurnState, Spawn*Actor, etc.)
// is dominated by scene-graph / rendering / animation / action-queue / script-VM
// coupling, NOT by pure character math. Those are deferred (see the cluster
// manifest) rather than papered over with cheap analogues (project rule 8).
//
// FindTavernTargetSlot is the sole cluster member that is pure record / slot logic:
// it validates a scene-object record and then linearly scans a 16-entry slot table
// for the slot whose entry id matches the object's "tavern target" record id and
// whose owner-word is assigned (!= 0xFFFF). The only out-of-TU dependency is the
// already-reconstructed turn-classification predicate VIBE_Character_IsObjectForTurn
// (0x453228, src/sim/character_state), routed here through a function-pointer hook
// to avoid coupling this pure scan to that file's record abstraction.
//
// Original prototype (recovered from the call site / register usage):
//   BOOL __usercall VIBE_Character_FindTavernTargetSlot(
//       int obj@<eax>, slot_array@<edx>, int* outIndex@<ebx>)
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Scene-object record view (gilde.exe — the eax record FindTavernTargetSlot
// classifies). Only the fields the function touches are modeled; everything
// else is opaque byte padding so the offsets stay byte-faithful.
//   +0x000 (word)  markerWord     -1 (0xFFFF) == free / invalid slot
//   +0x002 (byte)  typeByte       must be 0 for a tavern-eligible object
//   +0x008 (byte)  enabledByte    must be nonzero (object is enabled)
//   +0x184 (dword) tavernTarget   pointer to the linked "tavern target" record
// The turn-classification (IsObjectForTurn) reads further fields of the same
// record; that is delegated to the predicate hook below.
// ---------------------------------------------------------------------------
struct TavernObject {
    u16   markerWord;    // +0x000
    u8    typeByte;      // +0x002
    u8    enabledByte;   // +0x008
    const void* tavernTarget;  // +0x184  (the linked target record)
};

// ---------------------------------------------------------------------------
// "Tavern target" record (gilde.exe — *(obj+0x184)). Only +0x2C is read:
//   +0x2C (dword) matchId   the id the slot-table entries are matched against.
// ---------------------------------------------------------------------------
struct TavernTarget {
    int matchId;  // +0x2C
};

// ---------------------------------------------------------------------------
// Slot-table entry (gilde.exe — *(slot_array + 12*i)). The slot array is an
// array of 12-byte slots; each slot's first dword is a pointer to one of these
// entries (null pointer terminates the table early, hard cap 16 slots).
//   entry+0x01 (dword) entryId    matched against TavernTarget.matchId
//   entry+0x27 (word)  ownerWord  0xFFFF == unassigned (rejected)
// (The odd +1 / +0x27 offsets are exactly the original unaligned field reads.)
// ---------------------------------------------------------------------------
struct TavernSlotEntry {
    int entryId;   // +0x01 (unaligned dword)
    u16 ownerWord; // +0x27 (unaligned word)
};

// One 12-byte slot in the table: first dword is the entry pointer.
struct TavernSlot {
    const TavernSlotEntry* entry;  // +0x00
    u8  pad[8];                    // +0x04 .. +0x0B (untouched)
};

// ---------------------------------------------------------------------------
// Turn-classification dependency. The engine calls VIBE_Character_IsObjectForTurn
// (0x453228) on the object record; here we accept it as a predicate so this pure
// scan does not pull in the character_state record abstraction. The default
// predicate is a faithful no-op that would change behavior, so callers MUST wire
// the real predicate (see SetTavernTurnPredicate); tests supply a golden one.
// ---------------------------------------------------------------------------
using TavernTurnPredicate = bool (*)(const TavernObject* obj);
void SetTavernTurnPredicate(TavernTurnPredicate pred);
TavernTurnPredicate GetTavernTurnPredicate();

// ---------------------------------------------------------------------------
// gilde.exe 0x4d5c60 — VIBE_Character_FindTavernTargetSlot.
//
// Returns true and writes the matching slot index to *outIndex when:
//   obj.markerWord != 0xFFFF AND obj.enabledByte != 0 AND obj.typeByte == 0
//   AND obj.tavernTarget != null AND IsObjectForTurn(obj)
//   AND some slot i in [0,16) has slots[i].entry != null,
//       slots[i].entry->entryId == obj.tavernTarget->matchId,
//       and slots[i].entry->ownerWord != 0xFFFF,
//       with no earlier slot null-terminating the scan.
// Otherwise returns false and leaves *outIndex untouched.
//
// `slots` is the 16-slot table (array of TavernSlot, stride 12). `pred` is the
// turn predicate (defaults to GetTavernTurnPredicate()).
bool FindTavernTargetSlot(const TavernObject* obj, const TavernSlot* slots,
                          int* outIndex);
bool FindTavernTargetSlot(const TavernObject* obj, const TavernSlot* slots,
                          int* outIndex, TavernTurnPredicate pred);

} // namespace guild::sim
