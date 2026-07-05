#pragma once
// gilde.exe — per-frame RENDER-SUBMIT slot builders (guild::app).
//
// Faithful 1:1 reconstruction of the two per-object "build a render/animation
// submit slot for an entity" functions the in-game frame tick calls when it walks
// the live object list and hands each entity to the renderer:
//
//   0x412fa0  VIBE_GameLogic_Objects   (allocate + fill a widget/draw slot for an
//                                        entity, dispatching on the entity's kind)
//   0x413580  VIBE_GameLogic_Entities  (advance + submit an entity's animation
//                                        state: state-update -> decompress blob ->
//                                        per-kind Animation_Basic -> finalize)
//
// THE ENTITY RECORD ARRAY  (dword_62D204, 84-byte stride; count = dword_62D208)
// ---------------------------------------------------------------------------
// Both functions read the engine's flat entity-record array. Each 84-byte record
// (the same array the already-translated VIBE_GameLogic_Movement @0x412f18 walks):
//   +0x30 (+48)  redirect-suppress word (when 0, a 5/8 record redirects to +76)
//   +0x34 (+52)  scene/render-state handle  (0 => not yet realised; State_Helper)
//   +0x3C (+60)  kind/type dword   (5,8 = linked-pair; 21*idx stride feeds the blob)
//   +0x40 (+64)  refcount
//   +0x44 (+68)  flag byte (bit1 -> add byte_62D220 grid offset)
//   +0x4C (+76)  partner record index (5/8 pair)
//   +0x4E (+78)  packed sub-state hi-word (State_GetCurrent arg)
//   +0x50 (+80)  packed sub-state hi-word (State_GetCurrent arg)
// (a parallel 21-dword "state table" at the same base, indexed 21*idx, holds the
//  per-record compressed-state blob the Entities path decompresses; field +60 of
//  that 84-byte row, i.e. table[21*idx].dword[15], is the blob pointer.)
//
// THE WIDGET / DRAW SLOT  (dword_69FFB4, 740-byte stride; VIBE_Widget_AllocSlot)
// ---------------------------------------------------------------------------
// VIBE_GameLogic_Objects fills the freshly-allocated 740-byte widget slot the
// engine uses to carry a live object through the GUI/draw Z-order. The fields it
// touches (offsets are the engine's, REUSED via gui::Widget accessors):
//   +8   render-state handle (= ResolveObjectState v23)
//   +12  resolved entity index (= ResolveObjectState v22)
//   +16/+18 screen x,y  (a1,a2)   +24 kind byte (entity +60)   +26 order = 2
//   +28/+30/+32/+34  clip/screen bounds (dword_64A1B4/BC/B8/C0)
//   +72  button flag (=1 for kind 8)
//   +116 anim/shapeanim handle (kinds 1/4/8/17) or grid metric source
//   +448/+452/+456/+460/+464/+468/+472  per-kind anim geometry seeds
//   +104/+105  packed grid metric bytes (kind 8 Coord_Transform result)
//
// SCOPE / FIDELITY
// ---------------------------------------------------------------------------
// The engine reaches a dozen sibling leaves (ResolveObjectState, MarkObjectUsed,
// Coord_Transform, ShapeAnim_RegisterSlot, DecompressState_Blob, State_Update,
// State_GetCurrent, Animation_Basic, Decompression_Finalize). Per AGENT_GUIDE
// ("if you must call a function from a module you don't own, forward-declare a
// hook"), every such leaf is an injected function-pointer in the Hooks struct
// below, and the entity-record array + widget-slot writes go through caller-
// supplied bases. The CONTROL FLOW — the bounds/alloc gates, the exact per-kind
// (0,1,4,5,8,17) dispatch and the field writes — is reproduced 1:1; only the leaf
// calls are indirected (the same decoupling render/frame.h + render/scene.h use).
#include "guild/common/types.h"
#include "gui/object.h"   // gui::Widget, g_widgets, Widget_AllocSlot (REUSED)

#include <cstdint>
#include <functional>

namespace guild::app {

// One entity record as the submit builders read it (84-byte stride, dword_62D204).
// Only the touched fields are named; the rest is opaque padding so the stride is
// byte-exact (same layout family as EntityMoveRecord in entity_movement.h).
#pragma pack(push, 1)
struct EntitySubmitRecord {
    u8  pad0[48];      // +0x00
    i32 suppress;      // +0x30 (+48) redirect-suppress word
    i32 stateHandle;   // +0x34 (+52) scene/render-state handle (0 => unrealised)
    u8  pad1[4];       // +0x38
    i32 kind;          // +0x3C (+60) entity kind (5/8 = linked pair)
    i32 refcount;      // +0x40 (+64)
    i32 flag68;        // +0x44 (+68) bit1 => add the grid offset (byte_62D220)
    u8  pad2[4];       // +0x48
    i32 linkIndex;     // +0x4C (+76) partner record index
    // The State_GetCurrent tail reads two overlapping DWORDs and keeps their
    // high words (gilde.exe 0x413700/0x413703 + sar 16):
    //   arg3 = *(int *)(rec+0x50) >> 16  ->  the i16 at +0x52 (+82)
    //   arg4 = *(int *)(rec+0x4E) >> 16  ->  the i16 at +0x50 (+80)
    i16 subStateLo;    // +0x50 (+80) sub-state low word  (State_GetCurrent arg4)
    i16 subStateHi;    // +0x52 (+82) sub-state high word (State_GetCurrent arg3)
};
#pragma pack(pop)
static_assert(sizeof(EntitySubmitRecord) == 84, "entity record must be 84 bytes");

// Injected sibling-leaf hooks (the original's cross-module calls). A null hook is
// treated as a no-op returning 0 (the documented neutral outcome), matching the
// "if (global) call(...)" guards the render walk uses elsewhere.
struct RenderSubmitHooks {
    // gilde.exe 0x40eaf0 — VIBE_Gui_ResolveObjectState(idx). Resolves the live
    // record for entity `idx`: writes the render-state handle to *outState and the
    // resolved entity index to *outIndex; returns the grid-offset delta it set in
    // dword_62D2A4. (For a non-redirected record outState=record+52, outIndex=idx.)
    int (*resolveObjectState)(int idx, int* outState, int* outIndex) = nullptr;
    // gilde.exe 0x412ea4 — VIBE_Gui_MarkObjectUsed(idx). Stamps the record live.
    void (*markObjectUsed)(int idx) = nullptr;
    // gilde.exe 0x5d8d54 — VIBE_ShapeAnim_RegisterSlot(x, y, 0, geom). Registers a
    // shape-anim playback slot for kind-4 objects; returns the slot handle.
    int (*shapeAnimRegisterSlot)(int x, int y, int geom) = nullptr;
    // gilde.exe 0x5d8b00 — VIBE_Coord_Transform(geom, metric). Returns the packed
    // 2D metric record pointer the kind-8 grid placement reads (+6/+10/+44/+46).
    // Returns a small handle the caller indexes via gridMetricWord.
    int (*coordTransform)(int geom, int metric) = nullptr;
    int (*gridMetricWord)(int handle, int byteOff) = nullptr; // *(coordTransform+off)
    // The mesh-geometry handle for a realised slot (the original's
    // *(dword_69FFB4 + 740*slot + 12) Object3D pointer). Returns 0 when absent.
    int (*slotGeometry)(int slot) = nullptr;

    // --- VIBE_GameLogic_Entities leaves -----------------------------------
    // gilde.exe 0x40e9e8 — VIBE_State_Update(idx). Realises a record's render
    // state; returns the state handle (Entities passes it as the blob index).
    int (*stateUpdate)(int idx) = nullptr;
    // gilde.exe 0x423500 — VIBE_DecompressState_Blob(out, blob). Decompresses the
    // per-record compressed state into the working surface; returns nonzero on a
    // successful decode (the gate for the per-kind animation submit).
    int (*decompressBlob)(int out, int blob) = nullptr;
    // gilde.exe 0x5d85b8 — VIBE_Animation_Basic(x, y, frame, out, arg). Submits a
    // basic 2D animation frame for the realised entity.
    void (*animationBasic)(int x, int y, int frame, int out, int arg) = nullptr;
    // gilde.exe 0x40e728 — VIBE_State_GetCurrent(x, y, w, h). Marks the entity's
    // dirty screen rect (the submit's bookkeeping tail).
    void (*stateGetCurrent)(int x, int y, int w, int h) = nullptr;
    // gilde.exe 0x4235dc — VIBE_Decompression_Finalize(out). Releases the decode
    // scratch; returns the original's result (Entities returns it).
    int (*decompressionFinalize)(int out) = nullptr;

    // The per-record compressed-state blob dword the Entities path decodes:
    // *(dword_62D204 + 4*(21*idx) + 60) — NOTE 4*21 == 84, i.e. this is the SAME
    // +60 dword the kind reads come from; the binary compares this very value
    // (edx at 0x4136b3) for the per-kind animation dispatch. When null, the
    // builder reads entities[idx].kind directly (byte-identical source).
    int (*stateBlob)(int idx) = nullptr;
    // byte_62D220 grid-cell offset added to a +68-bit1 record's index.
    u8 gridOffset = 0;
    // The recovered clip/screen-bound globals written into the widget slot.
    i16 clipX0 = 0;  // dword_64A1B4
    i16 clipX1 = 0;  // dword_64A1BC
    i16 clipY0 = 0;  // dword_64A1B8
    i16 clipY1 = 0;  // dword_64A1C0
};

// gilde.exe 0x412fa0 — VIBE_GameLogic_Objects
//   (__usercall eax=fn(x@ax, y@dx, entityIdx@ebx)).
// Allocates a 740-byte widget/draw slot (Widget_AllocSlot, REUSED), resolves the
// entity's render state (resolveObjectState) and fills the slot:
//   slot.id(+8)=stateHandle  slot.dataPtr(+12)=entityIdx  slot.type(+24)=kind
//   slot.x/y(+16/+18)=x,y    slot.order(+26)=2  clip bounds(+28..+34)
//   if kind==8: slot.btnFlagB(+72)=1
//   slot +464=-1, +468=0, +472=0  (per-kind anim geometry seeds)
// then dispatches on the kind byte (entity +60), seeding the kind's geometry:
//   kind 0  -> nothing            kind 1 -> copy geom metric +44/+46 to slot w/h
//   kind 4  -> ShapeAnim_RegisterSlot -> slot +116; copy geom metric to w/h
//   kind 5/8 -> Coord_Transform grid metric -> slot +20/+22/+448..+460, +104/105=8
//   kind 17 -> copy geom metric +12/+14 -> slot w/h; slot +116 = 2
// Returns the widget slot index, or -1 (entityIdx out of range / array full).
int GameLogicObjects(i16 x, i16 y, int entityIdx, EntitySubmitRecord* entities,
                     int entityCount, const RenderSubmitHooks& hooks);

// gilde.exe 0x413580 — VIBE_GameLogic_Entities
//   (__usercall eax=fn(scrX@ax(result), scrY@dx, entityIdx@ebx, out@ecx)).
// Advances + submits an entity's animation state for the frame:
//   realise the record's state (State_Update, redirecting a 5/8 pair to its +76
//   partner when its +48 suppress word is 0), apply the +68-bit1 grid offset, then
//   DecompressState_Blob(out, blob); on success submit the per-kind Animation_Basic
//   frame, mark the dirty rect (State_GetCurrent) and Decompression_Finalize(out).
// `scrX`/`scrY` are the screen placement (the original threaded them in ax/dx high
// words). Returns DecompressState_Blob's result (0 if it failed / idx out of range).
int GameLogicEntities(i16 scrX, i16 scrY, int entityIdx, int out,
                      EntitySubmitRecord* entities, int entityCount,
                      const RenderSubmitHooks& hooks);

} // namespace guild::app
