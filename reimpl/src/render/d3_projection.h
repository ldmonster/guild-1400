#pragma once
// =============================================================================
// guild::render — the engine's exact perspective PROJECTION (rule 3).
//
// gilde.exe 0x5de3e4 — VIBE_Render_SetProjectionTransform(width,height,x,y,near,far)
//
// The original feeds Direct3D a D3DVIEWPORT2 *clip window* plus a partial
// projection matrix; together they define the perspective. Per rule 3 we
// reconstruct that projection math 1:1 and only swap the GPU API underneath
// (the IDirect3DDevice SetViewport/SetTransform calls become "store the params";
// the host rasteriser then applies the recovered mapping).
//
// Recovered verbatim from the decompile:
//   * D3DVIEWPORT2 clip window (44-byte struct v23):
//       dvClipX      = -1.0                       (v23[5] = 0xBF800000)
//       dvClipY      = height / width             (v23[6] = a2/a1)
//       dvClipWidth  =  2.0                        (v23[7] = 0x40000000)
//       dvClipHeight = (height/width) * 2.0        (v23[8] = (a2/a1)*flt_62A6F4)
//     with flt_62A6F4 @0x62A6F4 == 2.0 (read from the binary).  So the clip
//     window is x in [-1, 1] (width 2) and y in [-aspect, +aspect] (aspect=H/W,
//     top = +aspect).  tan(halfFovX)=1 -> a fixed ~90 degree HORIZONTAL FOV; the
//     vertical FOV is scaled by aspect (=H/W), which yields SQUARE pixels at any
//     viewport ratio (2/W == 2*aspect/H  <=>  W/H == 1/aspect).
//   * Projection matrix (16 floats v19, set for D3DTRANSFORMSTATE_PROJECTION):
//       _11 = 1, _22 = 1                           (no x/y scale; FOV is in the
//                                                   clip window above)
//       _33 = near/(far-near) + 1 == far/(far-near) == Q   (v20)
//       _34 = 1                                    (v21 = 0x3F800000)
//       _43 = 0                                    (left zeroed)
//       _44 = near                                 (v22 = a5)
//     so for a view-space row vector [x,y,z,1]:
//       clip = (x, y, z*Q, z + near)               (w = z*_34 + 1*_44 = z + near)
//
// The screen mapping (D3DVIEWPORT2 maps the clip window to the device viewport):
//       ndc = clip.xyz / clip.w
//       sx  = (ndc.x - dvClipX)        / dvClipWidth  * width
//       sy  = (dvClipY - ndc.y)        / dvClipHeight * height
// The screen centre is therefore (width*0.5, height*0.5) — matching the picking
// centre flt_13FCD18 = width*flt_6280DC (=0.5) and flt_13FCD10 = height*0.5 that
// VIBE_Render_SetupViewTransform @0x5af5f8 stores.
//
// VIBE_Menu_RunChooseCity @0x52e6d8 drives this for the city-select scene
// (width/height from dword_69FFBC, x=y=0, near/far from the scene camera globals).
// =============================================================================
#include "guild/common/types.h"

namespace guild::render {

// flt_62A6F4 @0x62A6F4 — the dvClipHeight multiplier (full NDC height). == 2.0.
inline constexpr float kClipHeightScale = 2.0f;
// flt_6280DC @0x6280DC — the viewport screen-centre factor (== 0.5).
inline constexpr float kScreenCentreFactor = 0.5f;

// The recovered projection parameters (the D3DVIEWPORT2 clip window + the matrix
// z terms). Built once per (viewport, near, far) change, exactly as the original
// caches them in dword_64A338.. / flt_64A348..
struct D3Projection {
    // viewport (the device target rectangle)
    float width  = 0.0f;   // a1 (dwWidth)
    float height = 0.0f;   // a2 (dwHeight)
    float originX = 0.0f;  // a3 (dwX)
    float originY = 0.0f;  // a4 (dwY)
    // D3DVIEWPORT2 clip window
    float clipX      = -1.0f;  // dvClipX
    float clipY      =  0.0f;  // dvClipY      = height/width
    float clipWidth  =  2.0f;  // dvClipWidth
    float clipHeight =  0.0f;  // dvClipHeight = (height/width)*2
    // projection matrix z terms
    float nearZ = 0.0f;        // a5  (_44, and the w offset)
    float farZ  = 0.0f;        // a6
    float q     = 0.0f;        // _33 = far/(far-near)
};

// A projected screen point (device pixels) + its NDC depth in [0,1] (post-divide).
struct ProjectedPoint {
    float sx = 0.0f;
    float sy = 0.0f;
    float ndcZ = 0.0f;   // ndc.z = z*Q / (z+near) — the D3D depth-buffer value
    float w = 0.0f;      // clip.w = z + near (>0 in front of the camera)
};

// gilde.exe 0x5de3e4 — build the projection params (the math half of
// VIBE_Render_SetProjectionTransform; the DDraw SetViewport/SetTransform calls
// are the swapped GPU API, rule 3). `width`/`height` are the device viewport
// size; `near`/`far` the clip planes.
D3Projection MakeProjection(float width, float height, float nearZ, float farZ,
                            float originX = 0.0f, float originY = 0.0f);

// Apply the recovered projection to a VIEW-SPACE point (x=right, y=up,
// z=forward depth). Returns device-pixel screen coords + NDC depth. Points with
// w<=0 are behind the near offset; the caller should clamp/cull (the engine
// clips against the near plane before this).
ProjectedPoint ProjectViewPoint(const D3Projection& p, float vx, float vy, float vz);

} // namespace guild::render
