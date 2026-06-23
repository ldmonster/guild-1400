#pragma once
// character_recon5_morph — 1:1 reconstruction of the VIBE_Character morph-anim
// release / re-check cluster and the per-slot fade-out stepper of gilde.exe.
//
// Reconstructed here (byte-faithful control flow + exact float op-order):
//   VIBE_Character_ReleaseMorphAni  0x403708  (__usercall eax,ecx,edi,esi)
//   VIBE_Character_CheckAniMorph    0x403764  (__usercall eax)
//   VIBE_Character_FadeOutSlots     0x4015cc  (cdecl)
//
// The genuine anim-stream subsystem (PruneExpiredAttachments / FindFreeMeshSlot /
// ReleaseMeshData / LoadStreamToStock / AttachToBone / CreateMorphAnim /
// ComputeBoneDelta / ComputeBoneMatrices) and the object transparency setter
// (ChangeTransparency) are not yet reconstructed; they are routed through
// MorphHooks / FadeHooks below as INERT-DEFAULT hooks.
//
// What IS in-scope and reproduced exactly:
//   * FadeOutSlots: the per-slot fade ramp  t = (now-start)/50  (or 1-that for the
//     fade-OUT direction), clamp to [0,1], scale by 255, the byte-transparency
//     packing, and the slot-table walk (stride 3 dwords, [0]=id [1]=dir [2]=start).
//   * CheckAniMorph: the morph-key blend  v33 = (delta + nextKey)/2  with the +0.5
//     bias, the bone-key index arithmetic (192-byte key stride), and the morph
//     state machine (which of primary/morph/pending handles get attached).
//   * ReleaseMorphAni: the morph release guard + handle clear.
//
// Object/character offsets and constants: see character_recon5_transport.h.
// No third-party tech introduced (rule 6 N/A).
#include "guild/common/types.h"
#include "character_recon5_transport.h"   // TChar / TObject / constants

namespace guild::sim {

// ===========================================================================
// Fade-out slot table.  In the binary this is a flat dword array
// [dword_66F010 .. dword_66F0D0), walked with `v0 += 3`.  That is 16 entries of
// 3 dwords (0xC0/12 = 16).  Each entry:
//   +0  int   live id    (0 == empty slot, skipped)
//   +4  byte  direction  (nonzero == fade IN, zero == fade OUT)
//   +8  int   start tick
// The struct below mirrors the 12-byte stride exactly.
// ===========================================================================
struct FadeSlot {
    int  id    = 0;     // +0
    u8   dir   = 0;     // +4 (byte; nonzero -> fade in)
    u8   _pad1 = 0, _pad2 = 0, _pad3 = 0;
    int  start = 0;     // +8
    // Per-slot the binary also reaches a character (v3) and its object via the
    // slot id; modelled as an explicit char pointer the caller supplies.
    TChar* ch  = nullptr;  // resolved character for this slot
};

struct FadeHooks {
    // VIBE_Object_ChangeTransparency 0x5b2710 — set object transparency byte.
    void (*changeTransparency)(TObject* obj, int sceneCtx, int packed, FadeSlot* slot) = nullptr;
    // VIBE_Character_SetVisible 0x401894 — toggle visibility (end of a fade).
    void (*setVisible)(TChar* ch, int visible) = nullptr;
    void* ctx = nullptr;
};

// Result of stepping one fade slot — exposed for golden tests.
struct FadeStep {
    f32  ramp01   = 0.0f;   // clamped [0,1] fraction
    f32  value255 = 0.0f;   // ramp01 * 255
    bool finished = false;  // reached an endpoint (slot cleared)
    bool madeVisible = false;   // SetVisible(.,1) on completed fade-in
    bool madeInvisible = false; // SetVisible(.,0) on completed fade-out
};

// Pure helper: compute the clamped fade fraction and 0..255 value for one slot,
// exactly as 0x4015e0..0x401676.  `now` is dword_62D008 (current tick).
// fadeRate = 1/50, fadeScale = 255.
FadeStep FadeComputeValue(const FadeSlot& s, int now);

// ---------------------------------------------------------------------------
// gilde.exe 0x4015cc — VIBE_Character_FadeOutSlots()
//   Walk the fade-slot table; for each live slot compute the ramp, apply it as a
//   transparency byte to the slot's character object (+ its secondary, if any),
//   and when the ramp reaches an endpoint set full/zero transparency, toggle
//   visibility, and clear the slot.
//   `now` = dword_62D008.  `slots` is the table (count 16 in the binary).
// ---------------------------------------------------------------------------
void FadeOutSlots(FadeSlot* slots, int count, int now, FadeHooks& H);

// ===========================================================================
// Morph anim hooks.
// ===========================================================================
struct MorphHooks {
    // VIBE_Anim_PruneExpiredAttachments 0x5d0d38
    void (*pruneExpired)(int attachListBase) = nullptr;
    // VIBE_Anim_FindFreeMeshSlot 0x5cf114 — returns a free mesh-slot index (0=none).
    int (*findFreeMeshSlot)() = nullptr;
    // VIBE_Anim_ReleaseMeshData 0x5cfe30
    int (*releaseMeshData)(int meshSlot, int a2, int a3, int a4) = nullptr;
    // VIBE_Anim_LoadStreamToStock 0x5d3858
    void (*loadStreamToStock)(const char* path, int flag) = nullptr;
    // VIBE_Anim_AttachToBone 0x5d0b64 — attach a stream to a bone; returns handle.
    int (*attachToBone)(int attachListBase, int request) = nullptr;
    // VIBE_Anim_CreateMorphAnim 0x5cf150 — build a morph stream; returns nonzero ok.
    int (*createMorphAnim)(int a1, int v31, int a3, void* keyA, void* keyB, int a6,
                           const char* name, int frames) = nullptr;
    // VIBE_Anim_ComputeBoneDelta 0x5cba40
    void (*computeBoneDelta)(int obj, int handle, int a3, int a4, int a5) = nullptr;
    // VIBE_Anim_ComputeBoneMatrices 0x5cc0d0
    void (*computeBoneMatrices)(int obj) = nullptr;
    // VIBE_Character_IndexFromPointer 0x426724 (universe -> slot)
    int (*indexFromPointer)(int universe) = nullptr;
    void* ctx = nullptr;
};

// Result of the morph-key blend math (in-scope), exposed for golden tests.
//   v35 = delta + nextKey  ; v33 = (v35)/2 ; frames = (int)((double)v33 + 0.5)
struct MorphKeyBlend {
    int sumDelta = 0;   // v35 = lastKey - firstKey + nextKey  (see cpp)
    int half     = 0;   // v33 = sumDelta / 2  (integer divide, as in the binary)
    int frames   = 0;   // (int)((double)half + 0.5)
};

// Pure helper for the CheckAniMorph blend at 0x4038ed..0x403948.
//   firstKeyVal / lastKeyVal : *(key0) / *(keyLast) (the +0 dword of a 192-byte key)
//   nextKeyVal               : *(pendingKey0)
MorphKeyBlend MorphComputeBlend(int firstKeyVal, int lastKeyVal, int nextKeyVal);

// ---------------------------------------------------------------------------
// gilde.exe 0x403708 — VIBE_Character_ReleaseMorphAni(ch)
//   If the character has a live morph handle (+124), prune expired attachments,
//   release its mesh data, and clear the handle.  Returns the (low byte of the)
//   release result, or the unchanged ch value when there was no morph.
// ---------------------------------------------------------------------------
int ReleaseMorphAni(TChar* ch, MorphHooks& H);

// ---------------------------------------------------------------------------
// gilde.exe 0x403764 — VIBE_Character_CheckAniMorph(ch)
//   Drive the morph-anim state machine: tear down a stale primary attachment,
//   release a finished morph, and (when both primary and morph are idle) attach
//   the pending CurrentAnimStream; or, when a primary is live, build a morph
//   blending stream between the current and pending key frames.
//   This reconstructs the full control flow + the key-blend math; all the
//   anim-subsystem effects go through MorphHooks.
// ---------------------------------------------------------------------------
void CheckAniMorph(TChar* ch, MorphHooks& H);

} // namespace guild::sim
