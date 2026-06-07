#include "world/family_query.h"

// Descendant + heir-line family-tree queries, built on the FamilyTree/FamilyRecord
// recovered in world/stammbaum.h. The downward walks mirror the office-succession
// collectors that chase a person's children when choosing an heir.

namespace guild::world {

// Dual of FamilyIsAncestorOf: descendant(a, d) == ancestor(a, d).
bool FamilyIsDescendantOf(const FamilyTree& tree, i32 ancestorId, i32 descendantId) {
    if (descendantId == kFamilyNone)
        return false;
    return FamilyIsAncestorOf(tree, ancestorId, descendantId);
}

int FamilyCollectDescendants(const FamilyTree& tree, i32 personId,
                             i32* out, int maxOut) {
    if (maxOut <= 0)
        return 0;
    const FamilyRecord* self = tree.Find(personId);
    if (!self)
        return 0;

    int n = 0;
    // BFS over the descendant frontier. `out` doubles as the queue: we enqueue
    // children as we visit, then expand each enqueued id in turn. The visit bound
    // (tree.count) guards against malformed cycles.
    int head  = 0;
    int guard = tree.count + 1;
    // Seed with the person's direct children.
    {
        i32 kids[kMaxChildren];
        int k = FamilyGetChildren(*self, kids, kMaxChildren);
        for (int i = 0; i < k && n < maxOut; ++i)
            out[n++] = kids[i];
    }
    while (head < n && guard-- > 0) {
        const FamilyRecord* cur = tree.Find(out[head++]);
        if (!cur)
            continue;
        i32 kids[kMaxChildren];
        int k = FamilyGetChildren(*cur, kids, kMaxChildren);
        for (int i = 0; i < k && n < maxOut; ++i)
            out[n++] = kids[i];
    }
    return n;
}

int FamilyHeirLine(const FamilyTree& tree, i32 personId, i32* out, int maxOut) {
    int n = 0;
    const FamilyRecord* cur = tree.Find(personId);
    int guard = tree.count + 1;
    while (cur && n < maxOut && guard-- > 0) {
        // Primary heir = first non-empty child slot (eldest-child line).
        i32 heir = kFamilyNone;
        for (int i = 0; i < kMaxChildren; ++i) {
            if (cur->children[i] != kFamilyNone) {
                heir = cur->children[i];
                break;
            }
        }
        if (heir == kFamilyNone)
            break;                 // line ends (no children)
        out[n++] = heir;
        cur = tree.Find(heir);
    }
    return n;
}

} // namespace guild::world
