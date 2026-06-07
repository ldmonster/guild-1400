#include "world/stammbaum_tree.h"

// Genuine family-tree data pass behind the dynasty window (gilde.exe 0x55ab84),
// plus the owning-house resolver (0x58c408). Drawing is intentionally omitted;
// only the deterministic record walk is reconstructed.

namespace guild::world {

const StammPerson* StammWorld::Find(i32 id) const {
    if (!records || id == kStammNone)
        return nullptr;
    for (int i = 0; i < count; ++i) {   // VIBE_Person_FindRecordById: linear scan
        if (records[i].id == id)
            return &records[i];
    }
    return nullptr;
}

// gilde.exe 0x55ab84 — the gather kernel. Mirrors:
//   v13 = FindRecordById(focus.father);
//   if (v13 && (focus.flags & 4)==0 && (v13.flags & 4)==0) { fatherShown; }
//   v15 = FindRecordById(focus.mother); if (v15) motherPortrait = *v15;
//   v16 = FindRecordById(focus.spouse);
//   if (v16) { spousePortrait; gather spouse.children (!= focus.id), cap 4; }
//   gather focus.children where child.category < 10, cap 5.
StammTree StammbaumGatherTree(const StammWorld& world, i32 focusId) {
    StammTree tree;
    const StammPerson* focus = world.Find(focusId);
    if (!focus)
        return tree;

    tree.focusPortrait = focus->portrait;        // *v127

    // ---- Father (with the flag-4 hide rule on BOTH the focus and the father) --
    const StammPerson* dad = world.Find(focus->father);
    if (dad
        && (focus->flags & kStammFlagHideFather) == 0
        && (dad->flags & kStammFlagHideFather) == 0) {
        tree.fatherShown = true;                 // v131 = 1
        tree.fatherPortrait = dad->portrait;     // LOWORD(v97) = *v13
    }

    // ---- Mother --------------------------------------------------------------
    const StammPerson* mom = world.Find(focus->mother);
    if (mom) {
        tree.motherKnown = true;
        tree.motherPortrait = mom->portrait;     // LOWORD(v108) = *v15
    }

    // ---- Spouse + the couple's children (read off the spouse record) ---------
    const StammPerson* spouse = world.Find(focus->spouse);
    if (spouse) {
        tree.spouseKnown = true;
        tree.spousePortrait = spouse->portrait;  // LOWORD(v104) = *v16

        // Original: v72 = 3; v17 = 0; v73 = spouse + 6 (word == +0xC). Each step
        // reads spouse.children[v72-3] (dword[23] off the advancing v73), skips
        // -1 and any child whose id == focus.id, then writes into the sparse
        // layout slot 4*v17-1 and advances v17 by the {0->1, 1->(2->4), 4->5}
        // sequence; the loop runs while v72 < 8 && v17 < 6 — i.e. up to 4 kept.
        int v72 = 3;
        int v17 = 0;
        for (int k = 0; v72 < 8 && v17 < 6; ++v72, ++k) {
            if (k >= kStammChildSlots)
                break;                            // spouse.children scan window
            i32 cid = spouse->children[k];
            if (cid == kStammNone)                // != -1 guard
                continue;
            if (cid == focusId)                   // v74 != *((DWORD*)v127 + 1)
                continue;
            const StammPerson* child = world.Find(cid);
            if (!child)
                continue;
            tree.spouseChildren.push_back(child->portrait);  // LOWORD(v92[..]) = *v75
            ++v17;
            if (v17 == 2)                         // if (v17 == 2) v17 = 4;
                v17 = 4;
        }
    }

    // ---- The focus person's own children (category < 10 filter) --------------
    // Original: v18 = 3; v19 = 0; v20 = focus + 6. Reads focus.children[v18-3],
    // keeps those that resolve AND have category < 10; v19 steps by 8 (word slot)
    // and stops at 40 -> at most 5 kept.
    int v18 = 3;
    int v19 = 0;
    for (int k = 0; v18 < 8 && v19 < 40; ++v18, ++k) {
        if (k >= kStammChildSlots)
            break;
        i32 cid = focus->children[k];
        if (cid == kStammNone)
            continue;
        const StammPerson* child = world.Find(cid);
        if (child && child->category < 10) {      // *((char*)v21 + 2) < 10
            tree.ownChildren.push_back(child->portrait);   // v102[v19] = *v21
            v19 += 8;
        }
    }

    return tree;
}

// gilde.exe 0x58c408 — VIBE_Person_GetFamilyRecord (index form).
//   v1 = person+2 (category);
//   if ((v1 != 6 && v1 != 7 && v1 != 5) || person+81 >= 0) return 0;
//   v2 = person+80 (word); HIBYTE(v2)=0; LOBYTE(v2) = v2 & 0xF;  // == (word & 0x0F)
//   return &word_13C3110[82 * v2];
i32 PersonGetFamilyRecordIndex(const FamilyRecordKey& key) {
    if ((key.category != 5 && key.category != 6 && key.category != 7)
        || key.signByte >= 0)
        return kStammNone;
    // HIBYTE(v2)=0 then LOBYTE(v2)=v2&0xF: the high byte is cleared first, then
    // the low byte is masked to the low nibble -> a 0..15 slot index.
    return static_cast<i32>(key.houseWord & 0x000F);
}

const HouseRecord* PersonGetFamilyRecord(const FamilyRecordKey& key,
                                         const HouseRecord* houses, int houseCount) {
    i32 idx = PersonGetFamilyRecordIndex(key);
    if (idx == kStammNone || !houses || idx >= houseCount)
        return nullptr;
    return &houses[idx];                          // &word_13C3110[82 * idx]
}

} // namespace guild::world
