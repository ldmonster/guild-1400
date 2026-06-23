#pragma once
// ===========================================================================
// guild::gui — drag-select ORCHESTRATION + drag-cursor render/state, 1:1.
//
// This module reconstructs the higher-level drag-select / drag-cursor functions
// that the pure-geometry cluster (play/picksel_recon.{h,cpp}) intentionally left
// OMITTED (deeply coupled to the command queue, the entity arrays, the building
// selection-flag subsystem, and the shape/anim render path). Here those couplings
// are modeled as mockable hook structs so the control flow, constants, clamp math,
// fixed-point truncation and side-effect ORDER are faithful and testable in-process;
// the host wires the hooks to the live subsystems.
//
// Reconstructed (each carries its gilde.exe address):
//   0x4ba2bc  VIBE_DragSelect_UpdateUnitList      (command-queue driver)
//   0x4bdc3c  VIBE_DragSelect_ApplyToUnits        (box hit-test over 32 units)
//   0x4bdecc  VIBE_DragSelect_ApplyToSelection    (box hit-test, selection-flag)
//   0x4be154  VIBE_DragSelect_DrawBox             (4 rect edges via DrawLineLocked)
//   0x41fa1c  VIBE_DragCursor_Render              (cursor-mesh + count badges)
//   0x41fcbc  VIBE_DragCursor_SetSprite           (cursor shape-anim slot register)
//
// The PURE clamp/normalize/centroid-hit kernels are REUSED from play::picksel
// (DragClampX/Y, DragSelectNormalize, DragUnitCentroidHit, ProjectParams, DragBox,
// Viewport) — see play/picksel_recon.h. No duplication, no ODR clash.
//
// Provenance constants (get_bytes / get_global_value verified):
//   dword_62D0C4..D4   drag dead-band box  (init 0x316,0x257,0,0,1)
//   dbl_61E208/210     0.125 centroid weight (ApplyToUnits / ApplyToSelection)
//   word_140642D/2F    drag-cursor child screen-pos table, stride 17
//   dword_62D30C       cursor shape-anim slot (init -1 = none)
//   dword_62D314       "cursor hidden" flag  (init 0)
//   word_62D310        cursor mode (init 0)
// ===========================================================================
#include "guild/common/types.h"
#include "play/picksel_recon.h"   // DragBox / Viewport / ProjectParams + kernels

namespace guild::gui {

using guild::i16;
using guild::i32;
using guild::u8;
using guild::u16;

// ---------------------------------------------------------------------------
// 0x4be154 — VIBE_DragSelect_DrawBox.
//
// When the box is active it draws its 4 edges by calling VIBE_Render_DrawLineLocked
// four times. From the disasm the register wiring of each call is
// DrawLineLocked(eax=x1, ecx/edx=y1, ebx=x2, push=color=-1); the four calls trace
// the rectangle:
//   (ax, ay)->(bx, ay)   top      [eax=ax, ecx=edx=ay, ebx=bx]
//   (bx, by+1)->(bx, by) right    [eax=bx, ecx=by+1, ebx=bx]  (note +1 on y1)
//   (bx, by)->(ax, by)   bottom   [eax=bx, ecx=edx=by, ebx=ax]
//   (ax, ay)->(ax, ay)   left     [eax=ax, ecx=edx=ay, ebx=ax]
// Each segment is captured as {x1,y1,x2,y2,color}. The actual blit is a Vulkan/SDL
// boundary (rule 3): we route only the GPU draw through a hook and reproduce the
// geometry/state logic (the active gate + the exact four endpoint tuples) here.
// ---------------------------------------------------------------------------
struct DragBoxSegment {
    int x1, y1;   // eax, ecx  (DrawLineLocked a1, a2)
    int x2;       // ebx       (DrawLineLocked a3)
    i16 color;    // push      (DrawLineLocked a4 = -1)
};

// Emit the four rectangle segments for an active box, in original call order.
// Returns the number emitted (0 when the box is inactive, else 4). `out` must hold
// at least 4 segments. This is the pure geometry; pass each to the render hook.
int DragSelect_BoxSegments(const guild::play::DragBox& box, DragBoxSegment out[4]);

// 0x4be154 driver: gate on box.active, build the segments, route each through the
// line-draw hook (the real VIBE_Render_DrawLineLocked / Vulkan line blit).
struct DragDrawHooks {
    // VIBE_Render_DrawLineLocked @0x4353dc (a1=x1@eax, a2=y1@ecx, a3=x2@ebx,
    // a4=color). Returns the original's char result (ignored by DrawBox).
    char (*drawLineLocked)(int x1, int y1, int x2, i16 color) = nullptr;
};
void DragSelect_DrawBox(const guild::play::DragBox& box, const DragDrawHooks& h);

// ---------------------------------------------------------------------------
// 0x4bdc3c / 0x4bdecc — VIBE_DragSelect_ApplyToUnits / ApplyToSelection.
//
// Both iterate the 32-slot selectable-unit array (dword_11BB6A0[0..31]); for each
// live unit (slot != 0, *(slot+8) != 0) they:
//   1. compute a per-unit selection flag (ApplyToUnits: Building_ComputeSelectionFlags
//      @0x588dec; ApplyToSelection: Combat_GetSelectionFlag @0x486460), then clear
//      *(slot+392)=0;
//   2. if (flag & 0x800): walk to the unit's mesh (slot+388 -> +136==off_649D64 ->
//      +52 -> +460 -> [0]), sum the 8 mesh-vertex (x,y,z) at stride 20 over 160 floats,
//      project the centroid (*0.125) to screen, and if it falls inside the normalized
//      drag rect set *(slot+392)=1 (the "in-box selected" flag).
// The box corner is first updated from the live cursor (>>16, clamped to the scissor
// rect dword_13ECE58/5C/60/64), exactly the head of each function.
//
// The PURE pieces (clamp, normalize, centroid project+contain) are play::picksel
// kernels. Here we drive them over a hooked unit array so the iteration order, the
// flag-gate, the +392 writes and the rect math are 1:1.
// ---------------------------------------------------------------------------
inline constexpr int kDragUnitSlots = 32;   // dword_11BB6A0 length

// One selectable unit, exposing only the fields the apply loop reads.
struct DragUnit {
    int   handle  = 0;     // dword_11BB6A0[i]   (slot != 0 == live)
    u8    active  = 0;     // *(slot+8)          (must be nonzero)
    u8    inBox   = 0;     // *(slot+392)        (output: 1 == inside box)
    // Pre-collected 8-corner mesh sums (the inner do/while accumulates these from
    // *(*(*(slot+388)+52)+460)[0], 8 verts, stride 20). Supplying the sums keeps the
    // mesh-walk a host concern while the centroid math stays faithful.
    bool  hasMesh = false; // (slot+388 && +136==off_649D64 && mesh ptr != 0)
    float sumX = 0.f, sumY = 0.f, sumZ = 0.f;
    // The original gates the mesh path on (flag & 0x800). The host computes the flag
    // (Building_ComputeSelectionFlags / Combat_GetSelectionFlag) and supplies it.
    i16   selFlag = 0;
};

inline constexpr int kDragSelectFlagBit = 0x800;   // (flag & 0x800) gate

// Run the box hit-test over a unit array. `cursorX16/cursorY16` are the packed
// fixed-point cursor globals (unk_67220E / dword_672210, used >>16). `vp` is the
// scissor rect (dword_13ECE58/5C/60/64). `pp` is the per-frame projection state.
// `weight` is 0.125 (dbl_61E208 for Units, dbl_61E210 for Selection). Mutates each
// unit's `inBox`, and writes the updated dragged corner back into `box`.
// (units variant uses Building flags; selection variant uses Combat flags — the
// difference is which value the host placed in unit.selFlag, so one routine serves
// both, matching the byte-identical apply loops.)
void DragSelect_Apply(guild::play::DragBox& box, DragUnit* units, int count,
                      int cursorX16, int cursorY16,
                      const guild::play::Viewport& vp,
                      const guild::play::ProjectParams& pp, float weight);

// Thin named wrappers matching the two originals (same body, distinct weight).
void DragSelect_ApplyToUnits(guild::play::DragBox& box, DragUnit* units, int count,
                             int cursorX16, int cursorY16,
                             const guild::play::Viewport& vp,
                             const guild::play::ProjectParams& pp);
void DragSelect_ApplyToSelection(guild::play::DragBox& box, DragUnit* units, int count,
                                 int cursorX16, int cursorY16,
                                 const guild::play::Viewport& vp,
                                 const guild::play::ProjectParams& pp);

// ---------------------------------------------------------------------------
// 0x4ba2bc — VIBE_DragSelect_UpdateUnitList.
//
// The "rubber-band -> follow command" driver. It scans the per-cell command staging
// arrays (byte_12CEA98 pending-flag, stride 536; 411648/536 == 768 cells), and for
// each pending cell with both an owner (dword_12CEA7C) and a target object
// (dword_12CEA8C) whose kind byte == 67 ('C', a character) issues "follow / form-up"
// commands through the command queue:
//   - QueueRequestSingle49        @0x494e4c
//   - QueueRequestNamedObject53   @0x494f0c
//   - QueueRequestEntity29        @0x4949c4 (tail, once, for the last target)
//   - QueueRequestSlotReset28     @0x4948c8 (tail, once, when a batch was opened)
// plus Character_ChangePlayerAction @0x4b09c8 when the owner is the player
// (dword_11BC2F4). It also collapses the target's 4-slot follow list (+196..+208):
// the first slot equal to the issued target id is set to -1.
//
// The loop is a small state machine over the local accumulator (v1 count, v2 last
// target ptr, v22/v32 batch-open flags, v30 "more than 4 -> stop"). All of the heavy
// lifting (find/queue/character) is the command/AI subsystem — a coupled cluster.
// We reconstruct the state machine 1:1 and route the coupled calls through a hook
// struct, so the scan order, the 67-kind gate, the >4 early-out and the tail flushes
// are faithful and testable; the host binds the hook to the live command queue.
// ---------------------------------------------------------------------------
inline constexpr int  kUpdateCellStride = 536;     // v3 += 536
inline constexpr int  kUpdateScanLimit  = 411648;  // (int)v3 < 411648 (768 cells)
inline constexpr u8   kCharKind         = 67;      // 'C'  (*(target+0) == 67)
inline constexpr int  kUpdateMaxBatch   = 4;       // v1 > 3 -> v30 = 1 (stop)

// Coupled-leaf hooks (command queue / character / object query). All optional; a
// null hook is a no-op (the state machine still steps identically). The order of
// invocation matches the decompile exactly.
struct UpdateUnitListHooks {
    // VIBE_GameObject_QueryFind @0x5857fc — returns an aux object ptr (modeled int).
    i32  (*queryFind)(i32 a1, int a2, int a3, int a4) = nullptr;
    // VIBE_Character_ChangePlayerAction @0x4b09c8 (player only path).
    void (*changePlayerAction)(i32 player, int a2, int a3, u16 actionWord) = nullptr;
    // VIBE_Command_QueueRequestSingle49 @0x494e4c.
    void (*queueSingle49)(i32 targetId) = nullptr;
    // VIBE_Command_QueueRequestNamedObject53 @0x494f0c.
    void (*queueNamed53)(i32 targetId, i32 a2, int a3, i32 a4, int a5, int a6) = nullptr;
    // VIBE_Command_QueueRequestEntity29 @0x4949c4 (tail, once).
    void (*queueEntity29)(i32 a1, i32 entity) = nullptr;
    // VIBE_Command_QueueRequestSlotReset28 @0x4948c8 (tail, once).
    void (*queueSlotReset28)(int a1) = nullptr;
};

// One staged command cell (the per-stride-536 record the scan reads).
//   pending : byte_12CEA98[v3]     (nonzero == pending; cleared to 0 by the scan)
//   owner   : dword_12CEA7C[cell]  (owner record ptr; 0 == skip)
//   target  : dword_12CEA8C[cell]  (target object ptr; 0 == skip)
//   kind    : *(target+0)          (must be 67 to act)
//   targetId: dword_12CE914[cell]  (the object id queued)
//   action  : word_12CE910[cell]   (player action word)
//   followSlots[4] : *(target+196..208)  (collapsed: first == targetId -> -1)
struct UpdateCell {
    u8  pending = 0;
    i32 owner   = 0;
    i32 target  = 0;
    u8  kind    = 0;
    i32 targetId = 0;
    u16 action  = 0;
    i32 followSlots[4] = {0, 0, 0, 0};
    i32 ownerLink = 0;   // *(target+196) base is the followSlots; +1 link read at queue
    i32 targetLink = 0;  // *(target+1)   passed to queueNamed53 (modeled)
    i32 ownerObjLink = 0; // *(owner+1)   passed to queueNamed53
};

// The player record id the original compares against (dword_11BC2F4). Defaults to a
// sentinel that never matches; the host sets it for the player-path branch.
struct UpdateUnitListState {
    i32 playerRecord = 0;   // dword_11BC2F4
    i32 hoverCellBase = 0;  // dword_12CE914[134 * word_63CC5C] (batch v23 seed)
};

// 0x4ba2bc driver. Scans `cells[0..count)` (count == kUpdateScanLimit/kUpdateCellStride
// == 768 in the original), runs the follow-command state machine, and clears each
// processed cell's `pending`. Returns the number of targets queued (the original's v1
// accumulator at exit), for test pinning.
int DragSelect_UpdateUnitList(UpdateCell* cells, int count,
                              const UpdateUnitListState& st,
                              const UpdateUnitListHooks& h);

// ---------------------------------------------------------------------------
// 0x41fcbc — VIBE_DragCursor_SetSprite.
// 0x41fa1c — VIBE_DragCursor_Render.
//
// The cursor carries a shape-anim slot (dword_62D30C, init -1), a mode (word_62D310)
// and a "hidden" flag (dword_62D314). SetSprite(a2):
//   a2 != 0: free any existing slot (ShapeAnim_GetSlot), then register a new slot
//            from the current cursor coords (word_75BF4A, SHIWORD(dword_75BF46)) via
//            ShapeAnim_RegisterSlot(state := State_Update(a2)).
//   a2 == 0: free the slot (if any) and set mode = 0.
// Render walks the per-cursor child table (dword_75B9F0[0..5] stride 3, count badges)
// and blits the cursor mesh + count labels; the actual draws (Shape/Animation/Coord)
// are the Vulkan/SDL boundary (rule 3) and route through the render hook. We
// reconstruct the slot lifecycle + the child-table geometry/state 1:1.
// ---------------------------------------------------------------------------
struct DragCursorSprite {
    i32 slot   = -1;   // dword_62D30C  (-1 == no slot)
    i16 mode   = 0;    // word_62D310
    int hidden = 0;    // dword_62D314
};

// Coupled shape-anim leaves (slot register/free + state lookup).
struct DragCursorSpriteHooks {
    // VIBE_ShapeAnim_GetSlot @0x5d8f1c — frees/returns the prior slot (we ignore the
    // return; the original assigns v3 then immediately overwrites, so it is a free).
    void (*freeSlot)(i32 slot) = nullptr;
    // VIBE_State_Update @0x40e9e8 — map sprite id -> state handle.
    i32  (*stateUpdate)(i32 spriteId) = nullptr;
    // VIBE_ShapeAnim_RegisterSlot @0x5d8d54 — register a new slot at (x,y).
    i32  (*registerSlot)(u16 x, i16 y, int a3, i32 state) = nullptr;
};

// Current cursor screen coords for slot registration (word_75BF4A == x,
// SHIWORD(dword_75BF46) == y). Modeled explicitly to keep the global-read 1:1.
struct DragCursorCoords { u16 x = 0; i16 y = 0; };

// 0x41fcbc — set/clear the cursor sprite slot. Reproduces both branches and the
// mode reset, in order. `c` are the current cursor coords used on register.
void DragCursor_SetSprite(DragCursorSprite& s, i32 spriteId,
                          const DragCursorCoords& c, const DragCursorSpriteHooks& h);

// ---------------------------------------------------------------------------
// 0x41fa1c — VIBE_DragCursor_Render: the count-badge child layout geometry.
//
// The original draws the cursor mesh then walks dword_75B9F0[0..17] in stride-3
// (six possible child slots); for each slot != -1 it shows a small shape and a count
// label. The badge placement is the pure, testable geometry:
//   for v11 in {0,3,6,9,12,15}:
//     if (dword_75B9F0[v11] != -1):
//       badgeY = (dword_69FFB0 + 26) * (v10 / 3) + cursorY
//       badgeX = 26 * (v10 % 3) + cursorX + (*(int*)(84*dword_62D2C4 + dword_62D204 + 78) >> 16)
//       (then Shape_ShowFromBankScaled + a "%i" count label at badgeX-8, badgeY+24)
//       ++v10
// dword_69FFB0 is a row-spacing addend (0 in the static image), and the +78 term is
// a per-cursor x bias read from a 84-stride record (0 here). The shape/label blits
// are the Vulkan/SDL boundary (rule 3); we reconstruct the slot walk + badge grid.
// ---------------------------------------------------------------------------
inline constexpr int kDragCursorChildSlots = 6;   // 18/3 stride-3 entries
inline constexpr int kDragCursorBadgeStep  = 26;  // 26 px grid step

struct DragCursorBadge {
    int x, y;     // badgeX, badgeY (top-left of the count shape)
    i32 shapeId;  // dword_75B9F0[v11]
    i32 count;    // dword_75B9F4[v11] (the "%i" value)
};

// Per-cursor render inputs (the globals the layout reads).
struct DragCursorRenderState {
    int hidden    = 0;   // dword_62D314 (nonzero == render nothing)
    int cursorX   = 0;   // v26 (= a2, the cursor screen x)
    int cursorY   = 0;   // v27 (= a4, the cursor screen y)
    int rowAddend = 0;   // dword_69FFB0
    int xBias     = 0;   // *(int*)(84*dword_62D2C4 + dword_62D204 + 78) >> 16
    // The 6 child slots (stride-3 in the original; we expose them flat).
    i32 childShape[6] = { -1, -1, -1, -1, -1, -1 };  // dword_75B9F0[v11]
    i32 childCount[6] = {  0,  0,  0,  0,  0,  0 };   // dword_75B9F4[v11]
};

// Compute the count-badge layout for the active (non -1) child slots, in walk order.
// Returns the number of badges (0 when hidden). `out` must hold kDragCursorChildSlots.
int DragCursor_BadgeLayout(const DragCursorRenderState& rs, DragCursorBadge* out);

} // namespace guild::gui
