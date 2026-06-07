#pragma once
// Family-tree query extensions — descendant and heir-line queries that complement
// the parent/child/sibling/ancestor queries in world/stammbaum.h. These model the
// family-aware office-succession collectors the original walks
// (VIBE_Office_CollectFamilyHeirCandidates 0x5555c8 and the relative/heir-line
// successor collectors), built on the same FamilyTree / FamilyRecord recovered in
// stammbaum.h — reused here, not redefined.
#include "guild/common/types.h"
#include "world/stammbaum.h"   // FamilyTree, FamilyRecord, kFamilyNone, kMaxChildren

namespace guild::world {

// True when `descendantId` is a (possibly indirect) descendant of `ancestorId`:
// reachable by walking child links downward from `ancestorId`. This is the dual of
// FamilyIsAncestorOf and is implemented in terms of it for consistency.
bool FamilyIsDescendantOf(const FamilyTree& tree, i32 ancestorId, i32 descendantId);

// Collects all (possibly indirect) descendants of `personId` into `out` (up to
// `maxOut`), in breadth-first child order (children, then grandchildren, ...).
// Returns the count written. Bounded by the tree size against cycles.
int FamilyCollectDescendants(const FamilyTree& tree, i32 personId,
                             i32* out, int maxOut);

// gilde.exe heir-line (the ordered succession chain the family successor collector
// walks): starting from `personId`, follow the line of succession — the first
// non-empty child of each generation — collecting up to `maxOut` heirs. This is
// the "primary heir at each generation" chain (eldest-child line) the office
// transfer uses when no living direct heir is found. Returns the chain length.
int FamilyHeirLine(const FamilyTree& tree, i32 personId, i32* out, int maxOut);

} // namespace guild::world
