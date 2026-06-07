#pragma once
// Stammbaum — dynasty / family-tree relationship queries. The full GUI
// (VIBE_Stammbaum_RunFamilyTreeWindow 0x55ab84) walks the person record's
// parent/spouse/child id links to lay out the tree; this module recovers those
// links and the parent/child/sibling/spouse queries the layout is built from.
//
// Link offsets recovered from the window's record reads (record accessed as a
// dword array `*((_DWORD*)rec + N)`):
//   +0x5C (+92,  dword index 23)  father id      (-1 == none)
//   +0x60 (+96,  dword index 24)  spouse id      (0xFFFF/-1 == none)
//   +0x64 (+100, dword index 25)  mother id      (-1 == none)
//   +0x68 (+104, dword index 26)  children id array, up to kMaxChildren ids
// A child slot holding -1 is empty. The window caps the displayed children but
// the underlying array is fixed-size; we model the full array.
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

constexpr i32 kFamilyNone   = -1;   // empty parent/spouse/child link sentinel
constexpr int kMaxChildren  = 8;    // children-id slots per record

// ===========================================================================
// Family record  (subset of the person record at the recovered link offsets)
// ===========================================================================
GUILD_PACKED_BEGIN
struct FamilyRecord {
    i32 id;                      // +0x00  person id (this record's own id)
    u8  pad4[88];                // +0x04..+0x5B  unrelated person fields
    i32 father;                  // +0x5C  (+92)  father person id (-1 = none)
    i32 spouse;                  // +0x60  (+96)  spouse person id (-1 = none)
    i32 mother;                  // +0x64  (+100) mother person id (-1 = none)
    i32 children[kMaxChildren];  // +0x68  (+104) children id array (-1 = empty)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(offsetof(FamilyRecord, father)   == 92,  "father @+92");
static_assert(offsetof(FamilyRecord, spouse)   == 96,  "spouse @+96");
static_assert(offsetof(FamilyRecord, mother)   == 100, "mother @+100");
static_assert(offsetof(FamilyRecord, children) == 104, "children @+104");

// A small in-memory dynasty: id -> FamilyRecord lookup. The engine resolves ids
// through VIBE_Person_FindRecordById; here the caller supplies a flat table.
struct FamilyTree {
    const FamilyRecord* records;  // array of person records
    int count;                    // number of records

    // Resolves a person id to its record, or nullptr (mirrors FindRecordById).
    const FamilyRecord* Find(i32 personId) const;
};

// Parent / spouse queries (direct link reads; -1 when the link is empty).
i32 FamilyGetFather(const FamilyRecord& rec);
i32 FamilyGetMother(const FamilyRecord& rec);
i32 FamilyGetSpouse(const FamilyRecord& rec);

// Children query: copies up to `maxOut` non-empty child ids of `rec` into `out`,
// returning the count written. Mirrors the window's child-slot walk that skips
// -1 entries.
int FamilyGetChildren(const FamilyRecord& rec, i32* out, int maxOut);

// Sibling query: a sibling shares at least one parent (father or mother) with
// `personId` and is not `personId` itself. Walks the tree's records, writing up
// to `maxOut` sibling ids into `out`; returns the count written.
int FamilyGetSiblings(const FamilyTree& tree, i32 personId, i32* out, int maxOut);

// True when `childId`'s father or mother equals `parentId` (a direct
// parent->child link), per the tree.
bool FamilyIsParentOf(const FamilyTree& tree, i32 parentId, i32 childId);

// ===========================================================================
// Succession queries (the family-aware office-successor collectors build on
// these). gilde.exe VIBE_Office_CollectFamilyHeirCandidates 0x5555c8 walks the
// person's children and collects up to kMaxHeirs (4) as heir candidates; the
// office succession then scores them. The He entity-handler walk and candidate
// scoring are sim-owned; the recoverable rule is the capped child collection.
// ===========================================================================
constexpr int kMaxHeirs = 4;   // v7 < 4 cap in CollectFamilyHeirCandidates

// Collects up to min(maxOut, kMaxHeirs) of `personId`'s children (heir
// candidates) into `out`, skipping empty (-1) child slots. Returns the count.
// (The first-found order matches the original's child-slot walk.)
int FamilyCollectHeirs(const FamilyTree& tree, i32 personId, i32* out, int maxOut);

// True when `ancestorId` is a (possibly indirect) ancestor of `descendantId`:
// reachable by walking father/mother links upward. Bounded by the tree size to
// avoid cycles. (Used by relative-successor eligibility.)
bool FamilyIsAncestorOf(const FamilyTree& tree, i32 ancestorId, i32 descendantId);

} // namespace guild::world
