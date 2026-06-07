#pragma once
// Action-queue coroutine driver for the Guild simulation (gilde.exe).
//
// DispatchCurrent is the per-character behaviour coroutine: each frame it runs
// the current action node's step fn, advances the call counter, and — when the
// motion gate clears — runs the node's chained "next-phase" fn. The duration
// (wait) handler and the set-visible finisher are the two self-contained step
// handlers that live here (the rest are render-entangled — see character.h).
//
// Translated functions:
//   VIBE_ActionQueue_DispatchCurrent      0x404768
//   VIBE_ActionQueue_CheckDurationExpiry  0x40bdd8   (the wait/duration handler)
//   VIBE_ActionQueue_FinishSetVisible     0x40b974
//   VIBE_Character_RunActionOrFree        0x406a00
//   VIBE_Character_StepMotionQueue        0x4041e8   (control-flow skeleton)
#include "guild/common/types.h"

namespace guild::sim {

struct ActionNode;
struct Character;

// The per-frame game-tick clock the duration handler compares against.
// Original: dword_62EB38 @0x62EB38. The host advances it once per simulation
// tick; CheckDurationExpiry stamps a node's start tick and expires it after
// `duration` ticks have elapsed.
extern u32 g_gameTick; // dword_62EB38

// gilde.exe 0x404768 — VIBE_ActionQueue_DispatchCurrent  (__usercall, eax=char).
// Runs the character's current action node (at char+296):
//   - bail if no node, node->ready == 0, or node->step == null;
//   - call node->step(char), then ++node->callCount;
//   - if node->next is set and the motion gate at char+112 has cleared
//     (and the priority/state gates pass), run node->next and latch +133.
// Returns 0 if there was no node, else 1.
int DispatchCurrent(Character* ch);

// gilde.exe 0x40bdd8 — VIBE_ActionQueue_CheckDurationExpiry (wait handler, type 59).
// On the first call (callCount==0) stamps node->args[2] (+52) with g_gameTick.
// Frees the node once  args[1](+48, duration) + args[2](+52, start) < g_gameTick,
// or the abort flag (node+400) is set. Returns 1 if the node still lives, else 0.
int CheckDurationExpiry(ActionNode* node);
// Step-fn (void-returning) adapter so the wait handler can be registered in the
// action-type catalog (DispatchCurrent ignores step return values).
void CheckDurationExpiryStep(ActionNode* node);

// gilde.exe 0x40b974 — VIBE_ActionQueue_FinishSetVisible (type 55). When the
// status word (node[3]/+12 here modeled as callCount-gate) clears, toggles the
// owner's visibility from the node's stored flag and frees the node.
void FinishSetVisible(ActionNode* node);

// gilde.exe 0x406a00 — VIBE_Character_RunActionOrFree (base step, type 0).
// Steps the character's motion queue; frees the node when motion returns -1
// (queue idle / aborted).
void RunActionOrFree(ActionNode* node);

} // namespace guild::sim
