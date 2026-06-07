#pragma once
// =============================================================================
// guild::play — REAL OBJECT / SCENE-NODE WORLD TRANSFORM DECODE (PLAYABLE_PLAN P2).
//
// This module reconstructs, 1:1 from gilde.exe, how the engine reads an object /
// scene-node's WORLD PLACEMENT out of the full render-node struct — the transform
// the per-frame scene-graph walk feeds into VIBE_Mesh_ProjectVerticesToScreen.
// It replaces world_render / scene_pick's previous *synthetic* id-derived grid
// placement hook with the engine's real layout read, so objects land in their
// true city positions.
//
// THE RENDER NODE (the big struct VIBE_Object_Spawn(4,name) allocates)
// ---------------------------------------------------------------------------
// The scene-graph walk (VIBE_SceneGraph_WalkAndInvoke @0x5ac738 / TraverseTree
// @0x5ac86c) threads these fields of every render node:
//     +496 (node[124])  next-sibling pointer (chain, NUL-terminated)
//     +508 (node[127])  first-child pointer  (descend)
//     +528              walk/dirty flags byte (bit0 chain-stop, bit2 dirty, ...)
//     +530..+533        the flags dword; HIBYTE(+530) == +533 is the TYPE byte
//                       (1/2 mesh-object, 3 camera, 4 light), used by
//                       VIBE_SceneGraph_TestNodeFlag as the flag SELECTOR.
//
// THE WORLD TRANSFORM (recovered from the writers + the projection reader)
// ---------------------------------------------------------------------------
//  * VIBE_Object_SetPosition @0x5af38c writes the node LOCAL POSITION:
//        node[19]=x  node[20]=y  node[21]=z   ->  +76 / +80 / +84   (3 floats)
//  * VIBE_Object_SetWorldTranslation @0x5af50c writes the node EULER ROTATION:
//        *(node+132)=ax  *(node+136)=ay  *(node+140)=az  (3 floats)
//    then builds a rotation frame matrix from those angles:
//        VIBE_Math_MatrixFromEuler(node+132, node+396)
//    (for a camera node, +533==3, it negates the angles first and that becomes
//     the eye/view basis instead).
//  * VIBE_Math_MatrixFromEuler @0x5cb1bc builds a 16-float (4x4, column-major)
//    rotation matrix M at +396:   with ax=pitch, ay=yaw(heading), az=roll
//        M[0]=cos(ay)cos(az)            M[8] (byte+32) = -sin(ay)
//        M[10](byte+40)=cos(ax)cos(ay)  M[15](byte+60)= 1.0   (rest of col4 = 0)
//    so for the common pitch=roll=0 ground object  M[0]=cos(yaw) M[8]=-sin(yaw)
//    => yaw == atan2(-M[8], M[0]).
//  * The PROJECTION reader (VIBE_Mesh_InterpolateMorphVertices @0x5c953c, called
//    from VIBE_Render_ProcessSceneNode @0x5add1c with the frame matrix at
//    record+72 where record == *(node+492)) transforms each vertex by the FULL
//    4x4 world matrix exactly as:
//        sx = vx*m[0] + vy*m[4] + vz*m[8]  + m[12]
//        sy = vx*m[1] + vy*m[5] + vz*m[9]  + m[13]
//        sz = vx*m[2] + vy*m[6] + vz*m[10] + m[14]
//    i.e. column-major rotation rows [0,4,8]/[1,5,9]/[2,6,10] and TRANSLATION in
//    the 4th column m[12]/m[13]/m[14]. That composed world matrix is the node's
//    rotation (from +396) seated with the node's world position.
//
// VISIBILITY
// ---------------------------------------------------------------------------
// The draw walk (VIBE_Render_DrawUniverseAndStats @0x5b3bbc) invokes the per-node
// project/append callback only when VIBE_SceneGraph_TestNodeFlag(+533 typebyte,
// mask 0x181) is true: type 1 -> &0x80, type 2 -> &0x100, type 3 -> &1 all pass;
// type 4 (light) fails. So a mesh-object node (type 1/2) or a camera (3) is
// "drawable"; lights (4) and empties (0) are not. We mirror that as `visible`.
//
// THE OFFSETS (single source of truth; all byte offsets into the render node)
// =============================================================================
#include "guild/common/types.h"

namespace guild::play {

// ---- render-node byte offsets (the big VIBE_Object_Spawn struct) ----
enum NodeOffset : int {
    kNodePosX        = 76,    // +76  node[19]  local position X  (SetPosition)
    kNodePosY        = 80,    // +80  node[20]  local position Y
    kNodePosZ        = 84,    // +84  node[21]  local position Z
    kNodeEulerX      = 132,   // +132 euler angle X (pitch)       (SetWorldTranslation)
    kNodeEulerY      = 136,   // +136 euler angle Y (yaw/heading)
    kNodeEulerZ      = 140,   // +140 euler angle Z (roll)
    kNodeFrameMatrix = 396,   // +396 4x4 (16-float) rotation frame matrix
    kNodeFlags       = 528,   // +528 walk/dirty flags byte
    kNodeTypeByte    = 533,   // +533 == HIBYTE(*(node+530)): TestNodeFlag selector
    kNodeSibling     = 496,   // +496 node[124] next-sibling ptr
    kNodeChild       = 508,   // +508 node[127] first-child ptr
    kNodeRecord      = 492,   // +492 node[123] mesh/parent record (+72 world matrix)
};

// The composed world matrix the projection actually reads lives at record+72.
enum RecordOffset : int {
    kRecordWorldMatrix = 72,  // +72  16-float composed world matrix (R + T col4)
};

// ---- node TYPE byte values (the +533 selector) ----
enum NodeType : u8 {
    kNodeTypeEmpty  = 0,
    kNodeTypeMeshA  = 1,   // drawable mesh-object (TestNodeFlag &0x80)
    kNodeTypeMeshB  = 2,   // drawable mesh-object (TestNodeFlag &0x100)
    kNodeTypeCamera = 3,   // camera             (TestNodeFlag &1)
    kNodeTypeLight  = 4,   // light — NOT drawn by the 0x181 draw walk
};

// ---------------------------------------------------------------------------
// The decoded world placement of one object / scene node.
// ---------------------------------------------------------------------------
struct WorldPlacement {
    float x = 0, y = 0, z = 0;   // world translation (world units)
    float yaw = 0;               // heading about +Y, radians (atan2(-M[8], M[0]))
    bool  visible = false;       // drawable per the engine draw-walk flag test
};

// ---------------------------------------------------------------------------
// Low-level matrix readers (the exact arithmetic the engine uses).
// ---------------------------------------------------------------------------

// Extract the world TRANSLATION from a 16-float column-major world matrix:
// m[12]/m[13]/m[14] (the 4th column) — exactly the +m[12..14] term every vertex
// of VIBE_Mesh_InterpolateMorphVertices @0x5c953c adds.
void WorldMatrixTranslation(const float m[16], float out[3]);

// Extract the heading YAW (radians) from a 16-float column-major world matrix
// built by VIBE_Math_MatrixFromEuler: yaw == atan2(-m[8], m[0]).
float WorldMatrixYaw(const float m[16]);

// ---------------------------------------------------------------------------
// High-level node / object placement decoders.
// ---------------------------------------------------------------------------

// Decode a render node's world placement from its real engine layout. `node` is
// a pointer to the start of the big render-node struct (the VIBE_Object_Spawn
// block). Reads the +76 world position, the +396 frame matrix yaw, and the +533
// type byte for visibility. Returns visible=false (and zeroed transform) for a
// null node, an empty node (type 0) or a light (type 4). 1:1 with the engine read.
WorldPlacement SceneNodeWorldPlacement(const void* node);

// Decode an object's world placement. An object record links to its render/scene
// node (VIBE_Object_AttachToUniverseNode @0x5b3e30 spawns the node and seats the
// object's position/rotation on it via SetPosition + SetWorldTranslation), so the
// object's true placement IS its scene node's placement. When `sceneNode` is
// non-null this delegates to SceneNodeWorldPlacement(sceneNode). When it is null
// (no node linked / not yet placed) the object is treated as not-yet-visible.
// `objectRec` is accepted for API symmetry and future per-record overrides; the
// transform itself lives on the node, exactly as the engine stores it.
WorldPlacement ObjectWorldPlacement(const void* objectRec, const void* sceneNode);

} // namespace guild::play
