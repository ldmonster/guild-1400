#pragma once
// =============================================================================
// guild::render — the LIVE scene-graph splice trio that builds the REAL traversable
// SceneNode tree the universe render walk (render::WalkAndInvoke) consumes. Faithful
// 1:1 reconstruction of the three functions that own scene-list / parent-child
// linkage:
//
//   0x5b0b3c  VIBE_Object_LinkAsSibling   (append node at a sibling-chain END)
//   0x5b0a20  VIBE_Object_LinkIntoScene   (prepend node as the scene-list head +
//                                          make-current camera for a type-3 MegaCam)
//   0x5b0b9c  VIBE_Object_SetParent       (re-parent / route to LinkIntoScene)
//
// LinkAsSibling / SetParent are ALREADY reconstructed 1:1 in render/scene_walk.cpp
// (render::LinkAsSibling / render::SetParent). This module REUSES those (it does not
// re-define them — ODR) and adds the missing third leg, LinkIntoScene, plus a small
// `LiveScene` state object that threads the engine's four file-scope globals so the
// splice is fully testable headless:
//
//   dword_13FD140  scene-list head        -> LiveScene::sceneHead
//   unk_13FCF4C    sentinel terminator    -> LiveScene::sentinel  (the SAME sentinel
//                                            SceneWalkEnv::listTerminator uses)
//   dword_13FCD1C  current camera node    -> LiveScene::currentCamera
//   off_649D64     active universe record -> LiveScene::activeUniverse
//
// THE NODE: render::SceneNode (scene_walk.h). The original is one opaque ~536-byte
// block addressed by raw byte offsets; the splice touches exactly the fields the walk
// reads — +496 nextSibling / +500 prevSibling / +504 parent / +508 firstChild /
// +528 flags528 / +533 nodeType — modelled as the SceneNode named members. Building
// the tree through these calls and walking it with WalkAndInvoke is therefore a
// closed loop on the SAME field set.
//
// THE +528 TERMINATOR BIT (verbatim from all three decompiles):
//   bit0 (0x01) = "sibling-chain terminator / scene-list head". A node carries it
//   iff it is a ROOT-level node (no parent): LinkAsSibling sets it from
//   (head->parent == 0); SetParent sets it from (parent == 0); LinkIntoScene OR-s in
//   0x03 (bit0 + bit1) as it becomes the new scene head. The walk stops a sibling
//   scan the moment it reaches a node with bit0 set (flags528 & 1) — so the bit
//   marks the boundary between sibling lists.
//
// THE MAKE-CURRENT-CAMERA LEG (LinkIntoScene, 0x5b0a20): when the node being linked
// is a type-3 MegaCam (nodeType +533 == 3) AND no camera is current yet
// (currentCamera == 0), it becomes the current camera, fires
// VIBE_Object_InvalidateCurrent(0) (0x5af2e4 — the universe dirty-flag / sky-dome /
// current-cam draw-block reset; a heavy global-state machine, routed through a hook
// here, inert by default), then re-seats its world translation via
// VIBE_Object_SetWorldTranslation(node, node+132) (0x5af50c — also a hook). These two
// leaves are the live-runtime legs; everything else (the actual list splice) is
// reconstructed in full.
// =============================================================================
#include "guild/common/types.h"
#include "render/scene_walk.h"   // render::SceneNode + the REUSED LinkAsSibling/SetParent

namespace guild::render {

// ---------------------------------------------------------------------------
// LiveScene — the engine's file-scope scene-list state, threaded explicitly so the
// splice is testable headless. Mirrors:
//   sceneHead       dword_13FD140  — the scene-list head (LinkIntoScene's prepend
//                                    target; its +496 is back-chained to the new node)
//   sentinel        unk_13FCF4C    — the sibling-chain terminator a freshly-linked
//                                    scene node points its +496 at (the SAME node the
//                                    walk's SceneWalkEnv::listTerminator stops on)
//   currentCamera   dword_13FCD1C  — the make-current camera node (0 => none yet)
//   activeUniverse  off_649D64     — the active universe record stamped into +520
// ---------------------------------------------------------------------------
struct LiveScene {
    SceneNode* sceneHead     = nullptr;  // dword_13FD140
    SceneNode* sentinel      = nullptr;  // unk_13FCF4C  (== SceneWalkEnv::listTerminator)
    SceneNode* currentCamera = nullptr;  // dword_13FCD1C
    void*      activeUniverse = nullptr;  // off_649D64 (stamped into node+520)

    // The make-current-camera leaves (0x5af2e4 InvalidateCurrent / 0x5af50c
    // SetWorldTranslation). Both default to nullptr (inert) so the headless splice is
    // deterministic; the real-runtime install binds them.
    void (*invalidateCurrent)(unsigned char arg) = nullptr;        // 0x5af2e4
    // 0x5af50c — the real fn re-reads the node's own +132 world-translation slot, so
    // the leaf only needs the node (the engine's second arg, node+132, is the node's
    // own euler block — not modelled as a SceneNode member; the real install resolves
    // it from the node's backing block).
    void (*setWorldTranslation)(SceneNode* node) = nullptr;        // 0x5af50c

    // node+520 (the +0x208 active-universe slot LinkIntoScene stamps). On the 32-bit
    // original this is a 4-byte dword inside the node block; modelled as a per-node
    // native pointer slot so a real 64-bit universe record survives (LP64). The walk
    // never reads it, so it lives beside the SceneNode rather than inside it.
};

// gilde.exe 0x5b0a20 — VIBE_Object_LinkIntoScene (__usercall al=fn(node@eax))
//   Prepend `node` as the scene-list head + (for a type-3 MegaCam with no current
//   camera) make it current. Verbatim:
//     if (node->nodeType == 3 && scene.currentCamera == 0) {
//         if (node) { scene.currentCamera = node; InvalidateCurrent(0); }
//         SetWorldTranslation(node, node+132);
//     }
//     if (node->nodeType != 0) {
//         node->nextSibling = scene.sentinel;       // +496 -> the terminator
//         node->prevSibling = scene.sceneHead;      // +500 -> the old head
//         scene.sceneHead->nextSibling = node;      // old-head +496 -> node
//         node->flags528 |= 3;                      // +528 bit0 terminator + bit1
//         scene.sceneHead = node;                   // node becomes the new head
//     }
//     // node->+520 = scene.activeUniverse  (modelled out-of-band; see LiveScene)
//   Returns `node` (the original eax). NOTE: the splice runs only for nodeType != 0;
//   nodeType 0 nodes still get the universe stamp but are NOT chained.
SceneNode* LinkIntoScene(LiveScene& scene, SceneNode* node);

// gilde.exe 0x5b0b9c — VIBE_Object_SetParent, threaded through a LiveScene so the
// parent==null leg routes to the REAL LinkIntoScene above. Identical control flow to
// render::SetParent (which it delegates to for the splice), but it captures the
// LiveScene for the LinkIntoScene path so the caller does not have to build the
// linkIntoScene trampoline by hand. Returns the original's low result byte.
char SetParent(LiveScene& scene, SceneNode* parent, SceneNode* child);

} // namespace guild::render
