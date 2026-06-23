// guild::render — the engine's exact perspective projection. See header.
#include "render/d3_projection.h"

namespace guild::render {

// gilde.exe 0x5de3e4 — VIBE_Render_SetProjectionTransform (math half).
D3Projection MakeProjection(float width, float height, float nearZ, float farZ,
                            float originX, float originY) {
    D3Projection p;
    p.width   = width;
    p.height  = height;
    p.originX = originX;
    p.originY = originY;

    // D3DVIEWPORT2 clip window (v23):
    //   dvClipX = -1, dvClipWidth = 2 ; dvClipY = height/width,
    //   dvClipHeight = (height/width) * flt_62A6F4 (==2.0).
    const float aspect = (width != 0.0f) ? (height / width) : 0.0f;  // v23[6] = a2/a1
    p.clipX      = -1.0f;
    p.clipWidth  =  2.0f;
    p.clipY      = aspect;
    p.clipHeight = aspect * kClipHeightScale;

    // Projection matrix z terms: _33 = near/(far-near)+1 == far/(far-near) == Q.
    p.nearZ = nearZ;
    p.farZ  = farZ;
    const float denom = farZ - nearZ;
    p.q = (denom != 0.0f) ? (nearZ / denom + 1.0f) : 1.0f;   // == far/(far-near)
    return p;
}

// gilde.exe 0x5de3e4 (matrix) + D3DVIEWPORT2 clip-window -> device mapping.
ProjectedPoint ProjectViewPoint(const D3Projection& p, float vx, float vy, float vz) {
    ProjectedPoint r;
    // clip = [x, y, z*Q, z + near]   (row vector * the recovered matrix)
    const float clipX = vx;
    const float clipY = vy;
    const float clipZ = vz * p.q;
    const float w     = vz + p.nearZ;     // _34=1, _44=near -> w = z + near
    r.w = w;
    if (w == 0.0f) return r;              // degenerate; caller clamps to near plane

    const float ndcX = clipX / w;
    const float ndcY = clipY / w;
    r.ndcZ = clipZ / w;

    // D3DVIEWPORT2 maps the clip window to [originX, originX+width] x
    // [originY, originY+height]; dvClipY is the TOP, growing downward.
    r.sx = p.originX + (ndcX - p.clipX) / p.clipWidth  * p.width;
    r.sy = p.originY + (p.clipY - ndcY) / p.clipHeight * p.height;
    return r;
}

} // namespace guild::render
