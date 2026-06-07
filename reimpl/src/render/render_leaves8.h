#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — render-math leaves, batch 8 (gilde.exe gfx/light/mesh/color).
//
// An eighth slice of self-contained, deterministic VIBE_Color_*/Mesh_*/Light_*/
// Render_* leaves translated 1:1 from the Hex-Rays reference (cross-checked
// against the raw disassembly where the decompiler dropped a register-passed
// output pointer). Each function is pure arithmetic / pointer-graph bookkeeping
// over caller-supplied records — no DDraw/GDI/device calls — so it is golden-
// testable in isolation.
//
// Cross-module callees that are NOT yet reconstructed in this repo are routed
// through an installable RenderLeaves8Hooks struct with inert default
// implementations defined in render_leaves8.cpp (tests install their own).
// Reconstructed callees are reused directly where available:
//   guild::util::MatrixToEuler  (0x5cb2cc VIBE_Math_MatrixToEuler) — wired by the
//                                AttachAtFrameMatrix integration test as the real
//                                sibling for the matrix->euler tail.
//
// Translated functions (all addresses verified UNTRANSLATED at time of writing,
// case-insensitively, by both address AND bare name across all of src/render):
//   0x4226bc  VIBE_Color_NotEqualRgb          (3-byte RGB inequality test)
//   0x4226e0  VIBE_Color_SetRgb               (write r,b,g into a 3-byte record)
//   0x433694  VIBE_Render_SetWindowRect       (store the 2D present clip rect)
//   0x5dd9c8  VIBE_Render_GetVertexBufferInfo (report VB handle + 2048 capacity)
//   0x5d3668  VIBE_Mesh_ReleaseStockObject    (guarded stock-object teardown)
//   0x43ea34  VIBE_Light_RefreshAllToggle     (deref-and-forward refresh flag)
//   0x5c8538  VIBE_Light_RequestObjectCache   (guarded scene-walk cache build)
//   0x5c8a78  VIBE_Light_AttachAtFrameMatrix  (bone world matrix -> euler)
//   0x5c7cf8  VIBE_Light_RecomputeForObject   (radius^2 recompute + re-illuminate)
//   0x5c7e58  VIBE_Light_PrepareObjectCache   (LOD-frame resolve + radius^2 cache)
//   0x4284f4  VIBE_Mesh_ComputeWorldAabb      (8-corner world AABB of one object)
//   0x5b2a58  VIBE_Mesh_ComputeObjectAabb     (sentinel-seeded scene AABB)
//
// Recovered constants (decoded from gilde.exe raw bytes via /tmp/ida.py bytes):
//   kAabbSentinelMax (+1e10f, 0x501502F9) / kAabbSentinelMin (-1e10f, 0xD01502F9)
//     — the min/max seeds ComputeObjectAabb writes before the scene fold.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Recovered AABB sentinel seeds (bit-exact float patterns from the binary).
// ComputeObjectAabb initialises min = +1e10, max = -1e10 before the walk.
// ---------------------------------------------------------------------------
constexpr float kAabbSentinelMax =  1.0e10f;   // 0x501502F9  (init for min[])
constexpr float kAabbSentinelMin = -1.0e10f;   // 0xD01502F9  (init for max[])

// ---------------------------------------------------------------------------
// 2D integer present/clip rectangle written by SetWindowRect. The original
// stores four present-state globals dword_75FB40..4C; we model them as an
// out-struct so this slice owns no engine globals (render_leaves6 already
// models the same dwords as its own ClipBounds for the line-clip path).
//   dword_75FB40  xMax   <- a1 (eax)
//   dword_75FB44  yMax   <- a2 (edx)
//   dword_75FB48  xMin   <- a4 (ebx)
//   dword_75FB4C  yMin   <- a3 (ecx)
// ---------------------------------------------------------------------------
struct WindowRect {
    i32 xMax = 0;   // dword_75FB40
    i32 yMax = 0;   // dword_75FB44
    i32 xMin = 0;   // dword_75FB48
    i32 yMin = 0;   // dword_75FB4C
};

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults in render_leaves8.cpp). The defaults are
// chosen so the deterministic math paths can be exercised in isolation.
// ---------------------------------------------------------------------------
struct RenderLeaves8Hooks {
    // dword_13FCD1C — the active frame/camera object. Several leaves read its
    // world pose ([+76..84] position, [+132..140] world translation) and pass it
    // to bone/space transforms. Default null: leaves that gate on it become inert.
    void* activeFrameObject = nullptr;

    // 0x5d1968 VIBE_Mesh_DeleteStockObject(obj) — free a refcounted stock mesh.
    // ReleaseStockObject tail-calls it when the refcount [obj+476] > 0. Default
    // inert: returns obj unchanged (no free).
    void* (*deleteStockObject)(void* obj) = nullptr;

    // 0x5c886c VIBE_Light_RefreshAllObjects(flag) — re-light every cached object.
    // RefreshAllToggle forwards the dereferenced flag byte. Default inert: returns
    // the flag (the original returns its al residue).
    u8 (*refreshAllObjects)(u8 flag) = nullptr;

    // 0x5ac86c VIBE_SceneGraph_TraverseTree(root, node, cb, mask) — pre-order walk
    // invoking cb per node. RequestObjectCache drives the per-object cache build
    // through it. Default inert: returns 0 (empty walk).
    u8 (*traverseTree)(void* root, void* node, void (*cb)(void*), i32 mask) = nullptr;

    // 0x5c8218 VIBE_Light_BuildObjectCache(obj) — the per-node cache callback fed
    // to traverseTree by RequestObjectCache. Default inert: no-op.
    void (*buildObjectCache)(void* obj) = nullptr;

    // 0x5c8c40 VIBE_Transform_ComputeBoneWorldMatrix(obj, ref, flag) — compose the
    // bone-chain world matrix into the engine's scratch matrix. AttachAtFrameMatrix
    // calls it before the euler extraction. Default inert: writes identity into
    // `outMatrix` (16 floats) so the matrix->euler tail is observable.
    void (*computeBoneWorldMatrix)(void* obj, void* ref, int flag, float* outMatrix) = nullptr;

    // VIBE_Math_MatrixToEuler(m) — extract euler angles into m[0..2] IN PLACE.
    // AttachAtFrameMatrix's tail. The integration test installs the REAL
    // guild::util::MatrixToEuler here. Default inert: leaves the matrix unchanged.
    void (*matrixToEuler)(float* m) = nullptr;

    // 0x5c7da4 VIBE_Light_RemoveCacheEntry(light, obj, a3, obj) — drop the light's
    // existing cache rows for obj. RecomputeForObject calls it first. Default inert.
    void (*removeCacheEntry)(void* light, void* obj, void* a3) = nullptr;

    // 0x5c8c40 VIBE_Transform_PointToBoneLocalSpace(obj, ref[3], dstA[3], dstB[3]) —
    // transform a world point into the object's bone-local frame. Used by
    // RecomputeForObject / PrepareObjectCache. Default inert: copies ref -> dstA
    // and ref -> dstB (identity), so subsequent radius math is observable.
    void (*pointToBoneLocalSpace)(void* obj, const float* ref, float* dstA, float* dstB) = nullptr;

    // 0x5c7804 VIBE_Light_IlluminateObject(obj, &lightPtr) — apply this light to
    // obj's cached vertices. RecomputeForObject calls it after the radius update.
    // Default inert: returns 0.
    u8 (*illuminateObject)(void* obj, void** lightPtr) = nullptr;

    // 0x5adb6c VIBE_Mesh_SelectLodFrame(obj) — pick/resolve obj's LOD mesh frame
    // and return its address. PrepareObjectCache calls it when [obj+460] is null.
    // Default inert: returns null (=> PrepareObjectCache bails, no math).
    void* (*selectLodFrame)(void* obj) = nullptr;

    // 0x5b2c70 VIBE_Object_AssignMeshData(obj) — force the mesh frame resident.
    // PrepareObjectCache / ComputeWorldAabb call it. Default inert: no-op.
    void (*assignMeshData)(void* obj) = nullptr;

    // 0x5af2c0 VIBE_Object_PropagateDirtyFlag(obj, flags) — push transform dirty.
    // ComputeWorldAabb calls it. Default inert: no-op.
    void (*propagateDirtyFlag)(void* obj, u32 flags) = nullptr;

    // 0x5af38c VIBE_Object_SetPosition(obj, vec3) / 0x5af50c
    // VIBE_Object_SetWorldTranslation(obj, vec3) — pose mutators ComputeWorldAabb
    // saves/restores around the fold. Default inert: no-op.
    void (*setPosition)(void* obj, const float* vec3) = nullptr;
    void (*setWorldTranslation)(void* obj, const float* vec3) = nullptr;

    // 0x4283ac VIBE_Mesh_AccumulateAabbRecursive(box, child) — child-subtree AABB
    // merge (already reconstructed in render_leaves4 as AccumulateAabbRecursive).
    // ComputeWorldAabb folds its child list through it. Default inert: no-op so the
    // single-object 8-corner fold is observable on its own.
    void (*accumulateAabbRecursive)(float* box, void* child) = nullptr;
};

void InstallRenderLeaves8Hooks(const RenderLeaves8Hooks& hooks);
const RenderLeaves8Hooks& CurrentRenderLeaves8Hooks();
// Returns a fully-populated table of the inert default implementations (every
// pointer non-null). Tests start from this and override only the slots they
// exercise, so the deterministic paths never call through a null pointer.
RenderLeaves8Hooks DefaultRenderLeaves8Hooks();

// ---------------------------------------------------------------------------
// 0x4226bc — VIBE_Color_NotEqualRgb (__usercall eax=a, edx=b).
// Returns nonzero iff the two 3-byte RGB records differ in any channel.
// ---------------------------------------------------------------------------
int NotEqualRgb(const u8* a, const u8* b);

// 0x4226e0 — VIBE_Color_SetRgb (__usercall eax=dst, dl=r, cl=g, bl=b).
// Stores the channels into the 3-byte record as dst[0]=r, dst[1]=b, dst[2]=g
// (the original's verbatim channel ordering). Returns dst.
u8* SetRgb(u8* dst, u8 r, u8 g, u8 b);

// 0x433694 — VIBE_Render_SetWindowRect (__usercall eax=a1, edx=a2, ecx=a3, ebx=a4).
// Stores the present clip rect (see WindowRect). Returns a1 (the eax residue).
i32 SetWindowRect(WindowRect& out, i32 a1, i32 a2, i32 a3, i32 a4);

// 0x5dd9c8 — VIBE_Render_GetVertexBufferInfo (__usercall eax=outHandle, edx=outCap).
// Reports the active vertex-buffer handle and a fixed 2048 element capacity.
// Returns 0.
int GetVertexBufferInfo(u32 vbHandle, u32* outHandle, u32* outCap);

// 0x5d3668 — VIBE_Mesh_ReleaseStockObject (__usercall eax=obj).
// If obj is non-null and its refcount [obj+476] (mesh[119] dword) is > 0, tail-calls
// DeleteStockObject; otherwise returns obj unchanged.
void* ReleaseStockObject(void* obj);

// 0x43ea34 — VIBE_Light_RefreshAllToggle (__usercall eax=flagPtr).
// Forwards *flagPtr to RefreshAllObjects and returns 1.
int RefreshAllToggle(const u8* flagPtr);

// 0x5c8538 — VIBE_Light_RequestObjectCache (__usercall eax=root).
// If root is non-null, walks root's subtree ([root+520] dword head) invoking
// BuildObjectCache (mask 576). Returns the original's low byte of the walk result
// (or 0 when root is null).
u8 RequestObjectCache(void* root);

// 0x5c8a78 — VIBE_Light_AttachAtFrameMatrix (__usercall eax=obj).
// Composes obj's bone-chain world matrix (relative to the active frame object when
// [obj+528] >= 0, else absolute) into a 16-float scratch, then extracts the euler
// angles in place via matrixToEuler. `outMatrix` receives the final matrix
// (m[0..2] hold the euler angles after the extraction) for testability.
void AttachAtFrameMatrix(void* obj, float outMatrix[16]);

// 0x5c7cf8 — VIBE_Light_RecomputeForObject (__usercall eax=light, edx=obj, ecx=a3).
// Drops the light's cache rows for obj, then (when light[+148] != 0, i.e. the
// light has a nonzero range) transforms obj's pose into the light's local frame,
// stores light[+484] = light[+144]^2 (range squared), and re-illuminates obj.
// Returns 1.
int RecomputeForObject(void* light, void* obj, void* a3);

// 0x5c7e58 — VIBE_Light_PrepareObjectCache (__usercall eax=obj).
// Resolves obj's LOD mesh frame ([obj+460], via SelectLodFrame when null); if a
// frame exists, transforms the object pose into bone-local space, caches
// obj[+484] = radius^2 (radius queried from the mesh's vtable slot +504, default 0
// in isolation), then applies the +0x04 (AssignMeshData) and +0x20 (vtable +492)
// flag actions in [obj+528].
void PrepareObjectCache(void* obj);

// 0x4284f4 — VIBE_Mesh_ComputeWorldAabb (__usercall eax=outMin, ebx=obj, edx=outMax).
// (The decompiler dropped the ecx/edx output pointer; recovered from disasm as a
// second 3-float output box.) Saves the active frame object's pose, neutralises it,
// assigns obj's mesh data, then folds obj's bound-mesh 8 corners into outMin[0..2]
// (min) and outMax[0..2] (max), recursing over obj's child list, and finally
// restores the frame object's pose. No-op (returns false) when obj has no bound
// mesh ([obj+460] == 0). Returns true when the fold ran.
bool ComputeWorldAabb(float outMin[3], void* obj, float outMax[3]);

// 0x5b2a58 — VIBE_Mesh_ComputeObjectAabb (__usercall eax=outMin, edx=outMax).
// Seeds outMin = {+1e10,+1e10,+1e10}, outMax = {-1e10,-1e10,-1e10}, neutralises the
// active frame object's pose, walks the whole scene tree accumulating each object's
// world AABB through AccumulateVertexAabb (driven via traverseTree), then restores
// the frame pose and writes the merged box into outMin / outMax. Returns outMax[2].
i32 ComputeObjectAabb(float outMin[3], float outMax[3]);

} // namespace guild::render
