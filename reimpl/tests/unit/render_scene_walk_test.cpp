#include "test.h"

#include "render/scene_walk.h"
#include "mem/heap.h"
#include "mem/mempool.h"
#include "mem/memory_debug.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;
using guild::mem::Heap;
using guild::mem::MemoryTracker;
using guild::mem::MemPool;

// =============================================================================
// TestNodeFlag — the verbatim type->bit table (golden vector). Each type t fires
// for exactly one mask bit; we probe the firing bit and an adjacent non-bit.
// =============================================================================
TEST(SceneWalk, TestNodeFlagTable) {
    struct { u8 type; i16 fireMask; } cases[] = {
        {0, 0x20}, {1, (i16)0x80}, {2, 0x100}, {3, 0x01},
        {4, 0x40}, {5, 0x02}, {6, 0x04}, {7, 0x08}, {8, 0x10},
    };
    for (auto& c : cases) {
        CHECK(TestNodeFlag(c.type, c.fireMask));        // its bit set => true
        CHECK(!TestNodeFlag(c.type, (i16)~c.fireMask)); // its bit clear => false
    }
    // Out-of-range types never fire.
    CHECK(!TestNodeFlag(9, (i16)0xFFFF));
    CHECK(!TestNodeFlag(200, (i16)0xFFFF));
    // type 3 uses (mask & 1) == 1 (verbatim): bit0 set fires, anything else doesn't.
    CHECK(TestNodeFlag(3, 0x01));
    CHECK(!TestNodeFlag(3, 0x02));
}

// =============================================================================
// MarkDirtyFlag — sets +528 bit2, conditionally clears +530 bit7, always clears
// +531 bit0.
// =============================================================================
TEST(SceneWalk, MarkDirtyFlagBits) {
    SceneNode n;
    n.flags528 = 0x01;       // keep bit0
    n.flagNoPivot = 0x80;    // no-pivot set
    n.flags531 = 0x05;       // bit0 + bit2 set
    CHECK_EQ(MarkDirtyFlag(&n, 1), (char)1);
    CHECK_EQ(n.flags528, (u8)0x05);   // bit2 set, bit0 preserved
    CHECK_EQ(n.flagNoPivot, (u8)0x00);// bit7 cleared (clearNoPivot != 0)
    CHECK_EQ(n.flags531, (u8)0x04);   // bit0 cleared, bit2 preserved

    SceneNode m;
    m.flagNoPivot = 0x80;
    m.flags531 = 0x01;
    MarkDirtyFlag(&m, 0);             // clearNoPivot == 0: leave +530 bit7
    CHECK_EQ(m.flagNoPivot, (u8)0x80);
    CHECK_EQ(m.flags528, (u8)0x04);
    CHECK_EQ(m.flags531, (u8)0x00);
}

// =============================================================================
// GetFirstActiveChild.
//   parented node -> returns its first sibling directly.
//   un-parented node -> first sibling whose flags528 bit0 (terminator) is set.
// =============================================================================
TEST(SceneWalk, GetFirstActiveChild) {
    SceneNode a, b, c, d;
    // Parented: returns nextSibling unconditionally.
    a.parent = &d;
    a.nextSibling = &b;
    CHECK(GetFirstActiveChild(&a) == &b);

    // Un-parented chain a -> b -> c; c is the terminator (bit0).
    SceneNode e, f, g;
    e.parent = nullptr;
    e.nextSibling = &f;
    f.flags528 = 0;     f.nextSibling = &g;
    g.flags528 = 0x01;  g.nextSibling = nullptr; // terminator
    CHECK(GetFirstActiveChild(&e) == &g);

    // Un-parented with no terminator -> walks to null.
    SceneNode h, k;
    h.parent = nullptr; h.nextSibling = &k;
    k.flags528 = 0; k.nextSibling = nullptr;
    CHECK(GetFirstActiveChild(&h) == nullptr);
}

// =============================================================================
// LinkAsSibling — appends to the tail; terminator bit inherits from list head's
// parentless-ness; node inherits the head's parent.
// =============================================================================
TEST(SceneWalk, LinkAsSibling) {
    SceneNode head, mid, tail, added, parent;
    // Parented list: head(parent set) -> mid -> tail.
    head.parent = &parent;
    head.nextSibling = &mid;
    mid.nextSibling = &tail;
    tail.nextSibling = nullptr;

    added.flags528 = 0x01;  // terminator bit set initially -> should be cleared
    SceneNode* ret = LinkAsSibling(&head, &added);
    CHECK(ret == &tail);                 // returns the old tail
    CHECK(tail.nextSibling == &added);   // appended
    CHECK(added.prevSibling == &tail);
    CHECK(added.parent == &parent);      // inherited head's parent
    CHECK_EQ(added.flags528 & 1, 0);     // head has a parent => terminator bit cleared

    // Parentless head => terminator bit set on the added node.
    SceneNode rhead, radd;
    rhead.parent = nullptr;
    rhead.nextSibling = nullptr;
    LinkAsSibling(&rhead, &radd);
    CHECK_EQ(radd.flags528 & 1, 1);
    CHECK(rhead.nextSibling == &radd);
}

// =============================================================================
// SetParent.
//   parent with no child  -> node becomes firstChild, node->parent set.
//   parent with a child   -> node linked as a sibling onto the child chain.
//   null parent           -> linkIntoScene callback invoked.
// =============================================================================
static int g_linkSceneCalls = 0;
static char LinkSceneStub(SceneNode* n) { ++g_linkSceneCalls; n->flags531 |= 0x40; return 1; }

TEST(SceneWalk, SetParentFirstChild) {
    SceneNode p, child;
    p.firstChild = nullptr;
    // The firstChild branch's return byte is a don't-care (the original returns the
    // low byte of a pointer); we assert the STRUCTURAL effects only.
    SetParent(&p, &child, LinkSceneStub);
    CHECK(p.firstChild == &child);
    CHECK(child.parent == &p);
    CHECK_EQ(child.flags528 & 1, 0);  // parent non-null => terminator bit clear
}

TEST(SceneWalk, SetParentAppendSibling) {
    SceneNode p, existing, added;
    p.firstChild = &existing;
    existing.parent = &p;
    existing.nextSibling = nullptr;
    char r = SetParent(&p, &added, LinkSceneStub);
    CHECK_EQ(r, (char)1);
    CHECK(p.firstChild == &existing);      // unchanged
    CHECK(existing.nextSibling == &added); // appended as sibling
    CHECK(added.parent == &p);
}

TEST(SceneWalk, SetParentNullLinksScene) {
    g_linkSceneCalls = 0;
    SceneNode orphan;
    char r = SetParent(nullptr, &orphan, LinkSceneStub);
    CHECK_EQ(g_linkSceneCalls, 1);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(orphan.flags528 & 1, 1);  // null parent => terminator bit set
    CHECK(orphan.flags531 & 0x40);     // stub side effect
}

// -----------------------------------------------------------------------------
// Traversal harness: build a small typed tree and record visit order.
// Node type 3 fires on mask bit0 (TestNodeFlag case 3); we tag every node with
// type 3 so the callback always fires when mask bit0 is set.
// -----------------------------------------------------------------------------
namespace {
std::vector<int>* g_visit = nullptr;
// We tag each node's id by stashing it in flags531's high nibble is too small;
// instead use a side map via pointer identity recorded into a small array.
struct IdNode { SceneNode n; int id; };

std::vector<std::pair<SceneNode*, int>> g_ids;
int IdOf(SceneNode* n) {
    for (auto& e : g_ids) if (e.first == n) return e.second;
    return -1;
}
void RecordVisit(SceneNode* n) { g_visit->push_back(IdOf(n)); }
} // namespace

// =============================================================================
// TraverseTree — pre-order: visit node, descend firstChild, advance nextSibling.
// Tree:  A -> B(sibling) ; A has children C -> D ; C has child E.
//   expected pre-order from node=A: A, C, E, D, B
// =============================================================================
TEST(SceneWalk, TraverseTreePreOrder) {
    IdNode A, B, C, D, E;
    A.id=0; B.id=1; C.id=2; D.id=3; E.id=4;
    for (auto* p : {&A,&B,&C,&D,&E}) p->n.nodeType = 3; // fire on bit0
    g_ids = {{&A.n,0},{&B.n,1},{&C.n,2},{&D.n,3},{&E.n,4}};

    A.n.nextSibling = &B.n;  A.n.firstChild = &C.n;
    C.n.nextSibling = &D.n;  C.n.firstChild = &E.n;
    // terminators clear (default 0); chain ends at null siblings.

    std::vector<int> visits; g_visit = &visits;
    SceneWalkEnv env;  // root null; not used for the node!=null path
    char r = TraverseTree(nullptr, &A.n, RecordVisit, 0x01, env);
    CHECK_EQ(r, (char)1);
    std::vector<int> expect = {0, 2, 4, 3, 1};
    CHECK(visits == expect);
}

// Walk mask 0x200 suppresses sibling traversal: only the head node + its subtree
// (with the child mask & 0xFDFF still descending) are visited.
TEST(SceneWalk, TraverseTreeStopSiblings) {
    IdNode A, B, C;
    A.id=0; B.id=1; C.id=2;
    for (auto* p : {&A,&B,&C}) p->n.nodeType = 3;
    g_ids = {{&A.n,0},{&B.n,1},{&C.n,2}};
    A.n.nextSibling = &B.n;
    A.n.firstChild  = &C.n;

    std::vector<int> visits; g_visit = &visits;
    SceneWalkEnv env;
    // mask 0x201: bit0 fires the callback, bit 0x200 stops sibling advance.
    TraverseTree(nullptr, &A.n, RecordVisit, (i16)0x201, env);
    // A visited; child C descended with mask & 0xFDFF == 0x001 (still fires);
    // sibling B NOT visited (0x200 stop).
    std::vector<int> expect = {0, 2};
    CHECK(visits == expect);
}

// Empty walk mask returns 0 and visits nothing.
TEST(SceneWalk, TraverseTreeEmptyMask) {
    IdNode A; A.id=0; A.n.nodeType = 3;
    g_ids = {{&A.n,0}};
    std::vector<int> visits; g_visit = &visits;
    SceneWalkEnv env;
    CHECK_EQ(TraverseTree(nullptr, &A.n, RecordVisit, 0, env), (char)0);
    CHECK(visits.empty());
}

// Root-list path (node == null): walk root.childHead chain until the terminator.
// Realistic engine layout: each top-level scene child is a sibling-list HEAD, i.e.
// carries the +528 bit0 terminator, so the inner sibling walk stops after that
// child's own subtree and the outer root loop drives the iteration. Tree:
//   root.childHead = A(head) -> B(head) -> term ; A.child = C -> (C.child = D)
//   expected: A, C, D (A's pre-order subtree), then B.
TEST(SceneWalk, TraverseTreeRootList) {
    IdNode A, B, C, D, term;
    A.id=0; B.id=1; C.id=2; D.id=3; term.id=99;
    for (auto* p : {&A,&B,&C,&D}) p->n.nodeType = 3;
    A.n.flags528 = 0x01;  B.n.flags528 = 0x01;  // each root child is a list head
    term.n.flags528 = 0x01;
    g_ids = {{&A.n,0},{&B.n,1},{&C.n,2},{&D.n,3},{&term.n,99}};
    A.n.nextSibling = &B.n;  A.n.firstChild = &C.n;
    C.n.firstChild = &D.n;
    B.n.nextSibling = &term.n;  // loop stops AT the terminator sentinel

    UniverseRoot root;
    root.childHead = &A.n;
    SceneWalkEnv env;
    env.root = &root;
    env.listTerminator = &term.n;

    std::vector<int> visits; g_visit = &visits;
    TraverseTree(&root, nullptr, RecordVisit, 0x01, env);
    std::vector<int> expect = {0, 2, 3, 1};
    CHECK(visits == expect);
}

// =============================================================================
// WalkAndInvoke — int-callback gating.
//   callback returning 0 aborts the whole walk (returns 0).
//   callback returning <0 (bit7) visits but does NOT descend children.
// =============================================================================
namespace {
SceneNode* g_abortAt = nullptr;   // abort when visiting this node
SceneNode* g_noDescendAt = nullptr;
char GateCallback(SceneNode* n, std::intptr_t) {
    g_visit->push_back(IdOf(n));
    if (n == g_abortAt) return 0;
    if (n == g_noDescendAt) return (char)0x80; // negative: no descend
    return 1;
}
} // namespace

TEST(SceneWalk, WalkAndInvokeAbort) {
    IdNode A, B, C;
    A.id=0; B.id=1; C.id=2;
    for (auto* p : {&A,&B,&C}) p->n.nodeType = 3;
    g_ids = {{&A.n,0},{&B.n,1},{&C.n,2}};
    A.n.nextSibling = &B.n;
    B.n.nextSibling = &C.n;

    std::vector<int> visits; g_visit = &visits;
    g_abortAt = &B.n; g_noDescendAt = nullptr;
    SceneWalkEnv env;
    char r = WalkAndInvoke(nullptr, &A.n, GateCallback, 0x01, 0, env);
    CHECK_EQ(r, (char)0);                // aborted
    std::vector<int> expect = {0, 1};    // A then B; abort before C
    CHECK(visits == expect);
}

TEST(SceneWalk, WalkAndInvokeNoDescend) {
    IdNode A, C, D;  // A has children C->D
    A.id=0; C.id=2; D.id=3;
    for (auto* p : {&A,&C,&D}) p->n.nodeType = 3;
    g_ids = {{&A.n,0},{&C.n,2},{&D.n,3}};
    A.n.firstChild = &C.n;
    C.n.nextSibling = &D.n;

    std::vector<int> visits; g_visit = &visits;
    g_abortAt = nullptr; g_noDescendAt = &A.n;  // visit A, skip its children
    SceneWalkEnv env;
    char r = WalkAndInvoke(nullptr, &A.n, GateCallback, 0x01, 0, env);
    CHECK_EQ(r, (char)1);
    std::vector<int> expect = {0};   // only A; children not descended
    CHECK(visits == expect);
}

// userArg threads through unchanged to the callback (polymorphic intptr_t).
namespace {
std::intptr_t g_seenArg = 0;
char ArgCallback(SceneNode*, std::intptr_t a) { g_seenArg = a; return 1; }
}
TEST(SceneWalk, WalkAndInvokeUserArg) {
    IdNode A; A.id=0; A.n.nodeType = 3;
    g_ids = {{&A.n,0}};
    SceneWalkEnv env;
    int sentinel = 7;
    WalkAndInvoke(nullptr, &A.n, ArgCallback, 0x01,
                  reinterpret_cast<std::intptr_t>(&sentinel), env);
    CHECK_EQ(g_seenArg, reinterpret_cast<std::intptr_t>(&sentinel));
}

// =============================================================================
// Octree mesh-cell management (real mem-pool backed).
// =============================================================================
TEST(SceneWalk, AddAndRemoveMeshToCell) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(2048);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};

    // Two objects that pass the AddMeshToCell gate (drawCell with meshPresent set,
    // flags531 bit2).
    SceneCell objCell0, objCell1;
    int present = 1;
    objCell0.meshPresent = &present;
    objCell1.meshPresent = &present;
    SceneNode obj0, obj1;
    obj0.drawCell = &objCell0; obj0.flags531 = 0x04;
    obj1.drawCell = &objCell1; obj1.flags531 = 0x04;

    SceneCell cell;  // the target cell
    CHECK_EQ(AddMeshToCell(&obj0, &cell, pools), (char)1);
    CHECK_EQ(AddMeshToCell(&obj1, &cell, pools), (char)1);
    CHECK_EQ(cell.refCount, 2);
    CHECK(cell.listHead != nullptr);
    CHECK(cell.listTail != nullptr);
    CHECK(cell.listHead->obj == &obj0);          // FIFO append order
    CHECK(cell.listHead->next == cell.listTail);
    CHECK(cell.listTail->obj == &obj1);

    // Gate rejection: an object without the +531 bit2 is not added.
    SceneNode obj2; obj2.drawCell = &objCell0; obj2.flags531 = 0x00;
    CHECK_EQ(AddMeshToCell(&obj2, &cell, pools), (char)1);
    CHECK_EQ(cell.refCount, 2);                   // unchanged

    // Remove obj0 (the head): list re-links to obj1, refCount drops, +531 bit2 clears.
    CHECK_EQ(RemoveMeshRecursive(&obj0, &cell, pools), (char)1);
    CHECK_EQ(cell.refCount, 1);
    CHECK(cell.listHead == cell.listTail);
    CHECK(cell.listHead->obj == &obj1);
    CHECK_EQ(obj0.flags531 & 4, 0);

    // Remove obj1 (now both head & tail): list empties.
    CHECK_EQ(RemoveMeshRecursive(&obj1, &cell, pools), (char)1);
    CHECK_EQ(cell.refCount, 0);
    CHECK(cell.listHead == nullptr);
    CHECK(cell.listTail == nullptr);

    MemPoolFreeAll(&listPool, tr);
    MemPoolFreeAll(&blockPool, tr);
    tr.Shutdown();
}

// RemoveMeshRecursive descends octant children and frees an emptied child cell.
TEST(SceneWalk, RemoveMeshFreesEmptiedChild) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(2048);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};

    int present = 1;
    SceneCell objCell; objCell.meshPresent = &present;
    SceneNode obj; obj.drawCell = &objCell; obj.flags531 = 0x04;

    // root cell with one octant child cell, both holding obj.
    SceneCell* root = static_cast<SceneCell*>(MemPoolAlloc(&blockPool, tr, sizeof(SceneCell), 8));
    SceneCell* child = static_cast<SceneCell*>(MemPoolAlloc(&blockPool, tr, sizeof(SceneCell), 8));
    new (root) SceneCell();  new (child) SceneCell();
    root->octant[1] = child;

    AddMeshToCell(&obj, root, pools);
    AddMeshToCell(&obj, child, pools);
    CHECK_EQ(root->refCount, 1);
    CHECK_EQ(child->refCount, 1);

    // Remove obj from root: drains root's own list, recurses into child (which
    // empties to refCount 0 and gets freed + nulled).
    RemoveMeshRecursive(&obj, root, pools);
    CHECK_EQ(root->refCount, 0);
    CHECK(root->octant[1] == nullptr);  // emptied child freed & slot cleared

    MemPoolFree(&blockPool, tr, root);
    MemPoolFreeAll(&listPool, tr);
    MemPoolFreeAll(&blockPool, tr);
    tr.Shutdown();
}

// FreeNodeRecursive drains a cell subtree's mesh lists and frees every block.
TEST(SceneWalk, FreeNodeRecursiveDrains) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(2048);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};

    int present = 1;
    SceneCell objCell; objCell.meshPresent = &present;
    SceneNode obj; obj.drawCell = &objCell; obj.flags531 = 0x04;

    SceneCell* root = static_cast<SceneCell*>(MemPoolAlloc(&blockPool, tr, sizeof(SceneCell), 8));
    SceneCell* child = static_cast<SceneCell*>(MemPoolAlloc(&blockPool, tr, sizeof(SceneCell), 8));
    new (root) SceneCell(); new (child) SceneCell();
    root->octant[0] = child;
    AddMeshToCell(&obj, root, pools);
    AddMeshToCell(&obj, child, pools);

    // Free the whole subtree; both list nodes and both cell blocks are returned.
    CHECK(FreeNodeRecursive(root, pools) == root);

    // After freeing, allocating two fresh blocks should reuse the freed slots
    // (the pool's empty-chunk recycle). We just assert the tracker has no leak by
    // confirming a subsequent alloc/free round-trips cleanly.
    void* p = MemPoolAlloc(&blockPool, tr, sizeof(SceneCell), 8);
    CHECK(p != nullptr);
    MemPoolFree(&blockPool, tr, p);

    MemPoolFreeAll(&listPool, tr);
    MemPoolFreeAll(&blockPool, tr);
    tr.Shutdown();
}
