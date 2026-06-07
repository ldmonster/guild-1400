// Live scene-actor driver + the representative per-action step handlers.
//
// Character_Update is a faithful port of the live-array walk and the
// action-dispatch handoff. The turn/take/drop/load-anim step handlers are ported
// at the control-flow level; their render/anim leaf effects (AttachAni,
// AttachItemToBone, CreateObjectAnim, …) are routed through the hook table so the
// coroutine state machine is observable in isolation.
#include "sim/character.h"

#include "sim/actionqueue.h"
#include "sim/charaction.h"
#include "sim/character_social.h"

#include <cstring>

namespace guild::sim {

// dword_66F0D0 @0x66F0D0 — 512 live-character slots (null == empty).
Character* g_characters[kCharacterCapacity] = {};
// dword_62D094 @0x62D094 — live count gate (Update returns 0 when this is 0).
int g_characterCount = 0;

void ResetCharacters() {
    std::memset(g_characters, 0, sizeof(g_characters));
    g_characterCount = 0;
}

// gilde.exe 0x405148 — VIBE_Character_Update.
// Brackets the frame with the tick clock, then for each live actor:
//   - skip if slot null or (+140 & 0x04) skip-update flag is set;
//   - run CheckAniMorph (render; omitted), then if the actor has an active
//     action (+296 / has script-active +296) dispatch the coroutine;
//   - else run the idle/social behaviour (FindNearbyInRadius -> spawn talk
//     action 45) which needs the heightmap/pathfinder and is DEFERRED.
// Returns 1 if it ran (live count > 0), else 0.
int CharacterUpdate() {
    // dword_62D008 = dword_62EB38 (frame-start bracket).
    if (g_characterCount == 0)
        return 0;

    for (int i = 0; i < kCharacterCapacity; ++i) {
        Character* ch = g_characters[i];
        if (!ch || (ch->flagsA & 0x04) != 0)   // null slot / skip-update
            continue;

        bool hasAction = (ch->actions != nullptr); // *(char+296) script active
        // VIBE_Character_CheckAniMorph(ch); — render/anim, omitted (deferred).

        if (hasAction) {
            // Sit/idle-anim teardown (+141 & 0x20) is render-only; the essential
            // step is the coroutine dispatch:
            DispatchCurrent(ch);
        } else {
            // Idle/social behaviour: find a nearby actor within 20.0 and enqueue
            // a "talk" (type 45) action, else attach a stand/sit idle anim. Now
            // implemented in character_social.cpp (UpdateIdleSocial does the
            // eligibility gate, the proximity scan and the dirty-mesh teardown).
            UpdateIdleSocial(ch);
        }
        // Transport/low-poly-mesh refresh (avatar[73], +492) — render, omitted.
    }
    // dword_62D004 = dword_62EB38 (frame-end bracket).
    return 1;
}

// ---------------------------------------------------------------------------
// Representative step handlers. Each is registered in the action catalog and
// invoked by DispatchCurrent with the live action node.
// ---------------------------------------------------------------------------

// gilde.exe 0x406a18 — VIBE_Character_TurnStepActionUpdate (type 7).
// The original builds a turn animation, computes the signed yaw delta, plays
// "dreh_90_links/rechts", and frees the node once the object-anim finishes. We
// translate the *state machine*: on the first call attach the turn anim and
// stash the target; each subsequent call advances one quantum via the turnStep
// hook; free the node when the turn completes.
void TurnStepActionUpdate(ActionNode* node) {
    Character* ch = node->owner;
    if (node->callCount == 0) {
        // First call: latch the target yaw quantum from args[1] (+48) and attach
        // the turn animation (mirrors AttachAni + CreateObjectAnim).
        ch->turnTarget = node->args[1];
        GetCharActionHooks().attachAnim(ch, "bewegung/dreh_90", 1);
        return;
    }
    // Subsequent calls: advance one step toward the target.
    if (GetCharActionHooks().turnStep(ch, node)) {
        UnlinkEntry(node);                     // turn complete -> free node
    }
}

// gilde.exe 0x40618c — VIBE_Character_TurnToTargetActionUpdate (type 53).
// Computes the angle to the target and chains a type-7 turn action immediately
// after itself, then frees itself. We model the chain via InsertActionAfter.
void TurnToTargetActionUpdate(ActionNode* node) {
    Character* ch = node->owner;
    // Original computes the signed angle through the bone chain and quantizes it;
    // here the caller passes the target quantum in args[1] (+48, the first
    // copied vararg slot).
    i32 turnArgs[2] = { node->args[1], node->args[1] };
    InsertActionAfter(ch, node, kActTurnAnim, turnArgs, 2);
    UnlinkEntry(node);
}

// gilde.exe 0x405c88 — VIBE_Character_TakeObjectActionUpdate (type 49).
// First call attaches the "take" movement anim. Once the anim reaches its
// trigger frame (animDone) the carried item is attached to the bone and the node
// frees. We model the carried result via the setCarried hook + ch->carried.
void TakeObjectActionUpdate(ActionNode* node) {
    Character* ch = node->owner;
    if (node->callCount == 0) {
        // Attach the take movement animation (mirrors AttachMovementAni).
        if (GetCharActionHooks().attachAnim(ch, "take", 1) == nullptr) {
            UnlinkEntry(node);                 // no anim -> finish immediately
            return;
        }
        return;
    }
    // NextAnim (+128) gating omitted; complete once the anim plays out.
    if (GetCharActionHooks().animDone(ch)) {
        int bone = (node->args[2] >> 24) & 0xFF;  // HIBYTE(*(node+53)) bone id
        GetCharActionHooks().setCarried(ch, bone, 1);
        ch->carried = bone + 1;                // mirror: nonzero == carrying
        UnlinkEntry(node);
    }
}

// gilde.exe 0x405f28 — VIBE_Character_DropObjectActionUpdate (type 50).
// Mirror of take: attaches the "drop" anim, then clears the carried item once
// the anim reaches its trigger frame, and frees the node.
void DropObjectActionUpdate(ActionNode* node) {
    Character* ch = node->owner;
    if (node->callCount == 0) {
        if (GetCharActionHooks().attachAnim(ch, "drop", 1) == nullptr) {
            int bone = (node->args[2] >> 24) & 0xFF;
            GetCharActionHooks().setCarried(ch, bone, 0);
            ch->carried = 0;
            UnlinkEntry(node);
            return;
        }
        return;
    }
    if (GetCharActionHooks().animDone(ch)) {
        int bone = (node->args[2] >> 24) & 0xFF;
        GetCharActionHooks().setCarried(ch, bone, 0);
        ch->carried = 0;                       // mirror: nothing carried
        UnlinkEntry(node);
    }
}

// gilde.exe 0x406250 — VIBE_Character_LoadAnimActionUpdate (type 54).
// First call attaches the named animation and marks +140 dirty-anim; later calls
// free the node once the animation has played out.
void LoadAnimActionUpdate(ActionNode* node) {
    Character* ch = node->owner;
    if (node->callCount == 0) {
        void* h = GetCharActionHooks().attachAnim(ch, ActionType(node->type).animName, 1);
        if (h) {
            ch->flagsA |= 0x02u;               // *(char+140) |= 2 (dirty-anim)
            ch->motion = node;                 // *(char+112) = handle (mirror)
        } else {
            UnlinkEntry(node);
        }
        return;
    }
    if (ch->motion && (GetCharActionHooks().animDone(ch) || ch->abort)) {
        ch->motion = nullptr;
        UnlinkEntry(node);
    }
}

} // namespace guild::sim
