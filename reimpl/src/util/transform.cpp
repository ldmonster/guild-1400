#include "util/transform.h"

namespace guild::util {

// The frame entity is addressed by raw byte offsets in the original. We model the
// base as float* and read fields by float index (byte/4). The parent-chain link is
// a pointer stored at byte 504 (float index 126); we read it as an embedded float*.
namespace {

// Read the parent-frame pointer embedded at byte 504 of a frame.
inline float* ParentLink(float* frame) {
    float** slot = reinterpret_cast<float**>(reinterpret_cast<char*>(frame) + 504);
    return *slot;
}

} // namespace

// gilde.exe 0x5c8b38 — VIBE_Transform_PointThroughBoneChain
//   out = point + frame[30..32];
//   for (i = parent(frame); i; i = parent(i)) {
//     t = out + i[27..29](bytes 108/112/116);
//     out = (3x3 at i[99/103/107 | 100/104/108 | 101/105/109]) * t;
//     out -= i[27..29];  out += i[19..21](bytes 76/80/84);  out += i[30..32](120/124/128);
//   }
float* PointThroughBoneChain(float* frame, const float* point, float* out) {
    // Original: *out = *point + frame[30]; ... (frame[30..32] == bytes 120/124/128).
    out[0] = point[0] + frame[30];
    out[1] = point[1] + frame[31];
    out[2] = point[2] + frame[32];

    float* result = out;
    for (float* i = ParentLink(frame); i; i = ParentLink(i)) {
        float t0 = out[0] + i[27];   // bytes 108/112/116 == float 27/28/29
        float t1 = out[1] + i[28];
        float t2 = out[2] + i[29];
        // 3x3 rotation: rows use i[99],i[103],i[107] / i[100],i[104],i[108] /
        // i[101],i[105],i[109] (bytes 396/412/428, 400/416/432, 404/420/436).
        double r0 = (double)t0 * i[99]  + (double)t1 * i[103] + (double)t2 * i[107];
        double r1 = (double)t0 * i[100] + (double)t1 * i[104] + (double)t2 * i[108];
        double r2 = (double)t0 * i[101] + (double)t1 * i[105] + (double)t2 * i[109];
        out[0] = (float)r0;
        out[1] = (float)r1;
        out[2] = (float)r2;
        out[0] -= i[27];
        out[1] -= i[28];
        out[2] -= i[29];
        out[0] += i[19];   // bytes 76/80/84
        out[1] += i[20];
        out[2] += i[21];
        out[0] += i[30];   // bytes 120/124/128
        out[1] += i[31];
        out[2] += i[32];
        result = out;
    }
    return result;
}

// gilde.exe 0x5c8d0c — VIBE_Transform_PointThroughBoneChainPivot
//   tmp = point + frame[27..29](bytes 108/112/116);  -- wait: original uses a1[27..29]
//   tmp = (3x3 at frame[99/103/107|100/104/108|101/105/109]) * tmp;
//   tmp -= frame[27..29];  tmp += frame[19..21];  return PointThroughBoneChain(frame, tmp, out).
float* PointThroughBoneChainPivot(float* frame, const float* point, float* out) {
    float t0 = point[0] + frame[27];
    float t1 = point[1] + frame[28];
    float t2 = point[2] + frame[29];
    double r0 = (double)t0 * frame[99]  + (double)t1 * frame[103] + (double)t2 * frame[107];
    double r1 = (double)t0 * frame[100] + (double)t1 * frame[104] + (double)t2 * frame[108];
    double r2 = (double)t0 * frame[101] + (double)t1 * frame[105] + (double)t2 * frame[109];
    float tmp[3];
    tmp[0] = (float)r0;
    tmp[1] = (float)r1;
    tmp[2] = (float)r2;
    tmp[0] -= frame[27];
    tmp[1] -= frame[28];
    tmp[2] -= frame[29];
    tmp[0] += frame[19];
    tmp[1] += frame[20];
    tmp[2] += frame[21];
    return PointThroughBoneChain(frame, tmp, out);
}

// gilde.exe 0x5c8990 — VIBE_Transform_RotateVectorByHierarchy
//   out = (3x3 of frame: cols frame[99..101]/[103..105]/[107..109]) * vec;
//   for (i = parent(frame); i; i = parent(i)) out = (3x3 of i) * out;  return last.
//   The frame's own 3x3 here uses indices 99/100/101 | 103/104/105 | 107/108/109
//   (bytes 396/400/404, 412/416/420, 428/432/436) — the column-major read.
float* RotateVectorByHierarchy(float* frame, const float* vec, float* out) {
    double y = (double)vec[0] * frame[100] + (double)vec[1] * frame[104] + (double)vec[2] * frame[108];
    double z = (double)vec[0] * frame[101] + (double)vec[1] * frame[105] + (double)vec[2] * frame[109];
    out[0] = (float)((double)vec[0] * frame[99] + (double)vec[1] * frame[103] + (double)vec[2] * frame[107]);
    out[1] = (float)y;
    out[2] = (float)z;

    float* result = out;
    for (float* v5 = ParentLink(frame); v5; v5 = ParentLink(v5)) {
        float n0 = (float)((double)out[0] * v5[99]  + (double)out[1] * v5[103] + (double)out[2] * v5[107]);
        float n1 = (float)((double)out[0] * v5[100] + (double)out[1] * v5[104] + (double)out[2] * v5[108]);
        float n2 = (float)((double)out[0] * v5[101] + (double)out[1] * v5[105] + (double)out[2] * v5[109]);
        out[0] = n0;
        out[1] = n1;
        out[2] = n2;
        result = out;
    }
    return result;
}

} // namespace guild::util
