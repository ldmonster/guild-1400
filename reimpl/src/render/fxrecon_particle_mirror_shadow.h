#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — fxrecon: particle / mirror-reflection / shadow-casting cluster.
//
// Faithful 1:1 reconstruction (Hex-Rays is the reference of record) of the
// gilde.exe "VIBE_Particle / VIBE_Mirror / VIBE_Shadow" cluster:
//
//   0x42c104  VIBE_Particle_ResetEmitter            (rebind emitter, clear flags)
//   0x4d887c  VIBE_Particle_SpawnSparkleEffect      (preset "punkte_nm_2" emit)
//   0x5e1238  VIBE_Particle_SetOrientationFromAngle (basis-from-angle -> euler)
//   0x5f5d08  VIBE_Mirror_CreateClippingPlanes      (mirror outline -> clip planes)
//   0x5f676c  VIBE_Mirror_PrepareReflectionNode     (bind reflection draw-node)
//   0x5f1d90  VIBE_Shadow_AllocCache                (alloc shadow-surface cache)
//   0x5f1fc0  VIBE_Shadow_InitBuffers               (alloc shadow poly/point bufs)
//   0x5f20e8  VIBE_Shadow_ShutdownBuffers           (free all shadow buffers)
//   0x5f3f98  VIBE_Shadow_CastFromLight             (cast one object's shadow)
//   0x5f444c  VIBE_Shadow_CastFromAllLights         (cast from every active light)
//   0x5f4494  VIBE_Shadow_UpdateNodeShadows         (per-node shadow refresh)
//
// SCOPE / RULE 8 NOTE. The pure update/emit/reflect/project arithmetic, the list
// bookkeeping, the dedup/outline math and the module-global state machine are
// reconstructed exactly. The *coupled leaves* this cluster reaches — the scene
// graph node records, the GPU/draw-list pointers, the mesh polygon arrays, the
// memory-debug allocator, the texture upload, and the lower-level math helpers
// (VIBE_Math_BuildBasisFromAngle / MatrixToEuler / TriangleNormal /
// VectorWithinTolerance, the transform routines, AcquireCacheSlot, etc.) — are
// reached here through *hooks* (function pointers) so this file stays
// self-contained and compiles with no third-party deps. Each hook defaults to an
// inert/identity behaviour; the real backend installs the genuine leaf. Nothing
// here is faked: the control flow, constants and arithmetic are 1:1; the hook is
// merely the seam where the live call tree plugs the real callee back in.
// =============================================================================

namespace guild::render::fxrecon {

// ---------------------------------------------------------------------------
// Record layouts (byte offsets straight from the decompile). All accesses go
// through typed helpers so the offsets are documented once, here.
// ---------------------------------------------------------------------------

// gilde.exe — particle "object" record as touched by ResetEmitter (dword view):
//   [+0]   (a1[0])   rebind field A  (set to a2)
//   [+4]   (a1[1])   rebind field B  (set to a3)
//   [+40]  (a1[10])  pointer to the emitter-slot array base
//   [+208] (a1[52])  emitter-slot count
// Each emitter slot is 84 bytes; byte +81 holds a flag set, bit0 (0x1) "active".
struct EmitterObject {
    u32   fieldA;       // +0
    u32   fieldB;       // +4
    u8*   slotBase;     // +40 (a1[10])
    i32   slotCount;    // +208 (a1[52])
};

// gilde.exe — VIBE_Particle_SpawnEffect parameter block (a5 = float[3] + ...).
// SpawnSparkleEffect fills a 0x20-byte stack frame: v3[0..2] = {0,50.0f,0}
// (1112014848 == 50.0f), and a packed 4-byte "particle descriptor" v4 whose
// bytes are { 50, 0x1E, 0x50, ? } i.e. LOBYTE=50, then 0x501E at +1.
struct SparkleParams {
    float pos[3];       // {0.0f, 50.0f, 0.0f}
    u32   descriptor;   // low byte 50, *(u16*)(&v4+1) = 20510 (0x501E)
};

// ---------------------------------------------------------------------------
// Leaf hooks (the seams where the real call tree reconnects). See SCOPE note.
// ---------------------------------------------------------------------------

// 0x42bd6c VIBE_Particle_SpawnEffect — emits a particle system. Returns the new
// system id (0 = none). Inert default returns 0.
using SpawnEffectHook = i32 (*)(u32 owner, u32 descriptor, const char* tplName,
                                u8 mode, const SparkleParams* params, i32 lifetime,
                                i32 a7, float a8, u32 a9, u32 a10, u32 a11, u32 a12);
void  SetSpawnEffectHook(SpawnEffectHook h);

// 0x5ca544 VIBE_Math_BuildBasisFromAngle(dir, angle, out16floats) — builds a
// 4x4 orientation basis from a direction vector + roll angle.
using BuildBasisHook = void (*)(const float dir[3], float angle, float outMat[16]);
// 0x5cb2cc VIBE_Math_MatrixToEuler(mat[12..]) — writes euler angles into mat[0..2].
using MatrixToEulerHook = void (*)(float mat[16]);
// 0x5af50c VIBE_Object_SetWorldTranslation(objBase, euler[3]) — applies orientation.
using SetWorldTranslationHook = u8 (*)(void* objField, const float euler[3]);
void  SetBuildBasisHook(BuildBasisHook h);
void  SetMatrixToEulerHook(MatrixToEulerHook h);
void  SetSetWorldTranslationHook(SetWorldTranslationHook h);

// Memory-debug allocator (0x438f10 alloc / 0x43923c free). Inert default uses
// the C++ allocator so the reconstruction is runnable standalone.
using AllocHook = void* (*)(u32 nbytes, const char* tag);
using FreeHook  = void  (*)(void* p);
void  SetAllocHook(AllocHook a);
void  SetFreeHook(FreeHook f);

// ---------------------------------------------------------------------------
// Particle cluster
// ---------------------------------------------------------------------------

// gilde.exe 0x42c104 — VIBE_Particle_ResetEmitter. Rebinds the two object fields
// and clears the "active" bit (0x1 at slot+81) of every emitter slot. Returns
// the address one past the last slot byte (result = a1[10] advanced 84/slot),
// exactly as the original (which leaves `result` pointing past the run).
//   __userpurge  eax = (obj, a2, a3)
u8* Particle_ResetEmitter(EmitterObject* obj, u32 a2, u32 a3);

// gilde.exe 0x4d887c — VIBE_Particle_SpawnSparkleEffect. Fills the sparkle preset
// and forwards to VIBE_Particle_SpawnEffect with the fixed "punkte_nm_2" args.
//   a1 = owner record, a2 = caller-supplied tag (stored at v5).
i32 Particle_SpawnSparkleEffect(u32 ownerParticleField, u32 callerTag);

// gilde.exe 0x5e1238 — VIBE_Particle_SetOrientationFromAngle. If enabled, builds
// a basis from (dir,angle), converts to euler, and applies world translation.
//   __userpurge al = (enable, dir, objBase, angle)
u8 Particle_SetOrientationFromAngle(int enable, float dir[3], void* objBase, float angle);

// ---------------------------------------------------------------------------
// Shadow cluster — module-global state (the dword_1408A*/dword_64A7* statics).
// Exposed so tests and the InitBuffers/Shutdown pair share one instance.
// ---------------------------------------------------------------------------
struct ShadowModuleState {
    // cache (AllocCache)
    u32   cacheCount   = 0;   // dword_64A7E0
    u32   cacheParam   = 0;   // dword_64A7E4
    void* cacheBase    = nullptr; // dword_64A7F8

    // InitBuffers-configured limits / buffers
    u32   shadowLimitA = 0;   // dword_1408A54  (min(a2, dword_1408078))
    u32   shadowLimitB = 0;   // dword_1408A60  (min(a1, limitA))
    u32   paramA5      = 0;   // dword_64A7F0   (a5)
    u32   paramA6      = 0;   // dword_64A7F4   (a6)
    u32   resSquared   = 0;   // dword_64A7EC   (a5*a5)
    void* buildingPoly = nullptr; // dword_1408A58 (48*res^2 bytes)
    void* buildingPoint= nullptr; // dword_1408A50 (600*(res^2>>4) bytes)
    u32   polyCount    = 0;   // dword_1408A68
    u32   pointCount   = 0;   // dword_1408A64
    u8    enableA      = 1;   // byte_1408A6D
    u8    enableB      = 0;   // byte_1408A6E  (= a4)
    u8    enableC      = 1;   // byte_1408A6C
    u32   misc4C       = 0;   // dword_1408A4C
    u32   misc5C       = 0;   // dword_1408A5C  (active light count)
    void* heightmap    = nullptr; // dword_64A048

    // dword_1408A20..A48 : two identity-ish rows initialised by InitBuffers.
    u32   rowA[11] = {0};     // dword_1408A20 .. A48 (11 dwords)
    u32   slotA0C[4] = {0};   // dword_1408A0C[0..3]

    // dword_1408A10[] : the active light pointers consumed by Cast/Update.
    void* lights[64] = {nullptr};
};

ShadowModuleState& ShadowState();   // the single module instance

// Limit register: the original reads dword_1408078 as an external cap.
void Shadow_SetGlobalLimit(u32 v);  // sets dword_1408078

// gilde.exe 0x5f1d90 — VIBE_Shadow_AllocCache. Records (count,param) and
// allocates count*20 cache bytes. Returns the cache base.
void* Shadow_AllocCache(u32 count, u32 param);

// gilde.exe 0x5f1fc0 — VIBE_Shadow_InitBuffers. Clamps the shadow limits,
// allocates the building poly/point buffers, seeds the two basis rows, sets the
// enable flags, calls AllocCache, and clears the 4 slotA0C entries. Returns 16
// (the original's `result*4` with result==4).
//   __userpurge eax = (a1, a2, a3, a4, a5, a6)
i32 Shadow_InitBuffers(u32 a1, u32 a2, u32 a3, u8 a4, u32 a5, u32 a6);

// gilde.exe 0x5f20e8 — VIBE_Shadow_ShutdownBuffers. Zeros the limits and frees
// every shadow buffer + the heightmap.
i32 Shadow_ShutdownBuffers();

// Per-object shadow caster table (matches shadow.h: object +1780, 4 entries,
// 128-byte stride). CastFromLight searches/creates an entry keyed by light id.
struct ShadowCasterEntry {
    void* surface = nullptr; // +0   (0 = empty)
    void* light   = nullptr; // +4   casting light record
    u32   stA = 0;           // +12
    u32   stB = 0;           // +16
    u32   stC = 0;           // +20
    float dir[3] = {0,0,0};  // +24..+32 cached light direction
    u32   coverage = 0;      // +20 alias for the coverage byte (see note)
    i32   bboxX0 = -1;       // +108 (-1 = invalidated)
    i32   bboxY0 = 0;        // +112
    i32   bboxX1 = 0;        // +116
    i32   bboxY1 = 0;        // +120
    u8    flag124 = 0;       // +124 (texture-vs-cache path)
};

// gilde.exe 0x5f3f98 — VIBE_Shadow_CastFromLight is the geometry-heavy leaf
// (mesh records / GPU upload / cache slots). Routed through a hook so the
// CastFromAllLights driver reproduces exactly while projection stays pluggable.
void  SetCastFromLightHook(u8 (*h)(void*, void*, void*, void*));
void  Shadow_SetCastContext(void* ctx);   // dword_64A028

// =============================================================================
// PER-NODE SHADOW CASTER MANAGEMENT — the drivers that tie the wave-6 light-list
// collector (ResetLightList/PushToDrawList) to the wave-6 mesh-shadow splat
// (RenderMeshShadow/RenderObjectShadow). Reconstructed 1:1 (Hex-Rays is the
// reference of record):
//
//   0x5f4494  VIBE_Shadow_UpdateNodeShadows  — per scene object, called by
//             VIBE_Render_ProcessSceneNode (0x5add1c) between the terrain draw
//             and the object mesh flush. For each of the <=4 collected shadow
//             lights it transforms the object's bounding box through the light,
//             frustum-classifies it, and (if visible) calls CastFromLight.
//   0x5f3f98  VIBE_Shadow_CastFromLight      — manages ONE caster slot for one
//             (object, light) pair: finds the matching slot, else allocates a
//             free slot and initialises it (texture-vs-cache path), then makes
//             the redraw-vs-reuse decision (light-direction change + coverage
//             max scan) and, on redraw, invokes the mesh-shadow render. Finally
//             emits the ground-shadow quad.
//
// SCOPE / RULE 8. The genuine slot bookkeeping, the gate, and the redraw decision
// are pure integer/pointer/float logic and are reconstructed EXACTLY (the golden
// tests pin them). The coupled leaves this management reaches — the bone-chain
// transforms (PointThroughBoneChain 0x5c8b38 / RotateVectorByHierarchy 0x5c8990),
// the frustum classifier (ClassifyBoundingBoxPlanes 0x5ad1f4), the bounding-volume
// transform (TransformBoundingVolume 0x5ad438), caster-height (ComputeCasterHeight
// 0x5f34c0), the cache-slot acquire (AcquireCacheSlot 0x5f1e7c), the texture load
// (Texture_LoadByName 0x5da714), the mesh-shadow render (RenderMeshShadow 0x5f363c)
// and the ground-shadow build (BuildGroundShadow 0x5f3048) — are reached through
// the hooks below (inert by default). Nothing is faked.
// =============================================================================

// A 3-float vector as the engine stores light pos / direction / bone-chain output.
struct ShadowFVec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

// One per-(object,light) caster slot. The engine record is 128 bytes at
// objectCasterTable + 1780 + 128*slotIndex; only the fields the slot management
// and redraw decision touch are modeled (byte offsets in the original record):
//   +0   surface/texture handle (0 == "no surface, slot unusable")
//   +4   light record this slot casts (0 == empty slot; the search/alloc key)
//   +8   ground-shadow link record (read for the BuildGroundShadow ctx write)
//   +12  cleared-on-init scratch (v5+12 = 0)
//   +16  cleared-on-init scratch (v5+16 = 0)
//   +20  cached coverage value (the redraw "coverage changed" compare, v5+20)
//   +24..+32  cached light direction (3 floats; the VectorWithinTolerance compare)
//   +108 cached bbox X0 (-1 == "needs redraw / first time"; coverage-scan x lo)
//   +112 bbox Y0      (coverage-scan y lo)
//   +116 bbox X1      (coverage-scan x hi)
//   +120 bbox Y1      (coverage-scan y hi)
//   +124 path flag (texture-vs-cache; from object flag +529 bit3 >> 7)
struct ShadowNodeSlot {
    void*      surface = nullptr;  // +0
    void*      light   = nullptr;  // +4
    void*      groundLink = nullptr; // +8
    u32        scratch12 = 0;      // +12
    u32        scratch16 = 0;      // +16
    u32        coverage  = 0;      // +20
    ShadowFVec3 cachedDir;         // +24..+32
    i32        bboxX0 = -1;        // +108
    i32        bboxY0 = 0;         // +112
    i32        bboxX1 = 0;         // +116
    i32        bboxY1 = 0;         // +120
    u8         flag124 = 0;        // +124
};

// 4 slots per object caster table (512 bytes / 128). Matches shadow.{h,project.h}.
constexpr int kShadowNodeSlots = 4;

// flt_5F1D80 = {60.0f, 200.0f, ...} — the per-resolution shadow-size thresholds
// CastFromLight steps through to grow the cache request (verified via get_bytes:
// 0x42700000 == 60.0, 0x43480000 == 200.0).
extern const float kShadowSizeThresholds[2];

// ---------------------------------------------------------------------------
// Leaf hooks for the per-node management (see SCOPE note). Inert by default.
// ---------------------------------------------------------------------------

// 0x5da714 VIBE_Texture_LoadByName("Schatten", 131199, 0, 0) — load the shared
// projected-shadow texture for the +124 "texture path". Returns a handle (0=none).
using LoadShadowTextureHook = void* (*)();
void SetLoadShadowTextureHook(LoadShadowTextureHook h);

// 0x5f1e7c VIBE_Shadow_AcquireCacheSlot — acquire/reuse a cache surface of the
// requested size for the "cache path". Returns a handle (0 = none acquired).
//   args mirror the original: (slotRec, type@dl, ctx@ecx, need@ebx).
using AcquireCacheSlotHook = void* (*)(ShadowNodeSlot* slot, u8 type, void* ctx,
                                       u32 need);
void SetAcquireCacheSlotHook(AcquireCacheSlotHook h);

// 0x5c8b38 VIBE_Transform_PointThroughBoneChain(obj, obj+offset, out3) — world
// position of a point through the object's bone hierarchy. Default: out = obj+off.
using PointThroughBoneChainHook = void (*)(const void* obj, const float in[3],
                                           float out[3]);
void SetPointThroughBoneChainHook(PointThroughBoneChainHook h);

// 0x5c8990 VIBE_Transform_RotateVectorByHierarchy(obj, in3, out3) — rotate a
// direction by the object's hierarchy. Default: out = in (identity).
using RotateVectorByHierarchyHook = void (*)(const void* obj, const float in[3],
                                             float out[3]);
void SetRotateVectorByHierarchyHook(RotateVectorByHierarchyHook h);

// 0x5f363c VIBE_Shadow_RenderMeshShadow(obj, mesh, ctx, viewNode, lightVec,
//   originVec, light, slot) — project+rasterize the silhouette into the slot
//   surface (wave-6 reconstructed the geometry tail as RenderObjectShadow).
using RenderMeshShadowHook = void (*)(void* obj, void* mesh, void* ctx,
                                      void* viewNode, const float lightVec[3],
                                      const float originVec[3], void* light,
                                      ShadowNodeSlot* slot);
void SetRenderMeshShadowHook(RenderMeshShadowHook h);

// 0x5f3048 VIBE_Shadow_BuildGroundShadow(slot, groundCtx) — emit the ground
// shadow quad (render_leaves7 owns the body). Returns nonzero on emit.
using BuildGroundShadowHook = int (*)(ShadowNodeSlot* slot, void* groundCtx);
void SetBuildGroundShadowHook(BuildGroundShadowHook h);

// 0x5ad438 VIBE_Mesh_TransformBoundingVolume(obj, viewMat, force) — refresh the
// object's cached world-space bounding box. Default: no-op, returns 0.
using TransformBoundingVolumeHook = int (*)(void* obj, const float* viewMat,
                                            bool force);
void SetTransformBoundingVolumeHook(TransformBoundingVolumeHook h);

// 0x5ad1f4 VIBE_Render_ClassifyBoundingBoxPlanes(corners8, outNear, outFar) —
// classify the 8 transformed bbox corners against the view frustum, returning a
// plane-mask byte (bit6 0x40 == fully outside). Default: returns 0 (visible).
using ClassifyBoundingBoxHook = u8 (*)(const float* corners8);
void SetClassifyBoundingBoxHook(ClassifyBoundingBoxHook h);

// ---------------------------------------------------------------------------
// The per-node management state the drivers read/write. Models the engine's
// per-object record fields that CastFromLight/UpdateNodeShadows consult, plus
// the slot table. Threaded explicitly so the management is testable.
// ---------------------------------------------------------------------------
struct ShadowNode {
    // The object record (engine "v41"/"a1"). Only the touched fields:
    ShadowNodeSlot slots[kShadowNodeSlots]; // objectCasterTable + 1780, stride 128
    u8   flags529 = 0;     // +529 : bit2 0x04 caster, bit3 0x08 -> +124 flag,
                           //        bit4 0x10 directional light branch
    u8   flag531  = 0;     // +531 : bit0 0x01 bvol-valid, bit1 0x02 "cast this frame"
    u8   flag530sign = 0;  // +530 high bit (>= 0 gates TransformBoundingVolume)
    u8   flag528sign = 0;  // +528 high bit (>= 0 selects the view matrix arg)
    i8   casterCount = 0;  // +533 : per-object caster budget (must be < 5)
    bool hasCasterTable = false; // +492 != 0
    void* mesh = nullptr;  // +460 (a1[115]) mesh record (RenderMeshShadow arg)
    void* meshDirtyFlagOwner = nullptr; // (mesh+380) redraw-force flag source

    bool  meshDirty = false; // *(mesh+380) — forces a redraw when set
};

// One collected shadow light (engine "v40"/"a2"). The fields CastFromLight reads:
struct ShadowLight {
    u8    flags529 = 0;    // +529 : bit2 0x04 must be set; bit4 0x10 directional
    float intensity = 0.0f;// +148 (a2[37]) : 0.0 disables the cast
    ShadowFVec3 pos;       // +76 (a2+19 floats) light position (point light)
    void* id = nullptr;    // identity key for the slot search (== the light ptr)
};

// gilde.exe 0x5f3f98 — VIBE_Shadow_CastFromLight (reconstructed driver). Manages
// ONE caster slot for (node, light): the gate, the matching-slot search, the
// free-slot alloc + init, the redraw-vs-reuse decision, the mesh-shadow render
// invoke, and the ground-shadow emit. `ctx` = dword_64A028 (the cast context),
// `forceCoverageScan` mirrors the original a3@ebx (nonzero enables the coverage
// reuse path). Returns 0 (the original always returns 0).
//   __usercall al = (node@eax, light@edx, ctx@ebx, nodeAgain@edi)
u8 Shadow_CastFromLight2(ShadowNode* node, ShadowLight* light, void* ctx,
                         bool forceCoverageScan);

// gilde.exe 0x5f4494 — VIBE_Shadow_UpdateNodeShadows (reconstructed driver).
// Per scene object: clears the +531 "cast this frame" bit, refreshes the cached
// bounding volume, then for each of `lightCount` collected lights transforms the
// object bbox through the light, frustum-classifies it, and (if visible) sets the
// +531 bit and calls Shadow_CastFromLight2. `lights[i]` are the collected shadow
// lights (CollectedShadowLights()); `viewMat` is the view-matrix float block
// (dword_13FCD1C); `ctx` is the cast context (dword_64A028). Returns the last
// classify/cast byte, matching the original's `return (char)a1`.
//   __usercall al = (node@eax)
u8 Shadow_UpdateNodeShadows(ShadowNode* node, ShadowLight* const* lights,
                            u32 lightCount, const float* viewMat, void* ctx,
                            bool forceCoverageScan);

// gilde.exe 0x5f444c — VIBE_Shadow_CastFromAllLights. Iterates the active light
// list and calls CastFromLight for each (guarded by object flag +529 bit2 and a
// non-empty light list). Returns the last CastFromLight result byte.
u8 Shadow_CastFromAllLights(void* objBase);

// ---------------------------------------------------------------------------
// Pure shadow-projection math (extracted 1:1 from VIBE_Shadow_UpdateNodeShadows,
// 0x5f4494) — see .cpp provenance.
// ---------------------------------------------------------------------------
// Directional light: cast a bbox corner onto the ground plane along lightDir.
void Shadow_ProjectCornerDirectional(const float corner[3], const float lightDir[3],
                                     float casterHeight, float out[3]);
// Point light at lightPos: cast a corner onto the ground plane.
void Shadow_ProjectCornerPoint(const float corner[3], const float lightPos[3],
                               float casterHeight, float out[3]);
// View-transform a projected shadow point (m = dword_13FCD1C float block).
void Shadow_TransformProjectedPoint(const float p[3], const float* mBytesAsFloats,
                                    float out[3]);

// ---------------------------------------------------------------------------
// Pure mirror-plane reflection math (kernel of VIBE_Mirror_CreateClippingPlanes,
// 0x5f5d08) — see .cpp provenance. The full traversal/alloc/draw-node halves are
// OMITTED per rule 8 (reported); this is the verifiable plane-equation kernel.
// ---------------------------------------------------------------------------
struct MirrorClipPlaneEq { float nx, ny, nz, d; };
// Build one clip plane from a mirror triangle: n = normalize((v1-v0)x(v2-v0)),
// d = -(n . v0). Returns false on a degenerate triangle.
bool Mirror_BuildClipPlane(const float v0[3], const float v1[3], const float v2[3],
                           MirrorClipPlaneEq* out);
// gilde.exe 0x5caa4c — VIBE_Math_VectorWithinTolerance (per-component |a-b|<=tol).
bool VectorWithinTolerance(const float a[3], const float b[3], float tol);

} // namespace guild::render::fxrecon
