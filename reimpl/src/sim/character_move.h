#pragma once
// Character universe (scene) transition + turn-action helpers for the Guild
// simulation (gilde.exe). These sit alongside the per-tick walk movement
// (charaction_walk.h): the universe-transition action moves a character from one
// scene/universe to another (e.g. entering a building), and the turn helpers
// build/queue a rotation action.
//
//   VIBE_Character_Move2UniverseActionUpdate  0x4063c8  (action type 51)
//   VIBE_Character_TurnByAngleAction          0x408a10  (build a turn anim)
//
// The render/universe/object leaves (scene-slot switch, texture-set select,
// bone-chain transform, visibility) are out of scope and routed through a hook.
// The validation, id bookkeeping and Character field-clear logic is translated 1:1.
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Character fields the universe transition touches (32-bit Character record):
//   +44   (dword) current universe id (dword index 11 == [11])
//   +48,+52,+56 (dword) avatar move-flag scratch (cleared on transition;
//                       [14],[15],[16] in the engine -> 0)
//   +48 also reused as the action's "target universe" arg in the action node.
// The ACTION NODE arg slots (a1 base, same record):
//   +48 (Data[0]) target universe index
//   +52 (Data[1]) target id-in-universe (-1 == CH_ID_NONE)
//   +56 (Data[2]) target sub-id
// ===========================================================================
struct MoveUniverseState {
    int dataUniverse;   // node +48  (Action->Data[0])  target universe index
    int dataId;         // node +52  (Action->Data[1])  target id (-1 none)
    int dataSubId;      // node +56  (Action->Data[2])
    int curUniverse;    // character +44 (dword [11])   current universe id
    // Scratch cleared on transition (engine v13[14..16] = 0):
    int scratch48;      // character +48
    int scratch52;      // character +52
    int scratch56;      // character +56
    int valid;          // result of the MoveToUniverse leaf (0 == failed)
};

// Hook for the render/universe leaves of the transition.
struct MoveUniverseHooks {
    // VIBE_Character_MoveToUniverse: relocate the character to the universe
    // record; returns nonzero on success. (Engine: MoveToUniverse(ch, universe).)
    int (*moveToUniverse)(MoveUniverseState* st, int universeIndex);
    // VIBE_Universe_SwitchActiveSlot: make `universeIndex` the active scene slot.
    void (*switchActiveSlot)(int universeIndex);
    // VIBE_Character_SetVisible.
    void (*setVisible)(MoveUniverseState* st, int visible);
};
void SetMoveUniverseHooks(const MoveUniverseHooks* hooks);
const MoveUniverseHooks& GetMoveUniverseHooks();

// Result of the transition action step.
enum class MoveUniverseResult {
    kInvalidUniverse,   // Data[0] < 0
    kInvalidCombo,      // (Data0==0 && Data1!=-1) or (Data0!=0 && Data1==-1)
    kMoveFailed,        // MoveToUniverse leaf failed
    kOk,                // transitioned
};

// gilde.exe 0x4063c8 — VIBE_Character_Move2UniverseActionUpdate (action type 51).
// Validates the (universe, id) combination, performs the move via the hook,
// switches the active scene slot, clears the character's move scratch (+48..56),
// sets the new universe id (+44 = Data[1]) and sub-id, and toggles visibility.
// Returns the classification; on any non-kOk it reports an error (engine logs) and
// the action would be unlinked. This faithfully mirrors the engine's validation
// ladder and field-clear order.
MoveUniverseResult Move2UniverseActionUpdate(MoveUniverseState* st);

// ===========================================================================
// Turn-by-angle action build (VIBE_Character_TurnByAngleAction 0x408a10).
// ---------------------------------------------------------------------------
// Picks the turn animation by sign of the requested angle:
//   angle >= 0.2  -> "bewegung/dreh_90_links"  (left)
//   angle <  -0.2 -> "bewegung/dreh_90_rechts" (right)
//   else            no anim swap (keep current)
// The thresholds are dbl_6107C4 == 0.2 and dbl_6107CC == -0.2.
// Returns the chosen animation selector: +1 left, -1 right, 0 none.
// ===========================================================================
int TurnPickAnimation(double angle);

} // namespace guild::sim
