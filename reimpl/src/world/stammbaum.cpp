#include "world/stammbaum.h"

// Family-tree relationship queries derived from the link offsets the family-tree
// window (gilde.exe 0x55ab84) reads off the person record.

namespace guild::world {

const FamilyRecord* FamilyTree::Find(i32 personId) const {
    if (!records || personId == kFamilyNone)
        return nullptr;
    for (int i = 0; i < count; ++i) {
        if (records[i].id == personId)
            return &records[i];
    }
    return nullptr;
}

i32 FamilyGetFather(const FamilyRecord& rec) { return rec.father; }
i32 FamilyGetMother(const FamilyRecord& rec) { return rec.mother; }
i32 FamilyGetSpouse(const FamilyRecord& rec) { return rec.spouse; }

int FamilyGetChildren(const FamilyRecord& rec, i32* out, int maxOut) {
    int n = 0;
    for (int i = 0; i < kMaxChildren && n < maxOut; ++i) {
        if (rec.children[i] != kFamilyNone)   // window skips -1 child slots
            out[n++] = rec.children[i];
    }
    return n;
}

bool FamilyIsParentOf(const FamilyTree& tree, i32 parentId, i32 childId) {
    if (parentId == kFamilyNone)
        return false;
    const FamilyRecord* child = tree.Find(childId);
    if (!child)
        return false;
    return child->father == parentId || child->mother == parentId;
}

// gilde.exe 0x5555c8 (recoverable rule) — collect up to kMaxHeirs children.
int FamilyCollectHeirs(const FamilyTree& tree, i32 personId, i32* out, int maxOut) {
    const FamilyRecord* self = tree.Find(personId);
    if (!self)
        return 0;
    int cap = maxOut < kMaxHeirs ? maxOut : kMaxHeirs;  // v7 < 4
    return FamilyGetChildren(*self, out, cap);
}

// Upward ancestor walk over father/mother links (cycle-bounded by tree size).
bool FamilyIsAncestorOf(const FamilyTree& tree, i32 ancestorId, i32 descendantId) {
    if (ancestorId == kFamilyNone)
        return false;
    const FamilyRecord* cur = tree.Find(descendantId);
    int guard = tree.count + 1;            // cycle guard
    while (cur && guard-- > 0) {
        if (cur->father == ancestorId || cur->mother == ancestorId)
            return true;
        // A node has up to two parents: chase the mother branch by recursion,
        // then follow the father chain iteratively.
        if (cur->mother != kFamilyNone
            && FamilyIsAncestorOf(tree, ancestorId, cur->mother))
            return true;
        cur = (cur->father != kFamilyNone) ? tree.Find(cur->father) : nullptr;
    }
    return false;
}

int FamilyGetSiblings(const FamilyTree& tree, i32 personId, i32* out, int maxOut) {
    const FamilyRecord* self = tree.Find(personId);
    if (!self || !tree.records)
        return 0;
    int n = 0;
    for (int i = 0; i < tree.count && n < maxOut; ++i) {
        const FamilyRecord& other = tree.records[i];
        if (other.id == personId)
            continue;
        bool sharesFather = self->father != kFamilyNone && other.father == self->father;
        bool sharesMother = self->mother != kFamilyNone && other.mother == self->mother;
        if (sharesFather || sharesMother)
            out[n++] = other.id;
    }
    return n;
}

} // namespace guild::world
