#include "render/node_lod.h"

#include "util/coord.h"   // ConvertX (x87 truncate toward zero)

#include <cmath>

namespace guild::render {

namespace {
// flt_62807C == 0xBF800000 == -1.0  (the "max LOD" fallback bias: lodCount + (-1)).
constexpr float kMaxLodBias = -1.0f;
} // namespace

// gilde.exe 0x5adb6c — VIBE_Mesh_SelectLodFrame.
i32 SelectLodFrame(const LodObject& obj, const LodView& view, bool* outSetCullBit) {
    if (outSetCullBit) *outSetCullBit = false;

    // if (!drawData || !*(drawData+2316)) return 0  (no drawable LOD).
    // (The frames array lives at drawData+244, so drawDataReady && lodCount>0 implies
    // obj.frames != null in the engine; the extra null check is a memory-safety guard
    // that never fires for a well-formed object and keeps the in-bounds path identical.)
    if (!obj.drawDataReady || obj.lodCount == 0 || !obj.frames)
        return -1;

    int index;

    // Forced-LOD branch: (object+531 & 0x30) != 0 || no active world.
    if ((obj.renderFlags & 0x30) != 0 || !view.worldPresent) {
        // v4 = ((u8)(4 * flags) >> 6) - 1; clamp to lodCount-1.
        unsigned int v4 = (unsigned int)(((u8)(4 * obj.renderFlags)) >> 6) - 1;
        if ((unsigned int)(obj.lodCount - 1) < v4)
            v4 = (unsigned int)(obj.lodCount - 1);
        index = (int)v4;
    } else {
        // Distance branch: LOD = sqrt(|obj-cam|^2) * lodCount * fovScale.
        float dx = obj.pos[0] - view.camPos[0];
        float dy = obj.pos[1] - view.camPos[1];
        float dz = obj.pos[2] - view.camPos[2];
        double dist = std::sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
        double lod = dist * (double)obj.lodCount * (double)view.fovScale;  // var_14

        // ConvertX truncates `lod` toward zero and it is stored as int (var_18). The
        // original then compares lodCount (left in eax) against that truncated LOD:
        //   if (lodCount > truncLod)  index = trunc(max(lod, 0))      (near -> low LOD)
        //   else                      index = lodCount + (-1.0)        (far -> max LOD)
        int truncLod = (int)guild::util::ConvertX(lod);  // var_18
        double v14;
        if ((int)obj.lodCount > truncLod) {
            v14 = lod;
            if (v14 < 0.0) v14 = 0.0;   // loc_5ADCEE: if (0 > var_14) var_14 = 0
        } else {
            v14 = (double)obj.lodCount + (double)kMaxLodBias;  // lodCount - 1
        }
        index = (int)guild::util::ConvertX(v14);
    }

    if (index < 0) index = 0;
    if (index >= obj.lodCount) index = obj.lodCount - 1;

    // Validate the chosen frame has polys (frame+8 && frame+12), else return null.
    const LodFrame& fr = obj.frames[index];
    if (fr.polyCount == 0 || fr.polyCap == 0)
        return -1;

    // Set the object +528 |= 0x40 when the frame changed (result != +460) or a
    // forced rebuild (byte_64A068) is pending.
    if (index != obj.currentFrameIndex || view.forceRebuild) {
        if (outSetCullBit) *outSetCullBit = true;
    }
    return index;
}

// gilde.exe 0x5ad1f4 — VIBE_Render_ClassifyBoundingBoxPlanes.
u8 ClassifyBoundingBoxPlanes(const float* corners, const Frustum& fr,
                             float* outMinZ, float* outMaxZ) {
    char andMask = 63;   // v23 = 0x3F (AND of all corner outcodes, low 6 bits)
    char orMask = 0;     // v24 = 0x00 (OR of all corner outcodes)

    for (int c = 0; c < 8; ++c) {
        const float* p = corners + 3 * c;
        float x = p[0], y = p[1], z = p[2];

        // 6-bit outcode: bit0..3 the 4 side planes (outside if a*x+b*y+c*z < d),
        // bit4 z < nearZ, bit5 z > farZ. Mask bytes (0xDF/0xCF/0xC7/0xC3) mirror the
        // original's progressive AND so each plane bit only sets if higher bits did.
        char oc =
            (char)((32 * (z > (double)fr.farZ))
                 | ((16 * (z < (double)fr.nearZ)) & 0xDF)
                 | ((8 * (x * fr.plane[3][0] + y * fr.plane[3][1] + z * fr.plane[3][2] < fr.plane[3][3])) & 0xCF)
                 | ((4 * (x * fr.plane[2][0] + y * fr.plane[2][1] + z * fr.plane[2][2] < fr.plane[2][3])) & 0xC7)
                 | ((2 * (x * fr.plane[1][0] + y * fr.plane[1][1] + z * fr.plane[1][2] < fr.plane[1][3])) & 0xC3)
                 | (x * fr.plane[0][0] + y * fr.plane[0][1] + z * fr.plane[0][2] < fr.plane[0][3]));

        orMask |= oc;
        andMask &= oc;
    }

    // v10 = (andMask != 0) << 6;  result = v10 | (orMask & 0xBF).
    char fullyOut = (andMask != 0) ? 0x40 : 0;
    u8 result = (u8)(fullyOut | (orMask & 0xBF));

    if ((fullyOut & 0x40) == 0) {
        float minZ = 1.0e10f, maxZ = -1.0e10f;
        for (int c = 0; c < 8; ++c) {
            float z = corners[3 * c + 2];
            if (z < minZ) minZ = z;
            if (z > maxZ) maxZ = z;
        }
        if (outMinZ) *outMinZ = minZ;
        if (outMaxZ) *outMaxZ = maxZ;
    }
    return result;
}

// gilde.exe 0x5ad588 — VIBE_Render_CullNodeAgainstFrustum.
u8 CullNodeAgainstFrustum(u8 obj529, u8 signByte, bool hasAabb,
                          const float* corners, const Frustum& fr,
                          float* outMinZ, float* outMaxZ) {
    // (object+529 & 0x20) != 0  -> force-visible: return (signByte & 0x80) | 0x3F.
    if ((obj529 & 0x20) != 0)
        return (u8)((signByte & 0x80) | 0x3F);

    // With a projected AABB classify it; else the node is fully culled (|0x40).
    if (hasAabb)
        return ClassifyBoundingBoxPlanes(corners, fr, outMinZ, outMaxZ);
    return (u8)(signByte | 0x40);
}

} // namespace guild::render
