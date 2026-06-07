#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — render-math leaves, batch 6.
//
// A sixth slice of self-contained, deterministic render leaves translated 1:1
// from the Hex-Rays reference. These are the geometry/clip/coordinate-math
// leaves that survive after batches 1..5: 2D line clipping (Cohen-Sutherland),
// mesh bounding-box min/max accumulation, surface rect clamping, and the two
// debug-overlay "anim list" builders (3D text labels / object markers) which
// compute camera-relative coordinates into a packed anim descriptor.
//
// None of these do DDraw/GDI/device work directly. The handful of cross-module
// callees that are NOT reconstructed (the actual DrawHLine raster, the vendor
// rect blit, the object-handle lookup, the bone-chain transforms and the
// anim-list creation) are routed through an installable hooks struct whose
// default implementations are INERT and defined in render_leaves6.cpp — so the
// pure arithmetic is golden-testable in isolation, and the unified build never
// sees an undefined reference.
//
// Reused reconstructed siblings (NO hook — extern-declared + linked):
//   guild::render::TruncToward  (0x5c6b08 VIBE_Coord_ConvertX via particle.cpp)
//                                — the x87 round-toward-zero chop. DrawLineClipped
//                                truncates its clipped endpoints through it.
//   guild::render::DrawHLine     (0x4351d8 render_leaves2.cpp) — the integration
//                                test forwards the drawSpan hook into this REAL
//                                sibling and asserts the cross-module flow.
//
// Translated functions (all verified UNTRANSLATED at time of writing, case-
// insensitively, by both address and bare name across ALL of src/render):
//   0x435434  VIBE_Render_DrawLineClipped   (Cohen-Sutherland 2D clip + DrawHLine)
//   0x426a30  VIBE_Mesh_DrawBoundingBox     (per-vertex AABB min/max, LOD filter)
//   0x423c70  VIBE_Surface_ColorFillRect    (rect clamp to surface bounds + fill)
//   0x428a84  VIBE_Render_DrawTextLabels3D  (label string-copy + cam-relative pos)
//   0x428d30  VIBE_Render_DrawObjectMarkers3D (marker cam-relative pos list)
//
// Recovered constants: none (these leaves carry no float tables — they read the
// clip-rect globals dword_75FB40..4C, which become the ClipBounds struct below,
// and the camera matrix, which becomes the CameraBasis struct).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// 2D integer clip rectangle (the originals read four present-state globals).
//   dword_75FB40  xMax (exclusive-ish upper X bound)
//   dword_75FB44  yMax
//   dword_75FB48  xMin (lower X bound)
//   dword_75FB4C  yMin
// DrawLineClipped tests an endpoint against all four; a fully-outside line is
// rejected (no draw). The naming mirrors how the original compares.
// ---------------------------------------------------------------------------
struct ClipBounds {
    i32 b40 = 0;   // dword_75FB40
    i32 b44 = 0;   // dword_75FB44
    i32 b48 = 0;   // dword_75FB48
    i32 b4C = 0;   // dword_75FB4C
};

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults in render_leaves6.cpp).
// ---------------------------------------------------------------------------
struct RenderLeaves6Hooks {
    // VIBE_Render_DrawHLine(x0, y0, x1, y1, color) — the actual span raster.
    // DrawLineClipped's tail-calls hit this with the clipped endpoints. Default
    // inert: records nothing, returns x0 (the original returns a pixel index).
    i32 (*drawSpan)(i32 x0, i32 y0, i32 x1, i32 y1, i16 color) = nullptr;

    // VIBE_Object_AssignMeshData(obj) — forces the mesh LOD frame resident.
    // Mesh_DrawBoundingBox calls it before walking submeshes. Default inert.
    void (*assignMeshData)(void* obj) = nullptr;

    // VIBE_Transform_PointThroughBoneChainPivot(obj, src[3], dst[3]) — transforms
    // a local-space corner into world space. Mesh_DrawBoundingBox emits 4 corners.
    // Default inert: copies src -> dst (identity), so the AABB math is observable.
    void (*transformPivot)(void* obj, const float* src, float* dst) = nullptr;

    // VIBE_Object_FindByHandle(a,b,namePtr,d,e) — resolve a label string to an
    // object record. DrawTextLabels3D uses it. Default inert: returns nullptr
    // (=> the original bails out of the per-label loop, returning 0).
    void* (*findByHandle)(const char* name) = nullptr;

    // VIBE_Anim_CreateObjectAnim(...) — install the built anim descriptor.
    // DrawTextLabels3D / DrawObjectMarkers3D tail-call it. Default inert: 0.
    i32 (*createObjectAnim)(void* cam, i32 firstFrame, const i32* desc,
                            i32 count, i32 stride) = nullptr;

    // VIBE_Transform_PointThroughBoneChain(obj, src, dst[3]) — marker world pos.
    // DrawObjectMarkers3D uses it. Default inert: zero-fills dst.
    void (*transformChain)(void* obj, const void* src, float* dst) = nullptr;

    // Vendor surface ColorFill / fill dispatch. Surface_ColorFillRect issues it
    // when the surface owns a vendor object. Default inert: records the clamped
    // rect (see SurfaceFillResult). Returns 0 (success).
    i32 (*surfaceFill)(void* surface, const i32* destRect, i32 color) = nullptr;
};

void InstallRenderLeaves6Hooks(const RenderLeaves6Hooks& hooks);
const RenderLeaves6Hooks& CurrentRenderLeaves6Hooks();

// ---------------------------------------------------------------------------
// Minimal record views (host-native; field offsets carry the original byte
// offset in comments). These are NOT serialized — they model the exact reads
// the leaves perform against the live process records.
// ---------------------------------------------------------------------------

// Object record (the camera/scene object). DrawTextLabels3D / Markers read the
// camera object's world matrix through `dword_13FCD1C`; we pass it explicitly.
// Layout mirrors the +76.. world-pos / +132.. translation / +396.. matrix reads.
struct CameraView {
    // +76,+80,+84   world position (read as floats off +76)
    float worldPos[3] = {0, 0, 0};
    // a2[20],a2[21]  (== +80,+84) reused as translation deltas in the label path
    // a1[19],a1[20],a1[21]  (== +76,+80,+84) marker subtract terms
    // a1[33],a1[34],a1[35]  (== +132,+136,+140) marker/label translation subtract
    float trans[3]    = {0, 0, 0};
};

// One labelled object: the original FindByHandle result, of which only the
// "visible" flag (+529 bit0) and the two position triples (+92.. and +144..)
// are read. Modeled directly.
struct LabelObject {
    bool  visible = false;        // +529 & 1
    float posA[3] = {0, 0, 0};    // +92,+96,+100
    float posB[3] = {0, 0, 0};    // +144,+148,+152
};

// Output of a single packed label/marker entry (the camera-relative coords the
// original writes into the anim descriptor). Two triples per label.
struct LabelEntry {
    float a[3];   // posA - cameraWorldPos
    float b[3];   // posB - cameraTrans
};

// ---------------------------------------------------------------------------
// 0x435434 — VIBE_Render_DrawLineClipped
//   (__userpurge: x0@eax, y0@edx, x1@ecx, y1@ebx, color@stack).
// Cohen-Sutherland-style clip of the segment (x0,y0)-(x1,y1) against `clip`.
// If the whole segment is trivially outside, returns the (unmodified) reject
// value the original returns and draws nothing. Otherwise clips each endpoint
// to the bound it violates (interpolating with the precomputed slopes), then
// emits the clipped span via the drawSpan hook (the real DrawHLine in the
// integration test). Returns whatever drawSpan returns (else the reject value).
i32 DrawLineClipped(const ClipBounds& clip, i32 x0, i32 y0, i32 x1, i32 y1,
                    i16 color);

// ---------------------------------------------------------------------------
// 0x426a30 — VIBE_Mesh_DrawBoundingBox
//   (__userpurge: obj@eax, lodCutoff, outMin[3], ...).
// Walks the object's resident LOD submeshes (those whose +4 LOD value <= cutoff)
// and accumulates a min/max AABB over each submesh's stored corner (+72 -> 3
// floats). Then transforms the 4 derived corners through the bone-chain pivot
// into outA..outD. Returns 1 if the object isn't class 4 (early out), else 0.
//
// We model the submesh list as a flat array: each entry has a `lod` value and a
// `corner[3]`. `obj` is opaque (passed to the transform hook). The min/max is
// written into `outMin`/`outMax` (the world-space corners the original derives).
struct SubmeshCorner {
    float lod;        // +4
    float corner[3];  // +72 -> [0..2]
};
i32 MeshDrawBoundingBox(void* obj, i32 objClass, float lodCutoff,
                        const SubmeshCorner* submeshes, i32 count,
                        float outA[3], float outB[3], float outC[3],
                        float outD[3]);

// ---------------------------------------------------------------------------
// 0x423c70 — VIBE_Surface_ColorFillRect
//   (__userpurge: x@eax, y@edx, h@ecx, w@ebx, surface@stack).
// Clamps a fill rect (x,y,w,h) to the surface bounds, then dispatches the fill:
// if the surface owns a vendor object it issues the vendor ColorFill of the
// clamped destination rect; otherwise it does the linear span fill (modeled
// through the surfaceFill hook). Returns 1 on success, 0 when surface is null.
//
// Surface record fields read by the original (all i32 unless noted):
//   +4  width   +8 height   +32 vendorObj(ptr)   +36 leftClampX
// We model just those (vendorObj as a void* tag the hook receives).
struct SurfaceView {
    i32   width      = 0;     // +4
    i32   height     = 0;     // +8
    void* vendorObj  = nullptr; // +32 (0 => linear-fill path)
    i32   leftClampX = 0;     // +36
};
// Output: the clamped destination rect [x0,y0,x1,y1] the fill targets.
struct SurfaceFillRect {
    i32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool issued = false;
};
i32 SurfaceColorFillRect(SurfaceView* surface, i32 x, i32 y, i32 h, i32 w,
                         i32 color, SurfaceFillRect* outRect);

// ---------------------------------------------------------------------------
// 0x428a84 — VIBE_Render_DrawTextLabels3D (int __cdecl(frame, count, labels...)).
// For each of `count` labels: copy the label name, resolve it via findByHandle;
// if resolved and visible, compute camera-relative coordinates (posA - cam world
// pos, posB - cam trans) into the entry list. Builds the anim descriptor and
// installs it via createObjectAnim. We expose the pure coordinate computation as
// `ComputeLabelEntries` (used by the e2e/unit golden tests); `DrawTextLabels3D`
// itself drives the full flow through the hooks.
//
// Returns the count of resolved+visible labels written into `out` (capacity
// `cap`). The camera-relative math is `out[k].a = obj.posA - cam.worldPos`,
// `out[k].b = obj.posB - cam.trans`.
i32 ComputeLabelEntries(const CameraView& cam, const LabelObject* labels,
                        i32 count, LabelEntry* out, i32 cap);

// ---------------------------------------------------------------------------
// 0x428d30 — VIBE_Render_DrawObjectMarkers3D.
// Like the label path but the per-object world position comes from the
// transformChain hook (bone-chain transform of the object), then has the camera
// world pos / trans subtracted. We expose the deterministic subtract as
// `ComputeMarkerEntry`: given a transformed world point and the camera basis,
// produce the camera-relative (a=worldPos-cam.trans subtract +132 terms,
// b=worldPos-cam.worldPos subtract +76 terms) entry exactly as the original.
LabelEntry ComputeMarkerEntry(const CameraView& cam, const float worldPosA[3],
                              const float worldPosB[3]);

} // namespace guild::render
