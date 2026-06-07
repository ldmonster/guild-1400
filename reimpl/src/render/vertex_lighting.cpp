#include "render/vertex_lighting.h"

#include "util/math.h"  // VectorNormalize (VIBE_Math_VectorNormalize @0x5cb148)

namespace guild::render {

namespace {
// Exact recovered constants (little-endian float bytes via get_bytes).
constexpr float kReflectCoeff = -2.0f;       // flt_628CBC = 0xC0000000
constexpr float kUvScaleBias  = 0.5f;        // flt_628CC0 = 0x3F000000
constexpr float kSkinScale    = 0.007843138f;// flt_628CC4 = 0x3C008081 (2/255)
constexpr float kSkinBias     = -128.0f;     // flt_628CC8 = 0xC3000000
} // namespace

// gilde.exe 0x5c9054 — VIBE_Mesh_ComputeVertexLighting (env-map reflection UV core)
//   v32 = n.x*m0 + n.y*m4 + n.z*m8;  (m3x3 passed as {m0,m1,m2, m4,m5,m6, m8,m9,m10})
//   v33 = n.x*m1 + n.y*m5 + n.z*m9;
//   v34 = n.x*m2 + n.y*m6 + n.z*m10;
//   v18 = (v32*pos.x + v33*pos.y + v34*pos.z) * (-2.0);
//   v29 = v18*v32 + pos.x;  v30 = v18*v33 + pos.y;  v31 = v18*v34 + pos.z;
//   VectorNormalize(&v29);
//   u = v29*0.5 + 0.5;  v = 0.5 + v30*0.5;
void ComputeEnvMapReflectionUv(const float* pos, const float* normal,
                               const float* m3x3, float* outUv) {
    // m3x3 layout: [0..2] = {m0,m1,m2}, [3..5] = {m4,m5,m6}, [6..8] = {m8,m9,m10}.
    double nt0 = (double)normal[0] * m3x3[0] + (double)normal[1] * m3x3[3] + (double)normal[2] * m3x3[6];
    double nt1 = (double)normal[0] * m3x3[1] + (double)normal[1] * m3x3[4] + (double)normal[2] * m3x3[7];
    double nt2 = (double)normal[0] * m3x3[2] + (double)normal[1] * m3x3[5] + (double)normal[2] * m3x3[8];

    double dot = (nt0 * pos[0] + nt1 * pos[1] + nt2 * pos[2]) * kReflectCoeff;  // v18

    float r[3];
    r[0] = (float)(dot * nt0 + pos[0]);  // v29
    r[1] = (float)(dot * nt1 + pos[1]);  // v30
    r[2] = (float)(dot * nt2 + pos[2]);  // v31
    guild::util::VectorNormalize(r);

    outUv[0] = (float)((double)r[0] * kUvScaleBias + kUvScaleBias);
    outUv[1] = (float)((double)kUvScaleBias + (double)r[1] * kUvScaleBias);
}

// gilde.exe 0x5c9054 — skin-normal unpack: n[i] = ((__int16)byte + (-128.0)) * (2/255).
void UnpackSkinNormal(const u8* bytes, float* outNormal) {
    outNormal[0] = (float)(((double)(i16)bytes[0] + kSkinBias) * kSkinScale);
    outNormal[1] = (float)(((double)(i16)bytes[1] + kSkinBias) * kSkinScale);
    outNormal[2] = (float)((kSkinBias + (double)(i16)bytes[2]) * kSkinScale);
}

// gilde.exe 0x5c953c — morph blend kernel.
//   v78 = ((double)p0.x * scale0.x + bias0.x) * w0;   (and y/z)
//   v44 = ((double)p1.x * scale1.x + bias1.x) * w1;   (and y/z)
//   blended = v78 + v44 (component-wise), then transform by the world matrix.
void InterpolateMorphVertex(const i16* p0, const float* scale0, const float* bias0,
                            const i16* p1, const float* scale1, const float* bias1,
                            float w0, float w1, const float* world, float* outPos) {
    float blended[3];
    blended[0] = (float)((((double)p0[0] * scale0[0] + bias0[0]) * w0)
                       + (((double)p1[0] * scale1[0] + bias1[0]) * w1));
    blended[1] = (float)((((double)p0[1] * scale0[1] + bias0[1]) * w0)
                       + (((double)p1[1] * scale1[1] + bias1[1]) * w1));
    blended[2] = (float)((((double)p0[2] * scale0[2] + bias0[2]) * w0)
                       + (((double)p1[2] * scale1[2] + bias1[2]) * w1));
    TransformPointByWorldMatrix(blended, world, outPos);
}

// gilde.exe 0x5c953c — flat 16-float world-matrix point transform (the `*v7 * *v6 +
//   v7[1]*v6[4] + v7[2]*v6[8] + v6[12]` idiom, per output axis).
void TransformPointByWorldMatrix(const float* p, const float* world, float* out) {
    double x = (double)p[0] * world[0] + (double)p[1] * world[4] + (double)p[2] * world[8]  + world[12];
    double y = (double)p[0] * world[1] + (double)p[1] * world[5] + (double)p[2] * world[9]  + world[13];
    double z = (double)p[0] * world[2] + (double)p[1] * world[6] + (double)p[2] * world[10] + world[14];
    out[0] = (float)x;
    out[1] = (float)y;
    out[2] = (float)z;
}

} // namespace guild::render
