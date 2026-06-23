#pragma once
// =============================================================================
// guild::sim — the per-frame world/entity SCENE-UPDATE pass (wave-21).
//
// 1:1 reconstruction of the two functions that make up the engine's per-frame
// scene-update / redraw pass:
//
//   gilde.exe 0x41ceb4  VIBE_DecompressGameState   -> SceneUpdatePass
//   gilde.exe 0x40e50c  VIBE_Decompressor_Init     -> DecompressorInit
//
// NB: the IDA "Decompress*" names are symbol artefacts — these are NOT a codec.
// 0x41ceb4 walks a "decompressor" record (684-byte stride, dword_676A60[684*a1])
// whose +388 (=v2+97) field is the count of child entity sub-records; for each
// sub-record it walks the active entity table (dword_67EB80, 238-dword stride),
// and for each entity's child-node list (740-byte scene nodes, dword_69FFB4) it
// dispatches the per-record renderers by the node's type byte at +24:
//
//   type 1            -> Animation_Basic (+ Advanced if +40) + Object_Reinitialize
//   type 4            -> Physics_Update  (+ Object_Reinitialize if no +44)
//   type 5 / 8        -> the animated-entity block (Velocity/Animation/Reinit +
//                        the +120 label via Animation_Apply)
//   type 0x11 (17)    -> countdown *(node+26)
//   type 0x41 (65)    -> Object_Update
//   type 0x42 (66)    -> Building_Update
//   type 0x43 (67)    -> State_Finalize + Animation_Apply (the +116 label)
//   type 69 (0x45)    -> Entity_AnimationUpdate + Entity_InteractionLogic + reinit
//
// The DDraw back-buffer lock/unlock pair (VIBE_DecompressState_Blob @0x423500 /
// VIBE_Decompression_Finalize @0x4235dc) is the rule-3 Vulkan boundary: routed
// through the graphics hook (decompressStateBlob/decompressionFinalize). Every
// other edge (the entity-table walk, the dispatch, the fixed-point >>16 reads,
// the type switch, Decompressor_Init's 10240/20 handler-table scan) is verbatim.
//
// The per-record renderers themselves (0x40eea0 / 0x415b78 / 0x418f34 /
// 0x41078c) have their fully-recovered cores in object_update.{h,cpp}; the
// per-node dispatch switch lives here (DispatchChildNode) and the GPU/scene
// leaves it emits are injected via the SceneFrameHooks below.
// =============================================================================
#include "guild/common/types.h"

#include <cstdint>

namespace guild::sim {

using namespace guild;

// ---------------------------------------------------------------------------
// 740-byte scene node (dword_69FFB4 + 740*idx). Modeled by the byte offsets the
// scene-update code reads. The fixed-point coordinate fields are 16.16 (read as
// i32 then arithmetic-shift >>16 for the integer pixel value).
// ---------------------------------------------------------------------------
// Logical model (not a memcpy image): each field carries its binary byte offset
// for provenance; the scene-update code accesses them via these named fields.
struct RenderNode {                   // dword_69FFB4 + 740*idx
    i32  id        = 0;   // +0   identity (compared against dword_62D22C)
    i32  meshHandle= 0;   // +4   (v6+4: stream handle; 0 => skip this node)
    i32  styleHandle=0;   // +12  (v6+12: font/shape handle for the blits)
    i32  x         = 0;   // +14  (16.16; >>16 = px X)
    i32  y         = 0;   // +16  (16.16; >>16 = px Y)
    i32  h         = 0;   // +18  (16.16; >>16 = px height)
    i32  w         = 0;   // +20  (16.16; >>16 = px width)
    i16  life      = 0;   // +22  (content-height stamp / counter)
    i32  clipX     = 0;   // +26  (16.16 clip x)
    i32  clipW     = 0;   // +28  (16.16 clip w)
    i32  clipH     = 0;   // +30  (16.16 clip h)
    i32  clipY     = 0;   // +32  (16.16 clip y)
    u8   typeByte  = 0;   // +24  (the dispatch selector)
    i32  pressed   = 0;   // +40  (pressed/active state)
    i32  child44   = 0;   // +44  (child anim record ptr; 0 => emit reinit border)
    i32  selectFlag= 0;   // +56
    i32  hover64   = 0;   // +64  (hover/highlight -> Velocity_Apply)
    i32  flag76    = 0;   // +76
    i32  flag88    = 0;   // +88
    i32  flag92    = 0;   // +92
    i16  state112  = 0;   // +112 (state index; if set, finalize *(node+110)>>16)
    i32  state110  = 0;   // +110 (16.16 state handle)
    const char* label116 = nullptr; // +116 (text label drawn by type 0x41/0x43)
    u8   label120  = 0;   // +120 (inline label buffer start; flag-checked)
    i32  anim444   = 0;   // +444 (anim-flag byte: &2 active, &1 latch)
    i32  reinitX456= -1;  // +456 (-1 => derive from x; else explicit)
    i32  reinitY460= -1;  // +460 (-1 => derive from y; else explicit)
    float fade480  = 0.0f;// +480 (cross-fade phase; &0x7fffffff != 0 => fading)
};

// ---------------------------------------------------------------------------
// 684-byte decompressor record (dword_676A60 + 684*a1). Modeled by the dword
// indices 97..105 the pass reads (the only ones the walk touches).
// ---------------------------------------------------------------------------
struct DecompRecord {                // dword_676A60 + 684*a1
    i32  childCount = 0;  // +388 = [97]  number of child sub-records (v5)
    i32  originX    = 0;  // +392 = [98]  subtracted from the reinit X
    i32  originY    = 0;  // +396 = [99]  subtracted from the reinit Y
    i32  flag100    = 0;  // +400 = [100] gate: must be nonzero
    i32  flag102    = 0;  // +408 = [102] gate: must be nonzero
    i32  blob105    = 0;  // +420 = [105] the DDraw surface blob (v3 / a4)
    u8   broadcast428 = 0;// +428        right-divider broadcast gate
    u8   broadcast492 = 0;// +492        bottom-divider broadcast gate
    // Each child sub-record is a dword pointer pair; [child+1] indexes the
    // entity table (238-dword stride). Modeled as an entity-index array.
    const i32* childIndex = nullptr; // *((v27)+1) per child (childCount entries)
};

// ---------------------------------------------------------------------------
// Entity table record (dword_67EB80 + 238*idx). The walk reads:
//   [155]  scene-node index of the entity body          (v21[155])
//   +2/+1/+2/+6 16.16 coords (reinit fallback)
//   +26 (16.16) child count   [v21 +26 >>16]
//   [6]   pointer to the child-index list (v21[6])
// ---------------------------------------------------------------------------
struct EntityRecord {                // dword_67EB80 + 238*idx
    i32  x2 = 0;         // +2  (16.16) [UNALIGNED]
    i32  y  = 0;         // +4  = [1] (16.16)
    i32  z  = 0;         // +8  = [2] (16.16)
    i32  w6 = 0;         // +6  (16.16) [UNALIGNED]
    i32  childCount = 0; // +26 (16.16; >>16 = count) [UNALIGNED]
    const i32* childNodes = nullptr;  // [6] -> array of scene-node indices
    i32  bodyNode = -1;  // [155] body scene-node index
};

// ---------------------------------------------------------------------------
// SceneFrameHooks — the genuine GPU/scene boundary leaves the scene-update pass
// emits. Defaults are inert (headless-faithful: the control flow runs, the
// leaves no-op). A live host overrides them to drive the reconstructed render
// leaves (Coord_Push, Animation_Basic/Advanced/Apply, Velocity_Apply, ...).
// ---------------------------------------------------------------------------
struct SceneFrameHooks {
    virtual ~SceneFrameHooks() = default;

    // VIBE_DecompressState_Blob @0x423500 — lock the DDraw back buffer (Vulkan
    // boundary). Returns nonzero if the blob is already locked/finalized.
    virtual int  decompressStateBlob(i32 blob) { (void)blob; return 0; }
    // VIBE_Decompression_Finalize @0x4235dc — unlock the back buffer.
    virtual int  decompressionFinalize(i32 blob) { (void)blob; return 0; }

    // VIBE_Coord_Push @0x5d8ae8 — push a clip rect (a=x b=y c=w d=h order per the
    // call sites: VIBE_Coord_Push(x, y, w, h)).
    virtual void coordPush(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; }

    // VIBE_EntityChild_Process @0x418f34 — dispatched per entity body.
    virtual void entityChildProcess(i32 entityIdx, i32 blob) { (void)entityIdx;(void)blob; }
    // VIBE_Object_Reinitialize @0x40e818 — reinit a node's transform/border.
    virtual void objectReinitialize(int x, int y, int w, int h, i32 rec) {
        (void)x;(void)y;(void)w;(void)h;(void)rec; }
    // VIBE_Object_Update @0x40eea0 — type 0x41 renderer.
    virtual void objectUpdate(i32 label, i32 blob) { (void)label;(void)blob; }
    // VIBE_Building_Update @0x40e2b4 — type 0x42 renderer.
    virtual void buildingUpdate(i32 a, i32 blob) { (void)a;(void)blob; }
    // VIBE_Animation_Apply @0x415b78 — type 0x43 label renderer.
    virtual void animationApply(int x, int y, int w, i32 blob, const char* s, u8 flags) {
        (void)x;(void)y;(void)w;(void)blob;(void)s;(void)flags; }
    // VIBE_Entity_AnimationUpdate @0x4244f8 — type 0x45 prep.
    virtual void entityAnimationUpdate(int a, int b, int c, int d, i32 blob) {
        (void)a;(void)b;(void)c;(void)d;(void)blob; }
    // VIBE_Entity_InteractionLogic @0x41078c — type 0x45 fill-bar renderer.
    virtual void entityInteractionLogic(i32 nodeId, i32 blob) { (void)nodeId;(void)blob; }
    // VIBE_Animation_Basic @0x5d85b8 — type 1/5/8 sprite blit.
    virtual void animationBasic(int x, int y, i32 style, i32 blob, u8 frame) {
        (void)x;(void)y;(void)style;(void)blob;(void)frame; }
    // VIBE_Animation_Advanced @0x5d89bc — type 1/5/8 active-state overlay.
    virtual void animationAdvanced(int x, int y, i32 style, i32 blob, u8 frame) {
        (void)x;(void)y;(void)style;(void)blob;(void)frame; }
    // VIBE_Velocity_Apply @0x5d883c — type 5/8 hover/shadow blit.
    virtual void velocityApply(int x, int y, i32 style, i32 blob, u8 frame) {
        (void)x;(void)y;(void)style;(void)blob;(void)frame; }
    // VIBE_State_Finalize @0x41e57c — push a font/state context.
    virtual void stateFinalize(i32 stateHandle) { (void)stateHandle; }
    // VIBE_Result_Broadcast @0x423980 — the right/bottom divider blit.
    virtual void resultBroadcast(int a, int b, int c, int d, i32 blob, int e, int f, i32 g) {
        (void)a;(void)b;(void)c;(void)d;(void)blob;(void)e;(void)f;(void)g; }
};

// Live clip-rect globals the pass references (the screen extent + the back-buffer
// clip used in the per-node reset Coord_Push). Modeled as a state struct so the
// pass is testable without the engine globals.
struct SceneFrameState {
    int screenW = 800;   // (dword_69FFB8>>16+2)  (the full-screen clip width)
    int screenH = 600;   // (dword_69FFBC>>16)    (the full-screen clip height)
    // dword_62D22C — index of the "focused" scene node (the one being dragged /
    // edited). When it equals a node's id the reinit takes the +40 path.
    i32 focusedNodeId = -1;
    // byte_62D25C — global "freeze countdowns" flag (type 17 path).
    u8  freezeCountdowns = 0;
    // dword_64A1A2 — the default reinit border extent (packed 16.16 hi/lo).
    i32 defaultBorder = 0;
};

// ===========================================================================
// gilde.exe 0x40e50c — VIBE_Decompressor_Init. Scans the 10240-byte /
// 20-byte-stride result-handler table (dword_62D2DC[0]) for entries bound to
// `rec` (entry+8 == rec) and dispatches the bound result handler then the gray
// fixup. The table walk is reconstructed 1:1; the two leaf calls are injected.
// Returns the number of dispatched handlers (0 in the headless/empty table).
// ===========================================================================
struct DecompHandlerEntry {          // dword_62D2DC[0] + 20*i  (5 dwords)
    i32 a0 = 0;     // [0] = *v3
    i32 a1 = 0;     // [1] = v3[1]
    i32 a2 = 0;     // [2] = v3[2]  (== &owner; see owner below)
    i32 a3 = 0;     // [3] = v3[3]
    i32 a4 = 0;     // [4] = v3[4]
    const DecompRecord* owner = nullptr; // *(entry+8) == rec  (the bound record)
};
struct DecompInitHooks {
    virtual ~DecompInitHooks() = default;
    // VIBE_Result_Handler_Interaction @0x42395c — the bound handler.
    virtual void resultHandlerInteraction(i32 a, i32 b, i32 c, i32 d,
                                          i32 e, i32 f, i32 g, i32 h) {
        (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h; }
    // VIBE_Light_SetGrayColorThunk @0x5c6af0 — the gray fixup.
    virtual void lightSetGrayColorThunk(int a, int b, i32 c) { (void)a;(void)b;(void)c; }
};
int DecompressorInit(const DecompRecord& rec,
                     const DecompHandlerEntry* table, int tableEntries,
                     DecompInitHooks& h);

// ===========================================================================
// gilde.exe 0x41ceb4 — VIBE_DecompressGameState. The per-frame scene-update
// walk. `rec` is dword_676A60[684*a1]; `entityTable` is dword_67EB80 (indexed by
// childIndex / entity-record fields); `nodes` is the 740-byte scene-node array
// (dword_69FFB4, indexed by EntityRecord.bodyNode / childNodes[]).
//
// Returns rec.blob105 (the surface blob) — matching the original returning the
// finalize result / record pointer.
// ===========================================================================
int SceneUpdatePass(const DecompRecord& rec,
                    const EntityRecord* entityTable, int entityTableCount,
                    RenderNode* nodes, int nodeCount,
                    SceneFrameState& st, SceneFrameHooks& h);

}  // namespace guild::sim
