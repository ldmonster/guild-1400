#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::sim — VIBE_Object_* reconstruction cluster "object_recon_extents".
//
// Two strict 1:1 translations of object/scene-record helpers of gilde.exe whose
// substance is pure record traversal + transform math (no UI / scene-graph
// mutation). The genuinely coupled leaves (matrix builders, the screen-space
// coordinate truncators) are routed through small hook structs with INERT
// defaults so the control flow / field arithmetic is faithfully reconstructed
// and golden-testable in isolation.
//
//   0x5e89b4  VIBE_Object_ApplyTransformConstraints
//             Resolves a position-delta + world-rotation-delta against an
//             object's local frame and parent bone matrix; produces the two
//             constrained output vectors. Returns bit0=position changed,
//             bit1=world changed. Callers (already reconstructed): the camera
//             track/orbit movers in src/render/camera_recon2.cpp use it through
//             Camera2Hooks::applyConstraints — this is the real body behind that
//             hook (matching ABI: obj, dpos, basis, dworld, outPos, outWorld).
//
//   0x5b6ebc  VIBE_Object_ComputeBoneScreenExtents
//             Walks the bone/face mesh of a posed object, projects flagged
//             vertices to screen space, and keeps the 4 extreme corner points
//             (min-sum / max-x-min-y / etc.) into a caller extent record.
//             Caller (in binary): VIBE_Pick_ComputeSelectionVolume (0x5b7134),
//             referenced as a comment in src/play/picksel_recon.h.
//
// Records are modeled exactly by original byte offset against a raw u8* base,
// matching the Hex-Rays pointer arithmetic; no struct reinterpretation is
// imposed so the layout cannot drift.
// =============================================================================

namespace guild::sim {

// The shared common/types.h carries only integer typedefs; the original's
// floats are 32-bit IEEE, aliased locally (same convention as camera_recon.h).
using f32 = float;

// ---------------------------------------------------------------------------
// Math/transform leaves used by ApplyTransformConstraints (0x5e89b4).
// All are pure; routed through hooks so this file stays self-contained and the
// arithmetic/branching is the unit under test. Inert defaults keep callers
// runnable headlessly (identity-ish behavior, documented per field).
// ---------------------------------------------------------------------------
struct TransformConstraintHooks {
    // 0x5c8fac VIBE_Transform_ComputeBoneWorldMatrix(node, localFrameOrNull, 1).
    //   Fills the 16-float world matrix at outMat for the given scene node.
    //   localFrame is the basis frame when obj+528 sign-bit clear, else null.
    //   Inert: identity matrix.
    void (*computeBoneWorldMatrix)(u32 node, const f32* localFrame, f32* outMat) = nullptr;
    // 0x5caa4c VIBE_Math_VectorWithinTolerance(a, b, tol) ->
    //   |a.x-b.x|<=tol && |a.y-b.y|<=tol && |a.z-b.z|<=tol.
    //   Implemented locally (pure); hook overrideable for testing.
    bool (*vectorWithinTolerance)(const f32* a, const f32* b, f32 tol) = nullptr;
    // 0x5ca504 VIBE_Math_VectorAngleWrapped(a, b) -> signed angle (radians),
    //   wrapped to [0,2pi) using the engine's VectorAngleBetween. Inert: 0.
    f32 (*vectorAngleWrapped)(const f32* a, const f32* b) = nullptr;
    // 0x5cb1bc VIBE_Math_MatrixFromEuler(euler3, outMat16). Inert: identity.
    void (*matrixFromEuler)(const f32* euler3, f32* outMat) = nullptr;
    // 0x5caaa4 VIBE_Math_MatrixTransformVectors(matA16, matB16, outMat16):
    //   3x3 multiply of the rotation parts. Inert: copy matB->out.
    void (*matrixMul)(const f32* a, const f32* b, f32* out) = nullptr;
    // 0x5cb2cc VIBE_Math_MatrixToEuler(mat16) — reads rotation, writes nothing
    //   observable to our outputs. Inert: no-op.
    void (*matrixToEuler)(f32* mat) = nullptr;
};

TransformConstraintHooks ApplyTransformConstraints_DefaultHooks();

// State globals the original reads (disjoint from the records).
struct TransformConstraintEnv {
    u32 d649EFC = 0;     // dword_649EFC — "selected/active node" handle
    u8  b64A024 = 0;     // byte_64A024  — alt/secondary-axis flag
};

// 0x5e89b4 — VIBE_Object_ApplyTransformConstraints.
//   objBase : u8* to the object record (fields read at +76,+80,+84 position;
//             +132,+136,+140 world translation; +504 bone-matrix node handle;
//             +528 axis-lock byte; +533 mode byte).
//   dpos    : float[3] position delta in local frame.
//   basis   : float[> +99*4] — the original passes a3 (v45); used as
//             v45[99..] (a 16-float matrix) when objBase+504 == 0, else as the
//             local frame for the bone-matrix builder.
//   dworld  : float[3] world-rotation delta (euler).
//   outPos  : float[3] resolved position (written when bit0 set).
//   outWorld: float[3] resolved world translation (written when bit1 set).
// returns: bit0 = position changed, bit1 = world changed.
u8 ApplyTransformConstraints(u8* objBase, const f32* dpos, f32* basis,
                             const f32* dworld, f32* outPos, f32* outWorld,
                             const TransformConstraintEnv& env,
                             const TransformConstraintHooks& h);

// ---------------------------------------------------------------------------
// ComputeBoneScreenExtents (0x5b6ebc).
// ---------------------------------------------------------------------------
// Screen-projection leaf. The original VIBE_Coord_ConvertX (0x5c6b08) is a
// __usercall that truncates an fp coordinate already on the x87 stack; here we
// model the two-call pattern (X then Y) as one explicit project(x,y) hook.
struct BoneExtentHooks {
    // Projects a posed vertex (worldX, worldY in the original's bone-space
    // floats) to integer screen X/Y. Mirrors the two back-to-back
    // VIBE_Coord_ConvertX calls. Inert: truncate toward zero (identity).
    void (*project)(f32 worldX, f32 worldY, i32* outX, i32* outY) = nullptr;
};

BoneExtentHooks ComputeBoneScreenExtents_DefaultHooks();

// Process-global screen extents the original uses for the two corner sums.
struct BoneExtentEnv {
    i32 screenW = 0;     // cy          (0x7626dc)
    i32 screenH = 0;     // dword_7626E0
};

// --- posed-mesh geometry, 64-bit-safe model of the original's inline layout ---
// The original is 32-bit and packs raw pointers inside each face record; on a
// 64-bit host those embedded-pointer offsets cannot coexist with the fixed
// scalar field offsets (+16,+20,+36,+38). We therefore model the geometry as
// typed records whose *logical* fields map 1:1 to the originals (offset noted),
// so the traversal arithmetic is behavior-identical and pointer-width-safe.
//
// BoneFace — original 40-byte face record (offsets are the original byte
// offsets the decompile reads):
//   verts[]  : the vertex-pointer array originally inline at face+0..+11
//              (3 dword pointers => kVertCount = 3).
//   uvBase   : face+16  — base of the per-vertex uv-pair (int[2]) table; the
//              loop indexes it by v11 (byte step 8) -> uvBase[2*vi + 0/1].
//   boneId   : face+20  — matched against ext[0].
//   flagSign : face+36  — processed only when (i8)flagSign < 0.
//   flag2    : face+38  — processed only when (flag2 & 2) == 0.
struct BoneVertex {
    // The decompile reads vert[4] (worldX) / vert[5] (worldY) as floats and
    // vert[0..2] as the three ints copied into the extent payload (a2[1..3] etc).
    f32 f[8];
};
struct BoneFace {
    const BoneVertex* verts[3];   // face+0 (inline pointer array)
    const i32*        uvBase;     // face+16
    i32               boneId;     // face+20
    i8                flagSign;   // face+36
    u8                flag2;      // face+38
};
// BonePose — original pose node (offsets noted):
//   facesBase  : pose+4   — base of the face array (count facesCount).
//   facesCount : pose+12  — number of faces.
//   idBlock    : pose+16  — record whose +480 dword is the pose-vertex-id count.
//   idBlockCount : *(idBlock+480).
//   idList     : pose+20  — list of pose vertex ids scanned for ext[0].
struct BonePose {
    const BoneFace* facesBase;    // pose+4
    i32             facesCount;   // pose+12
    const i32*      idBlock;      // pose+16 ; idBlock[480/4] = count
    const i32*      idList;       // pose+20
};

// 0x5b6ebc — VIBE_Object_ComputeBoneScreenExtents.
//   objBase : u8* to the posed object record. Field +460 is read as a native
//             pointer to a BonePose (pointer-width-safe; see ObjPtr in .cpp).
//   ext     : int* extent record (a2), indexed by dword exactly as the original.
//   Always returns 1.
u8 ComputeBoneScreenExtents(u8* objBase, i32* ext,
                            const BoneExtentEnv& env,
                            const BoneExtentHooks& h);

} // namespace guild::sim
