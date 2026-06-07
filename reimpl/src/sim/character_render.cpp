// character_render — self-contained Character render-leaf cluster (gilde.exe).
// Faithful 1:1 ports of the attach-offset geometry, listener/camera setup, head
// texture-variant selection, and the small mode/flag/queue accessors. Every render
// / anim / sound / scene-graph call is routed through CharRenderHooks; the default
// hook table is inert so the pure arithmetic is golden-testable in isolation.
#include "sim/character_render.h"

#include "sim/character_state.h"   // TurnObject + IsOwnerForTurn / IsObjectForTurn

namespace guild::sim {

// ===========================================================================
// Recovered float/double constants (verified via get_bytes).
//   dbl_6103AC = -15.0   (sit-height bump: y += dbl_6103AC when +140 & 0x10)
//   1078530011 -> 3.14159274f  (pi; case-0 rotation Y)
//   1035122882 -> 0.08726646f  (~5 degrees in radians; case-1/2 rotation X)
// ===========================================================================
namespace {
constexpr double kSitHeightBump = -15.0;          // dbl_6103AC
constexpr float  kPi            = 3.14159274f;     // 1078530011
constexpr float  kFiveDeg       = 0.08726646f;     // 1035122882
} // namespace

// ---------------------------------------------------------------------------
// Hook table (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const CharRenderHooks* g_hooks = nullptr;

void   DefPivot(void*, const float in[3], float out[3]) { out[0]=in[0]; out[1]=in[1]; out[2]=in[2]; }
void   DefRootTrans(void*, float[3]) {}
void   DefSetWorldTrans(const float[3]) {}
void   DefSetPos(const float[3]) {}
void   DefSetListenerVecs(const float[3], const float[3]) {}
void   DefSetListenerOri(const float[3], const float[3]) {}
int    DefSelectTex(void*, int) { return 0; }
void   DefSetVisible(RenderActor*, int) {}
void   DefStandUp(RenderActor*) {}
void*  DefUnlink(void*) { return nullptr; }
void   DefSetLoop(void*) {}
void   DefClearLoop(void*) {}
void   DefAttachItem(RenderActor*, int, const char*) {}
void   DefReportError(const char*) {}

const CharRenderHooks g_default = {
    DefPivot, DefRootTrans, DefSetWorldTrans, DefSetPos,
    DefSetListenerVecs, DefSetListenerOri, DefSelectTex,
    DefSetVisible, DefStandUp, DefUnlink, DefSetLoop, DefClearLoop,
    DefAttachItem, DefReportError,
};
} // namespace

void SetCharRenderHooks(const CharRenderHooks* h) { g_hooks = h; }
const CharRenderHooks& GetCharRenderHooks() { return g_hooks ? *g_hooks : g_default; }

// ===========================================================================
// Attach-offset geometry.
// ===========================================================================

// gilde.exe 0x404860 — VIBE_Character_ComputeAttachOffset.
//   switch (slot) {                       // a4 = offset, a3 = rotation
//     case 0: a4={-3,63,36};  a3={0, pi, 0};
//     case 1: a4={-10,63,-14}; a3={5deg, 0, 0};
//     case 2: a4={10,63,-14};  a3={5deg, 0, 0};
//     case 3: a4={0,65,0};     a3={0, 0, 0};
//   }
//   if (actor+140 & 0x10) a4[1] += dbl_6103AC;          // sit-height bump
//   PointThroughBoneChainPivot(mesh, a4, a4);
//   a4 += mesh+132/136/140 (root translation);
u8 ComputeAttachOffset(RenderActor* actor, u8 slot, AttachGeom* out) {
    if (!actor)
        return slot;

    switch (slot) {
        case 0:
            out->offset[0]   = -3.0f; out->offset[1]   = 63.0f; out->offset[2]   = 36.0f;
            out->rotation[0] = 0.0f;  out->rotation[1] = kPi;   out->rotation[2] = 0.0f;
            break;
        case 1:
            out->offset[0]   = -10.0f;    out->offset[1]   = 63.0f; out->offset[2]   = -14.0f;
            out->rotation[0] = kFiveDeg;  out->rotation[1] = 0.0f;  out->rotation[2] = 0.0f;
            break;
        case 2:
            out->offset[0]   = 10.0f;     out->offset[1]   = 63.0f; out->offset[2]   = -14.0f;
            out->rotation[0] = kFiveDeg;  out->rotation[1] = 0.0f;  out->rotation[2] = 0.0f;
            break;
        case 3:
            out->offset[0]   = 0.0f; out->offset[1]   = 65.0f; out->offset[2]   = 0.0f;
            out->rotation[0] = 0.0f; out->rotation[1] = 0.0f;  out->rotation[2] = 0.0f;
            break;
        default:
            // Original switch has no default: the out vectors are left as-is.
            break;
    }

    const CharRenderHooks& h = GetCharRenderHooks();
    if ((actor->flagsA & kRaSitting) != 0)
        out->offset[1] = out->offset[1] + static_cast<float>(kSitHeightBump);

    h.pointThroughPivot(actor->mesh, out->offset, out->offset);
    float root[3] = {0.0f, 0.0f, 0.0f};
    h.meshRootTranslation(actor->mesh, root);
    out->offset[0] += root[0];
    out->offset[1] += root[1];
    out->offset[2] += root[2];
    return slot;
}

// gilde.exe 0x404964 — VIBE_Character_ApplyAttachOffset.
//   ComputeAttachOffset(actor, slot, &rot, &off);
//   SetWorldTranslation(dword_13FCD1C, &rot);
//   SetPosition(dword_13FCD1C, &off);
void ApplyAttachOffset(RenderActor* actor, u8 slot) {
    AttachGeom g{};
    ComputeAttachOffset(actor, slot, &g);
    const CharRenderHooks& h = GetCharRenderHooks();
    h.setWorldTranslation(g.rotation);
    h.setObjectPosition(g.offset);
}

// gilde.exe 0x404998 — VIBE_Character_SetupAttachCamera.
//   if (actor) {
//     ComputeAttachOffset(actor, slot, &rot, &off);
//     FreeObjAnimData(dword_13FCD1C, ...);  dword_62D4E4 = dword_62D4E8 = 0;
//     SetListenerFromVectors(dword_13FCD1C, &off, ..., &rot, 40);
//   }
void SetupAttachCamera(RenderActor* actor, u8 slot) {
    if (!actor)
        return;
    AttachGeom g{};
    ComputeAttachOffset(actor, slot, &g);
    // FreeObjAnimData + listener-cache flag clears are renderer/cache side effects;
    // the observable per-actor result is the listener vectors.
    const CharRenderHooks& h = GetCharRenderHooks();
    h.setListenerFromVectors(g.offset, g.rotation);
}

// gilde.exe 0x4263fc — VIBE_Character_ApplyBoneTransform.
//   SetListenerOrientation(dword_13FCD1C, .., a1[23],a1[24],a1[25], a1[36],a1[37],a1[38], ..);
// a1 is the mesh float array; [23..25] forward bone, [36..38] up bone.
void ApplyBoneTransform(const float* mesh) {
    const float fwd[3] = {mesh[23], mesh[24], mesh[25]};
    const float up[3]  = {mesh[36], mesh[37], mesh[38]};
    GetCharRenderHooks().setListenerOrientation(fwd, up);
}

// gilde.exe 0x57c548 — VIBE_Character_ApplyHeadVariant.
//   if (!actor) return 0;
//   root = actor+388; if (!root) return 0;
//   v3 = root+52 (mesh); v4 = *(v3+492); v6 = *(v4+260); v7 = v4+244;
//   if (root+136 != byte_13ECEC8) {            // not the wild/empty universe
//     headCount = *(v6+484);
//     variant = (headCount > 4) ? (actor.id & 3) : (actor.id % headCount);
//     *(root.mesh + 506) = variant;
//   }
//   result = dword_62D080;
//   if (dword_62D080 == *(root + 44)) result = SelectTextureSet(v3, v7, 1, variant, v7);
//   return result;
// Modeling: `headCount` and the active-mesh comparison are supplied by the caller
// (the engine reads them out of the mesh/universe records). `activeMeshId` mirrors
// the `dword_62D080 == root+44` gate (pass the actor's universe id; equal => apply).
u8 ApplyHeadVariant(RenderActor* actor, int headCount, int activeMeshId) {
    if (!actor)
        return 0;
    RenderActor* root = actor->attached;
    if (!root)
        return 0;

    u8 variant = 0;
    // The original skips the variant write for the "wild" universe (root+136 ==
    // byte_13ECEC8). We model that as a null universe on the attached record.
    if (root->universe != nullptr) {
        if (headCount > 4)
            variant = static_cast<u8>(static_cast<unsigned>(actor->id) & 3u);
        else if (headCount != 0)
            variant = static_cast<u8>(static_cast<unsigned>(actor->id) % static_cast<unsigned>(headCount));
    }

    // dword_62D080 == *(root+44): the active scene mesh id equals the actor's.
    if (activeMeshId == static_cast<int>(static_cast<unsigned>(actor->id)))
        return static_cast<u8>(GetCharRenderHooks().selectTextureSet(root->mesh, variant));
    return variant;
}

// ===========================================================================
// Mode / flag accessors.
// ===========================================================================

// gilde.exe 0x5049d8 — VIBE_Character_FlagRedrawByMode.
//   v1 = *(BYTE*)(result+535);
//   if (v1==3 || v1==4 || v1<2) *(BYTE*)(result+531) |= 4;
void FlagRedrawByMode(RenderActor* a) {
    u8 m = a->mode;
    if (m == 3 || m == 4 || m < 2)
        a->redrawFlags |= kRrRedraw;
}

// gilde.exe 0x43de74 — VIBE_Character_SetLowPoly.
//   if (*a1) { if (*a2) +141 |= 2; else +141 &= ~2; return 1; }
//   else { ReportError("SetLowPoly(): invalid character or dummy"); return 1; }
int SetLowPoly(RenderActor* a, bool lowPoly) {
    if (a) {
        if (lowPoly)
            a->flagsB |= kRbLowPoly;
        else
            a->flagsB &= static_cast<u8>(~kRbLowPoly);
        return 1;
    }
    GetCharRenderHooks().reportError("SetLowPoly(): invalid character or dummy");
    return 1;
}

// gilde.exe 0x489b8c — VIBE_Character_ResetStateIfMode3.
//   if (*(BYTE*)(a1+535) == 3) *(DWORD*)(a1+536) = 0; return 1;
void ResetStateIfMode3(RenderActor* a) {
    if (a->mode == 3)
        a->mode3State = 0;
}

// gilde.exe 0x48cf18 — VIBE_Character_HideAttachedActor.
//   return SetVisible(*(*(a1+380)+388), 0);
void HideAttachedActor(RenderActor* host) {
    // host->attachHost is the +380 host record; its +388 is the attached actor.
    RenderActor* hostRec = static_cast<RenderActor*>(host->attachHost);
    if (!hostRec)
        return;
    GetCharRenderHooks().setVisible(hostRec->attached, 0);
}

// gilde.exe 0x43da2c — VIBE_Character_Stop.
//   if (*a1) StandUp(*a1); else ReportError("StopCharacter(): Invalid character");
//   return 0;
int Stop(RenderActor* a) {
    if (a)
        GetCharRenderHooks().standUp(a);
    else
        GetCharRenderHooks().reportError("StopCharacter(): Invalid character");
    return 0;
}

// gilde.exe 0x453358 — VIBE_Character_ResetAiTarget.
//   if ((type==1 || type==2) && +356) {
//     SetGrayColorThunk(0,16); v2 = +436; +448 = -1; +436 = v2 | 1;
//   }
void ResetAiTarget(RenderActor* a) {
    if (a->type == 1 || a->type == 2) {
        if (a->isMaster) {
            // SetGrayColorThunk(0,16) is a pure render side effect (no hook needed).
            a->aiTarget = -1;
            a->aiFlags |= kAiResetBit;
        }
    }
}

// ===========================================================================
// Eligibility predicates (delegate the turn gate to character_state.cpp).
// ===========================================================================
namespace {
// Build the TurnObject the *ForTurn rules read from a RenderActor.
TurnObject MakeTurnObject(const RenderActor* a) {
    TurnObject o{};
    o.type        = a->type;
    o.id          = a->id;
    o.isMaster    = a->isMaster;
    o.ownerKindByte = 0;
    o.ownerId     = a->ownerColumn0;   // resolved owner-person id (owner+1 byte-pun)
    o.ownerPlayer = a->ownerPlayer;
    o.hasOwner    = a->hasOwner;
    return o;
}
} // namespace

// gilde.exe 0x4d4f78 — VIBE_Character_IsAccidentCandidate.
//   v1 = *(WORD*)a1 != 0xFFFF; if (!*(BYTE*)(a1+8)) v1 = 0;
//   return !*(BYTE*)(a1+2) && v1 && (v3=*(DWORD*)(a1+364)) && (v4=*(DWORD*)(a1+388))
//       && *(DWORD*)(v4+44) == *(DWORD*)(v3+1) && IsOwnerForTurn(a1);
bool IsAccidentCandidate(const RenderActor* a) {
    bool aliveMarker = (a->marker != 0xFFFF);
    if (!a->alive)
        aliveMarker = false;
    if (a->type != 0)
        return false;
    if (!aliveMarker)
        return false;
    if (!a->owner)
        return false;
    if (!a->attached)
        return false;
    if (a->ownerHandle44 != a->ownerColumn0)
        return false;
    TurnObject o = MakeTurnObject(a);
    return IsOwnerForTurn(&o);
}

// (VIBE_Character_IsDiseaseCandidate 0x4d762c is already translated in illness.cpp
// as IllnessIsDiseaseCandidate — not duplicated here, ODR.)

// ===========================================================================
// Anim / queue control.
// ===========================================================================

// gilde.exe 0x4047f0 — VIBE_Character_ToggleAniPlayback.
//   if (result[74] && result[28]) {                 // animB && mesh handles
//     if (a2) { ClearLoopFlags(mesh+492+244); +140 |= 4; }
//     else    { SetLoopFlags(mesh+492+244, 1); +140 &= ~4; }
//   }
void ToggleAniPlayback(RenderActor* a, bool pause) {
    if (!a->action || !a->mesh)   // result[74] (animB) && result[28] (mesh) present
        return;
    const CharRenderHooks& h = GetCharRenderHooks();
    if (pause) {
        h.animClearLoopFlags(a->mesh);
        a->flagsA |= kRaAnimPaused;
    } else {
        h.animSetLoopFlags(a->mesh);
        a->flagsA &= static_cast<u8>(~kRaAnimPaused);
    }
}

// gilde.exe 0x43da5c — VIBE_Character_CmdKillCharacterAnimations.
//   if (*a1) { node = *(*a1+296); if (node) { node+400 = 1;
//       while (node+40) UnlinkEntry(node+40); } return 0; }
//   else { ReportError("DeleteCharacterAnimations(): Invalid character"); return 0; }
int CmdKillCharacterAnimations(RenderActor* a) {
    if (a) {
        void* node = a->action;
        if (node) {
            // node+400 (abort byte) is set on the action node; the queue is then
            // drained by repeatedly unlinking the head's first sub-entry (+40).
            const CharRenderHooks& h = GetCharRenderHooks();
            while (h.unlinkActionEntry(node) != nullptr) {
                // unlinkActionEntry returns the remaining head (0 == drained).
            }
        }
        return 0;
    }
    GetCharRenderHooks().reportError("DeleteCharacterAnimations(): Invalid character");
    return 0;
}

// gilde.exe 0x43d524 — VIBE_Character_AttachItemToBone2.
//   if (!*a1) return 0; AttachItemToBone(*a1, 2, *a2); return 1;
int AttachItemToBone2(RenderActor* a, const char* name) {
    if (!a)
        return 0;
    GetCharRenderHooks().attachItemToBone(a, 2, name);
    return 1;
}

} // namespace guild::sim
