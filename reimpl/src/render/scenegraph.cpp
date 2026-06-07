#include "render/scenegraph.h"

namespace guild::render {

// gilde.exe 0x5ad1f4 — VIBE_Render_ClassifyBoundingBoxPlanes
//
// Per corner, build a 6-bit outcode (the original's bit-OR chain with the
// progressive masks 0xDF/0xCF/0xC7/0xC3 preserved verbatim):
//   bit5 (0x20): z > farZ        bit4 (0x10): z < nearZ
//   bit3 (0x08): plane3 . p < d3 bit2 (0x04): plane2 . p < d2
//   bit1 (0x02): plane1 . p < d1 bit0 (0x01): plane0 . p < d0
// v24 ORs the codes (any corner outside that plane); v23 ANDs them (all corners
// outside that plane). If the AND over the low 6 bits is nonzero the box is fully
// outside one plane => bit6 (0x40) set. The returned byte keeps bits 0..5 of the
// OR plus the 0x40 fully-out flag (mask 0xBF clears the stale 0x40 from the OR).
u8 ClassifyBoundingBoxPlanes(const float* corners, const Frustum& f,
                             float* minZOut, float* maxZOut,
                             float* runningNear, float* runningFar) {
    const float* v5 = corners;
    u8 v23 = 63;  // AND accumulator (all low bits set)
    u8 v24 = 0;   // OR accumulator
    u8 v7 = 0;

    for (int corner = 0; corner < 8; ++corner) {
        float px = v5[0], py = v5[1], pz = v5[2];
        v7 = (u8)(
              (32 * (pz > (double)f.farZ))
            | ((16 * (pz < (double)f.nearZ)) & 0xDF)
            | ((8 * (px * f.plane[3][0] + py * f.plane[3][1] + pz * f.plane[3][2] < f.plane[3][3])) & 0xCF)
            | ((4 * (px * f.plane[2][0] + py * f.plane[2][1] + pz * f.plane[2][2] < f.plane[2][3])) & 0xC7)
            | ((2 * (px * f.plane[1][0] + py * f.plane[1][1] + pz * f.plane[1][2] < f.plane[1][3])) & 0xC3)
            | (u8)(px * f.plane[0][0] + py * f.plane[0][1] + pz * f.plane[0][2] < f.plane[0][3]));
        v24 = (u8)(v24 | v7);
        v23 = (u8)(v23 & v7);
        v5 += 20;
    }

    u8 v8 = (u8)(v7 | v24);
    u8 v9 = (u8)(v7 & v23);
    u8 v10 = (u8)((v9 != 0) << 6);
    u8 result = (u8)(v10 | (v8 & 0xBF));

    // Not fully culled: compute min/max corner z and expand running bounds.
    if ((v10 & 0x40) == 0) {
        float minZ = 1.0e10f;
        float maxZ = -1.0e10f;
        const float* v11 = corners;
        for (int i = 0; i < 8; ++i) {
            float z = v11[2];
            minZ = (minZ < (double)z) ? minZ : z;
            maxZ = (maxZ <= (double)z) ? z : maxZ;
            v11 += 20;
        }
        if (minZOut) {
            *minZOut = minZ;
            if (runningNear && (double)*runningNear > minZ)
                *runningNear = minZ;
        }
        if (maxZOut) {
            *maxZOut = maxZ;
            if (runningFar && (double)*runningFar < maxZ)
                *runningFar = maxZ;
        }
    }
    return result;
}

// gilde.exe 0x5f0664 — VIBE_SceneGraph_TransformNodeBoxCorners
//
// box[4..6] = max corner, box[8..10] = min corner, both relative to `origin`.
//   ax = box[4]-origin[0]  ay = box[5]-origin[1]  az = box[6]-origin[2]   (max)
//   bx = box[8]-origin[0]  by = box[9]-origin[1]  bz = box[10]-origin[2]  (min)
// Corner 0 = (ax,ay,az) transformed WITH the translation row matrix[12..14];
// the other 7 corners are transformed WITHOUT translation (engine quirk preserved).
// Output corners are written at stride 20 floats.
float* TransformNodeBoxCorners(const float* box, const float* origin,
                               const float* m, float* out) {
    float ax = box[4] - origin[0];
    float ay = box[5] - origin[1];
    float az = box[6] - origin[2];
    float bx = box[8] - origin[0];
    float by = box[9] - origin[1];
    float bz = box[10] - origin[2];

    auto xf = [&](float x, float y, float z, bool translate, float* dst) {
        dst[0] = x * m[0] + y * m[4] + z * m[8] + (translate ? m[12] : 0.0f);
        dst[1] = x * m[1] + y * m[5] + z * m[9] + (translate ? m[13] : 0.0f);
        dst[2] = x * m[2] + y * m[6] + z * m[10] + (translate ? m[14] : 0.0f);
    };

    // 8 corners in the exact order the original emitted them (stride 20 floats):
    //   0:(ax,ay,az)+T  1:(bx,ay,az)  2:(ax,by,az)  3:(bx,by,az)
    //   4:(ax,ay,bz)    5:(bx,ay,bz)  6:(ax,by,bz)  7:(bx,by,bz)
    xf(ax, ay, az, true,  out +   0);
    xf(bx, ay, az, false, out +  20);
    xf(ax, by, az, false, out +  40);
    xf(bx, by, az, false, out +  60);
    xf(ax, ay, bz, false, out +  80);
    xf(bx, ay, bz, false, out + 100);
    xf(ax, by, bz, false, out + 120);
    xf(bx, by, bz, false, out + 140);
    return out;
}

// gilde.exe 0x5f09f0 — VIBE_SceneGraph_CullOctreeAgainstFrustum
//   if (node): classify; if (0x40 fully-out fast path) stamp the leaf mesh list
//   with the visible tag; else if any low bit set recurse into octant children.
char CullOctreeAgainstFrustum(void* node, void* ctx, int visibleTag,
                              const CullCallbacks& cb) {
    if (!node)
        return 0;

    u8 code = cb.classify(node, ctx, *cb.frustum);
    if ((code & 0x40) != 0) {
        cb.stampLeaf(node, visibleTag);
    } else if ((code & 0x3F) != 0) {
        for (int i = 0; i < 4; ++i) {
            void* ch = cb.child(node, i);
            if (ch)
                code = (u8)CullOctreeAgainstFrustum(ch, ctx, visibleTag, cb);
        }
    }
    return (char)code;
}

} // namespace guild::render
