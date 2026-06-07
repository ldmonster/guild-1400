#pragma once
// Stammbaum — multi-generation family-tree queries (generation distance, nth-gen
// ancestor/descendant collection, primogeniture inheritance resolution). Built on
// the FamilyTree / FamilyRecord recovered in world/stammbaum.h and the descendant /
// heir-line walks in world/family_query.h — REUSED here, not redefined.
//
// These model the deeper walks the family-tree window (VIBE_Stammbaum_RunFamilyTree
// Window 0x55ab84) and the family-aware office-succession collectors perform across
// multiple generations of parent/spouse/child links.
#include "guild/common/types.h"
#include "world/stammbaum.h"     // FamilyTree, FamilyRecord, kFamilyNone, kMaxChildren
#include "world/family_query.h"  // FamilyCollectDescendants, FamilyHeirLine

namespace guild::world {

// The number of generations between `ancestorId` and `descendantId` along the
// shortest upward parent chain (father preferred, then mother), or -1 when
// `ancestorId` is not an ancestor of `descendantId`. 0 means they are the same id;
// 1 a direct parent; 2 a grandparent; etc. Cycle-bounded by the tree size.
int FamilyGenerationDistance(const FamilyTree& tree, i32 ancestorId, i32 descendantId);

// Collects every ancestor of `personId` exactly `generations` levels up (e.g.
// generations==2 collects all four grandparents present in the tree) into `out`
// (up to `maxOut`). Returns the count written. generations<=0 writes `personId`
// itself (capped to maxOut). Walks both father and mother branches.
int FamilyCollectAncestorsAtGen(const FamilyTree& tree, i32 personId,
                                int generations, i32* out, int maxOut);

// Collects every descendant of `personId` exactly `generations` levels down (e.g.
// generations==1 == direct children, 2 == grandchildren) into `out` (up to
// `maxOut`). Returns the count. generations<=0 writes `personId` itself.
int FamilyCollectDescendantsAtGen(const FamilyTree& tree, i32 personId,
                                  int generations, i32* out, int maxOut);

// Resolves the line of inheritance from `personId`: the ordered chain of primary
// heirs (eldest-child line) extended until it dies out or `maxOut` heirs are
// collected, then — if the direct line is empty — falls back to the nearest living
// relative via the sibling/heir collectors. Returns the chain length. This models
// the office-transfer successor search (direct heir line first, relatives next).
int FamilyResolveInheritance(const FamilyTree& tree, i32 personId,
                             i32* out, int maxOut);

// True when `aId` and `bId` share a common ancestor within `maxGenerations` levels
// (a blood relation test the relative-successor eligibility uses). Two ids are
// related when some ancestor of `aId` (within maxGenerations) is also an ancestor
// of, or equal to, `bId` (and vice-versa).
bool FamilyAreBloodRelated(const FamilyTree& tree, i32 aId, i32 bId, int maxGenerations);

} // namespace guild::world
