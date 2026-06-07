#include "world/stammbaum_query.h"

// Multi-generation family-tree queries built on the parent/child link walks in
// world/stammbaum.cpp + world/family_query.cpp. These model the deeper traversals
// the family-tree window and the office-succession relative collectors perform.

namespace guild::world {

namespace {

// Shortest upward distance from `fromId` to `ancestorId` (father preferred), or -1.
// Cycle-bounded by `guard`.
int UpwardDistance(const FamilyTree& tree, i32 ancestorId, i32 fromId, int guard) {
    if (fromId == kFamilyNone || guard < 0)
        return -1;
    if (fromId == ancestorId)
        return 0;
    const FamilyRecord* rec = tree.Find(fromId);
    if (!rec)
        return -1;
    int best = -1;
    if (rec->father != kFamilyNone) {
        int d = UpwardDistance(tree, ancestorId, rec->father, guard - 1);
        if (d >= 0 && (best < 0 || d + 1 < best))
            best = d + 1;
    }
    if (rec->mother != kFamilyNone) {
        int d = UpwardDistance(tree, ancestorId, rec->mother, guard - 1);
        if (d >= 0 && (best < 0 || d + 1 < best))
            best = d + 1;
    }
    return best;
}

// Appends `id` to out[0..n) if not already present and within maxOut; returns new n.
int PushUnique(i32* out, int n, int maxOut, i32 id) {
    if (id == kFamilyNone || n >= maxOut)
        return n;
    for (int i = 0; i < n; ++i)
        if (out[i] == id)
            return n;
    out[n] = id;
    return n + 1;
}

} // namespace

int FamilyGenerationDistance(const FamilyTree& tree, i32 ancestorId, i32 descendantId) {
    return UpwardDistance(tree, ancestorId, descendantId, tree.count + 1);
}

int FamilyCollectAncestorsAtGen(const FamilyTree& tree, i32 personId,
                                int generations, i32* out, int maxOut) {
    if (!out || maxOut <= 0)
        return 0;
    if (generations <= 0) {
        out[0] = personId;
        return personId == kFamilyNone ? 0 : 1;
    }
    // Frontier of the current generation; expand each id's two parents one level.
    i32 frontier[64];
    int fn = 0;
    if (personId != kFamilyNone)
        frontier[fn++] = personId;
    for (int g = 0; g < generations; ++g) {
        i32 next[64];
        int nn = 0;
        for (int i = 0; i < fn; ++i) {
            const FamilyRecord* rec = tree.Find(frontier[i]);
            if (!rec)
                continue;
            if (rec->father != kFamilyNone && nn < 64) {
                bool dup = false;
                for (int j = 0; j < nn; ++j) dup |= next[j] == rec->father;
                if (!dup) next[nn++] = rec->father;
            }
            if (rec->mother != kFamilyNone && nn < 64) {
                bool dup = false;
                for (int j = 0; j < nn; ++j) dup |= next[j] == rec->mother;
                if (!dup) next[nn++] = rec->mother;
            }
        }
        fn = nn;
        for (int i = 0; i < nn; ++i) frontier[i] = next[i];
        if (fn == 0)
            break;
    }
    int n = 0;
    for (int i = 0; i < fn; ++i)
        n = PushUnique(out, n, maxOut, frontier[i]);
    return n;
}

int FamilyCollectDescendantsAtGen(const FamilyTree& tree, i32 personId,
                                  int generations, i32* out, int maxOut) {
    if (!out || maxOut <= 0)
        return 0;
    if (generations <= 0) {
        out[0] = personId;
        return personId == kFamilyNone ? 0 : 1;
    }
    i32 frontier[64];
    int fn = 0;
    if (personId != kFamilyNone)
        frontier[fn++] = personId;
    for (int g = 0; g < generations; ++g) {
        i32 next[64];
        int nn = 0;
        for (int i = 0; i < fn; ++i) {
            const FamilyRecord* rec = tree.Find(frontier[i]);
            if (!rec)
                continue;
            i32 kids[kMaxChildren];
            int k = FamilyGetChildren(*rec, kids, kMaxChildren);
            for (int c = 0; c < k && nn < 64; ++c) {
                bool dup = false;
                for (int j = 0; j < nn; ++j) dup |= next[j] == kids[c];
                if (!dup) next[nn++] = kids[c];
            }
        }
        fn = nn;
        for (int i = 0; i < nn; ++i) frontier[i] = next[i];
        if (fn == 0)
            break;
    }
    int n = 0;
    for (int i = 0; i < fn; ++i)
        n = PushUnique(out, n, maxOut, frontier[i]);
    return n;
}

int FamilyResolveInheritance(const FamilyTree& tree, i32 personId,
                             i32* out, int maxOut) {
    if (!out || maxOut <= 0)
        return 0;
    // Primary path: the eldest-child line (heir of heir of ...).
    int n = FamilyHeirLine(tree, personId, out, maxOut);
    if (n > 0)
        return n;
    // No direct line: fall back to the nearest living relatives (siblings).
    return FamilyGetSiblings(tree, personId, out, maxOut);
}

bool FamilyAreBloodRelated(const FamilyTree& tree, i32 aId, i32 bId, int maxGenerations) {
    if (aId == kFamilyNone || bId == kFamilyNone)
        return false;
    if (aId == bId)
        return true;
    // Collect a's ancestors up to maxGenerations (including a itself), then test
    // whether any is an ancestor of (or equal to) b within maxGenerations.
    for (int g = 0; g <= maxGenerations; ++g) {
        i32 anc[64];
        int k = FamilyCollectAncestorsAtGen(tree, aId, g, anc, 64);
        for (int i = 0; i < k; ++i) {
            if (anc[i] == bId)
                return true;
            int d = FamilyGenerationDistance(tree, anc[i], bId);
            if (d >= 0 && d <= maxGenerations)
                return true;
        }
    }
    return false;
}

} // namespace guild::world
