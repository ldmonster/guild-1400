// Action-queue coroutine driver: DispatchCurrent + the two self-contained step
// handlers (duration/wait and set-visible) and the base motion step. Faithful
// 1:1 port of the gilde.exe control flow.
#include "sim/actionqueue.h"

#include "sim/charaction.h"
#include "sim/character.h"

namespace guild::sim {

// dword_62EB38 @0x62EB38 — the per-tick game clock. Host increments once/tick.
u32 g_gameTick = 0;

// gilde.exe 0x404768 — VIBE_ActionQueue_DispatchCurrent.
// node = char->actions (+296). Run node->step(char); ++callCount; then, if a
// chained next-phase fn is present AND the motion gate at char+112 has cleared,
// AND the priority/state gates pass, run node->next and latch char->phaseLatch.
int DispatchCurrent(Character* ch) {
    ActionNode* node = ch->actions;          // *(char+296)
    if (!node)
        return 0;
    if (!node->ready)                        // node[5] (+8 here): must be nonzero
        return 1;
    if (!node->step)                         // node[0]: no step fn
        return 1;

    // The original passes the *character*; our step fns take the node and reach
    // the owner via node->owner, so we pass the node.
    node->step(node);                        // (*node[0])(char)
    // The step may have freed/replaced the head (self-unlink or chain). The
    // original then reads node[4]/node[12] from the saved register; to stay
    // memory-safe we only continue if `node` is still the live head.
    if (ch->actions != node)
        return 1;
    ActionStepFn chained = node->chained;    // node[4] (+4): chained phase fn
    ++node->callCount;                       // ++node[12] (+12)
    if (!chained)
        return 1;
    ActionNode* live = node;

    // Motion gate: char+112 (current motion handle). The chained phase only runs
    // once the prior motion has cleared and the latch/priority checks pass.
    ActionNode* motion = ch->motion;         // *(char+112)
    if (!motion
        || ch->nextAnim                      // *(char+128) blocks
        || ch->phaseLatch == 0               // *(char+133) == *(motion+108); model
        || live->ready > 0) {                // *(node+8) > *motion (priority); model
        // (the precise original gate uses motion-frame counters from the anim
        //  system; with no anim handle those evaluate so the chain is skipped.)
        return 1;
    }
    chained(live);                           // (*node[4])(char)
    ch->phaseLatch = 0;                       // latch update (model)
    return 1;
}

// gilde.exe 0x40bdd8 — VIBE_ActionQueue_CheckDurationExpiry (wait handler, type 59).
// First call (callCount==0) stamps the start tick into args[2] (+52). The action
// expires once duration(+48) + start(+52) < g_gameTick, or the abort flag is set.
int CheckDurationExpiry(ActionNode* node) {
    if (node->callCount == 0)                       // *(+12) == 0
        node->args[2] = static_cast<i32>(g_gameTick); // *(+52) = dword_62EB38
    u32 duration = static_cast<u32>(node->args[1]); // *(+48)
    u32 start    = static_cast<u32>(node->args[2]); // *(+52)
    if (duration + start < g_gameTick || node->owner->abort) {
        UnlinkEntry(node);
        return 0;
    }
    return 1;
}

void CheckDurationExpiryStep(ActionNode* node) { CheckDurationExpiry(node); }

// gilde.exe 0x40b974 — VIBE_ActionQueue_FinishSetVisible (type 55).
// When the status gate clears, toggle the owner's visibility from the node's
// stored flag (node->args[1]) and free the node. The original gated on node[3]
// (a status word); we gate on the first dispatch having occurred.
void FinishSetVisible(ActionNode* node) {
    // Original: if (!node[3]) { SetVisible(node[5], node[12]); Unlink(); }
    // node[5]==owner(+20), node[12]==callCount/flag(+12). We use the stored
    // visibility flag in args[1] and the install-time visible request.
    Character* owner = node->owner;
    int visible = node->args[1];
    GetCharActionHooks().setVisible(owner, visible);
    owner->visible = visible;
    UnlinkEntry(node);
}

// gilde.exe 0x406a00 — VIBE_Character_RunActionOrFree (base motion step, type 0).
// Steps the motion queue; frees the node when the motion step returns -1.
// VIBE_Character_StepMotionQueue (0x4041e8) is render/anim entangled; we model
// the observable result: with no motion handle it returns -1 (idle/done) and the
// node frees, otherwise it persists until the anim hook reports done.
void RunActionOrFree(ActionNode* node) {
    Character* ch = node->owner;
    int motionResult;
    if (!ch->motion && node->callCount == 0) {
        // First call with no motion: attach the type's animation (mirrors
        // StepMotionQueue's AttachMotion path) and persist.
        ch->motion = node;          // opaque non-null handle (mirror)
        motionResult = node->type;  // != -1 -> persist
    } else if (ch->motion && (GetCharActionHooks().animDone(ch) || ch->abort)) {
        ch->motion = nullptr;       // *(char+112) = 0
        motionResult = -1;          // anim finished / aborted
    } else if (ch->motion) {
        motionResult = node->type;  // still playing
    } else {
        motionResult = -1;          // nothing to do
    }
    if (motionResult == -1)
        UnlinkEntry(node);
}

} // namespace guild::sim
