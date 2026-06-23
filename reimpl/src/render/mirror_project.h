#pragma once
#include "guild/common/types.h"
#include "render/mirror.h"   // MirrorPlane

// =============================================================================
// guild::render — Mirror remainder: reflect + project a vertex stream.
//
// Faithful 1:1 reconstruction of:
//   0x5F6084  VIBE_Mirror_ProjectReflectedVertices
//
// For each vertex (stride 20 floats: xyz at +0/+4/+8) the engine reflects the
// point across the mirror plane (flt_1408A88..90 = unit normal, flt_1408A94 = d,
// flt_62C39C = 2.0) IN PLACE, then perspective-projects it, writing:
//   t  (the reflection scalar)        -> +7   (*(v2-13) after the +=20 step)
//   screenX = (px*projX)*(1/z) + ox   -> +4   (*(v2-16))
//   screenY = (py*projY)*(1/z) + oy   -> +5   (*(v2-15))
// where projX=flt_13FCD0C, projY=flt_13FCAF8, ox=flt_13FCD18, oy=flt_13FCD10.
//
// The reflection is the standard mirror reflection (unit-length plane normal):
//   t  = -(P·n - d) * 2.0
//   P' = P + t·n     (written back to +0/+4/+8)
// matching ReflectPointAcrossPlane (render/mirror). The projection scalars were
// file-scope globals; we gather them into ProjectionParams so the routine is
// re-entrant and testable.
// =============================================================================
namespace guild::render {

// One vertex of the engine's 20-float stride record (only the fields this
// routine touches; the rest of the 20 floats are opaque scratch).
struct MirrorVertex {
    float x, y, z;     // +0/+4/+8  model-space (reflected in place)
    float screenX;     // +16 (index 4) projected screen X
    float screenY;     // +20 (index 5) projected screen Y
    float t;           // +28 (index 7) reflection scalar
};

// Perspective projection scalars (mirror flt_13FCD0C/AF8/D18/D10).
struct ProjectionParams {
    float projX;  // flt_13FCD0C  x scale
    float projY;  // flt_13FCAF8  y scale
    float offX;   // flt_13FCD18  x offset
    float offY;   // flt_13FCD10  y offset
};

// gilde.exe 0x5F6084 — VIBE_Mirror_ProjectReflectedVertices (result@eax=verts,
// edx=count). Reflect each of `count` vertices across `plane` and project, in the
// exact FPU evaluation order of the original.
void ReflectAndProjectVertices(MirrorVertex* verts, int count,
                               const MirrorPlane& plane,
                               const ProjectionParams& proj);

// =============================================================================
// SCENE-GRAPH HALVES OF THE REFLECTION PASS (wave-7 — the wave-6 rule-8 gap)
//
//   0x5F676C  VIBE_Mirror_PrepareReflectionNode   (per-reflective-node binder)
//   0x5F5D08  VIBE_Mirror_CreateClippingPlanes    (build the reflection clip planes)
//
// These bind the reflection draw-node to the scene graph and derive the mirror's
// clip-plane list from the mirror surface's polygons. Their pure arithmetic
// (poly collection, vertex dedup, outline tracing, plane-equation derivation) is
// reconstructed 1:1; the genuinely coupled leaves — the memory-debug allocator
// (0x438f10/0x43923c) and the runtime per-orientation frustum table
// (dword_13DB398) — are reached through NAMED hooks (rule 8), each defaulting to
// an inert/standalone behaviour so the module links with no third-party deps.
// =============================================================================

// ---- Allocator hook (gilde.exe 0x438f10 alloc / 0x43923c free) --------------
// The mirror binder/outline path allocates several scratch + result buffers via
// VIBE_Memory_AllocDebug(tag). Default = the C++ allocator so the recon runs
// standalone; the real backend installs the genuine debug allocator.
using MirrorAllocHook = void* (*)(u32 nbytes, const char* tag);
using MirrorFreeHook  = void  (*)(void* p);
void SetMirrorAllocHook(MirrorAllocHook a);
void SetMirrorFreeHook(MirrorFreeHook f);
void* MirrorAlloc(u32 nbytes, const char* tag);   // used by mirror_silhouette too
void  MirrorFree(void* p);

// ---- Mesh / poly views (offsets straight from the decompile) ----------------
// A mirror-surface polygon (40-byte stride in the mesh poly array):
//   +0/+4/+8  three vertex-record pointers (v0,v1,v2)
//   +20       the vertex-group / surface key (poly[20] == a2 selects the mirror)
//   +36       sign byte (i8 < 0 == back-facing / mirror-flagged)
// (Logical view, NOT a binary overlay: the original is 32-bit with 4-byte
// pointers giving a 40-byte stride; this host-portable view keeps the same field
// SEMANTICS at the documented original byte offsets.)
struct MirrorPoly {
    float* v[3];         // +0/+4/+8 vertex records (xyz at the record's +0/+4/+8)
    u32  _pad[2];        // +12/+16
    u32  surfaceKey;     // +20  (orig poly +20: the vertex-group / surface key)
    u32  _pad2[3];       // +24/+28/+32
    i32  signByte;       // +36 (only the low byte's sign is read)
    // The poly's stored surface normal, the source vector the engine rotates for
    // the mirror plane: PrepareReflectionNode reads it at *(boundPoly+16)+44 and
    // feeds it (via RotateVectorWithFrame) into ctx+24. Modelled here so the binder
    // takes the REAL stored normal (not a fabricated triangle normal).
    float normal[3];     // poly's stored normal (orig *(*(poly+16))-record +44)
};

// The mesh block hung off the node at object+460 (dword view indices):
//   [1] poly array base      [3] poly count      [4] parent/sub-object
//   [5] child-pointer array  ([4]+480 == child count)
struct MirrorMeshBlock {
    const MirrorPoly* polys;   // [1] +4  poly array (40-byte stride)
    u32   _pad0;               // [2] +8
    i32   polyCount;           // [3] +12
    void* parent;              // [4] +16 (parent[480>>2=120] == child count)
    void* childArray;          // [5] +20
};

// One clip plane the mirror reflection is culled against (matches MirrorClipPlane
// in render/mirror.h: inside when nx*x+ny*y+nz*z >= d). Stored 16 bytes per plane
// after an 8-byte header in the engine's final buffer (see CreateClippingPlanes).
struct MirrorClipPlaneOut { float nx, ny, nz, d; };

// The engine's final clip-plane buffer (EXACT layout, as returned by 0x5F5D08):
//   +0  i32  count   total plane count (silhouette + per-orientation frustum)
//   +4  u8   flag    = 1 (the engine writes byte[4]=1; bytes +5..+7 pad)
//   +8  ..   count * MirrorClipPlaneOut (16 bytes each: nx,ny,nz,d) — silhouette
//            planes first, then the appended frustum planes.
// MirrorClipPlaneList is a VIEW over that single allocation (no extra alloc): the
// pointer returned by CreateClippingPlanes IS the buffer base, exactly like the
// original. Use count()/flag()/planes() to read it; free it with MirrorFree.
struct MirrorClipPlaneList {
    i32 count() const { return *(const i32*)((const u8*)this + 0); }
    u8  flag()  const { return *((const u8*)this + 4); }
    const MirrorClipPlaneOut* planes() const {
        return (const MirrorClipPlaneOut*)((const u8*)this + 8);
    }
    MirrorClipPlaneOut* planes() {
        return (MirrorClipPlaneOut*)((u8*)this + 8);
    }
};

// ---- Per-orientation frustum-plane table (gilde.exe dword_13DB398) ----------
// A RUNTIME-populated global (all-zero at static analysis): indexed by the kept
// polygons' OR'd clip outcode (& 0x3F), stride 26 dwords; entry[0] = the number
// of extra frustum planes for that orientation, entry[2..] = those plane records
// (16 bytes each) appended verbatim to the outline-derived planes. Because it is
// not a static constant, it is supplied via this hook (rule 8): given the 6-bit
// orientation code, return how many frustum planes to append and a pointer to
// their 16-byte records. Default: 0 planes (the standalone outline-only path).
struct MirrorFrustumPlanes { i32 count; const MirrorClipPlaneOut* planes; };
using MirrorFrustumTableHook = MirrorFrustumPlanes (*)(u32 orientationCode6);
void SetMirrorFrustumTableHook(MirrorFrustumTableHook h);

// ---- Plane-normal rotation hook (gilde.exe VIBE_Transform_RotateVectorWithFrame
//      @0x5c8ab4) ------------------------------------------------------------
// PrepareReflectionNode rotates the surface poly's STORED normal by the camera
// node's frame before forming the mirror plane:
//   RotateVectorWithFrame(node, dword_13FCD1C, ctx+24 /*out*/, *(boundPoly+16)+44)
// The rotation is a transform-module leaf coupled to the camera node + frame; it
// is reached through this NAMED hook (rule 8). Given the source normal (the poly's
// stored normal) it returns the rotated normal. Default: IDENTITY — the engine's
// behaviour when there is no active camera node (dword_13FCD1C == 0 at static
// analysis), which is exactly the standalone path: the stored normal is used as-is.
using MirrorRotateNormalHook =
    void (*)(const float* node, const float* srcNormal, float* outNormal);
void SetMirrorRotateNormalHook(MirrorRotateNormalHook h);

// gilde.exe 0x5F5D08 — VIBE_Mirror_CreateClippingPlanes.
//   __usercall eax = fn(node@eax, surfaceKey@edx, scratch@esi)
// Collect the mesh's mirror-surface polygons (poly.surfaceKey == surfaceKey),
// gather + dedup their vertex points, trace the silhouette outline, derive one
// clip plane per outline edge (n = normalize((b-o)x(a-o)) via TriangleNormal,
// d = -(n . a)), append the per-orientation frustum planes, and return the
// allocated MirrorClipPlaneList. Returns nullptr when there is no mirror surface,
// the outline has < 3 unique points, or it degenerates (< 6 outline pointers).
MirrorClipPlaneList* CreateClippingPlanes(const MirrorMeshBlock* mesh,
                                          u32 surfaceKey);

// ---- PrepareReflectionNode --------------------------------------------------
// The reflection draw-node binder. `a2` in the original is a small draw-context
// record; we model the fields the routine reads/writes:
struct ReflectionBindContext {
    void*  node;            // +0   the owning node (v20)
    void*  currentMesh;     // +4   the mesh currently bound (v9 / set to v3)
    MirrorClipPlaneList* clipPlanes; // +8  the built clip-plane list (freed/rebuilt)
    void*  surfacePoly;     // +12  the chosen reflective poly (result)
    const MirrorPoly* boundPoly; // +16  the matched poly within the mesh (v10)
    void*  projectCallback; // +20  installed = VIBE_Mirror_ProjectReflectedVertices
    float  planeNormal[3];  // +24/+28/+32 the mirror plane normal (rotated)
    float  planeD;          // +36 the mirror plane distance (n . p)
    u8     flags;           // +40 status bits (bit0 valid-poly, bit1 rebuild-needed)
};

// A reflective-surface node as PrepareReflectionNode reads it:
//   +460 (idx115)  the mesh block (MirrorMeshBlock)
//   +528 (byte)    the "reflection active" flag byte (bit tested: 16*b>>7)
struct ReflectionNode {
    MirrorMeshBlock* mesh;   // +460
    u8  activeFlag;          // +528 low byte (the (16*b)>>7 == bit3 test)
};

// gilde.exe 0x5F676C — VIBE_Mirror_PrepareReflectionNode.
//   __usercall eax = fn(node@eax, ctx@edx)
// Walks the mesh's child sub-objects to find the reflective one (child flag
// byte[104] & 0x20), binds it into `ctx` (installs the project callback, the
// matched poly and mesh), recomputes the mirror plane (RotateVectorWithFrame of
// the surface normal + plane distance), (re)builds the clip planes via
// CreateClippingPlanes, and — when a valid clip-plane list results — publishes
// the node as the global "prepared reflection node" (sets reflectionPrepared).
// Returns the node pointer when published, else the last child examined.
//
// `reflectionPreparedOut` mirrors dword_649D6C: set to `node` on success, and
// gated at entry (the original early-outs while dword_649D6C is already set).
// `frame`/`basis` are the camera frame + basis for the plane-normal rotation
// (dword_13FCD1C); pass null to skip the rotation (then the plane normal is taken
// straight from the matched poly's stored normal).
void* PrepareReflectionNode(ReflectionNode* node, ReflectionBindContext* ctx,
                            void** reflectionPreparedGlobal,
                            float* frame, const float* basis);

} // namespace guild::render
