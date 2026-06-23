// Boundary / malformed hardening tests for the family-tree queries:
//   world/stammbaum.{h,cpp}        — FamilyTree::Find, parent/child/sibling,
//                                     FamilyCollectHeirs, FamilyIsAncestorOf
//   world/family_query.{h,cpp}     — FamilyCollectDescendants, FamilyHeirLine
//   world/stammbaum_query.{h,cpp}  — FamilyGenerationDistance, *AtGen, inheritance
//
// Focus (wave-12): cyclic / deeply-self-referential parent & child chains, person
// ids out of range, empty / single-element trees, and small/zero out buffers must
// terminate (the tree-size cycle guards) and never read or write out of bounds.
// These run clean under ASan+UBSan; valid-input behaviour stays byte-identical.
#include "test.h"

#include "world/stammbaum.h"
#include "world/stammbaum_query.h"
#include "world/family_query.h"

#include <vector>

using namespace guild;
using namespace guild::world;

namespace {

FamilyRecord MkRec(i32 id, i32 father = kFamilyNone, i32 mother = kFamilyNone) {
    FamilyRecord r{};
    r.id = id;
    r.father = father;
    r.mother = mother;
    for (int i = 0; i < kMaxChildren; ++i)
        r.children[i] = kFamilyNone;
    return r;
}

} // namespace

// --- Out-of-range / unknown person ids resolve to "not found", not OOB --------
TEST(FamilyHarden, UnknownIdQueriesAreEmpty) {
    std::vector<FamilyRecord> recs = {MkRec(1), MkRec(2)};
    FamilyTree t{recs.data(), static_cast<int>(recs.size())};

    // Find on an id not present, on kFamilyNone, and on a wild value.
    CHECK(t.Find(999) == nullptr);
    CHECK(t.Find(kFamilyNone) == nullptr);
    CHECK(t.Find(0x7FFFFFFF) == nullptr);

    i32 out[8];
    CHECK_EQ(FamilyGetSiblings(t, 999, out, 8), 0);
    CHECK_EQ(FamilyCollectHeirs(t, 999, out, 8), 0);
    CHECK_EQ(FamilyCollectDescendants(t, 999, out, 8), 0);
    CHECK_EQ(FamilyHeirLine(t, 999, out, 8), 0);
    CHECK(!FamilyIsAncestorOf(t, 1, 999));
    CHECK(!FamilyIsParentOf(t, 1, 999));
}

// --- Null / empty tree: every query is a safe no-op ---------------------------
TEST(FamilyHarden, NullAndEmptyTree) {
    FamilyTree nullTree{nullptr, 0};
    i32 out[8];
    CHECK(nullTree.Find(1) == nullptr);
    CHECK_EQ(FamilyGetSiblings(nullTree, 1, out, 8), 0);
    CHECK_EQ(FamilyCollectDescendants(nullTree, 1, out, 8), 0);
    CHECK(!FamilyIsAncestorOf(nullTree, 1, 2));

    std::vector<FamilyRecord> one = {MkRec(5)};
    FamilyTree t1{one.data(), 1};
    CHECK_EQ(FamilyHeirLine(t1, 5, out, 8), 0);     // no children
    CHECK_EQ(FamilyCollectDescendants(t1, 5, out, 8), 0);
}

// --- Cyclic parent chain (A's father is B, B's father is A) must terminate -----
TEST(FamilyHarden, CyclicParentChainTerminates) {
    std::vector<FamilyRecord> recs = {MkRec(1, /*father=*/2), MkRec(2, /*father=*/1)};
    FamilyTree t{recs.data(), static_cast<int>(recs.size())};

    // IsAncestorOf is bounded by tree.count; a 2-cycle must not loop forever.
    CHECK(FamilyIsAncestorOf(t, 2, 1));   // 1's father is 2 -> true
    CHECK(FamilyIsAncestorOf(t, 1, 2));   // 2's father is 1 -> true
    // A third id that is not in the cycle is not an ancestor.
    CHECK(!FamilyIsAncestorOf(t, 99, 1));

    // Generation distance over the cycle: bounded by guard, returns >= 0 or -1.
    int d = FamilyGenerationDistance(t, 2, 1);
    CHECK_EQ(d, 1);
}

// --- Cyclic CHILD chain (A child B, B child A) must terminate in BFS -----------
TEST(FamilyHarden, CyclicChildChainTerminates) {
    FamilyRecord a = MkRec(1);
    FamilyRecord b = MkRec(2);
    a.children[0] = 2;   // 1 -> 2
    b.children[0] = 1;   // 2 -> 1  (cycle)
    std::vector<FamilyRecord> recs = {a, b};
    FamilyTree t{recs.data(), static_cast<int>(recs.size())};

    i32 out[16];
    int n = FamilyCollectDescendants(t, 1, out, 16);   // guarded by tree.count
    CHECK(n >= 1);
    CHECK(n <= 16);

    // HeirLine follows the eldest-child line; the cycle must not loop forever.
    i32 line[16];
    int ln = FamilyHeirLine(t, 1, line, 16);
    CHECK(ln >= 1);
    CHECK(ln <= 16);   // bounded by guard (tree.count+1) and maxOut
}

// --- Self-parent (id is its own father) must terminate ------------------------
TEST(FamilyHarden, SelfParentTerminates) {
    std::vector<FamilyRecord> recs = {MkRec(7, /*father=*/7, /*mother=*/7)};
    FamilyTree t{recs.data(), static_cast<int>(recs.size())};
    CHECK(FamilyIsAncestorOf(t, 7, 7));         // its own father
    CHECK_EQ(FamilyGenerationDistance(t, 7, 7), 0);  // same id -> 0
}

// --- Deep linear chain past the frontier buffer (64) in *AtGen ----------------
// FamilyCollectAncestorsAtGen / DescendantsAtGen use a 64-wide local frontier;
// drive a chain that would over-expand it and confirm no OOB and a sane result.
TEST(FamilyHarden, DeepChainAtGenBounded) {
    // Build a 200-deep father chain: rec[i].father = i+1.
    const int N = 200;
    std::vector<FamilyRecord> recs;
    recs.reserve(N);
    for (int i = 0; i < N; ++i)
        recs.push_back(MkRec(i, /*father=*/(i + 1 < N ? i + 1 : kFamilyNone)));
    FamilyTree t{recs.data(), N};

    i32 out[8];
    // 5 generations up from id 0 == id 5 (single father chain).
    int n = FamilyCollectAncestorsAtGen(t, 0, 5, out, 8);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0], 5);

    // generations == 0 writes the person itself (capped to maxOut).
    int self = FamilyCollectAncestorsAtGen(t, 3, 0, out, 8);
    CHECK_EQ(self, 1);
    CHECK_EQ(out[0], 3);
}

// --- Tiny / zero out buffers must never write past maxOut ----------------------
TEST(FamilyHarden, ZeroAndTinyOutBuffers) {
    FamilyRecord p = MkRec(1);
    for (int i = 0; i < kMaxChildren; ++i)
        p.children[i] = 100 + i;
    std::vector<FamilyRecord> recs = {p};
    for (int i = 0; i < kMaxChildren; ++i)
        recs.push_back(MkRec(100 + i));
    FamilyTree t{recs.data(), static_cast<int>(recs.size())};

    i32 one[1];
    CHECK_EQ(FamilyGetChildren(p, one, 1), 1);          // capped at maxOut 1
    CHECK_EQ(FamilyCollectDescendants(t, 1, one, 1), 1);
    CHECK_EQ(FamilyHeirLine(t, 1, one, 1), 1);

    // maxOut == 0 paths write nothing.
    i32 dummy[1] = {-7};
    CHECK_EQ(FamilyCollectAncestorsAtGen(t, 1, 2, dummy, 0), 0);
    CHECK_EQ(FamilyCollectDescendantsAtGen(t, 1, 2, dummy, 0), 0);
    CHECK_EQ(dummy[0], -7);   // untouched
}

// --- Heir collection cap (kMaxHeirs == 4) honoured even with more children -----
TEST(FamilyHarden, HeirCollectionCap) {
    FamilyRecord p = MkRec(1);
    for (int i = 0; i < kMaxChildren; ++i)   // 8 children
        p.children[i] = 100 + i;
    std::vector<FamilyRecord> recs = {p};
    FamilyTree t{recs.data(), 1};

    i32 out[8] = {0};
    int n = FamilyCollectHeirs(t, 1, out, 8);
    CHECK_EQ(n, 4);   // min(maxOut, kMaxHeirs)
    CHECK_EQ(out[0], 100);
    CHECK_EQ(out[3], 103);
}

// --- Inheritance resolution on a childless line falls back to siblings ---------
TEST(FamilyHarden, InheritanceFallbackBounded) {
    // Two siblings of the same father, neither with children.
    FamilyRecord dad = MkRec(1);
    FamilyRecord s1 = MkRec(10, /*father=*/1);
    FamilyRecord s2 = MkRec(11, /*father=*/1);
    std::vector<FamilyRecord> recs = {dad, s1, s2};
    FamilyTree t{recs.data(), static_cast<int>(recs.size())};

    i32 out[8];
    int n = FamilyResolveInheritance(t, 10, out, 8);  // no direct line -> sibling 11
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0], 11);

    // Tiny buffer must not overflow.
    i32 tiny[1];
    CHECK_EQ(FamilyResolveInheritance(t, 10, tiny, 1), 1);
}

// --- FamilyAreBloodRelated: out-of-range ids, cycles, self --------------------
TEST(FamilyHarden, BloodRelatedBounds) {
    // Two siblings sharing a father.
    FamilyRecord dad = MkRec(1);
    FamilyRecord s1 = MkRec(10, /*father=*/1);
    FamilyRecord s2 = MkRec(11, /*father=*/1);
    std::vector<FamilyRecord> recs = {dad, s1, s2};
    FamilyTree t{recs.data(), static_cast<int>(recs.size())};

    CHECK(FamilyAreBloodRelated(t, 10, 10, 3));        // self -> true
    CHECK(FamilyAreBloodRelated(t, 10, 1, 3));         // child & parent
    CHECK(FamilyAreBloodRelated(t, 10, 11, 3));        // siblings (common ancestor)
    CHECK(!FamilyAreBloodRelated(t, 10, kFamilyNone, 3));   // none id
    CHECK(!FamilyAreBloodRelated(t, kFamilyNone, 10, 3));
    CHECK(!FamilyAreBloodRelated(t, 10, 999, 3));      // unknown id

    // maxGenerations 0 still terminates and does not OOB.
    CHECK(FamilyAreBloodRelated(t, 10, 10, 0));

    // Over a parent cycle it must terminate (guarded by tree size).
    std::vector<FamilyRecord> cyc = {MkRec(1, 2), MkRec(2, 1)};
    FamilyTree tc{cyc.data(), 2};
    bool r = FamilyAreBloodRelated(tc, 1, 2, 5);   // bounded — must not hang
    CHECK(r);
}
