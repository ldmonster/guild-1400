#version 450
// guild — 3D scene vertex shader. Reproduces play::BuildSceneDrawList's exact
// engine transform & projection (the camera basis + render::ProjectViewPoint /
// the D3DVIEWPORT2 mapping), so the Vulkan rasteriser projects vertices 1:1 with
// the CPU reference render::RasterizeDrawList. Rule 3: only the GPU API is swapped.
//
// gl_Position.w is set to the VIEW-SPACE z (vz), so the smooth-qualified UV
// interpolates perspective-correct with the same 1/vz weight the CPU uses, while
// vColor / vViewZ are noperspective (screen-space barycentric) to match the CPU's
// flat Gouraud + linear depth interpolation. Depth is written in the fragment
// shader from vViewZ (the engine z-buffer compares linear view z).

layout(location = 0) in vec3 inPos;     // world-space position
layout(location = 1) in vec3 inColor;   // 0..1 modulator (baked/flat shade)
layout(location = 2) in vec2 inUV;      // repeating texture coords

layout(push_constant) uniform PC {
    vec4 eye;     // xyz = camera eye
    vec4 right;   // xyz = view right basis
    vec4 up;      // xyz = view up basis
    vec4 fwd;     // xyz = view forward basis
    vec4 proj0;   // nearZ, q, clipX, clipWidth
    vec4 proj1;   // clipY, clipHeight, originX, originY
    vec4 proj2;   // projWidth, projHeight, fbW, fbH
    vec4 proj3;   // engineProjection(>0.5), fproj, aspect, farRef
} pc;

layout(location = 0) noperspective out vec3 vColor;
layout(location = 1) smooth        out vec2 vUV;
layout(location = 2) noperspective out float vViewZ;

void main() {
    vec3 d = inPos - pc.eye.xyz;
    float vx = dot(d, pc.right.xyz);
    float vy = dot(d, pc.up.xyz);
    float vz = dot(d, pc.fwd.xyz);
    float nearZ = pc.proj0.x;
    if (vz < nearZ) vz = nearZ;

    float fbW = pc.proj2.z, fbH = pc.proj2.w;
    float sx, sy;
    if (pc.proj3.x > 0.5) {
        // render::ProjectViewPoint (engine D3DVIEWPORT2 mapping).
        float w        = vz + nearZ;
        float ndcX     = vx / w;
        float ndcY     = vy / w;
        float clipX    = pc.proj0.z, clipWidth  = pc.proj0.w;
        float clipY    = pc.proj1.x, clipHeight = pc.proj1.y;
        float originX  = pc.proj1.z, originY    = pc.proj1.w;
        float pW       = pc.proj2.x, pH         = pc.proj2.y;
        sx = originX + (ndcX - clipX) / clipWidth  * pW;
        sy = originY + (clipY - ndcY) / clipHeight * pH;
    } else {
        // Fallback look-at projection (the non-engine host views).
        float fproj = pc.proj3.y, aspect = pc.proj3.z;
        sx = (vx * fproj / aspect / vz * 0.5 + 0.5) * fbW;
        sy = (0.5 - vy * fproj / vz * 0.5) * fbH;
    }

    // Pixel coords -> Vulkan NDC (viewport 0,0,fbW,fbH; top-left origin, y down).
    float ndcx = sx / fbW * 2.0 - 1.0;
    float ndcy = sy / fbH * 2.0 - 1.0;
    gl_Position = vec4(ndcx * vz, ndcy * vz, 0.0, vz);

    vColor = inColor;
    vUV    = inUV;
    vViewZ = vz;
}
