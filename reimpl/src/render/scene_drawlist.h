#pragma once
// =============================================================================
// guild::render — backend-neutral 3D SCENE DRAW LIST.
//
// The engine's 3D scene path is fixed-function DirectDraw/Direct3D: a transform &
// lighting stage (the parent-composed world transform + the per-vertex baked light
// cache + the D3DVIEWPORT2 projection) feeding a textured, z-buffered rasteriser.
// Rule 3 lets us swap the *rasteriser's GPU API* (DirectDraw -> Vulkan) while the
// transform/lighting/projection MATH stays the reconstructed 1:1 engine math.
//
// This struct is the seam: play::BuildSceneDrawList runs the exact engine T&L
// (world transform, baked vertex colours, the camera basis + projection params)
// and emits this list; then EITHER the CPU rasteriser (RasterizeDrawList, the
// reference path used headless + in tests) OR the Vulkan backend
// (IGraphicsDevice::renderScene3D) consumes the SAME list, so both project and
// shade identically. The per-vertex colour folds both shading modes:
//   * baked lighting -> colour = bakedVertexColour/255 (Gouraud across the tri);
//   * flat shading    -> colour = faceShade*baseColour/255 (textured: grey shade).
// The fragment result is always texel * colour (texel = white when texId < 0).
// Texture wrap is per-axis floor(uv*w) mod w, NEAREST (== SampleTexel == a Vulkan
// REPEAT+NEAREST sampler), so no special addressing is needed on the GPU.
// =============================================================================
#include "guild/common/types.h"

#include <cstdint>
#include <vector>

namespace guild::render {

struct Surface;

// One ready-to-raster vertex: world-space position, a 0..1 RGB modulator, a UV.
struct SceneDrawVertex {
    float pos[3];     // world space (projected per-frame by the camera params)
    float color[3];   // 0..1 modulator (multiplies the texel; texel = white if untextured)
    float uv[2];      // raw repeating UV (perspective-correct interpolated, then wrapped)
};

// texId sentinel for a SHADOW batch: the triangles DARKEN the receiver (dest *= the
// interpolated vertex colour) instead of painting it — a projected contact-shadow
// pass. Two-sided (no backface cull), depth-tested like opaque geometry.
inline constexpr int kSceneShadowBatch = -2;

// Depth-test slack (fraction of view-z): a new opaque fragment must be more than
// z*kCoplanarSlack closer than the stored depth to overwrite it. Stabilises the
// winner between near-coplanar surfaces (furniture flush against a wall) so they do
// not shimmer as the camera moves. Constant in screen/NDC terms (scales with depth).
inline constexpr float kCoplanarSlack = 0.0025f;

// Per-batch blend mode — gilde.exe 0x5e0358 VIBE_Render_SetBlendMode, driven by the
// .bgf material's transparency byte (VIBE_Model_LoadFastChunk @0x5F87B8, byte0/v62):
//   0 = OPAQUE   — SRCBLEND=ONE, DESTBLEND=ZERO, z-write on (the default mesh path).
//   1 = ALPHA    — material byte0==1: ALPHABLENDENABLE, normal alpha blend, z-write off.
//   2 = ADDITIVE — material byte0==2: ALPHABLENDENABLE, additive (dst += src), z-write
//                  OFF, ZBIAS, FOGENABLE off. Used by the window light shaft
//                  (LICHTKEGEL: gl_Fenster + fx_stau dust motes) and the candle flame
//                  (fx_feua). `opacity` (material byte1 / 255) scales the added source.
// Transparent batches (blend != 0) are emitted AFTER all opaque + shadow geometry, are
// depth-TESTED against the opaque z-buffer but do NOT write depth (so the glow layers
// without occluding, and is occluded by closer walls).
enum SceneBlendMode { kBlendOpaque = 0, kBlendAlpha = 1, kBlendAdditive = 2 };

// A run of triangles (3*n consecutive verts, non-indexed) sharing one texture.
// texId indexes the list's `textures`, or -1 for untextured (flat/baked colour only),
// or kSceneShadowBatch (-2) for the darkening shadow pass.
struct SceneDrawBatch {
    int firstVertex = 0;
    int vertexCount = 0;
    int texId = -1;
    int blend = kBlendOpaque;   // SceneBlendMode
    float opacity = 1.0f;       // material byte1/255 — scales the source for blend != 0
};

// A decoded texture (RGBA8, top-down) — a copy of the resolved DecodedBmp pixels.
// `mips` (built on demand) holds the downsampled chain (mips[k] = level k+1, each a
// box-filtered half of the previous), used for mipmapped minification (anti-aliasing
// the texel shimmer of detailed surfaces under camera motion).
struct SceneDrawTexture {
    int w = 0, h = 0;
    std::vector<std::uint32_t> argb;                       // level 0 (0xAARRGGBB)
    std::vector<std::vector<std::uint32_t>> mips;          // level 1..N (square pow2)
};

// Build the box-filtered mip chain for `t` (down to 1x1). No-op if already built or
// the base is not a usable size. Square power-of-two textures (the shipped assets).
void BuildSceneTextureMips(SceneDrawTexture& t);

// Everything render::ProjectViewPoint + the camera basis need, captured once so any
// backend reproduces RenderSceneObjects' projection exactly.
struct SceneDrawCamera {
    float eye[3]   = {0, 0, 0};
    float right[3] = {1, 0, 0};
    float up[3]    = {0, 1, 0};
    float fwd[3]   = {0, 0, 1};
    float nearZ = 0.05f;
    float farZ  = 4000.0f;   // depth-normalization reference for the GPU path (the CPU
                             // raster compares raw view-z; the GPU writes view-z/farZ)
    // engine projection (D3Projection) terms:
    float q = 1.0f;
    float clipX = -1.0f, clipWidth = 2.0f, clipY = 0.0f, clipHeight = 0.0f;
    float originX = 0.0f, originY = 0.0f, width = 0.0f, height = 0.0f;
    bool  engineProjection = true;
    // fallback projection (engineProjection == false): screen = f(fproj, aspect).
    float fproj = 1.0f, aspect = 1.0f;
};

struct Scene3DDrawList {
    std::vector<SceneDrawVertex>  verts;
    std::vector<SceneDrawBatch>   batches;
    std::vector<SceneDrawTexture> textures;
    SceneDrawCamera cam;
    int  backfaceCull = 0;            // 0 none, 1 drop area<0, 2 drop area>0
    bool clearFirst = true;
    std::uint8_t clearR = 0, clearG = 0, clearB = 0;
    int  width = 0, height = 0;
    bool bilinear = false;            // bilinear texture filtering (else NEAREST)
    // Bumped by the caller whenever `verts`/`textures` change (NOT for camera-only
    // updates). A GPU backend keys its cached vertex/texture uploads on this, so a
    // pure camera move re-records with new push constants and skips re-uploading.
    unsigned geometryId = 0;
};

struct DrawListRasterStats { int trianglesDrawn = 0; int pixelsWritten = 0; };

// Reference CPU rasteriser for a Scene3DDrawList: projects each vertex with the
// list's camera (render::ProjectViewPoint when engineProjection), screen-winding
// backface-culls, depth-tests on the (linearly interpolated) view-space z, and
// writes texel*colour with perspective-correct UV + per-axis REPEAT/NEAREST wrap.
// This is byte-identical to the path RenderSceneObjects used before it was split,
// and is the oracle the Vulkan pipeline is validated against. `fb` 16/32 bpp.
DrawListRasterStats RasterizeDrawList(const Scene3DDrawList& dl, Surface* fb);

} // namespace guild::render
