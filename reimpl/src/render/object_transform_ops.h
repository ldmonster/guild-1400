#pragma once
#include "guild/common/types.h"
#include "render/scene_walk.h"
#include <cstdint>

// =============================================================================
// guild::render — object/scene-graph transform + scene-graph removal driver ops
// reconstructed from gilde.exe (wave-20, W20-OBJECT2).
//
// This module owns the ONE genuinely-missing scene-graph driver in the
// object/transform cluster:
//
//   0x5f0b18  VIBE_SceneGraph_RemoveMeshFromTree   (top-level mesh removal)
//
// and re-exposes the bone-chain / parent-transform MATH leaves (which already
// live faithfully in util/transform.cpp + util/matrix.cpp) as a small, testable
// surface so the object-reparent transform can be golden-pinned end to end
// without speculation. The reparent/transform/transparency/suspend/visual-state
// bodies themselves (0x5b7e54 / 0x5b7d14 / 0x5b2710 / 0x5b4274 / 0x506b68) are
// ALREADY reconstructed in src/sim/object_lifecycle5.cpp + object_lifecycle9.cpp
// and are reused, not redefined here (see progress/object-ops-wave20.md).
// =============================================================================
namespace guild::render {

// gilde.exe 0x5f0b18 — VIBE_SceneGraph_RemoveMeshFromTree
//   (__usercall al=fn(node@eax, regionCell@edx)).
//
//   char RemoveMeshFromTree(node, regionCell) {
//       if (regionCell && node)
//           node-low = WalkAndInvoke(off_649D64, node, RemoveMeshRecursive,
//                                    *(u16*)(regionCell+68) | 0x280, regionCell);
//       return node-low-byte;
//   }
//
// It drives a gated pre-order walk of `node`'s scene subtree (via the universe
// root off_649D64); for every visited node it calls RemoveMeshRecursive(visited,
// regionCell), unlinking that node's mesh from the octree `regionCell`. The walk
// mask comes from the region cell's +0x44 (dword[17]) field OR'd with 0x280
// (bits 0x200 = "don't advance sibling" + 0x80 = type-1 nodes).
//
// `regionCell` is the octree REGION cell the mesh lives in; here it is the
// SceneCell whose mesh-cell bookkeeping RemoveMeshRecursive mutates, extended
// with the +0x44 walk-mask field (regionMask) the original reads.
//
// `env` and `pools` are the scene-walk environment + the mem pools the recursive
// removal frees into (the original's off_649D64 / dword_64A7D0 / dword_64A7CC).
char RemoveMeshFromTree(SceneNode* node, SceneCell* regionCell,
                        const SceneWalkEnv& env, const SceneCellPools& pools,
                        u16 regionMask);

// ---------------------------------------------------------------------------
// Golden-pinnable transform math wrappers (thin re-exports of the already-
// reconstructed util/ primitives, gathered so the object-reparent transform can
// be verified against synthetic frames in one place). These do NOT redefine the
// underlying routines — they call guild::util::* (transform.h / matrix.h).
// ---------------------------------------------------------------------------

// VIBE_Transform_PointThroughBoneChain @0x5c8b38 — translate `point` by the
// frame's local translation then walk the parent chain. (Reuses util.)
float* PointThroughBoneChain(float* frame, const float* point, float* out);

// The TransformPointToParent inner math (the no-parent fast path of 0x5b7d14):
// outPoint = point (pass-through) and outAngles = inAngles passed through
// MatrixToEuler. Exposed for golden-pinning the affine no-parent branch.
void TransformPointPassThrough(const float point[3], float inAngles[3],
                               float outPoint[3], float outAngles[3]);

// The TransformPointToParent inner math (the parented branch of 0x5b7d14):
// outPoint = point * inverse(parentWorld) (the affine row layout from the
// decompile: y = p.x*m1 + p.y*m5 + p.z*m9 + m13, etc.). `inv` is the already-
// inverted 4x4 parent world matrix (16 floats, column/row layout as the engine).
void TransformPointByInverseWorld(const float point[3], const float inv[16],
                                  float outPoint[3]);

}  // namespace guild::render
