// =============================================================================
// scene_link.cpp — the LIVE scene-graph splice trio. See scene_link.h for the module
// overview, the +528 terminator-bit semantics and the make-current-camera leg.
// namespace guild::render.
//
// REUSE (no ODR re-definition):
//   * render::LinkAsSibling @0x5b0b3c — reconstructed in scene_walk.cpp; SetParent
//     here delegates to it for the firstChild sibling-chain append.
//   * render::SetParent @0x5b0b9c — also in scene_walk.cpp (the linkIntoScene-by-
//     trampoline form). This module adds the LiveScene-threaded SetParent that binds
//     the REAL LinkIntoScene; it reproduces the SAME control flow 1:1 (it does not
//     wrap scene_walk's SetParent because that one cannot capture the LiveScene the
//     LinkIntoScene leg needs — but it is byte-for-byte the same flow).
// =============================================================================
#include "render/scene_link.h"

#include <cstdint>  // std::intptr_t

namespace guild::render {

// gilde.exe 0x5b0a20 — VIBE_Object_LinkIntoScene
SceneNode* LinkIntoScene(LiveScene& scene, SceneNode* node) {
    // --- make-current-camera leg (type-3 MegaCam, no camera current yet) ---
    if (node->nodeType == 3 && scene.currentCamera == nullptr) {   // 0x5b0a38
        if (node) {                                                // 0x5b0a3c
            scene.currentCamera = node;                            // 0x5b0a42 dword_13FCD1C
            if (scene.invalidateCurrent)
                scene.invalidateCurrent(0);                        // 0x5b0a49
        }
        if (scene.setWorldTranslation)                             // 0x5b0a56
            scene.setWorldTranslation(node);                       // node (+132 arg resolved in leaf)
    }

    // --- the prepend-as-scene-head splice (nodeType != 0) ---
    if (node->nodeType != 0) {                                     // 0x5b0a5b
        SceneNode* oldHead = scene.sceneHead;                      // 0x5b0a74 v2 = dword_13FD140
        node->nextSibling = scene.sentinel;                        // 0x5b0a7a +496 = &unk_13FCF4C
        node->prevSibling = oldHead;                               // 0x5b0a84 +500 = v2
        if (oldHead)
            oldHead->nextSibling = node;                           // 0x5b0a8a v2+496 = node
        node->flags528 = (u8)(node->flags528 | 3);                 // 0x5b0a96/0x5b0a9f bit0+bit1
        scene.sceneHead = node;                                    // 0x5b0a99 dword_13FD140 = node
    }

    // node->+520 = off_649D64 (the active-universe stamp). Modelled out-of-band on the
    // LiveScene (the walk never reads +520, and a real 64-bit universe record cannot
    // survive the node's 4-byte +0x208 slot — LP64). The stamp is recorded by binding
    // the universe pointer onto the LiveScene; the per-node copy is a no-op here.
    (void)scene.activeUniverse;                                    // 0x5b0a6a
    return node;                                                   // 0x5b0a72
}

// gilde.exe 0x5b0b9c — VIBE_Object_SetParent (LiveScene-threaded form)
char SetParent(LiveScene& scene, SceneNode* parent, SceneNode* child) {
    u8 base = (u8)(child->flags528 & 0xFE);                        // 0x5b0bb2
    child->flags528 = base;                                        // 0x5b0bb5
    child->flags528 = (u8)(((parent == nullptr) ? 1u : 0u) | base);  // 0x5b0bb9 bit0 := (parent==0)

    char result = (char)child->flags528;
    if (parent) {                                                 // 0x5b0bbd
        if (parent->firstChild) {                                // 0x5b0bbf
            LinkAsSibling(parent->firstChild, child);            // 0x5b0bdc (REUSE scene_walk)
            result = 1;  // original: low byte of the returned (always non-null) tail
        } else {
            parent->firstChild = child;                          // 0x5b0bc9 +508 = child
        }
        child->parent = parent;                                  // 0x5b0bcf +504 = parent
    } else {
        // original: LOBYTE(v4) = LinkIntoScene(child) (the returned node ptr's low
        // byte; callers only test truthiness — node is always non-null here).
        result = (char)reinterpret_cast<std::intptr_t>(LinkIntoScene(scene, child));  // 0x5b0be5
    }
    return result;                                               // 0x5b0bd6
}

} // namespace guild::render
