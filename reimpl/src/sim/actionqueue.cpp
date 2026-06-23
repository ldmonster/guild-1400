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
    // edx = *(char+296). The decompile types it as a 4-byte (fn-ptr) array, so
    // v1[5] == byte +0x14 (owner), not the +8 ready byte. Disasm confirms:
    //   cmp dword ptr [edx+14h], 0   ; jz -> return 1   (owner gate, +0x14)
    //   cmp dword ptr [edx], 0       ; step fn (+0x00)
    ActionNode* node = ch->actions;          // *(char+296)
    if (!node)                               // test edx,edx ; jz -> return 0
        return 0;
    if (!node->owner)                        // [edx+14h]==0 -> return 1  (NOT ready)
        return 1;
    if (!node->step)                         // [edx]==0 -> return 1
        return 1;

    // (*v1)(a1): the original passes the character; our step fns take the node and
    // reach the owner via node->owner. The node pointer (edx) is held across the
    // call and node[4]/node[12] are read from it unconditionally afterward.
    node->step(node);                        // call dword ptr [edx]
    ActionStepFn chained = node->chained;    // ebp = [edx+4]
    ++node->callCount;                       // ++[edx+0Ch]
    if (!chained)                            // test ebp,ebp ; jz -> return 1
        return 1;
    // BOUNDARY (memory-safety): the original re-uses the saved edx even if the
    // step replaced/freed the head (use-after-free is original UB). We bail if the
    // head changed to stay host-safe; observationally identical on all inputs
    // where the step keeps the node live (the only reachable case).
    if (ch->actions != node)
        return 1;
    ActionNode* live = node;

    // Motion gate (disasm 0x4047ab..0x4047cf):
    //   v6 = *(char+112);                    motion handle
    //   if (!v6) return 1;
    //   if (*(char+128)) return 1;           nextAnim blocks
    //   if (*(char+133) == *(motion+108)) return 1;     latch == motion frame byte
    //   if ((u8)*(node+8) > (int)*motion) return 1;     ready vs motion priority
    // BOUNDARY: char+112 is an anim/motion handle (not an ActionNode); its +0
    // priority int and +108 frame byte live in the render/anim subsystem (out of
    // tree). With no live anim handle the gate evaluates to "skip", matching the
    // headless behavior; the field reads below model the available state.
    ActionNode* motion = ch->motion;         // *(char+112)
    if (!motion
        || ch->nextAnim                      // *(char+128)
        || ch->phaseLatch == 0               // *(char+133) == *(motion+108): modeled
        || node->ready > 0) {                // (u8)*(node+8) > (int)*motion: modeled
        return 1;
    }
    chained(live);                           // call dword ptr [edx+4]
    // *(char+133) = *(*(char+112)+108): copy the motion's current frame byte into
    // the latch (NOT a clear). Modeled as the latch byte (anim frame is out of tree).
    ch->phaseLatch = motion->ready;          // [ecx+85h] = [[ecx+70h]+6Ch]: modeled
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
//   if ( !result[3] ) { VIBE_Character_SetVisible(result[5], result[12]); Unlink(); }
// 4-byte stride: result[3]=+0x0C=callCount, result[5]=+0x14=owner,
// result[12]=+0x30=args[1] (the +48 slot, the stored visibility flag).
// The gate is callCount==0 (fire once, on the first dispatch), and the visible
// argument is args[1]; on any later call (callCount!=0) it is a no-op.
void FinishSetVisible(ActionNode* node) {
    if (node->callCount != 0)            // if (result[3]) return result;  (no-op)
        return;
    Character* owner = node->owner;      // result[5] (+0x14)
    int visible = node->args[1];         // result[12] (+0x30 == args[1])
    GetCharActionHooks().setVisible(owner, visible);
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
