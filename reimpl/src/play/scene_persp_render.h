#pragma once
// =============================================================================
// guild::play — PERSPECTIVE scene-mesh renderer (host-side).
//
// The engine's perspective scene path (VIBE_Render_BuildViewMatrix @0x5accd0 +
// VIBE_Render_SetProjectionTransform @0x5de3e4 + the textured perspective raster)
// is NOT reconstructed in the reimpl — the reconstructed projections
// (VIBE_Mesh_ProjectVerticesToScreen @0x5c5120, VIBE_Coord_ProjectPoint @0x407428)
// are the engine's TOP-DOWN city/tile view, which cannot render an upright 3D scene
// like Menu/ChooseCity.ed3 (it backface-culls vertical geometry away).
//
// This module is the host-side perspective renderer the menu's 3D screens use,
// the same way play::RealCityRenderer is the host-side top-down city composition.
// It takes real `render::MeshGeometry` (decoded from the shipped .BGF via the now-
// working bgf_loader), a look-at camera derived from the scene's MegaCam eye/target,
// and rasterizes the triangles with a depth buffer and per-face flat shading into a
// render::Surface (16/32 bpp) that presents through the normal device path.
//
// It is geometry- and camera-accurate (real assets, real MegaCam params); only the
// rasterizer itself is a faithful-equivalent (the engine's exact texel pipeline is
// the separate, unreconstructed d3 raster).
// =============================================================================
#include "guild/common/types.h"

namespace guild::render { struct MeshGeometry; struct Surface; }

namespace guild::play {

// A look-at perspective camera. `up` defaults to +Y. `fovY` in radians.
struct PerspCamera {
    float eye[3]    = {0, 0, 0};
    float target[3] = {0, 0, 0};
    float up[3]     = {0, 1, 0};
    float fovY      = 1.1f;   // ~63 degrees (only used when !engineProjection)
    float nearZ     = 0.05f;
    float farZ      = 4000.0f;
    // When true, project with the engine's exact perspective
    // (render::MakeProjection / VIBE_Render_SetProjectionTransform @0x5de3e4):
    // fixed ~90 deg horizontal FOV, vertical scaled by aspect=H/W, w=z+near.
    // The real ChooseCity scene (VIBE_Menu_RunChooseCity @0x52e6d8) uses this;
    // `fovY` is then ignored. Default off so other host views are unaffected.
    bool  engineProjection = false;
};

struct PerspRenderOptions {
    // Directional light direction (world space, points toward the surface). Flat
    // per-face Lambert shading uses max(0, n·(-light)) folded into [ambient,1].
    float lightDir[3] = {-0.4f, -0.8f, -0.5f};
    float ambient     = 0.35f;
    // Base material colour (R,G,B) the shade scales (warm stone by default).
    u8    baseR = 196, baseG = 160, baseB = 110;
    bool  clearFirst = true;
    u8    clearR = 0x20, clearG = 0x22, clearB = 0x3A;
    // Backface culling by projected winding: 0 = none (two-sided), 1 = drop
    // negative screen-area triangles, 2 = drop positive ones. Needed when the
    // camera sits INSIDE an enclosed room (the wall back-faces must not occlude).
    int   backfaceCull = 0;

    // Baked per-vertex lighting (the engine's VIBE_Light_BuildObjectCache path,
    // rule 3/8): when true, each vertex is lit by `ambientRGB` (the .ed3 rig band)
    // + the scene's light nodes (render::AccumulatePoint/DirectionalLight via the
    // falloff LUT), reduced to a per-vertex colour and Gouraud-interpolated to
    // modulate the texel/material — replacing the flat per-face Lambert above.
    bool  bakedLighting = false;
    float ambientRGB[3] = {64.0f, 64.0f, 64.0f};  // engine 0..255 ambient light

    // Projected contact shadows: each prop's geometry is flattened onto the surface it
    // rests on (its own base plane) along `shadowDir`, and the receiver is darkened by
    // `shadowAlpha` (0=black .. 1=none). Host-side grounding pass over the scene.
    bool  shadows = false;
    float shadowDir[3] = {0.35f, -1.0f, 0.28f};   // light direction the shadow falls along
    float shadowAlpha = 0.5f;                     // receiver darken factor (dest *= this)

    // Bilinear texture filtering instead of the engine's NEAREST point sampling. Off by
    // default (NEAREST == render::SampleTexel, byte-exact). On, it smooths the texel
    // shimmer that NEAREST produces on detailed surfaces as the camera moves.
    bool  bilinear = false;
};

struct PerspRenderStats {
    int trianglesDrawn = 0;   // triangles that wrote at least one pixel
    int pixelsWritten  = 0;   // depth-buffer accepted fragments
};

// Render `mesh` through `cam` into `fb` (already created at fb->width x fb->height).
// Depth-buffered, flat-shaded. Returns per-frame counts. Supports 16bpp and 32bpp
// target surfaces. A null/empty mesh is a no-op (returns {} ).
PerspRenderStats RenderMeshPerspective(const render::MeshGeometry& mesh,
                                       const PerspCamera& cam,
                                       render::Surface* fb,
                                       const PerspRenderOptions& opt = {});

// Convenience: frame `cam` to look at the mesh's bounding-sphere centre from a
// given orbit (azimuth/elevation radians, distance = radius * distFactor). Fills
// cam.eye/target. Returns the bounding radius (0 if the mesh is empty).
float FrameCameraToMesh(const render::MeshGeometry& mesh, PerspCamera& cam,
                        float azimuth, float elevation, float distFactor = 2.6f);

} // namespace guild::play
