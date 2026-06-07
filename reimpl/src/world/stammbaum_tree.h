#pragma once
// Stammbaum tree GATHER — the genuine family-tree data pass behind the dynasty
// window, recovered faithfully from gilde.exe:
//
//   VIBE_Stammbaum_RunFamilyTreeWindow  0x55ab84   (gather kernel only)
//   VIBE_Person_GetFamilyRecord         0x58c408
//
// The window itself is GUI (Form/Paintbox/Hud) and not translated here; what IS
// recovered is the deterministic record-walk it performs before any drawing —
// resolving the focus person's father / spouse / mother and gathering the two
// child rows (the focus person's own children, and the spouse's children). Those
// link reads, the slot caps, and the two filters (the father display flag-4 skip
// and the own-children category<10 filter) are the genealogy logic and are
// reproduced bit-for-bit.
//
// PERSON-RECORD LINK OFFSETS (proven from the window's dword/word reads; they
// match the family-link map already documented in world/stammbaum.h):
//   word +0x00  (word[0])    portrait/display id   (*v127, used for labels)
//   dword +0x04 (dword[1])   person id             (*((DWORD*)v127 + 1))
//   byte +0x02               category byte         (5/6/7 family roles; <10 child)
//   dword +0x5C (dword[23])  father id   (-1 == none)
//   dword +0x60 (dword[24])  spouse id   (-1 == none)
//   dword +0x64 (dword[25])  mother id   (-1 == none)
//   dword +0x68.. (dword[26..30]) children ids, 5 slots scanned (-1 == empty)
//   word +0x1CA (word[229])  status flags; bit 4 (0x4) hides the father edge
//
// GetFamilyRecord (0x58c408) resolves the owning HOUSE/clan record (word_13C3110,
// 82-word == 164-byte stride) for a person whose category is 5/6/7 and whose
// +0x51 sign byte is negative, indexed by (word@+0x50 & 0xF).
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Person record view for the gather. Field names/offsets mirror the window's
// reads; a flat table of these stands in for the engine's id-resolved records
// (VIBE_Person_FindRecordById). `id == kStammNone` marks an unused slot.
// ===========================================================================
constexpr i32 kStammNone        = -1;     // empty link / id sentinel (-1)
constexpr int kStammChildSlots  = 5;      // child dwords the window scans (v18 3..7)
constexpr int kStammMaxRowOwn   = 5;      // own-children row cap (v19: 0,8..32 < 40)
constexpr int kStammMaxRowSpouse= 4;      // spouse-children row cap (v17: 0,1,4,5 < 6)
constexpr u16 kStammFlagHideFather = 0x4; // word[229] & 4 hides the father edge

struct StammPerson {
    i32 id        = kStammNone;            // dword +0x04 (person id)
    i16 portrait  = 0;                     // word  +0x00 (display/portrait id)
    u8  category  = 0;                     // byte  +0x02 (5/6/7 family; <10 child)
    u16 flags     = 0;                     // word  +0x1CA (bit 0x4 = hide father)
    i32 father    = kStammNone;            // dword +0x5C
    i32 spouse    = kStammNone;            // dword +0x60
    i32 mother    = kStammNone;            // dword +0x64
    i32 children[kStammChildSlots] = {     // dword +0x68..
        kStammNone, kStammNone, kStammNone, kStammNone, kStammNone};
};

// id -> record resolver (linear scan, like VIBE_Person_FindRecordById).
struct StammWorld {
    const StammPerson* records = nullptr;
    int count = 0;
    const StammPerson* Find(i32 id) const;
};

// The gathered tree the window would lay out. ids are -1 when the slot is empty.
struct StammTree {
    i16 focusPortrait = 0;          // *v127

    bool fatherShown = false;       // v131 (father edge survives the flag-4 skip)
    i16  fatherPortrait = 0;        // v97  (only meaningful when fatherShown)
    bool motherKnown = false;
    i16  motherPortrait = 0;        // v93/v108 path

    bool spouseKnown = false;       // spouse record resolved
    i16  spousePortrait = 0;        // v104

    // The focus person's own children (category<10 filtered), in scan order.
    std::vector<i16> ownChildren;   // v102[] portraits
    // The spouse's children, deduped against the focus person's id, in scan order.
    std::vector<i16> spouseChildren;// v92[] portraits
};

// gilde.exe 0x55ab84 (gather kernel) — resolve and gather the family tree rooted
// at `focusId`. Faithful to the window's link reads, caps and filters; performs
// no drawing. Returns an all-empty tree if `focusId` does not resolve.
StammTree StammbaumGatherTree(const StammWorld& world, i32 focusId);

// ---------------------------------------------------------------------------
// GetFamilyRecord owning-house resolver (POD model).
// ---------------------------------------------------------------------------
// One record in word_13C3110 (82 words == 164 bytes). Modeled as opaque words so
// callers can index it exactly as the engine does without depending on the (large)
// building layout.
constexpr int kHouseRecordWords = 82;     // 82-word == 164-byte stride
struct HouseRecord {
    i16 word[kHouseRecordWords] = {0};
};

// The person fields GetFamilyRecord reads, surfaced as a POD so the indexer is
// testable without the 536-byte person record.
struct FamilyRecordKey {
    u8  category;   // person +0x02
    i8  signByte;   // person +0x51 (must be < 0 to own a house record)
    u16 houseWord;  // person +0x50 (low nibble selects the house slot)
};

// gilde.exe 0x58c408 — VIBE_Person_GetFamilyRecord.
//   if (category != 5 && category != 6 && category != 7) return null;
//   if (signByte >= 0)                                    return null;
//   idx = houseWord & 0x0F;                               (HIBYTE cleared)
//   return &houses[idx];   // &word_13C3110[82 * idx]
// Returns the index into `houses` (>= 0) or kStammNone when the person owns none.
i32 PersonGetFamilyRecordIndex(const FamilyRecordKey& key);

// Convenience: returns the resolved HouseRecord* (or nullptr) from a table.
const HouseRecord* PersonGetFamilyRecord(const FamilyRecordKey& key,
                                         const HouseRecord* houses, int houseCount);

} // namespace guild::world
