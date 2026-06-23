#pragma once
// charaction_misc — the remaining self-contained CharAction step handlers and
// builders for the Guild simulation (gilde.exe) that the earlier charaction
// agents deferred:
//
//   VIBE_Character_SoundActionUpdate        0x4055d4  (type 46)
//   VIBE_Character_CreateSoundAction        0x405670
//   VIBE_Character_PlaySampleActionUpdate   0x405740  (type 47)
//   VIBE_Character_CreatePlaySampleAction   0x405838
//   VIBE_Character_SampleLoopActionUpdate   0x4058f0  (type 48)
//   VIBE_Character_CreateSampleLoopAction   0x40598c
//   VIBE_Character_UseGateActionUpdate      0x4059f4  (type 52)
//   VIBE_Character_CreateUseGateAction      0x405bec
//   VIBE_CharAction_QueueWalkToTarget       0x40b6a8  (walk-to-target builder)
//   VIBE_CharAction_QueueWalk2RndDummy      0x40b760  (random idle-walk step)
//   VIBE_CharAction_RotateInterpolate       0x40b998  (the transparency/morph
//                                                      fade-in/out interpolation)
//   VIBE_CharAction_GroupInteractStep       0x4d19c0  (the He-record group-talk
//                                                      coroutine, type 8 interaction)
//
// The state machines, RNG draws, the fade math (constants recovered byte-for-byte)
// and the queue builders are translated 1:1. Render / anim / sound / mesh leaves
// (AttachMovementAni, AttachAni, StepMotionQueue, ChangeTransparency, Heightmap,
// SwitchActiveSlot, command emission) are out of this module's scope and routed
// through the hook tables below, mirroring the existing charaction.h /
// charaction_walk.h / npcaction*.h hook pattern.
#include <cstring>

#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

struct Character;   // character.h
struct ActionNode;  // charaction.h

// ===========================================================================
// Recovered IEEE-754 constants (resolved byte-for-byte via get_bytes).
// ---------------------------------------------------------------------------
// Fade / morph interpolation (VIBE_CharAction_RotateInterpolate):
extern const double kFadeRatePerTick;   // dbl_6109E4 = 0.02   (alpha step / elapsed tick)
extern const double kFadeSocialThresh;  // dbl_6109EC = 0.1    (interrupt-for-talk gate)
extern const double kFadeAlphaScale;    // dbl_6109F4 = 255.0  (t -> alpha byte)
extern const float  kFadeFullOpaque;    // flt_6109FC = 255.0  (full-opaque sentinel)
// Group-interaction coroutine (VIBE_CharAction_GroupInteractStep):
extern const double kGroupDwellThresh;  // dbl_61EB2C = 3.0    (state-1 dwell ticks)
extern const float  kGroupMaxSize;      // flt_61EB34 = 5.0    (full group size)
extern const float  kGroupProbScale;    // flt_61EB38 = 0.2    (leave-probability scale)
extern const double kGroupAccumScale;   // dbl_61EB3C = 0.1    (accumulator increment)

// ===========================================================================
// Sound / sample / use-gate action ids (catalog type bytes set by the builders).
// ===========================================================================
constexpr u8 kActSoundEffect = 46;  // SoundActionUpdate
constexpr u8 kActPlaySample  = 47;  // PlaySampleActionUpdate
constexpr u8 kActSampleLoop  = 48;  // SampleLoopActionUpdate
constexpr u8 kActUseGate2    = 52;  // UseGateActionUpdate (== kActUseGate in charaction.h)

// ===========================================================================
// Hooks for the render/anim/sound leaves the sound/sample/use-gate steps call.
// Defaults are inert: attach returns a non-null handle, the motion step reports
// "done" (-1) so the queue drains, queries are no-ops. A test installs a mock.
// ===========================================================================
struct MiscActionHooks {
    // VIBE_Character_AttachMovementAni(ch, name, mode): attach a movement anim;
    // returns a non-null handle on success (sound step). Mode: anim speed/dir.
    void* (*attachMovementAni)(Character* ch, const char* name, int mode);
    // VIBE_Character_AttachAni(ch, name, loop): attach a named looping anim;
    // returns the anim handle (sample steps). Stored at ch+112 by the caller.
    void* (*attachAni)(Character* ch, const char* name, int loop);
    // VIBE_Character_StepMotionQueue(ch): advance the motion queue one tick;
    // returns -1 when the queue is idle/done (the step then frees the node).
    int   (*stepMotionQueue)(Character* ch);
    // VIBE_Character_CheckQueueReady(ch): nonzero when the sound action may end.
    int   (*checkQueueReady)(Character* ch);
    // VIBE_Character_StopSample(ch): stop a looping sample (SampleLoop teardown).
    void  (*stopSample)(Character* ch);
    // VIBE_Character_IndexFromPointer(scene): scene -> universe slot index, used
    // by UseGate/Walk2RndDummy to switch the active universe slot.
    int   (*sceneSlotIndex)(void* scene);
    // VIBE_Heightmap_WorldToTileWithHeight: resolve the gate/idle world point to
    // a tile (col,row) + height; returns nonzero on success and fills *col,*row.
    int   (*worldToTile)(Character* ch, int destScene, int* col, int* row);
    // VIBE_Character_FindNearbyInRadius wrapper used by the fade interrupt: scan
    // for a busy neighbour within `radius`; returns it (or null). See character_
    // social.h for the real scan.
    Character* (*findNearby)(Character* ch, float radius);
};
void SetMiscActionHooks(const MiscActionHooks* hooks);
const MiscActionHooks& GetMiscActionHooks();

// ===========================================================================
// Sound / sample step handlers.
// ===========================================================================

// gilde.exe 0x4055d4 — VIBE_Character_SoundActionUpdate (type 46). First call
// (callCount==0): if the sound count (args[1]) == -1 sets the avatar dirty-anim
// flag (+140 |= 2); clamps a <1 count to 1; attaches the movement animation. On
// later calls: when aborting (+400) or the count is finite, clears the dirty-anim
// flag, and once CheckQueueReady passes frees the node and re-checks ani-morph.
// Always propagates the node's playback speed (node->speedScale) into the active
// anim's speed field if the avatar has a live motion handle.
void SoundActionUpdate(ActionNode* node);

// gilde.exe 0x405670 — VIBE_Character_CreateSoundAction. Enqueues a type-46 sound
// action carrying `name` (looping anim) with `soundCount` (args[1], -1 = endless)
// and default speed 1.0. Returns the node or null.
ActionNode* CreateSoundAction(Character* ch, const char* name, int soundCount);

// gilde.exe 0x405740 — VIBE_Character_PlaySampleActionUpdate (type 47). First
// call: if the name buffer is empty falls through to the active-anim setup;
// otherwise sets dirty-anim, attaches the sample anim (with an optional seek when
// node->seekFlag bit0 is set) and marks the anim active (frame 0, flags 0x10). On
// later calls steps the motion queue; on -1 marks idle-anim pending and frees.
void PlaySampleActionUpdate(ActionNode* node);

// gilde.exe 0x405838 — VIBE_Character_CreatePlaySampleAction. Enqueues a type-47
// sample action carrying `name`. Returns the node or null.
ActionNode* CreatePlaySampleAction(Character* ch, const char* name);

// gilde.exe 0x4058f0 — VIBE_Character_SampleLoopActionUpdate (type 48). If the
// avatar is mid-anim (+112 or +124 set) and this node has not started (state bit0
// clear) it defers (sets callCount=-1). Otherwise marks started; on the first
// real call (callCount==0) attaches the looping sample if idle-anim is pending
// (+140 & 0x10), else frees; on later calls steps the motion queue and, on -1,
// stops the sample and frees.
void SampleLoopActionUpdate(ActionNode* node);

// gilde.exe 0x40598c — VIBE_Character_CreateSampleLoopAction. Enqueues a type-48
// looping-sample action — but only when the avatar's idle-anim-pending flag
// (+140 & 0x10) is set; returns 0 otherwise. Carries `name`.
ActionNode* CreateSampleLoopAction(Character* ch, const char* name);

// ===========================================================================
// Use-gate step + builder (type 52: walk a character through a room "gate").
// ===========================================================================

// gilde.exe 0x4059f4 — VIBE_Character_UseGateActionUpdate. On the first call
// (callCount==0): resolve the character's current scene slot; if it already equals
// the gate's destination room the action is done (free). Otherwise switch into
// the destination universe slot, resolve the gate object's world point to a tile,
// and chain a walk action (type 45) toward it; conditionally chain a visibility
// fade (type 56) and a relocation (type 51) carrying the destination room and the
// waypoint buffer; finally free the gate node.
void UseGateActionUpdate(ActionNode* node);

// gilde.exe 0x405bec — VIBE_Character_CreateUseGateAction. Enqueues a type-52
// use-gate action: `name` gate object, `room` destination room id, `scene` dest
// scene id, `speed` playback speed. Returns the node or null.
ActionNode* CreateUseGateAction(Character* ch, const char* name, int room,
                                int scene, float speed);

// ===========================================================================
// Walk builders.
// ===========================================================================

// gilde.exe 0x40b6a8 — VIBE_CharAction_QueueWalkToTarget. Resolves the avatar's
// world point to a destination tile via the heightmap and builds a type-45 walk
// action toward it (InsertActionVararg), tagging the node's idle-walk flag
// (+345 |= 0x10) and stashing the exact world point (+324..+332). Returns the
// created node, or null if the tile resolve fails.
ActionNode* QueueWalkToTarget(Character* ch, ActionNode* into);

// gilde.exe 0x40b760 — VIBE_CharAction_QueueWalk2RndDummy. The idle "wander to a
// random nearby spot" step: only acts on the first call and only while the
// character is in its own scene slot; picks a random wait-position animation,
// resolves it to a tile, and chains a "Walk2RndDummy"-tagged type-45 walk; then
// frees itself. Returns the (modified) result register.
int QueueWalk2RndDummy(ActionNode* node, void* meshArg);

// ===========================================================================
// Transparency / morph fade interpolation.
// ===========================================================================

// Pure fade ramp: given the morph node's start tick and the current frame end
// tick, returns the clamped fade parameter t in [0,1]. `fadeIn` selects the
// 1 - elapsed*rate ramp (fade-out) vs elapsed*rate ramp (fade-in). This is the
// (dword_62D008 - node+80) * dbl_6109E4 block of RotateInterpolate, exposed for
// testing the fade math at t=0/0.5/1.
double FadeParam(int frameEndTick, float startTick, bool fadeIn);

// Maps a clamped fade parameter t in [0,1] to the 0..255 alpha byte the engine
// writes through VIBE_Object_ChangeTransparency (t * 255.0, rounded to nearest
// like the engine's frndint). Returns 0..255.
int FadeAlpha(double t);

// gilde.exe 0x40b998 — VIBE_CharAction_RotateInterpolate. The fade-in/out morph
// step (type 59-family). On the first dispatch it installs the morph (full alpha,
// stamps the start tick, sets the "fading" flag); each tick it advances the fade
// parameter, optionally interrupts to a social "talk" action when a neighbour is
// near and the fade is below the social threshold, writes the interpolated alpha
// through the transparency leaves, and on completion restores visibility and frees
// the node. The control flow + clamps are translated 1:1; the transparency writes
// and the neighbour scan are routed through MiscActionHooks.
void RotateInterpolate(ActionNode* node);

// ===========================================================================
// Group-interaction coroutine (He record, opcode-8 object interaction).
// ===========================================================================
// Hooks for the GroupInteract leaves (Person lookup + command emission + clock).
struct GroupInteractHooks {
    // VIBE_Person_FindRecordById(id): person record (or null). Used to count the
    // live members of the group and to resolve the partner record.
    void* (*findPersonById)(i32 id);
    // The partner-alive predicate: nonzero when *(person+8) (the "ready" byte) is
    // set on both the partner and the resolved record (the original's && chain).
    int   (*personReady)(void* person);
    // VIBE_He_FreeHandlerEntry(h): release the handler record (terminal states).
    void  (*freeHandlerEntry)(HeRecord* h);
    // VIBE_Command_EnqueueObjectInteraction(8, a, 0, b, c, 0, 0, 2): the opcode-8
    // talk/interaction packet (a/b = the two person ids, c = object handle field).
    void  (*enqueueObjectInteraction)(i32 fromId, i32 toId, i32 objField);
    // VIBE_Command_QueueRequest39(packet): the appointment/event packet built when
    // the partner's kind byte == 6 (a player character). The 30-minute deadline is
    // pre-baked into the packet by GroupInteractStep before the call.
    void  (*queueRequest39)(i32 personId, i32 partnerId, i32 deadlineSec);
};
void SetGroupInteractHooks(const GroupInteractHooks* hooks);
const GroupInteractHooks& GetGroupInteractHooks();

// The group-interaction He record fields (byte offsets into the 332-byte record):
//   +8    person index into the Person array (word; group leader slot).
//   +82   per-state dwell counter (dword).
//   +112  state machine (dword: -2 done, -1 fail, 0 join, 1 active).
//   +172  fractional accumulator (float).
// The group leader's Person record (word_12CE910[268*index]) carries:
//   +4    object/handle field (dword, used as the interaction object).
//   +9    role byte (0 -> leader is the "from" id, else swapped).
//   +92   partner/leader person id (dword).
//   +104..+120  up to 5 member person ids (dword each; -1 == empty).
// +82 is NOT 4-byte aligned (82 % 4 == 2). The original x86 binary reads it with an
// unaligned `*(DWORD*)(h+82)`; in portable C++ binding an `i32&` to a misaligned
// address is UB (UBSAN flags it). Use byte-exact memcpy load/store helpers instead —
// the observable value (a 32-bit little-endian int at byte 82) is identical.
inline i32 Gi_GetDwellCounter(HeRecord* h) {
    i32 v; std::memcpy(&v, HeBytes(h) + 82, sizeof(v)); return v;
}
inline void Gi_SetDwellCounter(HeRecord* h, i32 v) {
    std::memcpy(HeBytes(h) + 82, &v, sizeof(v));
}
inline i32& Gi_State(HeRecord* h)        { return *reinterpret_cast<i32*>(HeBytes(h) + 112); }
inline float& Gi_Accum(HeRecord* h)      { return *reinterpret_cast<float*>(HeBytes(h) + 172); }

// A group-leader Person record view (the word_12CE910[268*index] slot). The real
// array is the 536-byte Person record; GroupInteractStep only touches these
// fields, so we model them as a small POD the test/host fills.
struct GroupLeader {
    i32 objField;     // +4    object/interaction handle
    u8  roleByte;     // +9    leader role (0 -> not swapped)
    u8  kindByte;     // +2    leader kind (6 -> player char, triggers cmd39)
    i32 partnerId;    // +92   partner/leader person id
    i32 memberIds[5]; // +104..+120  member person ids (-1 == empty)
};

// gilde.exe 0x4d19c0 — VIBE_CharAction_GroupInteractStep. The group-conversation
// coroutine driven over a He handler record. `leader` is the resolved Person-array
// group slot (word_12CE910[268 * h+8]); `partnerId` is its +92 id. State machine:
//   * partner/record not ready -> free (LABEL_18).
//   * state < 0: -2 -> free, else (e.g. -1) just return.
//   * state == 0 (join): count live members; if all 5 present -> free; else roll
//     a leave probability ((5-count)*0.2)^2 * accum vs (rand+1 - sq) and either
//     advance to state 1 (start talking, bump dwell) or grow the accumulator.
//   * state == 1 (active): dwell up to 3.0; once elapsed, if the leader is a
//     player char (kind 6) queue a cmd39 appointment (+30 min), then emit the
//     opcode-8 interaction packet (ids ordered by the +9 role byte) and reset to
//     state 0 with a random 1..8 dwell bump.
// The Person/command/clock leaves are routed through GroupInteractHooks.
void GroupInteractStep(HeRecord* h, GroupLeader* leader, i32 partnerId);

} // namespace guild::sim
