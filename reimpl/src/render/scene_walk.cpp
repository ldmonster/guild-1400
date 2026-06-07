#include "render/scene_walk.h"

namespace guild::render {

// gilde.exe 0x5ac6d8 — VIBE_SceneGraph_TestNodeFlag
//
// switch(nodeType) maps each type byte to one walk-mask bit (verbatim table).
bool TestNodeFlag(u8 nodeType, i16 walkMask) {
    switch (nodeType) {
    case 0: return (walkMask & 0x20)  != 0;
    case 1: return (walkMask & 0x80)  != 0;
    case 2: return (walkMask & 0x100) != 0;
    case 3: return (walkMask & 0x01)  == 1;
    case 4: return (walkMask & 0x40)  != 0;
    case 5: return (walkMask & 0x02)  != 0;
    case 6: return (walkMask & 0x04)  != 0;
    case 7: return (walkMask & 0x08)  != 0;
    case 8: return (walkMask & 0x10)  != 0;
    default: return false;
    }
}

// gilde.exe 0x5ac684 — VIBE_SceneGraph_GetFirstActiveChild
//
//   if (node->parent) return node->nextSibling;
//   else walk nextSibling chain until a (flags528 bit0) list terminator (or null).
SceneNode* GetFirstActiveChild(SceneNode* node) {
    if (node->parent)
        return node->nextSibling;
    SceneNode* result = node->nextSibling;
    while (result) {
        if (result->flags528 & 1)
            break;
        result = result->nextSibling;
    }
    return result;
}

// gilde.exe 0x5af298 — VIBE_Object_MarkDirtyFlag
//
//   flags528 |= 4; if (clearNoPivot) flagNoPivot &= ~0x80; flags531 &= ~1; return 1.
char MarkDirtyFlag(SceneNode* node, char clearNoPivot) {
    node->flags528 |= 4u;
    if (clearNoPivot)
        node->flagNoPivot &= (u8)~0x80u;
    node->flags531 &= (u8)~1u;
    return 1;
}

// gilde.exe 0x5b0b3c — VIBE_Object_LinkAsSibling
//
//   node->flags528 bit0 := (listHead->parent == nullptr);  node->parent := listHead->parent;
//   walk listHead's nextSibling chain to its tail; tail->nextSibling := node;
//   node->prevSibling := tail. Returns the tail.
SceneNode* LinkAsSibling(SceneNode* listHead, SceneNode* node) {
    bool headIsRoot = (listHead->parent == nullptr);
    u8 v3 = (u8)(node->flags528 & 0xFE);
    node->flags528 = v3;
    node->flags528 = (u8)((headIsRoot ? 1u : 0u) | v3);

    node->parent = listHead->parent;
    SceneNode* tail = listHead;
    while (tail->nextSibling)
        tail = tail->nextSibling;
    tail->nextSibling = node;
    node->prevSibling = tail;
    return tail;
}

// gilde.exe 0x5b0b9c — VIBE_Object_SetParent
//
//   flags528 bit0 := (parent == nullptr).
//   if (parent): if (parent->firstChild) LinkAsSibling(firstChild, node)
//                else parent->firstChild := node;   node->parent := parent.
//   else: linkIntoScene(node).
// The original returns a byte that is, on the LinkAsSibling path, the low byte of
// the returned tail pointer (a don't-care; the callers only test truthiness). We
// return 1 there (the engine's tail is always non-null), preserving observable
// behaviour; the linkIntoScene path returns that callback's byte.
char SetParent(SceneNode* parent, SceneNode* node, char (*linkIntoScene)(SceneNode*)) {
    u8 base = (u8)(node->flags528 & 0xFE);
    node->flags528 = base;
    node->flags528 = (u8)(((parent == nullptr) ? 1u : 0u) | base);

    char result = (char)node->flags528;
    if (parent) {
        if (parent->firstChild) {
            LinkAsSibling(parent->firstChild, node);
            result = 1;
        } else {
            parent->firstChild = node;
        }
        node->parent = parent;
    } else {
        result = linkIntoScene ? linkIntoScene(node) : 0;
    }
    return result;
}

namespace {
// Apply the four root-init copies (the dword_13FCF10/13FD140/1408438/140874C
// seeding) when a root walk targets the env's universe root.
void SeedRootInit(UniverseRoot* treeRoot, const SceneWalkEnv& env) {
    if (env.applyRootInit && treeRoot == env.root && treeRoot) {
        treeRoot->childHead = env.childInit;   // root[32]
        treeRoot->slot132   = env.slot132Init; // root[33]
        treeRoot->slot164   = env.slot164Init; // root[41]
        treeRoot->slot168   = env.slot168Init; // root[42]
    }
}
} // namespace

// gilde.exe 0x5ac86c — VIBE_SceneGraph_TraverseTree
char TraverseTree(UniverseRoot* treeRoot, SceneNode* node, void (*callback)(SceneNode*),
                  i16 walkMask, const SceneWalkEnv& env) {
    if (!walkMask)
        return 0;

    if (node) {
        SceneNode* v5 = node;
        while (true) {
            if (TestNodeFlag(v5->nodeType, walkMask))
                callback(v5);
            SceneNode* child = v5->firstChild;
            if (child)
                TraverseTree(treeRoot, child, callback, (i16)(walkMask & 0xFDFF), env);
            v5 = (walkMask & 0x200) ? nullptr : v5->nextSibling;
            if (!v5)
                break;
            if (v5->flags528 & 1)
                return 1;
        }
        return 1;
    }

    // node == null: seed the root-init copies, then walk the root's child list.
    SeedRootInit(treeRoot, env);
    if (treeRoot) {
        for (SceneNode* i = treeRoot->childHead; i; i = i->nextSibling) {
            if (i == env.listTerminator)
                break;
            TraverseTree(treeRoot, i, callback, walkMask, env);
        }
    }
    return 1;
}

// gilde.exe 0x5ac738 — VIBE_SceneGraph_WalkAndInvoke
char WalkAndInvoke(UniverseRoot* treeRoot, SceneNode* node,
                   char (*callback)(SceneNode*, std::intptr_t), i16 walkMask,
                   std::intptr_t userArg, const SceneWalkEnv& env) {
    if (!walkMask)
        return 0;

    if (node) {
        i16 childMask = (i16)(walkMask & 0xFDFF);
        SceneNode* v6 = node;
        while (true) {
            char v11 = 1;
            if (TestNodeFlag(v6->nodeType, walkMask))
                v11 = callback(v6, userArg);
            if (!v11)
                return 0;

            SceneNode* child = v6->firstChild;
            if (child && (v11 & 0x80) == 0)  // v11 >= 0
                v11 = WalkAndInvoke(treeRoot, child, callback, childMask, userArg, env);
            if (!v11)
                return 0;

            v6 = (walkMask & 0x200) ? nullptr : v6->nextSibling;
            if (!v6)
                return 1;
            if (v6->flags528 & 1)
                return 1;
        }
    }

    SeedRootInit(treeRoot, env);
    if (treeRoot) {
        SceneNode* v9 = treeRoot->childHead;
        if (v9) {
            while (v9 != env.listTerminator) {
                char result = WalkAndInvoke(treeRoot, v9, callback, walkMask, userArg, env);
                if (!result)
                    return result;
                v9 = v9->nextSibling;
                if (!v9)
                    return 1;
            }
        }
    }
    return 1;
}

// gilde.exe 0x5efe50 — VIBE_SceneGraph_FreeNodeRecursive
//
//   drain cell->listHead (free each node from cellListPool, caching node->next
//   before the free); recurse into the four octant children; free the cell block.
SceneCell* FreeNodeRecursive(SceneCell* cell, const SceneCellPools& pools) {
    if (!cell)
        return cell;

    MeshCellNode* listNode = cell->listHead;
    while (listNode) {
        MeshCellNode* next = listNode->next;  // cached before the free (ecx in orig)
        mem::MemPoolFree(pools.cellListPool, *pools.tracker, listNode);
        listNode = next;
    }

    for (int i = 0; i < 4; ++i) {
        if (cell->octant[i])
            FreeNodeRecursive(cell->octant[i], pools);
    }

    mem::MemPoolFree(pools.cellBlockPool, *pools.tracker, cell);
    return cell;
}

// gilde.exe 0x5efea0 — VIBE_SceneGraph_AddMeshToCell
//
//   gate: obj->drawCell != null && drawCell->meshPresent != null && obj->flags531 bit2.
//   then: ++cell->refCount; node = alloc8(cellListPool); node->obj = obj;
//         append node to cell's (listHead/listTail) singly-linked list.
char AddMeshToCell(SceneNode* obj, SceneCell* cell, const SceneCellPools& pools) {
    SceneCell* drawCell = obj->drawCell;
    if (!drawCell || !drawCell->meshPresent || (obj->flags531 & 4) == 0)
        return 1;

    ++cell->refCount;
    // The original allocates 8 bytes (two 4-byte pointers); on a 64-bit host the
    // list node is sizeof(MeshCellNode) (two 8-byte pointers). The pool zeroes the
    // slot, so node->next starts null.
    MeshCellNode* node = static_cast<MeshCellNode*>(mem::MemPoolAlloc(
        pools.cellListPool, *pools.tracker, sizeof(MeshCellNode), 128));
    node->obj = obj;            // list node [0] = object
    node->next = nullptr;
    MeshCellNode* tail = cell->listTail;
    if (tail)
        tail->next = node;      // old tail -> node
    else
        cell->listHead = node;  // empty list: node becomes head
    cell->listTail = node;      // node is the new tail
    return 1;
}

// gilde.exe 0x5f0a74 — VIBE_SceneGraph_RemoveMeshRecursive
//
//   scan cell->listHead for the node whose obj == `obj`; unlink it (fix prev->next /
//   head and the tail), decrement cell->refCount, free the list node. Recurse into
//   the four octant children; free+null a child cell that empties (refCount == 0).
//   Finally clear obj->flags531 bit2. Returns 1.
char RemoveMeshRecursive(SceneNode* obj, SceneCell* cell, const SceneCellPools& pools) {
    MeshCellNode* v4 = cell->listHead;
    MeshCellNode* v5 = nullptr;  // trailing pointer
    if (!v4)
        return 1;

    while (obj != v4->obj) {
        v5 = v4;
        v4 = v4->next;
        if (!v4)
            return 1;
    }

    if (v5)
        v5->next = v4->next;          // prev->next = found->next
    else
        cell->listHead = v4->next;    // head = found->next
    if (v4 == cell->listTail)         // found was the tail
        cell->listTail = v5;
    --cell->refCount;
    mem::MemPoolFree(pools.cellListPool, *pools.tracker, v4);

    for (int i = 0; i < 4; ++i) {
        SceneCell*& slot = cell->octant[i];
        if (slot) {
            RemoveMeshRecursive(obj, slot, pools);
            if (slot->refCount == 0) {
                mem::MemPoolFree(pools.cellBlockPool, *pools.tracker, slot);
                slot = nullptr;
            }
        }
    }

    obj->flags531 &= (u8)~4u;
    return 1;
}

} // namespace guild::render
