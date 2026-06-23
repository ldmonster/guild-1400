#include "test.h"

// =============================================================================
// SceneLink — golden coverage of the LIVE scene-graph splice trio (no assets):
//   0x5b0b3c  VIBE_Object_LinkAsSibling  (render::LinkAsSibling, REUSED from scene_walk)
//   0x5b0a20  VIBE_Object_LinkIntoScene  (render::LinkIntoScene, scene_link.cpp)
//   0x5b0b9c  VIBE_Object_SetParent      (render::SetParent,     scene_link.cpp)
//
// Each test exercises the exact decompiled semantics:
//   * LinkAsSibling appends at the chain END, inherits the head's parent, and sets the
//     +528 bit0 terminator iff the head is root-level (head->parent == 0).
//   * SetParent under a parent: firstChild then sibling-chain; sets child->parent (+504)
//     and clears the +528 terminator bit (parent != 0).
//   * SetParent(parent=0): routes to LinkIntoScene — prepends the node as the scene head,
//     OR-s +528 bits 0+1, and (real fn) stamps the universe slot.
//   * LinkIntoScene of a type-3 MegaCam makes it the current camera EXACTLY ONCE.
//   * The sentinel terminates the freshly-linked node's +496 chain.
// Then it builds a small tree through these calls and runs render::WalkAndInvoke over it
// (UniverseRoot.childHead = sceneHead, env.listTerminator = sentinel), asserting it
// visits every spliced node in the right pre-order — proving the splice is walkable.
// =============================================================================
#include "render/scene_link.h"
#include "render/scene_walk.h"

#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// A node whose type passes TestNodeFlag for the mask we use (0x1FF == all bits): any
// type 0..8 fires; we use type 4 (-> bit 0x40) for plain mesh nodes, type 3 for cams.
SceneNode MakeNode(u8 type) {
    SceneNode n;
    n.nodeType = type;
    return n;
}

// Boot state from VIBE_Render_InitEngineDevice @0x5af984 (5afc21..5afc40):
//   InitStruct(sentinel)   -> sentinel +528 = (..&0x9A)|0x65 (bit0 SET), +533 = 1.
//   dword_13FD140 = dword_13FCF10 = sentinel  -> the scene head STARTS as the sentinel.
// So the sentinel is a permanent chain terminator (the walk's flags528&1 stop test) AND
// the initial head, so the first LinkIntoScene back-chains the sentinel's +496 to the
// first real node — the walk descends from sentinel->nextSibling.
LiveScene FreshScene(SceneNode* sentinel) {
    sentinel->flags528 = 0x65;       // InitStruct +528 seed (bit0 set)
    sentinel->nodeType = 1;          // InitStruct +533 seed
    LiveScene s;
    s.sentinel  = sentinel;
    s.sceneHead = sentinel;          // dword_13FD140 starts AS the sentinel
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// LinkAsSibling: appends at the END, inherits parent, sets terminator iff head root.
// ---------------------------------------------------------------------------
TEST(SceneLink, LinkAsSiblingAppendsAtEndRootHead) {
    SceneNode head = MakeNode(4);     // root-level (parent == nullptr)
    SceneNode a = MakeNode(4), b = MakeNode(4);

    LinkAsSibling(&head, &a);
    CHECK(head.nextSibling == &a);
    CHECK(a.prevSibling == &head);
    CHECK(a.parent == nullptr);                 // inherited head->parent (null)
    CHECK_EQ((int)(a.flags528 & 1), 1);         // head is root => terminator bit set

    // append b: walks to the END of the chain (head -> a -> b), not after head.
    LinkAsSibling(&head, &b);
    CHECK(a.nextSibling == &b);
    CHECK(b.prevSibling == &a);
    CHECK(b.nextSibling == nullptr);
    CHECK_EQ((int)(b.flags528 & 1), 1);
}

TEST(SceneLink, LinkAsSiblingInheritsNonRootParentNoTerminator) {
    SceneNode parent = MakeNode(4);
    SceneNode head = MakeNode(4);
    head.parent = &parent;                      // head is NOT root-level
    SceneNode child = MakeNode(4);
    child.flags528 = 0x01;                       // pre-set terminator: must be CLEARED

    LinkAsSibling(&head, &child);
    CHECK(child.parent == &parent);             // inherits head's parent
    CHECK_EQ((int)(child.flags528 & 1), 0);     // head non-root => terminator cleared
    CHECK(head.nextSibling == &child);
}

// ---------------------------------------------------------------------------
// SetParent under a parent: firstChild then sibling chain; sets +504; clears +528 bit0.
// ---------------------------------------------------------------------------
TEST(SceneLink, SetParentUnderParentFirstChildThenSiblings) {
    SceneNode sentinel;
    LiveScene scene = FreshScene(&sentinel);
    SceneNode parent = MakeNode(4);
    SceneNode c0 = MakeNode(4), c1 = MakeNode(4), c2 = MakeNode(4);

    // first child: parent->firstChild becomes c0; parent set; terminator cleared.
    SetParent(scene, &parent, &c0);
    CHECK(parent.firstChild == &c0);
    CHECK(c0.parent == &parent);
    CHECK_EQ((int)(c0.flags528 & 1), 0);        // parent != 0 => bit0 clear

    // second child: links as sibling onto firstChild's chain (NOT a new firstChild).
    SetParent(scene, &parent, &c1);
    CHECK(parent.firstChild == &c0);            // unchanged
    CHECK(c0.nextSibling == &c1);
    CHECK(c1.parent == &parent);
    // c1 is a sibling of a non-root head (c0->parent != 0) => terminator cleared.
    CHECK_EQ((int)(c1.flags528 & 1), 0);

    // third child: appended at the END of the sibling chain (c0 -> c1 -> c2).
    SetParent(scene, &parent, &c2);
    CHECK(c1.nextSibling == &c2);
    CHECK(c2.prevSibling == &c1);
}

// ---------------------------------------------------------------------------
// SetParent(parent=0): routes to LinkIntoScene; prepends as scene head; sets +528 0+1.
// ---------------------------------------------------------------------------
TEST(SceneLink, SetParentNullPrependsSceneHeadSetsBitsAndUniverse) {
    SceneNode sentinel;
    LiveScene scene = FreshScene(&sentinel);
    int universeRec = 0;
    scene.activeUniverse = &universeRec;

    SceneNode n0 = MakeNode(4);
    SetParent(scene, /*parent=*/nullptr, &n0);

    // first scene node: becomes head; +496 -> sentinel; +500 -> old head (== sentinel);
    // the sentinel back-chains its +496 to n0; bits 0+1 set.
    CHECK(scene.sceneHead == &n0);
    CHECK(n0.nextSibling == &sentinel);
    CHECK(n0.prevSibling == &sentinel);         // old head was the sentinel
    CHECK(sentinel.nextSibling == &n0);         // sentinel +496 -> first real node
    CHECK_EQ((int)(n0.flags528 & 3), 3);        // bit0 terminator + bit1

    // second scene node: prepends (becomes new head); old head (n0) back-chains to it.
    SceneNode n1 = MakeNode(4);
    SetParent(scene, nullptr, &n1);
    CHECK(scene.sceneHead == &n1);
    CHECK(n1.nextSibling == &sentinel);
    CHECK(n1.prevSibling == &n0);               // +500 -> old head
    CHECK(n0.nextSibling == &n1);               // old head +496 -> new node
    CHECK_EQ((int)(n1.flags528 & 3), 3);
}

// ---------------------------------------------------------------------------
// LinkIntoScene of a type-3 MegaCam makes it the current camera EXACTLY ONCE.
// ---------------------------------------------------------------------------
TEST(SceneLink, LinkIntoSceneMakesCurrentCameraOnce) {
    SceneNode sentinel;
    LiveScene scene = FreshScene(&sentinel);

    static int s_invalidateCalls = 0;
    static int s_setWorldCalls = 0;
    s_invalidateCalls = 0;
    s_setWorldCalls = 0;
    scene.invalidateCurrent  = [](unsigned char) { ++s_invalidateCalls; };
    scene.setWorldTranslation = [](SceneNode*)    { ++s_setWorldCalls; };

    SceneNode cam0 = MakeNode(3);   // type-3 MegaCam
    LinkIntoScene(scene, &cam0);
    CHECK(scene.currentCamera == &cam0);
    CHECK_EQ(s_invalidateCalls, 1);             // make-current leg fired
    CHECK_EQ(s_setWorldCalls, 1);

    // a SECOND type-3 cam does NOT become current (currentCamera already set).
    SceneNode cam1 = MakeNode(3);
    LinkIntoScene(scene, &cam1);
    CHECK(scene.currentCamera == &cam0);        // unchanged
    CHECK_EQ(s_invalidateCalls, 1);             // not fired again
    CHECK_EQ(s_setWorldCalls, 1);
    // but cam1 IS still chained into the scene list (type != 0).
    CHECK(scene.sceneHead == &cam1);
}

// type-0 node is NOT chained (nodeType == 0 -> no splice), only the universe stamp.
TEST(SceneLink, LinkIntoSceneType0NotChained) {
    SceneNode sentinel;
    LiveScene scene = FreshScene(&sentinel);
    SceneNode n = MakeNode(0);
    LinkIntoScene(scene, &n);
    CHECK(scene.sceneHead == &sentinel);        // unchanged (head stays the sentinel)
    CHECK(n.nextSibling == nullptr);            // node not chained
}

// ---------------------------------------------------------------------------
// Build a small tree via the splice trio and WALK it: prove it is traversable.
//
//   root scene list (prepend order): link rootA then rootB.
//     rootA  (type 4, root)  children: a0, a1
//     rootB  (type 4, root)  child:    b0
//   The scene head starts AS the sentinel (boot state), so after linking rootA then
//   rootB the +496 next-chain is: sentinel -> rootA -> rootB -> sentinel. The WALK
//   descends from sentinel->nextSibling (== rootA, the oldest real node) and follows
//   nextSibling, stopping at the sentinel (flags528 bit0 terminator).
//   Expected pre-order: rootA, a0, a1, rootB, b0   (every spliced node visited once).
// ---------------------------------------------------------------------------
TEST(SceneLink, BuiltTreeIsWalkable) {
    SceneNode sentinel;
    LiveScene scene = FreshScene(&sentinel);

    SceneNode rootA = MakeNode(4), rootB = MakeNode(4);
    SceneNode a0 = MakeNode(4), a1 = MakeNode(4), b0 = MakeNode(4);

    // root-level scene nodes (parent = 0 -> LinkIntoScene). Prepend semantics: rootA
    // first, then rootB -> head = rootB, rootB->prev = rootA, rootA->next = rootB.
    SetParent(scene, nullptr, &rootA);
    SetParent(scene, nullptr, &rootB);

    // children under each root.
    SetParent(scene, &rootA, &a0);
    SetParent(scene, &rootA, &a1);
    SetParent(scene, &rootB, &b0);

    // The walk's child head is the OLDEST real root node == sentinel->nextSibling.
    SceneNode* childHead = sentinel.nextSibling;   // -> &rootA
    CHECK(childHead == &rootA);

    UniverseRoot uroot;
    uroot.childHead = childHead;                // == &rootA (oldest-first next-chain)
    SceneWalkEnv env;
    env.root = &uroot;
    env.listTerminator = scene.sentinel;

    static std::vector<SceneNode*> s_visited;
    s_visited.clear();
    auto cb = [](SceneNode* n, std::intptr_t) -> char {
        s_visited.push_back(n);
        return 1;  // visit + descend
    };

    // node==null form: walk the root's child list (head rootB) to the sentinel.
    char rc = WalkAndInvoke(&uroot, /*node=*/nullptr, cb, /*walkMask=*/0x1FF,
                            /*userArg=*/0, env);
    CHECK_EQ((int)rc, 1);

    // Every spliced node visited exactly once.
    CHECK_EQ((int)s_visited.size(), 5);
    auto seen = [&](SceneNode* n) {
        for (auto* v : s_visited) if (v == n) return true;
        return false;
    };
    CHECK(seen(&rootB)); CHECK(seen(&b0));
    CHECK(seen(&rootA)); CHECK(seen(&a0)); CHECK(seen(&a1));
    CHECK(!seen(&sentinel));                    // the terminator itself is NOT visited

    // Pre-order: each root precedes its children; rootA (oldest, the next-chain head)
    // precedes rootB (linked later, further down the next-chain).
    auto idx = [&](SceneNode* n) -> int {
        for (int i = 0; i < (int)s_visited.size(); ++i) if (s_visited[i] == n) return i;
        return -1;
    };
    CHECK(idx(&rootA) < idx(&a0));
    CHECK(idx(&rootA) < idx(&a1));
    CHECK(idx(&rootB) < idx(&b0));
    CHECK(idx(&rootA) < idx(&rootB));
}
