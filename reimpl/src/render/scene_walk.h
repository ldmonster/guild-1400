#pragma once
#include "guild/common/types.h"
#include "mem/mempool.h"
#include <cstdint>  // std::intptr_t

// =============================================================================
// guild::render — the RAW scene-graph node walk + node/mesh-cell management
// family from gilde.exe (d3_engine.c). Faithful 1:1 reconstruction of the
// generic scene-graph traversal that drives per-object dirty-flag propagation,
// shadow-caster reset, AABB accumulation and octree mesh-cell bookkeeping:
//
//   0x5ac6d8  VIBE_SceneGraph_TestNodeFlag          (node-type -> walk-mask bit)
//   0x5ac684  VIBE_SceneGraph_GetFirstActiveChild   (first non-suspended child)
//   0x5ac86c  VIBE_SceneGraph_TraverseTree          (void-callback pre-order walk)
//   0x5ac738  VIBE_SceneGraph_WalkAndInvoke         (int-callback gated walk)
//   0x5af298  VIBE_Object_MarkDirtyFlag             (per-node dirty-flag stamp)
//   0x5b0b3c  VIBE_Object_LinkAsSibling             (append to a sibling chain)
//   0x5b0b9c  VIBE_Object_SetParent                 (re-parent / link-into-scene)
//   0x5efe50  VIBE_SceneGraph_FreeNodeRecursive     (free a cell's octree subtree)
//   0x5efea0  VIBE_SceneGraph_AddMeshToCell         (push a mesh node into a cell)
//   0x5f0a74  VIBE_SceneGraph_RemoveMeshRecursive   (unlink a mesh from an octree)
//   0x5f0b18  VIBE_SceneGraph_RemoveMeshFromTree    (top-level mesh removal)
//
// THE SCENE NODE (recovered byte offsets, from the raw *(type*)(node+off) access)
// ---------------------------------------------------------------------------
// The engine's scene/object node is one opaque block; the walk family touches:
//   +0x1EC (492)  drawData/octreeCell pointer (the +260 = mesh-list presence test)
//   +0x1F0 (496)  next sibling          ( node[124] in the original )
//   +0x1F4 (500)  prev sibling          ( for the doubly-linked sibling chain )
//   +0x1F8 (504)  parent                ( 0 => node is a sibling-list head )
//   +0x1FC (508)  first child           ( node[127]; descended by the walk )
//   +0x210 (528)  flags byte 0: bit0 = sibling-chain terminator / list head,
//                               bit2 = "transform dirty"
//   +0x213 (531)  flags byte 3: bit0 = active, bit2 = mesh-in-cell present
//   +0x215 (533)  node-type byte (0..8) -> TestNodeFlag dispatch
// Octree CELL block (the +0x1EC drawData, and the a2 of the mesh-cell ops):
//   +0x00..+0x0F  four octant child-cell pointers (FreeNodeRecursive scans these)
//   +0x0C (12)    mesh count in this cell (RemoveMeshRecursive decrements)
//   +0x30 (48)    live-mesh refcount (AddMeshToCell increments; 0 => free cell)
//   +0x3C (60)    mesh-cell list HEAD  ( cell[15] )
//   +0x40 (64)    mesh-cell list TAIL
//   +0x104 (260)  mesh-list-present sentinel (AddMeshToCell gate)
// MESH-CELL list node (allocated from cellPool, 8 bytes):
//   +0x00 object pointer        +0x04 next cell-list node
// =============================================================================
namespace guild::render {

struct SceneCell;  // forward: octree cell block (the +0x1EC drawCell payload)

// ---------------------------------------------------------------------------
// SceneNode — the scene/object node. The original is one opaque ~536-byte block
// addressed by raw byte offsets; on a 64-bit host the 4-byte pointer spacing
// (496/500/504/508) cannot be preserved literally, so the links the walk family
// touches are modelled as NAMED members (mirroring the documented offsets) while
// the per-node flag/type bytes keep their exact bit semantics. This matches the
// abstraction the existing render/scenegraph.h + scene.h walks already use.
//   nodeType  -> +0x215 (533): TestNodeFlag dispatch byte.
//   flags528  -> +0x210 (528): bit0 = sibling-chain terminator / list head,
//                              bit2 = "transform dirty".
//   flagNoPivot-> the +0x212 (530) bit7 "no-pivot" sign flag MarkDirtyFlag clears.
//   flags531  -> +0x213 (531): bit2 = mesh-in-cell present.
// ---------------------------------------------------------------------------
struct SceneNode {
    SceneCell* drawCell = nullptr;       // +0x1EC (492)  octree cell / draw-data
    SceneNode* nextSibling = nullptr;    // +0x1F0 (496)  node[124]
    SceneNode* prevSibling = nullptr;    // +0x1F4 (500)
    SceneNode* parent = nullptr;         // +0x1F8 (504)
    SceneNode* firstChild = nullptr;     // +0x1FC (508)  node[127]
    u8   flags528 = 0;                   // +0x210 (528)
    u8   flagNoPivot = 0;                // +0x212 (530) bit7 source byte
    u8   flags531 = 0;                   // +0x213 (531)
    u8   nodeType = 0;                   // +0x215 (533)
};

// ---------------------------------------------------------------------------
// MeshCellNode — the 8-byte mesh-cell list node allocated from cellListPool
// (the +0/+4 the original reads). [0] = object, [4] = next list node.
// ---------------------------------------------------------------------------
struct MeshCellNode {
    SceneNode* obj = nullptr;        // +0x00
    MeshCellNode* next = nullptr;    // +0x04
};

// ---------------------------------------------------------------------------
// SceneCell — an octree cell block (the SceneNode::drawCell payload). Modelled
// with named members at the documented dword indices:
//   octant[4] -> dword[0..3]  = bytes +0x00..+0x0F  four child-cell pointers
//   refCount  -> dword[12]    = +0x30 (48)  live-mesh refcount (0 => cell is free;
//                              AddMeshToCell ++ / RemoveMeshRecursive --)
//   listHead  -> dword[15]    = +0x3C (60)  mesh-cell list head
//   listTail  -> dword[16]    = +0x40 (64)  mesh-cell list tail
//   meshPresent-> +0x104 (260) AddMeshToCell gate sentinel
// ---------------------------------------------------------------------------
struct SceneCell {
    SceneCell* octant[4] = {nullptr, nullptr, nullptr, nullptr}; // dword[0..3]
    i32  refCount = 0;                // dword[12] = +0x30 (48)
    MeshCellNode* listHead = nullptr; // dword[15] = +0x3C (60)
    MeshCellNode* listTail = nullptr; // dword[16] = +0x40 (64)
    void* meshPresent = nullptr;      // +0x104 (260) gate sentinel
};

// ---------------------------------------------------------------------------
// UniverseRoot — the active universe record (off_649D64). The walk descends its
// child list (root + 128) when invoked with node == nullptr. The four init slots
// at +128/+132/+164/+168 are the dword copies the root walk seeds when (treeRoot
// == off_649D64): childHead<-dword_13FCF10, slot132<-dword_13FD140,
// slot164<-dword_1408438, slot168<-dword_140874C. Modelled with named members so
// the layout is host-pointer-width-independent.
// ---------------------------------------------------------------------------
struct UniverseRoot {
    SceneNode* childHead = nullptr;  // +0x80  (128)  root child-list head
    void* slot132 = nullptr;         // +0x84  (132)
    void* slot164 = nullptr;         // +0xA4  (164)
    void* slot168 = nullptr;         // +0xA8  (168)
};

// ---------------------------------------------------------------------------
// SceneWalkEnv — the file-scope universe state the raw walk reads when it is
// asked to descend the ROOT's child list (node == nullptr). Mirrors:
//   root            off_649D64        (the active universe record)
//   listTerminator  dword_13FCF4C     (the root child-list sentinel)
// When `applyRootInit` and the walked `treeRoot` IS `root`, the four init slots
// (childInit/slot132Init/slot164Init/slot168Init) are copied into the root before
// descending (the dword_13FCF10/13FD140/1408438/140874C seeding in the original).
// ---------------------------------------------------------------------------
struct SceneWalkEnv {
    UniverseRoot* root = nullptr;        // off_649D64
    SceneNode* listTerminator = nullptr; // dword_13FCF4C
    bool  applyRootInit = false;
    SceneNode* childInit = nullptr;      // dword_13FCF10 -> root.childHead
    void* slot132Init = nullptr;         // dword_13FD140 -> root.slot132
    void* slot164Init = nullptr;         // dword_1408438 -> root.slot164
    void* slot168Init = nullptr;         // dword_140874C -> root.slot168
};

// gilde.exe 0x5ac6d8 — VIBE_SceneGraph_TestNodeFlag  (__usercall al=fn(type@al, mask@dx))
//   Maps a node-type byte (0..8) to a single bit of the walk mask: returns true if
//   that bit is set. Type->bit table (verbatim): 0->0x20, 1->0x80, 2->0x100,
//   3->0x01, 4->0x40, 5->0x02, 6->0x04, 7->0x08, 8->0x10; any other type -> false.
bool TestNodeFlag(u8 nodeType, i16 walkMask);

// gilde.exe 0x5ac684 — VIBE_SceneGraph_GetFirstActiveChild (__usercall eax=fn(node@eax))
//   If the node has a parent (node+504 != 0) it returns the node's first sibling
//   (node+496). Otherwise it walks the sibling chain from node+496 until it finds a
//   list-terminator node ((sibling+528 & 1) != 0), returning it (or null).
SceneNode* GetFirstActiveChild(SceneNode* node);

// gilde.exe 0x5af298 — VIBE_Object_MarkDirtyFlag (__usercall al=fn(node@eax, clearNoPivot@dl))
//   Sets node+528 bit2 (transform dirty). If `clearNoPivot` is non-zero, clears
//   node+530 bit7 (the "no-pivot" sign flag). Always clears node+531 bit0. Returns 1.
//   (Used as the WalkAndInvoke callback during SetPosition/SetWorldTranslation.)
char MarkDirtyFlag(SceneNode* node, char clearNoPivot);

// gilde.exe 0x5b0b3c — VIBE_Object_LinkAsSibling (__usercall eax=fn(listHead@eax, node@edx))
//   Appends `node` to the END of `listHead`'s sibling chain. `node`'s +528 bit0
//   (list-head terminator) is set iff `listHead` has no parent (listHead+504 == 0);
//   `node` inherits listHead's parent (node+504 = listHead+504). Returns the tail.
SceneNode* LinkAsSibling(SceneNode* listHead, SceneNode* node);

// gilde.exe 0x5b0b9c — VIBE_Object_SetParent (__usercall al=fn(parent@eax, node@edx))
//   Re-parents `node` under `parent`. node+528 bit0 := (parent == nullptr). When
//   `parent` is non-null: if it already has a child (parent+508) the node is linked
//   as a sibling onto that chain, else it becomes the first child; node+504 = parent.
//   When `parent` is null the node is linked into the scene root via `linkIntoScene`
//   (an injected callback mirroring VIBE_Object_LinkIntoScene, which is owned by the
//   scene/camera-global module). Returns the last result byte.
char SetParent(SceneNode* parent, SceneNode* node, char (*linkIntoScene)(SceneNode*));

// gilde.exe 0x5ac86c — VIBE_SceneGraph_TraverseTree
//   (__usercall al=fn(treeRoot@eax, node@edx, callback@ecx, walkMask@bx))
//   Pre-order walk that invokes a VOID callback per node whose type passes
//   TestNodeFlag(node+533, walkMask). For node != null it walks the sibling chain
//   from `node`: visit, descend node+508 (child) with mask & 0xFDFF, advance to
//   node+496 (sibling) unless walkMask & 0x200, stopping at a +528-bit0 terminator.
//   For node == null it (optionally) seeds the root-init globals and walks the
//   root's child list (root+128) until the env's list terminator. Returns 1 (0 only
//   on an empty walk mask).
char TraverseTree(UniverseRoot* treeRoot, SceneNode* node, void (*callback)(SceneNode*),
                  i16 walkMask, const SceneWalkEnv& env);

// gilde.exe 0x5ac738 — VIBE_SceneGraph_WalkAndInvoke
//   (__userpurge al=fn(treeRoot@eax, node@edx, callback@ecx, walkMask@bx, userArg))
//   Same structure as TraverseTree but the callback returns a control byte:
//     callback(node, userArg): 0 => abort the whole walk (return 0),
//       <0 => visit but do NOT descend children, >0 => descend.
//   Nodes whose type fails TestNodeFlag are visited as if the callback returned 1
//   (descend, no side effect). Returns 1 on a completed walk, 0 if aborted.
//   `userArg` is the original's a5: a polymorphic 32-bit value that is sometimes a
//   small int (SetPosition passes 1) and sometimes a pointer (RemoveMeshFromTree
//   passes the region cell). It is typed std::intptr_t so it round-trips either on
//   a 64-bit host.
char WalkAndInvoke(UniverseRoot* treeRoot, SceneNode* node,
                   char (*callback)(SceneNode*, std::intptr_t), i16 walkMask,
                   std::intptr_t userArg, const SceneWalkEnv& env);

// ---------------------------------------------------------------------------
// Octree mesh-cell management (mem-pool backed). The two pools mirror the file-
// scope MemPool heads dword_64A7D0 (8-byte mesh-cell list nodes) and dword_64A7CC
// (cell blocks). Gathered into one context so the ops stay re-entrant/testable.
// ---------------------------------------------------------------------------
struct SceneCellPools {
    mem::MemPool* cellListPool = nullptr; // dword_64A7D0: 8-byte list nodes
    mem::MemPool* cellBlockPool = nullptr;// dword_64A7CC: octree cell blocks
    mem::MemoryTracker* tracker = nullptr;
};

// gilde.exe 0x5efe50 — VIBE_SceneGraph_FreeNodeRecursive (__usercall eax=fn(cell@eax))
//   Frees an octree cell subtree: drains the cell's mesh-cell list (cell+60, each
//   8-byte node freed from cellListPool, next cached from node+4 before the free),
//   recurses into the four octant child cells (cell[0..3]), then frees the cell
//   block itself from cellBlockPool. Returns the cell pointer (unchanged).
SceneCell* FreeNodeRecursive(SceneCell* cell, const SceneCellPools& pools);

// gilde.exe 0x5efea0 — VIBE_SceneGraph_AddMeshToCell (__usercall al=fn(obj@eax, cell@edx))
//   Pushes object `obj` into octree `cell`'s mesh-cell list, but only when `obj` has
//   a draw cell with a mesh list (*(obj+492) != 0 && *(obj+492 +260) != 0) and
//   obj+531 bit2 is set. Increments cell+48 (refcount), allocates an 8-byte list node
//   from cellListPool ([0]=obj), and appends it to the cell's (+60 head, +64 tail)
//   list. Returns 1. (The original requests 8 bytes for the list node — two 4-byte
//   pointers; on a 64-bit host this becomes sizeof(MeshCellNode) = two 8-byte ptrs.)
char AddMeshToCell(SceneNode* obj, SceneCell* cell, const SceneCellPools& pools);

// gilde.exe 0x5f0a74 — VIBE_SceneGraph_RemoveMeshRecursive (__usercall al=fn(obj@eax, cell@edx))
//   Removes object `obj` from octree `cell`: scans the cell's mesh-cell list
//   (cell+60) for the [0]==obj node, unlinks it (fixing head/tail at +60/+64),
//   decrements cell+12 and frees the list node. Then recurses into the four octant
//   children (cell[0..3]); a child cell that empties (child+48 == 0) is freed and
//   its slot nulled. Finally clears obj+531 bit2. Returns 1.
char RemoveMeshRecursive(SceneNode* obj, SceneCell* cell, const SceneCellPools& pools);

// NOTE: VIBE_SceneGraph_RemoveMeshFromTree @0x5f0b18 (the top-level driver that
// routes RemoveMeshRecursive through WalkAndInvoke over an octree REGION node's
// +68 mask) is intentionally deferred: it operates on a region-node block distinct
// from SceneCell whose layout is not yet mapped here, and threading the cell pools
// through the fixed-ABI walk callback would require speculation. Listed in the
// module report.

} // namespace guild::render
