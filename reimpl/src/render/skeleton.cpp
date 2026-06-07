#include "render/skeleton.h"

#include "util/transform.h"  // RotateVectorByHierarchy (bone-chain rotation walker)
#include "util/matrix.h"     // MatrixIdentity, MatrixTransformVectors
#include "util/coord.h"      // ConvertX (x87 truncate-toward-zero) — used by callers

namespace guild::render {

// The bone "frame" record is addressed by raw byte offsets in the original. We view
// the base as float* and read fields by float index (byte/4). The parent-frame link
// is a pointer stored at byte 504 (float index 126).
namespace {

inline float* ParentLink(float* frame) {
    float** slot = reinterpret_cast<float**>(reinterpret_cast<char*>(frame) + 504);
    return *slot;
}

// Byte access into a frame record (the +444/+448/+452 scratch triple, +528 sign).
inline float* ByteFloat(float* frame, int byteOff) {
    return reinterpret_cast<float*>(reinterpret_cast<char*>(frame) + byteOff);
}

} // namespace

// gilde.exe 0x5ccf18 — VIBE_Anim_AdvanceFrameIndex
//   v6 = cur - 1;
//   if (flags & 2) { return cur <= first ? first+1 : v6; }   // reverse leg
//   v8 = cur + 1;
//   if (cur < last) return v8;                               // mid-run: step forward
//   if (!(flags & 0x10)) { return (flags & 1) ? v6 : first; }// hold/loop at end
//   result = count - 1; return v8 <= count-1 ? v8 : result;  // clamp-to-count
i32 AdvanceFrameIndex(u8 flags, i32 cur, i32 last, i32 first, i32 count) {
    i32 v6 = cur - 1;
    if ((flags & 2) != 0) {
        if (cur <= first)
            return first + 1;
        return v6;
    }
    i32 v8 = cur + 1;
    if (cur < last)
        return v8;
    if ((flags & 0x10) == 0) {
        if ((flags & 1) == 0)
            return first;
        return v6;
    }
    i32 result = count - 1;
    if (v8 <= count - 1)
        return v8;
    return result;
}

// gilde.exe 0x5cbc10 — VIBE_Anim_InterpolateBoneFrame (translation-accumulation core)
//
// Reproduces the three accumulation phases of the original. In the original `v5+348`
// is the frame data array and v28 = duration of the `toFrame` segment, `result` =
// duration of the `fromFrame` segment; here those come straight from
// frames[k].duration. The trailing-segment phase ratio (v32) is:
//   - if fromFrame == toFrame:  (phaseEnd - phaseNum) / dur(to)
//   - else:                      phaseEnd            / dur(to)
// (the original's v36/v37 split: `if (v35 == v27) { num = v31 - v29; den = v28; }`).
void InterpolateBoneFrame(const AnimFrame* frames, const float* bone,
                          i32 fromFrame, i32 toFrame, i32 phaseNum, i32 phaseEnd,
                          float* out) {
    float ax = 0.0f, ay = 0.0f, az = 0.0f;

    // Phase 1: leading partial segment (original: v30 = 1 - phaseNum/dur(from)).
    if (toFrame > fromFrame) {
        double durFrom = (double)frames[fromFrame].duration;
        double lead = 1.0 - (double)phaseNum / durFrom;  // v30
        ax = (float)((frames[fromFrame + 1].tx - frames[fromFrame].tx) * lead);
        ay = (float)((frames[fromFrame + 1].ty - frames[fromFrame].ty) * lead);
        az = (float)((frames[fromFrame + 1].tz - frames[fromFrame].tz) * lead);

        // Phase 2: whole inner segments (fromFrame+1 .. toFrame-1), added in full.
        for (int k = fromFrame + 1; k < toFrame; ++k) {
            ax += frames[k + 1].tx - frames[k].tx;
            ay += frames[k + 1].ty - frames[k].ty;
            az += frames[k + 1].tz - frames[k].tz;
        }
    }

    // Phase 3: trailing partial segment at toFrame (original: v32 = num/dur(to)).
    {
        double durTo = (double)frames[toFrame].duration;
        double num = (fromFrame == toFrame) ? (double)(phaseEnd - phaseNum)
                                            : (double)phaseEnd;
        double trail = num / durTo;  // v32
        ax += (float)((frames[toFrame + 1].tx - frames[toFrame].tx) * trail);
        ay += (float)((frames[toFrame + 1].ty - frames[toFrame].ty) * trail);
        az += (float)((frames[toFrame + 1].tz - frames[toFrame].tz) * trail);
    }

    // Rotate the accumulated delta by the bone's local 3x3 (column-major read at
    // float indices 99/103/107 | 100/104/108 | 101/105/109), then add the bone's
    // base translation (indices 19/20/21). Matches v18/v19 + v33[19..21].
    double rx = (double)ax * bone[99]  + (double)ay * bone[103] + (double)az * bone[107];
    double ry = (double)ax * bone[100] + (double)ay * bone[104] + (double)az * bone[108];
    double rz = (double)ax * bone[101] + (double)ay * bone[105] + (double)az * bone[109];
    out[0] = (float)(rx + bone[19]);
    out[1] = (float)(ry + bone[20]);
    out[2] = (float)(rz + bone[21]);
}

// gilde.exe 0x5c8eb4 — VIBE_Transform_AccumulateBoneMatrices
float* AccumulateBoneMatrices(float* frame, u8 isRoot, float* out, float* acc) {
    float* parent = ParentLink(frame);  // v4 = *(frame + 504)
    if (isRoot) {
        // Root: zero the +444/+448/+452 scratch triple.
        *ByteFloat(frame, 444) = 0.0f;
        *ByteFloat(frame, 448) = 0.0f;
        *ByteFloat(frame, 452) = 0.0f;
    } else {
        // Offset the accumulator's translation column (acc[12..14]) by this bone's
        // pivot (bytes 108/112/116 == idx 27/28/29).
        acc[12] = *ByteFloat(frame, 108) + acc[12];
        acc[13] = *ByteFloat(frame, 112) + acc[13];
        acc[14] = *ByteFloat(frame, 116) + acc[14];

        // Build the +444 scratch = local(120/124/128) + (base(76/80/84) - pivot(112/116))
        // exactly as the original's v5..v12 FPU shuffle.
        float v5 = *ByteFloat(frame, 120);
        float v6 = *ByteFloat(frame, 124);
        float v7 = *ByteFloat(frame, 128);
        float v8 = *ByteFloat(frame, 80) - *ByteFloat(frame, 112);
        float v9 = *ByteFloat(frame, 84) - *ByteFloat(frame, 116);
        *ByteFloat(frame, 444) = *ByteFloat(frame, 76) - *ByteFloat(frame, 108);
        *ByteFloat(frame, 448) = v8;
        *ByteFloat(frame, 452) = v9;
        *ByteFloat(frame, 444) = v5 + *ByteFloat(frame, 444);
        *ByteFloat(frame, 448) = v6 + *ByteFloat(frame, 448);
        *ByteFloat(frame, 452) = v7 + *ByteFloat(frame, 452);
    }

    float* local = ByteFloat(frame, 396);  // the bone's 4x4 at float index 99
    if (!parent)
        return guild::util::MatrixTransformVectors(acc, local, out);

    float tmp[16];
    guild::util::MatrixTransformVectors(acc, local, tmp);
    return AccumulateBoneMatrices(parent, isRoot, out, tmp);
}

// gilde.exe 0x5c8fac — VIBE_Transform_ComputeBoneWorldMatrix
float* ComputeBoneWorldMatrix(float* frame, const float* pivot, u8 isRoot,
                              float* out) {
    float ident[16];
    guild::util::MatrixIdentity(ident);
    if (!pivot)
        return AccumulateBoneMatrices(frame, isRoot, out, ident);

    float acc[16];
    AccumulateBoneMatrices(frame, isRoot, acc, ident);
    // Subtract the pivot's base (idx 19/20/21) then local (idx 30/31/32) translation
    // from the accumulated translation column (acc[12..14] == var_1C/18/14).
    acc[12] = acc[12] - pivot[19];
    acc[13] = acc[13] - pivot[20];
    acc[14] = acc[14] - pivot[21];
    acc[12] = acc[12] - pivot[30];
    acc[13] = acc[13] - pivot[31];
    acc[14] = acc[14] - pivot[32];
    // Re-apply the pivot's own 3x3 (pivot + 99 floats == byte 396).
    return guild::util::MatrixTransformVectors(acc, pivot + 99, out);
}

// gilde.exe 0x5c8ab4 — VIBE_Transform_RotateVectorWithFrame
float* RotateVectorWithFrame(float* frame, i8 signByte, const float* basis,
                             const float* vec, float* out) {
    // Original: if (*(char*)(frame+528) < 0 || !basis) return RotateVectorByHierarchy(frame, vec, out);
    if (signByte < 0 || !basis)
        return guild::util::RotateVectorByHierarchy(frame, vec, out);

    float r[3];
    guild::util::RotateVectorByHierarchy(frame, vec, r);
    // Additionally rotate by `basis`'s 3x3 (column-major: idx 99/103/107 | ...).
    double y = (double)r[0] * basis[100] + (double)r[1] * basis[104] + (double)r[2] * basis[108];
    double z = (double)r[0] * basis[101] + (double)r[1] * basis[105] + (double)r[2] * basis[109];
    out[0] = (float)((double)r[0] * basis[99] + (double)r[1] * basis[103] + (double)r[2] * basis[107]);
    out[1] = (float)y;
    out[2] = (float)z;
    return out;
}

} // namespace guild::render
