#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — render-engine object-list / texture-budget leaves.
//
// A self-contained slice of VIBE_Render_* leaves translated 1:1 from the
// Hex-Rays reference of record (gilde.exe, imagebase 0x400000). Every function
// here is pure pointer-graph bookkeeping or integer arithmetic over
// caller-supplied records and a small block of engine globals — no DDraw/GDI
// device calls in the math path.
//
// The few genuinely-external touchpoints (CRT sprintf, the engine allocator's
// debug-free, texture-record release, the DirectDraw "evict managed textures"
// call, and the available-vidmem query — all of which are DDraw/CRT boundary
// per rules 3 & 6) are routed through an installable hook struct with inert
// defaults defined in render_recon_objlist.cpp. Tests install their own hooks.
//
// Translated functions (verified UNTRANSLATED at time of writing):
//   0x5e0e00  VIBE_Render_InitObjectList     (draw-list head reset + identity)
//   0x5e0f30  VIBE_Render_FreeObjectNode     (doubly-linked node teardown)
//   0x5e0e74  VIBE_Render_FreeObjectList     (drain the object list)
//   0x5dcd50  VIBE_Render_FormatCardInfo     (3D-card caps -> text, bit math)
//   0x5b379c  VIBE_Render_AdjustTextureBudget(per-frame vidmem budget heuristic)
//   0x5b9ef4  VIBE_Render_SetGammaTable      (gamma-change cache/texture reload)
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Object-list globals (gilde.exe data block around 0x1408100 / 0x1408438).
//
// The original engine keeps one global draw-list whose head/tail sentinel pair
// lives at 0x1408438 (dword_1408438) / 0x1408440 (unk_1408440). The reconstruction
// models the sentinel as a fixed address-stable node so that the empty-list
// predicate (head == &sentinel) reproduces 1:1. Plus a 16-float transform block
// at 0x1408100 reset to an identity-ish matrix by InitObjectList.
// ---------------------------------------------------------------------------

// 0x1408100.. — the transform/state block written by InitObjectList. Stored as
// raw 32-bit words because the original writes float bit-patterns (1065353216 ==
// 1.0f) and integer zeros interchangeably.
struct ObjListState {
    // 0x1408748, 0x140843C, 0x140874C — list bookkeeping words.
    u32 word_748;     // dword_1408748
    u32 word_43C;     // dword_140843C
    u32 word_74C;     // dword_140874C  (= &block sentinel at 0x1408130 in orig)
    // 0x1408100..0x140812C — 16-word transform block (bit patterns, not floats).
    u32 block[12];    // dword_1408100 .. dword_140812C  (12 words written)
    // 0x1408438 — list head pointer; empty when == sentinel value below.
    u32 listHead;     // dword_1408438
};

// Returns the process-wide ObjListState (mirrors the gilde.exe globals).
ObjListState& ObjListGlobals();

// Sentinel value the original stores into dword_1408438 / dword_140874C to mark
// "empty list" (the address of unk_1408440 / unk_1408130 respectively). The
// concrete numeric value is irrelevant to behavior; only the head==sentinel test
// matters, so the reconstruction uses a stable opaque marker.
u32 ObjListSentinel();

// ---------------------------------------------------------------------------
// Render-node record layout (subset used by FreeObjectNode), gilde.exe offsets.
// Caller passes a pointer to this; FreeObjectNode unlinks it and releases its
// owned allocations. Offsets match the original exactly.
// ---------------------------------------------------------------------------
struct RenderNode {
    u8   pad0[0x28];
    u32  alloc28;     // +0x28  — owned allocation A (freed)
    u8   pad2C[0xD4 - 0x2C];
    u32  texEntry;    // +0xD4  — texture-record handle (released if non-zero)
    u8   padD8[0xDC - 0xD8];
    u32  allocDC;     // +0xDC  — owned allocation B (freed)
    u32  allocE0;     // +0xE0  — owned allocation C (freed)
    u8   padE4[0x2F0 - 0xE4];
    void* owner;      // +0x2F0 — owning list (compared to off_649D64)
    u8   pad2F4[0x308 - 0x2F4];
    RenderNode* prev; // +0x308 — doubly-linked prev
    RenderNode* next; // +0x30C — doubly-linked next
};

// Owning-list record (subset): head/tail at +0xA4 / +0xA8 are RenderNode*.
struct RenderNodeOwner {
    u8   pad0[0xA4];
    RenderNode* head; // +0xA4
    RenderNode* tail; // +0xA8
};

// ---------------------------------------------------------------------------
// 3D-card caps record (subset used by FormatCardInfo), gilde.exe offsets.
// ---------------------------------------------------------------------------
struct CardCaps {
    u8   pad0[780];
    u8   flags;          // +780 — bit0 translucent, bit1 fake_translucency,
                         //        bit2 perspective_correction, bit4 linear_filter,
                         //        bit3 can_clip  (decoded by the original's shifts)
    u8   pad[3];
    i32  minTexW;        // +784
    i32  minTexH;        // +788
    i32  maxTexW;        // +792
    i32  maxTexH;        // +796
};

// ---------------------------------------------------------------------------
// Texture-budget globals (gilde.exe 0x649D8C..0x649D9C).
// ---------------------------------------------------------------------------
struct TextureBudgetState {
    u32 limitA;     // dword_649D8C — first budget limit (halved on pressure)
    u32 limitB;     // dword_649D90 — second budget limit (halved on pressure)
    u32 missesA;    // dword_649D98 — consecutive-pressure counter A
    u32 missesB;    // dword_649D9C — consecutive-pressure counter B
    u32 lastTick;   // dword_649D94 — frame stamp of last evaluation
};
TextureBudgetState& TextureBudgetGlobals();

// ---------------------------------------------------------------------------
// Cross-module / boundary hooks (inert defaults in the .cpp).
// ---------------------------------------------------------------------------
struct RenderReconObjListHooks {
    // 0x5e0e9c VIBE_Render_UnlinkObjectNode — unlink a node that is NOT owned by
    // the global list (the != off_649D64 branch). Already reconstructed in
    // render_leaves3; routed via hook to keep this unit self-contained.
    void (*unlinkObjectNode)(RenderNode* node) = nullptr;
    // 0x5d9a0c VIBE_Texture_ReleaseEntry — release a texture-record by handle.
    void (*releaseTextureEntry)(u32 texEntry) = nullptr;
    // 0x43923c VIBE_Memory_FreeDebug — engine debug allocator free, called on the
    // node's owned allocation handles (node+0x28, +0xDC, +0xE0).
    void (*memoryFreeDebug)(u32 ptr) = nullptr;
    // 0x43923c VIBE_Memory_FreeDebug applied to the node record itself (the
    // original's final `mov eax,edx; call FreeDebug`). Separate entry because the
    // node is a real pointer, not a u32 handle.
    void (*memoryFreeNode)(RenderNode* node) = nullptr;

    // CRT sprintf boundary (0x5cba00 VIBE_Crt_Sprintf_0). Writes the formatted
    // card-info string into out (caller guarantees capacity). Default uses
    // std::snprintf. Returns the count the original sprintf would return.
    int (*sprintfCardInfo)(char* out, int translucent, int fakeTranslucency,
                           int perspectiveCorrection, int linearFilter, int canClip,
                           int minW, int minH, int maxW, int maxH) = nullptr;

    // 0x435ba8 VIBE_Render_QueryAvailableVidMem (DDraw GetAvailableVidMem). The
    // original signature: total returned in ecx, free written to *freeOut.
    // total = queryVidMem(&freeOut). Default returns 0/0 (inert).
    u32 (*queryVidMem)(u32* freeOut) = nullptr;
    // 0x64A31C-vtable+0x2C EvictManagedTextures (DDraw/D3D). Returns HRESULT-like
    // status (0 == ok). Default returns 0.
    u32 (*evictManagedTextures)() = nullptr;
    // engineEnabled mirrors byte_649D70; deviceReady mirrors dword_64A31C!=0;
    // lockDepth mirrors dword_64A050. AdjustTextureBudget only runs when
    // engineEnabled && deviceReady && lockDepth <= 1.
    bool (*budgetActive)() = nullptr;

    // SetGammaTable callees (0x5b9ef4): texture-cache flush + per-floor reload.
    int  (*textureCacheReset)() = nullptr;             // 0x5b9f54
    int  (*floorReloadTextures)(u32 floorHandle) = nullptr; // 0x5bd2d8
    // Active primary floor handle (dword_64A028, 0 if none).
    u32  (*activeFloor)() = nullptr;
    // Walk the floor-record table (dword_13ECF74, stride 246, 64 slots); invoke
    // visit(handle) for each non-zero entry != primaryFloor. Mirrors the loop.
    void (*forEachFloorRecord)(u32 primaryFloor, void (*visit)(u32)) = nullptr;
    // Mirror of byte_64A02C (last-applied gamma byte).
    u8*  gammaByte = nullptr;
};

RenderReconObjListHooks& ObjListHooks();

// ---------------------------------------------------------------------------
// Translated functions.
// ---------------------------------------------------------------------------

// 0x5e0e00 — VIBE_Render_InitObjectList. Resets the global draw-list head and
// transform block to their initial (identity) state. Returns 1065353216 (1.0f
// bit pattern), exactly as the original.
u32 InitObjectList();

// 0x5e0f30 — VIBE_Render_FreeObjectNode (__usercall, eax=node, esi=owner unused).
// If node->owner == the global list owner, splice it out of the doubly-linked
// list (fixing prev/next and the owner head/tail); otherwise call the external
// unlink hook. Then release the node's texture entry (if any) and free its three
// owned allocations and the node itself via the allocator hook.
void FreeObjectNode(RenderNode* node, RenderNodeOwner* globalOwner);

// 0x5e0e74 — VIBE_Render_FreeObjectList. Drains the list by repeatedly freeing
// whatever the head currently references until the list is empty. Caller supplies
// the head accessor + owner so the unit stays self-contained.
//   while (head() != sentinelNode) FreeObjectNode(head(), owner);
void FreeObjectList(RenderNode* (*head)(), void (*advance)(), RenderNode* sentinel,
                    RenderNodeOwner* globalOwner);

// 0x5dcd50 — VIBE_Render_FormatCardInfo (__usercall, eax=caps, ecx=out buffer).
// Formats the card capability bitfields + texture-size limits into out. Returns
// the sprintf return value. Bit extraction reproduced exactly from the shifts.
int FormatCardInfo(const CardCaps* caps, char* out);

// 0x5b379c — VIBE_Render_AdjustTextureBudget (__usercall, eax=tick, ebx=arg2).
// Per-frame texture-memory budget heuristic. When at least 0x40 ticks have
// elapsed since the last evaluation and the device is ready, queries available
// vidmem and, under pressure, halves limitA or limitB and asks the driver to
// evict managed textures. Returns the low byte of the propagated status.
u8 AdjustTextureBudget(u32 tick, u32 arg2);

// 0x5b9ef4 — VIBE_Render_SetGammaTable (__usercall, eax=gamma byte). On a change,
// flushes the texture cache, records the new gamma, and reloads textures for the
// primary floor and every other live floor record. Returns the propagated status.
int SetGammaTable(u8 gamma);

} // namespace guild::render
