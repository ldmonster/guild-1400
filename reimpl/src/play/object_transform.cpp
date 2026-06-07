// =============================================================================
// guild::play — REAL OBJECT / SCENE-NODE WORLD TRANSFORM DECODE. See header.
//
// Every read here mirrors the engine byte layout exactly; the node is addressed
// as a raw byte buffer (the real struct is opaque/pointer-rich) and the transform
// floats / type byte are read at their recovered offsets.
// =============================================================================
#include "play/object_transform.h"

#include <cmath>
#include <cstring>

namespace guild::play {

namespace {

// Read a little-endian float at byte offset `off` from a node/record base.
inline float RdF(const void* base, int off) {
    float f;
    std::memcpy(&f, static_cast<const unsigned char*>(base) + off, sizeof f);
    return f;
}

// Read a byte at `off`.
inline u8 RdB(const void* base, int off) {
    return static_cast<const unsigned char*>(base)[off];
}

} // namespace

// ---------------------------------------------------------------------------
// Matrix readers.
// ---------------------------------------------------------------------------

void WorldMatrixTranslation(const float m[16], float out[3]) {
    // The +m[12]/m[13]/m[14] term the projection adds to every transformed vertex
    // (VIBE_Mesh_InterpolateMorphVertices: ... + v6[12], + v6[13], + v6[14]).
    out[0] = m[12];
    out[1] = m[13];
    out[2] = m[14];
}

float WorldMatrixYaw(const float m[16]) {
    // VIBE_Math_MatrixFromEuler: M[0]=cos(yaw)cos(roll), M[8] (byte+32) = -sin(yaw).
    // For the common ground object (roll==0) M[0]=cos(yaw), M[8]=-sin(yaw), giving
    // yaw = atan2(sin(yaw), cos(yaw)) = atan2(-M[8], M[0]). With a non-zero roll the
    // engine's heading basis still encodes -sin(yaw) at M[8]; atan2(-M[8], M[0]) is
    // the heading about +Y exactly as the engine seats it.
    return std::atan2(-m[8], m[0]);
}

// ---------------------------------------------------------------------------
// Node / object placement.
// ---------------------------------------------------------------------------

WorldPlacement SceneNodeWorldPlacement(const void* node) {
    WorldPlacement p;
    if (!node)
        return p;   // null -> not visible, zeroed

    const u8 type = RdB(node, kNodeTypeByte);

    // The engine draw walk (DrawUniverseAndStats, mask 0x181 over TestNodeFlag with
    // the +533 type byte as selector): type 1 -> &0x80, 2 -> &0x100, 3 -> &1 pass;
    // type 0 (empty) and 4 (light) do not. Mirror that exactly.
    const bool drawable =
        type == kNodeTypeMeshA || type == kNodeTypeMeshB || type == kNodeTypeCamera;

    // World TRANSLATION: the node's world position seated at +76/+80/+84 by
    // VIBE_Object_SetPosition (node[19]/node[20]/node[21]). This is what the
    // composed +72 world matrix carries in its 4th column for a top-level object.
    p.x = RdF(node, kNodePosX);
    p.y = RdF(node, kNodePosY);
    p.z = RdF(node, kNodePosZ);

    // YAW: read the +396 frame matrix (built by MatrixFromEuler from the +132 euler
    // angles) and recover the heading exactly as the matrix encodes it.
    float m[16];
    std::memcpy(m, static_cast<const unsigned char*>(node) + kNodeFrameMatrix,
                sizeof m);
    p.yaw = WorldMatrixYaw(m);

    p.visible = drawable;
    return p;
}

WorldPlacement ObjectWorldPlacement(const void* objectRec, const void* sceneNode) {
    (void)objectRec;   // the transform lives on the linked node, as the engine stores it
    if (!sceneNode)
        return WorldPlacement{};   // unplaced object -> not visible
    return SceneNodeWorldPlacement(sceneNode);
}

} // namespace guild::play
