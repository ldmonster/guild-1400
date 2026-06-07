#pragma once
#include "guild/common/types.h"
#include "render/types.h"   // ColorFormat (reused by the Surface pixel leaves)

// =============================================================================
// guild::render — render-math leaves, batch 4 (gilde.exe gfx/light/mesh/surface).
//
// A fourth slice of self-contained, deterministic VIBE_Light_*/Mesh_*/Surface_*
// leaves translated 1:1 from the Hex-Rays reference (cross-checked against the
// raw disassembly where the decompiler dropped pointer-init or loop-index code).
// Each function is pure arithmetic / pointer-graph bookkeeping over caller-
// supplied records — no DDraw/GDI/device calls — so it is golden-testable.
//
// Cross-module callees that are NOT yet reconstructed are routed through an
// installable RenderLeaves4Hooks struct with inert default implementations
// defined in render_leaves4.cpp (tests install their own). Reconstructed callees
// are reused directly:
//   guild::crt::RandNext              (0x5cb8bc VIBE_Util_RandNext)
//   guild::render::ComputeAabbExtents (0x42756c, mesh_scene.cpp)
//   guild::render::PackColor          (0x434f30 VIBE_Result_Handler_Final)
//   guild::render::UnpackColor        (0x434f7c VIBE_Render_UnpackColor)
//
// Translated functions (all addresses verified UNTRANSLATED at time of writing,
// case-insensitively, by both address and bare name across all of src/render):
//   0x5c6b30  VIBE_Light_RefreshChildBrightness   (propagate brightness to kids)
//   0x5c6be0  VIBE_Light_UpdateFlickerIntensity   (RNG torch-flicker intensity)
//   0x4283ac  VIBE_Mesh_AccumulateAabbRecursive   (recursive AABB merge)
//   0x427820  VIBE_Mesh_TestAabbOverlapRecursive  (recursive AABB overlap probe)
//   0x423d74  VIBE_Surface_GetPixelRgb            (bpp-dispatched pixel read)
//   0x423e5c  VIBE_Surface_SetPixelRgb            (bpp-dispatched, clip-tested write)
//   0x422ee4  VIBE_Surface_BlitRgbToPixels        (RGB-triple block -> 16bpp pack)
//
// Recovered constant scalars (decoded from gilde.exe raw bytes):
//   kRandScale (flt_628C0C / flt_628748)       1/32767 RNG-to-unit scale
//   kFlickerTwo / kFlickerQuarter / kFlickerByteMax (flt_628C10/dbl_628C14/flt_628C1C)
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Recovered constants (get_bytes; bit-exact float/double bit patterns).
// ---------------------------------------------------------------------------
constexpr float  kRandScale        = 3.0518509447574615e-05f; // flt_628C0C = 1/32767
constexpr float  kFlickerTwo       = 2.0f;                 // flt_628C10
constexpr double kFlickerQuarter   = 0.25;                 // dbl_628C14
constexpr float  kFlickerByteMax   = 255.0f;               // flt_628C1C

// ---------------------------------------------------------------------------
// Module globals recovered from the binary (defined once in render_leaves4.cpp).
// ---------------------------------------------------------------------------
// The light-node child array is a flat array of {pointer, packed-colour-dword}
// pairs (the original 32-bit `_DWORD pair[2]` at node+0xC). We model the node and
// the pair with native structs so pointer widths are correct on this host; tests
// build the records through the same structs so the layout matches end-to-end.
// In the original 32-bit record the layout is exactly { i32 count; void* obj;
// void* next; pair[count] } (offsets 0,4,8,0xC); we keep that field ORDER.
struct LightChildPair {
    void* vbase;   // v12[0] — the affected vertex/colour record
    u32   color;   // v12[1] — packed colour DWORD (b,g,r in bytes 0,1,2)
};

struct LightNode {
    i32         count;          // [node+0]  number of child pairs (*v8)
    void*       obj;            // [node+4]  owning scene object (v8[1])
    LightNode*  next;           // [node+8]  next list node (v8[2])
    LightChildPair pairs[1];    // [node+0xC] flexible array of pairs
};

// The bound-mesh geometry record the AABB passes read. In the original 32-bit
// layout: [+0] corner-array base, [+4] triangle-array base, [+8] start corner
// index, [+12] triangle count. We model it as a native struct so the two pointer
// fields don't overlap on this 64-bit host; the corner array is an array of
// 80-byte vertex records (the original strides corners by 80 / 20 floats).
struct MeshGeom {
    const float* corners;     // [+0]  base of the 80-byte-stride corner array
    void*        triangles;   // [+4]  base of the MeshTriangle array
    i32          startIndex;  // [+8]  first corner index (80 * idx byte offset)
    i32          triCount;    // [+12] number of triangles
};

// A mesh triangle in the original is a 10-dword (40-byte) record whose first
// three dwords are the three vertex pointers (v15[0..2]); the remaining seven
// dwords are per-triangle attributes the AABB-overlap pass never touches. We
// model just the three vertex pointers as a native struct (so pointer width is
// correct on this host); TestAabbOverlapRecursive strides by sizeof(MeshTriangle).
struct MeshTriangle {
    float* v[3];   // v15[0], v15[1], v15[2] — the triangle's three vertices
};

// dword_62EB38 — the engine frame/tick counter consulted by the flicker timer.
extern u32 g_flickerTick;
// byte_649D70 — global "use linear (raw) lighting" flag. When set the brightness
// passes copy raw values; when clear they apply the >>2 / clamp transforms.
extern u8 g_rawLightingFlag;

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults in render_leaves4.cpp). The defaults are
// chosen so the deterministic math paths can be exercised in isolation.
// ---------------------------------------------------------------------------
struct RenderLeaves4Hooks {
    // 0x5b2c70 VIBE_Object_AssignMeshData(obj) — ensure obj's mesh frame is
    // resolved before AABB accumulation reads its vertex array.
    void (*assignMeshData)(void* obj);
    // 0x5af2c0 VIBE_Object_PropagateDirtyFlag(obj, flags) — push transform dirty.
    void (*propagateDirtyFlag)(void* obj, u32 flags);
};

void InstallRenderLeaves4Hooks(const RenderLeaves4Hooks& hooks);
const RenderLeaves4Hooks& CurrentRenderLeaves4Hooks();

// ---------------------------------------------------------------------------
// 0x5c6b30 — VIBE_Light_RefreshChildBrightness (__usercall eax = root).
// Walks the root's light-list (root[+0x1E8] -> head[+0x198]); for each list node
// with a valid, non-hidden owning object and bound mesh, iterates the node's
// child-pair array (count at [node], pairs at node+0xC stride 8). For each child:
//   raw mode  (g_rawLightingFlag != 0): child[+0x40] = child[+0x44]   (dword copy)
//   else:                               child[+0x42] = (i8)child[+0x46] >> 2 (byte)
// Returns the last pointer the original left in eax (the node pointer when the
// list terminates) for trace fidelity; callers ignore it.
void* RefreshChildBrightness(void* root);

// ---------------------------------------------------------------------------
// 0x5c6be0 — VIBE_Light_UpdateFlickerIntensity (__usercall eax = light).
// Advances the torch flicker envelope. Cross-fades between the previous and next
// random target over the configured period (light->cfg[+416] ticks), reseeding
// with RandNext when the period elapses, then ORs the per-vertex flicker byte
// into the affected child meshes' colour channels (g_rawLightingFlag gates the
// matrix-scale vs. simple-scale path). Returns the original's eax residue.
// Operates over caller records via byte offsets; see render_leaves4.cpp.
u8 UpdateFlickerIntensity(void* light);

// ---------------------------------------------------------------------------
// 0x4283ac — VIBE_Mesh_AccumulateAabbRecursive (__usercall edx=box, ebx=obj).
// Merges obj's 8-corner bounding box into the running min(box[0..2])/max(box[4..6]),
// then recurses over child objects (obj[+0x1FC] list, link at child[+0x1F0]).
//   box layout: float box[7] = { min0,min1,min2, _, max0,max1,max2 } (slot 3 pad).
// ---------------------------------------------------------------------------
void AccumulateAabbRecursive(float* box, void* obj);

// ---------------------------------------------------------------------------
// 0x427820 — VIBE_Mesh_TestAabbOverlapRecursive (__usercall eax=probe, edx=obj).
// For a class-4 obj with a bound mesh, computes the mesh's 8-corner world AABB and
// tests it against the probe record's [+88..+112] interval box; on overlap walks
// the mesh triangles (via ComputeAabbExtents) accumulating the probe's [+16]/[+20]
// Y-span. Recurses over obj's children (obj[+0x1FC], link child[+0x1F0]) AND-ing
// each child's result. Returns 1 unless a class-4 node was processed (then 0),
// AND-ed across the subtree — matching the original's `v3 &= v26`.
// ---------------------------------------------------------------------------
int TestAabbOverlapRecursive(void* probe, void* obj);

// ---------------------------------------------------------------------------
// Software-surface record fields used by the pixel leaves (byte offsets in the
// original 32-bit surface struct; only `pixels` is a pointer and it is read in
// isolation so plain byte access is layout-safe on this host):
//   [+16] pitch (pixels-per-row stride), [+20] bpp, [+28] pixel buffer base,
//   [+36/+40] clip x0/y0, [+44/+48] clip x1/y1 (SetPixel bounds test).
// ---------------------------------------------------------------------------

// 0x423d74 — VIBE_Surface_GetPixelRgb (__usercall, eax=x, edx=y, ecx=out3, ebx=surf).
// Reads the pixel at (x,y) and writes its r,g,b into out[0..2]. Dispatches on the
// surface bpp ([surf+20]): 15/16bpp -> UnpackColor of the u16; 24bpp -> three raw
// bytes (b,g,r order); 32bpp -> three raw bytes from the dword. `fmt` supplies the
// channel shifts for the 15/16bpp unpack (the original used the global format).
// Returns the blue byte (the original's al residue).
u8 GetPixelRgb(const ColorFormat& fmt, int x, int y, u8 out[3], const void* surf);

// 0x423e5c — VIBE_Surface_SetPixelRgb (__userpurge: eax=x, edx=y, cl=r, bl=g, +a5=b, +a6=surf).
// Clip-tests (x,y) against [surf+36..+48]; on pass writes the packed/raw pixel by
// bpp: 8bpp -> (r+g+b)/3 grey; 15/16bpp -> PackColor u16; 24bpp -> b,g,r bytes;
// 32bpp -> packed dword. Returns 1 if written, 0 if rejected/out of range.
int SetPixelRgb(const ColorFormat& fmt, int x, int y, u8 r, u8 g, u8 b, void* surf);

// 0x422ee4 — VIBE_Surface_BlitRgbToPixels (__userpurge: ecx=rows, ebx=cols, a3=src,
//   a6=surf). Packs an `rows`x`cols` block of tightly-packed RGB triples (`src`,
//   3 bytes/pixel, row-major) into the 16bpp destination surface via PackColor,
//   using the surface pitch [surf+16] and pixel base [surf+28].
void BlitRgbToPixels(const ColorFormat& fmt, u32 rows, u32 cols,
                     const u8* src, void* surf);

} // namespace guild::render
