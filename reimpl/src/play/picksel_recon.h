#pragma once
// ===========================================================================
// gilde.exe — Mouse picking / object selection / drag-select box / interaction
// routing  (CLUSTER: VIBE_Pick / VIBE_Selection / VIBE_Interaction /
// VIBE_DragSelect / VIBE_DragCursor).
//
// STRICT 1:1 reconstruction of the PURE pick/select/hit-test/projection math
// from the Hex-Rays decompile. Entity arrays, the scene-graph walker, UI panel
// records and the command/AI subsystems are COUPLED leaves: they are exposed
// here as inert-default hooks so the pure geometry is faithful and testable in
// process, while the live wiring is supplied by the caller.
//
// Provenance (each reconstructed entry carries its gilde.exe address):
//   0x5b5938  VIBE_Pick_TestObjectAtPoint        (__usercall)
//   0x5b5aec  VIBE_Pick_CollectObjectsAt         (__stdcall)
//   0x5b5b6c  VIBE_Pick_FindNearestObjectScreen  (__userpurge)
//   0x5b7134  VIBE_Pick_ComputeSelectionVolume   (__stdcall)
//   0x4b9444  VIBE_Selection_Reset               (__usercall)   [hooked iters]
//   0x4bdb98  VIBE_DragSelect_BeginBox
//   0x4bdc30  VIBE_DragSelect_Cancel
//   0x4be154  VIBE_DragSelect_DrawBox
//   0x4bdc3c  VIBE_DragSelect_ApplyToUnits       [hooked entity array]
//   0x4bdecc  VIBE_DragSelect_ApplyToSelection   [hooked entity array]
//   0x41f860  VIBE_DragCursor_Reset
//   0x41f878  VIBE_DragCursor_SetMode
//   0x4c094c  VIBE_DragCursor_RenderForState
//   0x595e54  VIBE_Interaction_IsPanelModeTwo
//   0x595f70  VIBE_Interaction_IsPanelActive
//   0x595e74  VIBE_Interaction_InvokeHandlerSlot60
//
// OMITTED (rule 8 — no faithful pure kernel; deeply coupled to AI/command/gesetz
// subsystems that are not in this cluster):
//   0x4ba2bc  VIBE_DragSelect_UpdateUnitList     (command-queue driver)
//   0x46dcf8  VIBE_Interaction_EvalPickTarget    (AI-method dispatch)
//   0x46e33c  VIBE_Interaction_EvalAttackTarget  (AI-method dispatch)
// ===========================================================================
#include "guild/common/types.h"

namespace guild::play {

// ---------------------------------------------------------------------------
// Recovered rodata constants (decoded with get_bytes).
//   flt_62852C  = 0.5f              viewport-half scale (FindNearest alt branch)
//   dbl_628708  = 1e-07             ray/plane parallel epsilon
//   flt_628710  = 1.0f/3.0f (0.33333334f)  barycentric averaging weight
//   flt_6282F4  = 0.125f            1/8 corner-sum (bounding radius; reused)
//   dbl_61E208  = 0.125f            ApplyToUnits centroid 1/8 weight
//   dbl_61E210  = 0.125f            ApplyToSelection centroid 1/8 weight
//   loc_5B3E28  = 1e10f             ComputeSelectionVolume initial min seed
//   dword_13FD460 init = 1343554297 (== 1e10f bit pattern) bucket "best dist" seed
// The projection bias/scale globals flt_13FCD0C / flt_13FCAF8 / flt_13FCD18 /
// flt_13FCD10 are runtime per-frame projection state (screen center x/y + scale);
// zero in the static image. They are modeled as ProjectParams below.
// ---------------------------------------------------------------------------
namespace picksel_const {
    constexpr float  kHalf        = 0.5f;
    constexpr double kParallelEps = 1e-07;
    constexpr float  kThird       = 0.3333333432674408f;  // flt_628710 exact bits
    constexpr float  kEighth      = 0.125f;
    constexpr float  kBigDist     = 1.0e10f;              // loc_5B3E28 / bucket seed
    // 1343554297 reinterpreted as float == 1e10f (the bucket distance seed the
    // original stores as an int constant). Kept as the literal int for fidelity.
    constexpr i32    kBucketDistSeedBits = 1343554297;
}

// Per-frame projection state the original reads from flt_13FCD18 (screen cx),
// flt_13FCD10 (screen cy), flt_13FCD0C (x scale) and flt_13FCAF8 (y scale).
// world->screen used by the pick/drag math:
//   sx = scaleX * (dxWorld) * invDepth + centerX
//   sy = scaleY * (dyWorld) * invDepth + centerY
struct ProjectParams {
    float centerX = 0.0f;  // flt_13FCD18
    float centerY = 0.0f;  // flt_13FCD10
    float scaleX  = 0.0f;  // flt_13FCD0C
    float scaleY  = 0.0f;  // flt_13FCAF8
};

// ===========================================================================
// 0x5b5b6c / 0x5b5aec — bucket-array nearest-pick selection.
//
// The original keeps a 9-slot bucket array; each slot is { object*, bestDist }.
// Slots are seeded with object=0, bestDist=1e10f (the int 1343554297). A scene
// walk fills the slots (the per-object visitor below), then the nearest live
// slot wins. This is the PURE selection kernel, reconstructed 1:1.
// ===========================================================================
struct PickBucket {
    i32   object = 0;          // dword_13FD45C-style slot.0  (object id / pointer)
    float bestDist = 0.0f;     // slot.1, seeded to 1e10f
};
inline constexpr int kPickBucketCount = 9;  // 18 dwords / 2 == 9 slots

// Seed all buckets exactly as the original loop:
//   for (i=0; i!=18; i+=2) { slot[i].dist = seed; slot[i].obj = 0; }
void PickBucketsSeed(PickBucket buckets[kPickBucketCount]);

// 0x5b5b6c tail — return the object of the nearest live bucket (dist < 1e10
// running min), 0 if none. Faithful: strict-less compare against running min
// pre-seeded to 1e10f.
i32 PickBucketsNearest(const PickBucket buckets[kPickBucketCount]);

// ---------------------------------------------------------------------------
// 0x5b5938 — VIBE_Pick_TestObjectAtPoint inner screen-distance test.
//
// Given an object's bone-local point (bx,by,bz from ObjectComputeBoundingRadius)
// and its bounding radius `radius`, plus the cursor screen-space deltas
// (cursorDX = centerX - mouseX stored at +80, cursorDY at +84), the original
// computes (with v5 = 1/bz):
//   r      = scaleX * radius * (1/bz)                 // projected radius
//   px     = scaleX * by * (1/bz)                     // <-- note: by, the +0 comp
//   qx     = (scaleX*bx)*(1/bz) ... see .cpp for the EXACT field wiring
// and tests  screenDistSq < slot.bestDist && sqrt(screenDistSq) < r .
// This helper reproduces that hit-test given the already-projected terms.
//
// hitDistSq : (projPointX + cursorDX)^2 + (projPointY + cursorDY)^2
// projRadius: scaleX * radius / bz
// Returns true and writes outDistSq when this is a closer hit than curBestDist.
// ---------------------------------------------------------------------------
bool PickScreenDistTest(float hitDistSq, float projRadius,
                        float curBestDist, float* outDistSq);

// ===========================================================================
// 0x5b7134 — VIBE_Pick_ComputeSelectionVolume.
//
// Ray-vs-(two-triangle quad) barycentric solve over a set of 4 projected bone
// extent points. The scene walk (VIBE_Object_ComputeBoneScreenExtents) fills an
// array of 4 screen-space corner points {x,y,z(depth)} plus a per-corner (u,v).
// This is the PURE geometry: pick the spanning corners, intersect the cursor ray
// against the quad, and return the interpolated (u,v). Reconstructed 1:1.
//
// Inputs:
//   cornerPos[4][3] : the 4 corner world/projection points (v60[1..3] groups)
//   cornerUV[4][2]  : the |u|,|v| per corner (v60[17],[18] groups)
//   mouseX,mouseY   : cursor screen pos
//   pp              : projection state
// Output: outU, outV (the original's *a5,*a6). Returns 1 on hit, 0 on miss.
// ===========================================================================
char ComputeSelectionVolumeSolve(const float cornerPos[4][3],
                                 const float cornerUV[4][2],
                                 float mouseX, float mouseY,
                                 const ProjectParams& pp,
                                 float* outU, float* outV);

// ===========================================================================
// Drag-select box  (0x4bdb98 / 0x4bdc30 / 0x4be154 + clamp/point-in-box math).
//
// The box lives in a 5-int record { active, minX, minY, maxX, maxY }.
//   dword_11BC24C active, dword_11BC250 ax, 254 ay, 258 bx, 25C by.
// BeginBox seeds both corners to the clamped cursor. The clamp is to the scissor
// rect [vpX0,vpX1-1] x [vpY0,vpY1-1] (dword_13ECE58/5C/60/64).
// ===========================================================================
struct DragBox {
    int active = 0;   // dword_11BC24C
    int ax = 0;       // dword_11BC250
    int ay = 0;       // dword_11BC254
    int bx = 0;       // dword_11BC258
    int by = 0;       // dword_11BC25C
};

// Viewport / scissor rect the clamp uses (dword_13ECE58/5C/60/64).
struct Viewport {
    int x0 = 0;  // dword_13ECE58
    int y0 = 0;  // dword_13ECE5C
    int x1 = 0;  // dword_13ECE60
    int y1 = 0;  // dword_13ECE64
};

// clamp(v, lo, hi-1) using the EXACT decompiled idiom (lo first, then hi-1).
int DragClampX(int v, const Viewport& vp);
int DragClampY(int v, const Viewport& vp);

// 0x4bdb98 — seed both corners to the clamped cursor (>>16 fixed-point cursor).
// cursorX16 = unk_67220E (>>16 to pixels), cursorY16 = dword_672210 (>>16).
void DragSelectBeginBox(DragBox& box, int cursorX16, int cursorY16,
                        const Viewport& vp);

// 0x4bdc30
void DragSelectCancel(DragBox& box);

// Recompute the dragged corner (bx,by) from the live cursor, exactly as the
// head of ApplyToUnits/ApplyToSelection do before iterating.
void DragSelectUpdateDragCorner(DragBox& box, int cursorX16, int cursorY16,
                                const Viewport& vp);

// The min/max normalisation the apply functions do (v4/v23/v24/v22).
struct DragRect { int minX, minY, maxX, maxY; };
DragRect DragSelectNormalize(const DragBox& box);

// Point-in-box hit-test, exactly the apply-loop predicate:
//   minX <= sx && maxX >= sx && minY <= sy && maxY >= sy
bool DragBoxContains(const DragRect& r, float sx, float sy);

// Project a unit centroid (sum of 8 mesh corners, *0.125) to screen, then test
// containment — the PURE per-unit kernel of ApplyToUnits/ApplyToSelection.
//   sumX/sumY/sumZ : the 8-corner sums (mesh +0/+1/+... over 8 verts, stride 20)
//   weight         : 0.125 (dbl_61E208 / dbl_61E210)
// screen:
//   sx = scaleX * (sumX*weight) * (1/(weight*sumZ)) + centerX
//   sy = (1/(weight*sumZ)) * (scaleY * (sumY*weight)) + centerY
bool DragUnitCentroidHit(float sumX, float sumY, float sumZ, float weight,
                         const ProjectParams& pp, const DragRect& r);

// ===========================================================================
// Drag cursor mode  (0x41f860 / 0x41f878 / 0x4c094c).
// ===========================================================================
struct DragCursorState {
    i32 lastButton = -1;   // dword_75BF38
    u8  flag       = 0;    // byte_67225C
    i16 mode       = 0;    // word_62D310
};

// 0x41f860 — reset. (VIBE_Input_ResetMouseButtonState is a coupled leaf hook.)
void DragCursorReset(DragCursorState& st);
// 0x41f878
i16 DragCursorSetMode(DragCursorState& st, i16 mode);
// 0x4c094c — map mode -> (dx,dy) offsets, then render. Returns the (dx,dy) the
// original passes to VIBE_DragCursor_RenderMouse; rendering itself is a hook.
struct CursorOffset { int dx; int dy; };
CursorOffset DragCursorRenderForState(const DragCursorState& st);

// ===========================================================================
// Interaction panel predicates  (0x595e54 / 0x595f70 / 0x595e74).
//
// off_5953F0 is the panel record; the original reads:
//   *((u32*)off_5953F0 + 14) -> panel "mode" int at +56
//   *(u8*)off_5953F0          -> panel "kind" byte at +0
//   *((u32*)off_5953F0 + 5)  -> handler-table pointer at +20 (+60 = slot)
// dword_649CD0 is the "panel system enabled" flag.
// ===========================================================================
struct PanelState {
    int enabled  = 0;   // dword_649CD0
    int mode     = 0;   // *(off_5953F0 + 56)
    u8  kind     = 0;   // *(off_5953F0 + 0)
    int blocked  = 0;   // dword_62EB4C
    bool hasHandler = false;   // off_5953F0+20 != 0 && (+20)+60 != 0
};

// 0x595e54
bool InteractionIsPanelModeTwo(const PanelState& ps);
// 0x595f70
bool InteractionIsPanelActive(const PanelState& ps);
// 0x595e74 — returns 1 (no panel / no handler), 0 (blocked), or the handler
// result. The handler itself is supplied via the hook (coupled leaf).
int InteractionInvokeHandlerSlot60(const PanelState& ps,
                                   int (*handler)(char,int,int,int),
                                   char a1, int a2, int a3, int a4);

} // namespace guild::play
