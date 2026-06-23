// charaction_misc — the deferred sound/sample/use-gate CharAction steps, the
// walk builders, the transparency/morph fade interpolation, and the He group-
// interaction coroutine. Faithful 1:1 port of the gilde.exe control flow; render/
// anim/sound/command leaves routed through the module hook tables.
#include "sim/charaction_misc.h"

#include "sim/actionqueue.h"
#include "sim/charaction.h"
#include "sim/charaction_walk.h"  // g_tickEnd / g_tickStart (frame brackets)
#include "sim/character.h"

#include "crt/rand.h"
#include "util/math_random.h"
#include "util/math_rng_float.h"

#include <cmath>
#include <cstring>

namespace guild::sim {

// --- recovered constants (decoded byte-for-byte; see header) ----------------
const double kFadeRatePerTick  = 0.02;   // dbl_6109E4
const double kFadeSocialThresh = 0.1;    // dbl_6109EC
const double kFadeAlphaScale   = 255.0;  // dbl_6109F4
const float  kFadeFullOpaque   = 255.0f; // flt_6109FC
const double kGroupDwellThresh = 3.0;    // dbl_61EB2C
const float  kGroupMaxSize     = 5.0f;   // flt_61EB34
const float  kGroupProbScale   = 0.2f;   // flt_61EB38
const double kGroupAccumScale  = 0.1;    // dbl_61EB3C

// ---------------------------------------------------------------------------
// Hook tables (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const MiscActionHooks* g_misc = nullptr;

void* DefAttachMovementAni(Character*, const char*, int) { return reinterpret_cast<void*>(1); }
void* DefAttachAni(Character*, const char*, int)         { return reinterpret_cast<void*>(1); }
int   DefStepMotionQueue(Character*)                     { return -1; }
int   DefCheckQueueReady(Character*)                     { return 1; }
void  DefStopSample(Character*)                          {}
int   DefSceneSlotIndex(void*)                           { return 0; }
int   DefWorldToTile(Character*, int, int* c, int* r)    { if (c) *c = 0; if (r) *r = 0; return 1; }
Character* DefFindNearby(Character*, float)              { return nullptr; }

const MiscActionHooks g_miscDefault = {
    DefAttachMovementAni, DefAttachAni, DefStepMotionQueue, DefCheckQueueReady,
    DefStopSample, DefSceneSlotIndex, DefWorldToTile, DefFindNearby,
};

const GroupInteractHooks* g_gi = nullptr;
void* DefFindPersonById(i32)                             { return nullptr; }
int   DefPersonReady(void*)                              { return 1; }
void  DefFreeHandlerEntry(HeRecord*)                     {}
void  DefEnqueueObjectInteraction(i32, i32, i32)         {}
void  DefQueueRequest39(i32, i32, i32)                   {}
const GroupInteractHooks g_giDefault = {
    DefFindPersonById, DefPersonReady, DefFreeHandlerEntry,
    DefEnqueueObjectInteraction, DefQueueRequest39,
};
} // namespace

void SetMiscActionHooks(const MiscActionHooks* h) { g_misc = h; }
const MiscActionHooks& GetMiscActionHooks() { return g_misc ? *g_misc : g_miscDefault; }

void SetGroupInteractHooks(const GroupInteractHooks* h) { g_gi = h; }
const GroupInteractHooks& GetGroupInteractHooks() { return g_gi ? *g_gi : g_giDefault; }

// ===========================================================================
// Sound action (type 46).  gilde.exe 0x4055d4.
// ---------------------------------------------------------------------------
// Original (a1 = node, +20 = owner ch, +12 = callCount, +48 = args[1] sound count
// (-1 = endless), +400 = abort, +376 = speed; ch+140 dirty-anim, ch+112 motion):
//   if (callCount) {
//     if (abort || count != -1) {
//       if (count == -1) ch+140 &= ~2;
//       if (CheckQueueReady(ch)) { Unlink(node); CheckAniMorph(ch); }
//     }
//   } else {
//     if (count == -1) ch+140 |= 2;
//     if (count < 1) count = 1;
//     AttachMovementAni(ch, name, count);
//   }
//   if (ch+112) { if ((node+376 & 0x7FFFFFFF)) anim+96 = node+376; }
void SoundActionUpdate(ActionNode* node) {
    Character* ch = node->owner;
    const MiscActionHooks& h = GetMiscActionHooks();
    i32 count = node->args[1];                 // *(+48)

    if (node->callCount) {                     // *(+12) != 0
        if (ch->abort || count != -1) {        // *(+400) || *(+48) != -1
            if (count == -1)
                ch->flagsA &= ~0x02u;          // ch+140 &= ~2
            if (h.checkQueueReady(ch)) {
                UnlinkEntry(node);
                // CheckAniMorph(ch) — render/anim, omitted (re-eval next frame).
                return;
            }
        }
    } else {
        if (count == -1)
            ch->flagsA |= 0x02u;               // ch+140 |= 2 (dirty-anim)
        i32 mode = count;
        if (mode < 1)
            mode = 1;                           // LOBYTE(v3) = 1
        h.attachMovementAni(ch, node->animBuf, mode);
    }
    // Propagate the playback speed into the live anim if the avatar is animating.
    if (ch->motion) {
        // (node+376 & 0x7FFFFFFF) != 0 -> speed is a real (nonzero) float.
        u32 bits;
        std::memcpy(&bits, &node->speedScale, sizeof(bits));
        if ((bits & 0x7FFFFFFFu) != 0) {
            // anim+96 = node speed (mirrored: the misc model has no anim record).
            ch->animTicks = static_cast<int>(node->speedScale); // observable mirror
        }
    }
}

// gilde.exe 0x405670 — VIBE_Character_CreateSoundAction.
ActionNode* CreateSoundAction(Character* ch, const char* name, int soundCount) {
    ActionNode* n = QueueInsertEntry(ch);
    if (!n)
        return nullptr;
    n->callCount = 0;                          // +12
    n->step  = &SoundActionUpdate;             // +0
    n->type  = kActSoundEffect;                // +9 = 46
    n->speedScale = 1.0f;                       // +376 = 1065353216 (== 1.0f)
    n->next_link = nullptr;                     // +40
    n->state = 0;                               // +16
    n->owner = ch;                              // +20
    n->ready = 1;                               // +8 (dispatchable)
    n->args[1] = soundCount;                    // +48
    std::memset(n->animBuf, 0, sizeof(n->animBuf));
    if (name) {
        std::size_t k = 0;
        while (name[k] && k + 1 < sizeof(n->animBuf)) { n->animBuf[k] = name[k]; ++k; }
    }
    return n;
}

// ===========================================================================
// Play-sample action (type 47).  gilde.exe 0x405740.
// ---------------------------------------------------------------------------
// Original (a1 = node, +12 = callCount, +240 = name, +396 & 1 = seek, ch+112/+116
// = anim handle, anim+108 = 1 active, anim+109: clear 0xC0 then |= 0x10):
//   if (callCount) {
//     if (StepMotionQueue(ch) == -1) { (switch slot) ch+140 |= 0x10; ch+140 &= ~2; Unlink; }
//   } else {
//     if (name[0]) {
//       ch+140 |= 2;
//       if (node+396 & 1) { h = AttachAni(ch,name,1); ch+112=ch+116=h; Anim_SeekToFrame(mesh,h,-1); }
//       else              { h = AttachAni(ch,name,1); ch+112=ch+116=h; }
//       if (ch+112) { anim+108=1; anim+109 &= 0x3F; anim+109 |= 0x10; }
//     } else {
//       if (ch+112) { anim+108=1; anim+109 &= 0x3F; anim+109 |= 0x10; }   // LABEL_9
//     }
//   }
void PlaySampleActionUpdate(ActionNode* node) {
    Character* ch = node->owner;
    const MiscActionHooks& h = GetMiscActionHooks();

    if (node->callCount) {                     // *(+12) != 0
        int r = h.stepMotionQueue(ch);
        if (r == -1) {
            // Resolve scene slot, switch active slot (render leaf), then:
            (void)h.sceneSlotIndex(ch->scene);
            ch->flagsA |= 0x10u;               // ch+140 |= 0x10 (idle-anim pending)
            ch->flagsA &= ~0x02u;              // ch+140 &= ~2
            UnlinkEntry(node);
        }
        return;
    }

    // First call.
    if (node->animBuf[0]) {                    // *(+240) != 0
        ch->flagsA |= 0x02u;                   // ch+140 |= 2 (dirty-anim)
        void* handle = h.attachAni(ch, node->animBuf, 1);
        ch->motion = static_cast<ActionNode*>(handle); // ch+112
        ch->animB  = handle;                   // ch+116 (mirror)
        if (node->seekFlag & 1) {
            // VIBE_Anim_SeekToFrame(mesh, handle, -1) — render leaf, omitted.
        }
    }
    // LABEL_9: mark the active anim (frame 0, flags) if a handle exists.
    if (ch->motion) {
        // anim+108 = 1; anim+109 = (anim+109 & 0x3F) | 0x10 — observable mirror:
        ch->animTicks = 1;
    }
}

// gilde.exe 0x405838 — VIBE_Character_CreatePlaySampleAction.
ActionNode* CreatePlaySampleAction(Character* ch, const char* name) {
    ActionNode* n = QueueInsertEntry(ch);
    if (!n)
        return nullptr;
    n->callCount = 0;                          // +12
    n->step  = &PlaySampleActionUpdate;        // +0
    n->type  = kActPlaySample;                 // +9 = 47
    n->next_link = nullptr;                     // +40
    n->state = 0;                               // +16
    n->owner = ch;                              // +20
    n->ready = 1;                               // +8 (dispatchable)
    std::memset(n->animBuf, 0, sizeof(n->animBuf));
    if (name) {
        std::size_t k = 0;
        while (name[k] && k + 1 < sizeof(n->animBuf)) { n->animBuf[k] = name[k]; ++k; }
    }
    return n;
}

// ===========================================================================
// Sample-loop action (type 48).  gilde.exe 0x4058f0.
// ---------------------------------------------------------------------------
// Original (a1=node, +12=callCount, +16=state, ch+112/+124 anim handles):
//   if ((ch+112 || ch+124) && !(node+16 & 1)) { node+12 = -1; }   // defer
//   else {
//     v3 = node+12; node+16 |= 1;
//     if (v3) { if (StepMotionQueue(ch) == -1) { StopSample(ch); Unlink; } }
//     else if ((ch+140 & 0x10) && (ch+112 = AttachAni(ch,name,1)) != 0) {
//          anim+108=1; anim+109 &= 0x3F; anim+109 |= 0x10;
//     } else { Unlink; }
//   }
void SampleLoopActionUpdate(ActionNode* node) {
    Character* ch = node->owner;
    const MiscActionHooks& h = GetMiscActionHooks();

    if ((ch->motion || ch->animB) && (node->state & 1) == 0) {
        node->callCount = -1;                  // *(+12) = -1 (defer this tick)
        return;
    }
    i32 started = node->callCount;             // v3 = *(+12)
    node->state |= 1;                          // *(+16) |= 1
    if (started) {
        if (h.stepMotionQueue(ch) == -1) {
            h.stopSample(ch);
            UnlinkEntry(node);
        }
        return;
    }
    // First real call: attach the looping sample only if idle-anim pending.
    if ((ch->flagsA & 0x10) != 0) {
        void* handle = h.attachAni(ch, node->animBuf, 1);
        ch->motion = static_cast<ActionNode*>(handle); // ch+112
        if (handle) {
            ch->animTicks = 1;                 // anim+108=1; anim+109 |= 0x10 (mirror)
            return;
        }
    }
    UnlinkEntry(node);
}

// gilde.exe 0x40598c — VIBE_Character_CreateSampleLoopAction.
ActionNode* CreateSampleLoopAction(Character* ch, const char* name) {
    if ((ch->flagsA & 0x10) == 0)              // ch+140 & 0x10 idle-anim pending
        return nullptr;                         // original returns 0
    ActionNode* n = QueueInsertEntry(ch);
    if (!n)
        return nullptr;
    n->callCount = 0;                          // +12
    n->step  = &SampleLoopActionUpdate;        // +0
    n->type  = kActSampleLoop;                 // +9 = 48
    n->next_link = nullptr;                     // +40
    n->state = 0;                               // +16
    n->owner = ch;                              // +20
    n->ready = 1;                               // +8 (dispatchable)
    std::memset(n->animBuf, 0, sizeof(n->animBuf));
    if (name) {
        std::size_t k = 0;
        while (name[k] && k + 1 < sizeof(n->animBuf)) { n->animBuf[k] = name[k]; ++k; }
    }
    return n;
}

// ===========================================================================
// Use-gate action (type 52).  gilde.exe 0x4059f4.
// ---------------------------------------------------------------------------
// Original (a1=node, +12=callCount, +20=owner ch, ch+136=scene, +368=destRoom,
// +52=mesh; resolves the gate object, world->tile, then chains walk/fade/relocate):
//   ch = *(+20);  if (!callCount) {
//     slot = IndexFromPointer(ch+136);  destRoom = node+368;
//     if (slot == destRoom) return Unlink(node);
//     SwitchActiveSlot(slot,1,destRoom,slot);
//     obj = Object_FindByHandle(0,288, node+304, 0, slot);
//     if (obj) { scene = dword_13ECF78[246*slot];
//                PointThroughBoneChain(obj, obj+19, world);
//                if (WorldToTileWithHeight(scene, world, &col, &row))
//                   InsertAction(ch, node|0x2D.., col);   // walk (type 45)
//                else Sprintf("UsingGate ... failed"); }
//     SwitchActiveSlot(destScene,1,_,_);
//     if (*(ch+52)+533 != 1) InsertAction(ch, node|0x38.., 0);  // fade (type 56)
//     ins = InsertAction(ch, node|0x33.., node+368);            // relocate (type 51)
//     memcpy(ins+240, node+240);                                // copy waypoints
//     InsertAction(ch, ins|0x38.., 1);                          // fade in (type 56)
//     return Unlink(node);
//   }
void UseGateActionUpdate(ActionNode* node) {
    Character* ch = node->owner;
    const MiscActionHooks& h = GetMiscActionHooks();

    if (node->callCount)                       // *(+12) != 0: nothing to do
        return;

    int slot = h.sceneSlotIndex(ch->scene);    // IndexFromPointer(ch+136)
    int destRoom = node->gateRoom;             // *(node+368)
    if (slot == destRoom) {                    // already there
        UnlinkEntry(node);
        return;
    }

    // Switch into the destination universe slot, resolve the gate world point.
    // SwitchActiveSlot(slot,1,destRoom,slot) — render/universe leaf, omitted.
    int col = 0, row = 0;
    bool resolved = h.worldToTile(ch, node->gateScene, &col, &row) != 0;
    if (resolved) {
        i32 walkArgs[2] = { col, row };        // InsertAction type 45 (walk)
        InsertActionAfter(ch, node, kActWalk, walkArgs, 2);
    }
    // SwitchActiveSlot(destScene,1,_,_) back — render/universe leaf, omitted.

    // Conditional visibility fade (type 56) when the destination is not "open"
    // (*(mesh+533) != 1). We model the gate-open test via ch->visible==0 default.
    {
        i32 fadeArg = 0;
        InsertActionAfter(ch, node, /*type 56*/ 56, &fadeArg, 1);
    }
    // Relocate action (type 51) carrying the destination room; copy waypoint name.
    i32 relocArg = node->gateRoom;
    ActionNode* reloc = InsertActionAfter(ch, node, kActMove2Univ, &relocArg, 1);
    if (reloc) {
        std::memcpy(reloc->animBuf, node->animBuf, sizeof(reloc->animBuf)); // +240 copy
        i32 fadeIn = 1;
        InsertActionAfter(ch, reloc, /*type 56*/ 56, &fadeIn, 1);          // fade in
    }
    UnlinkEntry(node);
}

// gilde.exe 0x405bec — VIBE_Character_CreateUseGateAction.
ActionNode* CreateUseGateAction(Character* ch, const char* name, int room,
                                int scene, float speed) {
    ActionNode* n = QueueInsertEntry(ch);
    if (!n)
        return nullptr;
    n->step = &UseGateActionUpdate;            // +0
    n->type = kActUseGate2;                    // +9 = 52
    n->owner = ch;                             // +20
    n->ready = 1;                              // +8 (dispatchable)
    n->callCount = 0;                          // +12
    std::memset(n->animBuf, 0, sizeof(n->animBuf));
    if (name) {                                 // node+240 gate object name
        std::size_t k = 0;
        while (name[k] && k + 1 < sizeof(n->animBuf)) { n->animBuf[k] = name[k]; ++k; }
    }
    n->gateRoom  = room;                       // node+368 (v9[92])
    n->gateScene = scene;                      // node+372 (v9[93])
    n->speedScale = speed;                     // node+376
    return n;
}

// ===========================================================================
// Walk builders.  gilde.exe 0x40b6a8 / 0x40b760.
// ===========================================================================

// gilde.exe 0x40b6a8 — VIBE_CharAction_QueueWalkToTarget.
// Resolves the avatar's world point to a tile and builds a type-45 walk action.
ActionNode* QueueWalkToTarget(Character* ch, ActionNode* /*into*/) {
    const MiscActionHooks& h = GetMiscActionHooks();
    int col = 0, row = 0;
    if (!h.worldToTile(ch, 0, &col, &row))     // WorldToTileWithHeight failed
        return nullptr;
    i32 args[2] = { col, row };
    ActionNode* n = InsertActionVararg(ch, kActWalk, args, 2); // type 45
    if (!n)
        return nullptr;
    // node+345 |= 0x10 (idle-walk tag); node+324..+332 = exact world point. The
    // node has no +345/+324 slots in this model; the tag is observable via a high
    // arg-slot bit so the walk step can detect the idle-walk source.
    n->args[10] |= 0x10;                        // +345 region marker (mirror)
    return n;
}

// gilde.exe 0x40b760 — VIBE_CharAction_QueueWalk2RndDummy.
// Idle wander: only on the first call, only while in the character's own slot,
// pick a wait-position animation, resolve it, chain a "Walk2RndDummy"-tagged walk.
int QueueWalk2RndDummy(ActionNode* node, void* /*meshArg*/) {
    Character* ch = node->owner;
    const MiscActionHooks& h = GetMiscActionHooks();
    int result = 1;
    if (node->callCount)                       // *(+12) != 0
        return result;

    int slot = h.sceneSlotIndex(ch->scene);    // IndexFromPointer(ch+136)
    int destSlot = node->args[1];              // *(node+48)
    if (slot != destSlot) {
        UnlinkEntry(node);                     // not in our slot -> done
        return result;
    }
    // SwitchActiveSlot(slot,...) — universe leaf, omitted.
    // PickWaitAnimation -> world point -> tile (modeled by worldToTile).
    int col = 0, row = 0;
    if (h.worldToTile(ch, 0, &col, &row)) {
        i32 args[2] = { col, row };
        ActionNode* walk = InsertActionAfter(ch, node, kActWalk, args, 2); // type 45
        if (walk) {
            walk->args[10] |= 0x10;            // +345 |= 0x10 (idle-walk tag)
            std::memcpy(walk->animBuf, "Walk2RndDummy", 14); // node+112 name
        }
    }
    UnlinkEntry(node);
    return result;
}

// ===========================================================================
// Transparency / morph fade interpolation.  gilde.exe 0x40b998.
// ===========================================================================

double FadeParam(int frameEndTick, float startTick, bool fadeIn) {
    // RotateInterpolate:
    //   elapsed = (double)dword_62D008 - node+80;
    //   if (node+48) v9 = elapsed * 0.02;          (fade-out ramp: 0 -> 1)
    //   else         v9 = 1.0 - elapsed * 0.02;    (fade-in ramp:  1 -> 0)
    // (The branch is on node+48; here `fadeIn==true` selects the `else` ramp.)
    double elapsed = static_cast<double>(frameEndTick) - static_cast<double>(startTick);
    double t = fadeIn ? (1.0 - elapsed * kFadeRatePerTick)
                      : (elapsed * kFadeRatePerTick);
    // clamp to [0,1]: if (t <= 0) t = 0;  then if (t >= 1) t = 1;
    if (t <= 0.0) t = 0.0;
    if (t >= 1.0) t = 1.0;
    return t;
}

int FadeAlpha(double t) {
    // v26 = clamp01(t); alpha = (int)(v26 * 255.0) via VIBE_Coord_ConvertX (the
    // @0x40b998 call site: ConvertX(); v27 = (int)v26). ConvertX @0x5c6b08 sets
    // RC=11 (round-toward-zero) before frndint, so this TRUNCATES toward zero —
    // NOT round-to-nearest (verified: decompile 0x5c6b08 + the 0x40b998 site).
    double a = t * kFadeAlphaScale;
    double r = std::trunc(a);                  // VIBE_Coord_ConvertX == truncate
    int v = static_cast<int>(r);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return v;
}

// gilde.exe 0x40b998 — VIBE_CharAction_RotateInterpolate.
// node fields: +12 callCount, +20 owner, +48 fade-direction (0 = fade-in),
// +80 start-tick stamp. ch fields: +52 mesh, +136 scene, +140/+141 flags,
// +292/+492 secondary meshes. The transparency writes + the neighbour scan are
// routed through hooks; the fade math + clamps are translated 1:1.
void RotateInterpolate(ActionNode* node) {
    Character* ch = node->owner;
    const MiscActionHooks& h = GetMiscActionHooks();

    if (node->callCount == 0) {                // first dispatch: install the morph
        // Original: if (!*(mesh+460)) { Unlink; return; }  — no morph slot.
        // We model the morph slot as present (mesh-owned) and proceed to install.
        // Install: SetVisible(ch,1); full-opaque transparency write; stamp start.
        ch->visible = 1;
        node->args[8] = node->callCount;       // (mirror; original uses +80 float)
        // *(node+80) = (float)(dword_62D008 - 1); set the start-tick stamp.
        node->speedScale = static_cast<float>(g_tickEnd - 1);
        ch->flagsA |= 0x40u;                   // ch+140 |= 0x40 (fading)
        ch->flagsB &= ~0x40u;                  // ch+141 &= ~0x40
        // (Full-opaque ChangeTransparency writes for mesh/+292/+492 — render leaf.)
    }

    bool fadeOut = (node->args[1] != 0);       // *(node+48) != 0 -> fade-out ramp
    double t = FadeParam(g_tickEnd, node->speedScale, /*fadeIn=*/!fadeOut);

    // Social interrupt: only while in the active scene, fading (+141 & 0x40 clear),
    // the fade param below 0.1, and a neighbour within 30.0 is found -> restamp,
    // mark the neighbour busy, and chain a talk action (type 59, arg 75).
    if (ch->scene /* == off_649D64 */
        && node->args[1]                       // *(node+48) (fade-out)
        && (ch->flagsB & 0x40) == 0
        && t < kFadeSocialThresh) {
        Character* other = h.findNearby(ch, 30.0f);
        if (other) {
            node->speedScale = static_cast<float>(g_tickEnd - 1); // restamp +80
            other->flagsB |= 0x40u;            // *(other+141) |= 0x40
            i32 talkArg = 75;
            InsertActionAfter(ch, node, kActWaitDuration, &talkArg, 1); // node|0x3B (type 59)
            return;
        }
    }

    int alpha = FadeAlpha(t);                  // v26 * 255.0 -> rounded alpha byte
    ch->carried = alpha;                        // observable mirror of the written alpha
    // (ChangeTransparency(mesh/+292/+492, alpha) — render leaf, omitted.)

    // Completion: when the fade reaches a terminal value (alpha 0 == fully faded,
    // or alpha 255 == fully opaque) finish — clear the fading flag, write the
    // final transparency, restore/clear visibility and free the node.
    bool atFull   = (t >= 1.0);                // (LODWORD(v26) & 0x7FFFFFFF)==0 path inverse
    bool atZero   = (alpha == 0);
    if (atZero || atFull) {
        ch->flagsA &= ~0x40u;                  // ch+140 &= ~0x40 (done fading)
        if (atFull) {
            // fade-in complete -> fully visible.
            ch->visible = 1;
        } else {
            // fade-out complete -> hidden (SetVisible(ch, node+48==-1 ? args[1] : 0)).
            ch->visible = (node->args[1] == -1) ? node->args[1] : 0;
        }
        UnlinkEntry(node);
    }
}

// ===========================================================================
// Group-interaction coroutine (He record).  gilde.exe 0x4d19c0.
// ===========================================================================
void GroupInteractStep(HeRecord* h, GroupLeader* leader, i32 partnerId) {
    const GroupInteractHooks& gi = GetGroupInteractHooks();

    // v31 = FindRecordById(leader+92); if (!v31 || !partner.ready || !rec.ready) free.
    void* partnerRec = gi.findPersonById(partnerId);
    if (!partnerRec || !gi.personReady(partnerRec) || !gi.personReady(leader)) {
        gi.freeHandlerEntry(h);                // LABEL_18
        return;
    }

    i32 state = Gi_State(h);                    // *(a1+112)
    if (state < 0) {
        if (state != -2)
            return;                             // -1 (or other) -> idle this tick
        gi.freeHandlerEntry(h);                 // -2 -> done
        return;
    }

    if (state <= 0) {                           // state == 0 : "join / decide"
        // Count the live members (member ids != -1 that resolve to a record).
        int count = 0;
        for (int i = 0; i < 5; ++i) {
            i32 id = leader->memberIds[i];
            if (id != -1 && gi.findPersonById(id))
                ++count;
        }
        if (count == 5) {                        // group full -> done
            gi.freeHandlerEntry(h);              // LABEL_18
            return;
        }
        // Leave-probability roll:
        //   v33 = ((5 - count) * 0.2);  v33 = v33 * v33;
        //   v30 = v33 * accum;
        //   if (rand + 1.0 - v33 >= v30) accum += v33 * 0.1;   (stay)
        //   else { state = 1; counter += 1; accum = 0; }       (start talking)
        double f = (static_cast<double>(kGroupMaxSize) - count) * kGroupProbScale;
        double sq = f * f;
        float& accum = Gi_Accum(h);
        double prob = sq * accum;
        double roll = util::RandomFloatScaled() + 1.0 - sq;
        if (roll >= prob) {
            accum = static_cast<float>(sq * kGroupAccumScale + accum);
        } else {
            Gi_State(h) = 1;                     // *(a1+112) = 1
            Gi_SetDwellCounter(h, Gi_GetDwellCounter(h) + 1);  // *(a1+82) += 1
            accum = 0.0f;                        // *(a1+172) = 0
        }
        return;
    }

    if (state == 1) {                            // state == 1 : "active talk"
        float& accum = Gi_Accum(h);
        if (static_cast<double>(accum) < kGroupDwellThresh) {
            accum = accum + 1.0f;                // *(a1+172) += 1.0
            Gi_SetDwellCounter(h, Gi_GetDwellCounter(h) + 1);  // *(a1+82) += 1
            return;
        }
        // Dwell elapsed: optionally queue a cmd39 appointment for a player char,
        // then emit the opcode-8 interaction packet and reset to state 0.
        if (leader->kindByte == 6) {             // *(v34+2) == 6 (player character)
            gi.queueRequest39(partnerId, leader->partnerId, /*+30 min*/ 30 * 60);
        }
        accum = 0.0f;                            // *(a1+172) = 0
        // ids ordered by the role byte: role!=0 -> swap from/to.
        i32 fromId = leader->partnerId;          // *(v34+1) leader id
        i32 toId   = leader->objField;           // resolved partner record id (+1)
        // The original passes (8, idA, 0, idB, objField, 0,0,2); idA/idB swap on +9.
        if (leader->roleByte) {
            gi.enqueueObjectInteraction(partnerId, fromId, toId);
        } else {
            gi.enqueueObjectInteraction(fromId, partnerId, toId);
        }
        Gi_State(h) = 0;                         // *(a1+112) = 0
        Gi_SetDwellCounter(h, Gi_GetDwellCounter(h) + (util::RandomModulo(8) + 1)); // *(a1+82) += rand%8 + 1
    }
}

} // namespace guild::sim
