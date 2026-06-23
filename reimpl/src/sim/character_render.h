#pragma once
// character_render — the self-contained Character "render-coupled leaf" cluster of
// gilde.exe: attach-offset geometry, listener/camera setup, head-variant texture
// selection, and the small mode/flag accessors and script-command wrappers that sit
// on top of the live actor record. These functions are 1:1 ports; the actual render
// / anim / sound / scene-graph calls they make live in OTHER modules, so they are
// routed through a CharRenderHooks dispatch table (defaulting to inert stubs) so the
// pure arithmetic (attach offsets, head-variant modulo) is testable in isolation.
//
// Translated functions (this TU):
//   VIBE_Character_ComputeAttachOffset    0x404860  (attach slot -> offset+rotation)
//   VIBE_Character_ApplyAttachOffset      0x404964  (set world translation/position)
//   VIBE_Character_SetupAttachCamera      0x404998  (3D sound listener from attach)
//   VIBE_Character_ApplyBoneTransform     0x4263fc  (listener orientation from bones)
//   VIBE_Character_ApplyHeadVariant       0x57c548  (head texture-set by id modulo)
//   VIBE_Character_FlagRedrawByMode       0x5049d8  (set +531 bit 2 for some modes)
//   VIBE_Character_SetLowPoly             0x43de74  (set/clear +141 bit 1)
//   VIBE_Character_ResetStateIfMode3      0x489b8c  (clear +536 when mode == 3)
//   VIBE_Character_HideAttachedActor      0x48cf18  (SetVisible(attached, 0))
//   VIBE_Character_Stop                   0x43da2c  (StandUp wrapper + error path)
//   VIBE_Character_ResetAiTarget          0x453358  (clear +448, set +436 bit 0)
//   VIBE_Character_IsAccidentCandidate    0x4d4f78  (accident eligibility predicate)
//   VIBE_Character_ToggleAniPlayback      0x4047f0  (set/clear +140 bit 2 anim pause)
//   VIBE_Character_CmdKillCharacterAnimations 0x43da5c (abort + unlink all queue nodes)
//   VIBE_Character_AttachItemToBone2      0x43d524  (AttachItemToBone wrapper)
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// RenderActor — the live scene actor as the render-leaf cluster sees it. Only the
// touched byte offsets are modeled (the full 536+ byte record lives elsewhere; this
// is a behaviour mirror, exactly as character_state.h's TurnObject / LiveActor do).
//   +0   record marker word (0xFFFF == free) — used by the candidate predicates.
//   +2   record-type byte.
//   +4   record id (used by head-variant + candidate modulo).
//   +8   "exists/alive" byte (candidate predicates require it set).
//   +52  mesh handle pointer (render).
//   +136 universe/scene tag pointer.
//   +140 flag byte A (0x04 anim-paused, 0x10 sitting).
//   +141 flag byte B (0x02 low-poly).
//   +296 action-queue head (ActionNode*).
//   +356 "is master" / player-owned byte (AI-controllable / accident gates).
//   +364 owner-object pointer (candidate predicates).
//   +380 attached-actor host pointer (HideAttachedActor: host+388 == attached record).
//   +388 attached-actor record pointer (head-variant root).
//   +436 AI flag byte (ResetAiTarget sets bit 0).
//   +448 AI target id (ResetAiTarget sets -1).
//   +531 redraw flag byte (FlagRedrawByMode sets bit 2).
//   +535 mode byte (FlagRedrawByMode / ResetStateIfMode3 read it).
//   +536 mode-3 transient state dword.
// ===========================================================================
struct RenderActor {
    u16   marker;        // +0
    u8    type;          // +2
    int   id;            // +4
    u8    alive;         // +8
    void* mesh;          // +52
    void* handle112;     // +112  (ToggleAniPlayback gate: result[28], esi); a
                         //        distinct handle from the +52 mesh used in the body.
    void* universe;      // +136
    u8    flagsA;        // +140
    u8    flagsB;        // +141
    void* action;        // +296
    u8    isMaster;      // +356
    void* owner;         // +364   owner-object record pointer
    void* attachHost;    // +380   (HideAttachedActor reads host+388)
    RenderActor* attached; // +388  attached actor record (head-variant root etc.)
    u8    aiFlags;       // +436
    int   aiTarget;      // +448
    u8    redrawFlags;   // +531
    u8    mode;          // +535
    int   mode3State;    // +536

    // owner-record fields the candidate / owner-for-turn rules read indirectly
    // (the engine indexes a peer person table; we carry the resolved values).
    int   ownerColumn0;  // owner+4 (== *(DWORD*)(owner+1) in the byte-pun source)
    int   ownerHandle44; // *(DWORD*)(attached+44), compared to *(DWORD*)(owner+1)
    bool  hasOwner;      // owner pointer present
    u16   ownerPlayer;   // owner+39 (0xFFFF == none)
};

// Flag-byte-A bit constants (shared with the rest of the Character cluster).
constexpr u8 kRaAnimPaused = 0x04;  // +140 & 0x04
constexpr u8 kRaSitting    = 0x10;  // +140 & 0x10
// Flag-byte-B bit constant.
constexpr u8 kRbLowPoly    = 0x02;  // +141 & 0x02
// Redraw-flag-byte bit constant.
constexpr u8 kRrRedraw     = 0x04;  // +531 & 0x04
// AI-flag-byte bit constant.
constexpr u8 kAiResetBit   = 0x01;  // +436 & 0x01

// ===========================================================================
// Cross-module / render dispatch hooks. Every renderer / anim / sound / script call
// the originals make is one of these slots; the default implementation is inert so
// the math is testable. Tests install a recording mock.
// ===========================================================================
struct CharRenderHooks {
    // VIBE_Transform_PointThroughBoneChainPivot(mesh, in, out): transform a local
    // offset through the mesh root pivot. Default: copies in -> out unchanged.
    void (*pointThroughPivot)(void* mesh, const float in[3], float out[3]);
    // mesh root translation (mesh+132/136/140) added to the transformed offset.
    void (*meshRootTranslation)(void* mesh, float out[3]);
    // VIBE_Object_SetWorldTranslation / SetPosition (ApplyAttachOffset).
    void (*setWorldTranslation)(const float rot[3]);
    void (*setObjectPosition)(const float pos[3]);
    // VIBE_Sound3d_SetListenerFromVectors (SetupAttachCamera).
    void (*setListenerFromVectors)(const float pos[3], const float rot[3]);
    // VIBE_Sound3d_SetListenerOrientation (ApplyBoneTransform): forward + up bones.
    void (*setListenerOrientation)(const float fwd[3], const float up[3]);
    // VIBE_Object_SelectTextureSet (ApplyHeadVariant): returns the selection result.
    int  (*selectTextureSet)(void* mesh, int variant);
    // VIBE_Character_SetVisible(actor, visible) (HideAttachedActor).
    void (*setVisible)(RenderActor* a, int visible);
    // VIBE_Character_StandUp(actor) (Stop).
    void (*standUp)(RenderActor* a);
    // VIBE_ActionQueue_UnlinkEntry(node): unlink head node; returns next head
    // (0 when the queue is empty). Used by CmdKillCharacterAnimations.
    void* (*unlinkActionEntry)(void* node);
    // VIBE_Anim_SetLoopFlags / ClearLoopFlags (ToggleAniPlayback).
    void (*animSetLoopFlags)(void* mesh);
    void (*animClearLoopFlags)(void* mesh);
    // VIBE_Character_AttachItemToBone(actor, bone, name) (AttachItemToBone2).
    void (*attachItemToBone)(RenderActor* a, int bone, const char* name);
    // VIBE_Script_ReportError(msg) (Stop / SetLowPoly error paths).
    void (*reportError)(const char* msg);
};
void SetCharRenderHooks(const CharRenderHooks* hooks);
const CharRenderHooks& GetCharRenderHooks();

// ===========================================================================
// dword_62D080 (g_motionActiveUniverse, owned by charaction_motion.cpp) and the
// active mesh-id used by ApplyHeadVariant. Declared extern; reused, not redefined.
// ===========================================================================

// Per-attach-slot offset + rotation. `slot` ∈ {0,1,2,3}; out-of-range leaves the
// vectors untouched (matches the original switch's missing default).
struct AttachGeom {
    float offset[3];    // local offset (a4 in the original)
    float rotation[3];  // rotation triple (a3 in the original; [2] always 0)
};

// gilde.exe 0x404860 — VIBE_Character_ComputeAttachOffset.
//   __usercall(a1=actor@eax, a2=slot@dl, a3=rot-out@ecx, a4=off-out@ebx).
// Loads the per-slot constant offset+rotation; when sitting (+140 & 0x10) it adds
// dbl_6103AC == -15.0 to Y (i.e. lowers the attach point by 15), transforms the
// offset through the mesh pivot, then adds the mesh root translation (mesh+132/6/40).
// Returns the original slot byte (al). actor==0 is a no-op.
u8 ComputeAttachOffset(RenderActor* actor, u8 slot, AttachGeom* out);

// gilde.exe 0x404964 — VIBE_Character_ApplyAttachOffset. Computes the attach geom
// then pushes rotation via SetWorldTranslation and offset via SetObjectPosition.
void ApplyAttachOffset(RenderActor* actor, u8 slot);

// gilde.exe 0x404998 — VIBE_Character_SetupAttachCamera. Computes the attach geom,
// frees the bound obj-anim, clears the listener-cache flags, then sets the 3D sound
// listener from the offset/rotation vectors. actor==0 is a no-op.
void SetupAttachCamera(RenderActor* actor, u8 slot);

// gilde.exe 0x4263fc — VIBE_Character_ApplyBoneTransform. Reads the forward
// (mesh[23..25]) and up (mesh[36..38]) bone vectors and sets the 3D sound listener
// orientation from them.
void ApplyBoneTransform(const float* mesh);

// gilde.exe 0x57c548 — VIBE_Character_ApplyHeadVariant. Picks a head texture-set:
// variant = (headCount>4) ? (id & 3) : (id % headCount); writes it to the attached
// mesh's +506 byte, then (when the actor's universe matches the active mesh id)
// applies it via SelectTextureSet. Returns the variant byte. actor==0 / no attached
// actor is a no-op (returns 0).
u8 ApplyHeadVariant(RenderActor* actor, int headCount, int activeMeshId);

// ===========================================================================
// Mode / flag accessors.
// ===========================================================================

// gilde.exe 0x5049d8 — VIBE_Character_FlagRedrawByMode. For mode ∈ {0,1,3,4} sets
// the redraw flag (+531 |= 4).
void FlagRedrawByMode(RenderActor* a);

// gilde.exe 0x43de74 — VIBE_Character_SetLowPoly. Sets/clears +141 bit 1 from the
// boolean. Null actor reports a script error. Returns 1 (original always returns 1).
int SetLowPoly(RenderActor* a, bool lowPoly);

// gilde.exe 0x489b8c — VIBE_Character_ResetStateIfMode3. Clears +536 when mode == 3.
void ResetStateIfMode3(RenderActor* a);

// gilde.exe 0x48cf18 — VIBE_Character_HideAttachedActor. Hides the actor attached to
// host+388 (host == a->attachHost). Routed through the SetVisible hook.
void HideAttachedActor(RenderActor* host);

// gilde.exe 0x43da2c — VIBE_Character_Stop. If the script handle resolves, calls
// StandUp; else reports a script error. Returns 0.
int Stop(RenderActor* a);

// gilde.exe 0x453358 — VIBE_Character_ResetAiTarget. For type ∈ {1,2} with isMaster
// set: invokes the light-reset hook, sets +448 (target) to -1 and +436 |= 1.
void ResetAiTarget(RenderActor* a);

// ===========================================================================
// Eligibility predicates (reuse IsOwnerForTurn / IsObjectForTurn from
// character_state.cpp — declared there, linked, NOT redefined here).
// ===========================================================================

// gilde.exe 0x4d4f78 — VIBE_Character_IsAccidentCandidate. type==0, marker!=0xFFFF,
// alive set, has owner, has attached, attached+44 == owner.column0, and the actor is
// owned by this peer this turn (IsOwnerForTurn).
bool IsAccidentCandidate(const RenderActor* a);
// NB: VIBE_Character_IsDiseaseCandidate (0x4d762c) is already translated in
// illness.cpp as IllnessIsDiseaseCandidate — not duplicated here (ODR).

// ===========================================================================
// Anim / queue control.
// ===========================================================================

// gilde.exe 0x4047f0 — VIBE_Character_ToggleAniPlayback. When the actor has both an
// animB (+74 dword) and a mesh (+28 dword) handle: pause==true clears the loop flags
// and sets +140 bit 2; pause==false sets the loop flags and clears +140 bit 2.
void ToggleAniPlayback(RenderActor* a, bool pause);

// gilde.exe 0x43da5c — VIBE_Character_CmdKillCharacterAnimations. Sets the action
// head's abort byte (+400) and unlinks every queue node. Null handle reports an
// error. Returns 0.
int CmdKillCharacterAnimations(RenderActor* a);

// gilde.exe 0x43d524 — VIBE_Character_AttachItemToBone2. Attaches an item to the
// right hand (bone 2). Returns 1 if the handle resolved, else 0.
int AttachItemToBone2(RenderActor* a, const char* name);

} // namespace guild::sim
