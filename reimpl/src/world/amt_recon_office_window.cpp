// amt_recon_office_window.cpp — 1:1 reconstruction of the pure rules of the
// political-office (Amt) window cluster of gilde.exe. See the header for the full
// provenance map. Addresses are gilde.exe (imagebase 0x400000).
#include "world/amt_recon_office_window.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Static tables (recovered with get_bytes).
// ---------------------------------------------------------------------------

// funcs_557292 @0x63d584 — 5 records (collector, renderer, listIndex).
const AmtRoleEntry kAmtRoleTable[kAmtRoleTableRows] = {
    {0x554e34u, 0x555f8cu, 1}, // CollectRelatedNpcs        -> ShowCandidateListWithRoles
    {0x515150u, 0x556108u, 2}, // (collector)               -> ShowCandidateCardListVariantA
    {0x553778u, 0x55623cu, 3}, // (collector)               -> ShowCandidateCardListVariantB
    {0x5555c8u, 0x556370u, 4}, // (collector)               -> ShowCandidateCardListVariantC
    {0x5556c4u, 0x5564acu, 5}, // (collector)               -> ShowCandidateListWithSkillLabels
};

// dword_5526B0 @0x5526b0 — overview slot office-type ids: 00 06 04 05 01 02 03 07.
const u8 kAmtOverviewSlotOfficeType[kAmtOverviewSlotCount] = {
    0x00, 0x06, 0x04, 0x05, 0x01, 0x02, 0x03, 0x07,
};

// ---------------------------------------------------------------------------
// VIBE_Office_PrepareCandidatePage (0x555eb4)
// ---------------------------------------------------------------------------
PreparePageResult OfficePrepareCandidatePage(i32 primaryWin, i32 scrollWin) {
    PreparePageResult r{};
    r.primaryWin = primaryWin;
    r.scrollWin  = scrollWin;
    // 0x555ebb select primary; 0x555ee0 clear "$C"; 0x555ef2 remove children.
    if (scrollWin == -1) {        /*0x555efa*/
        r.builtScroll = false;
        r.returnValue = 0;        // original returns uninitialized ecx; the only
                                  // wired caller overwrites the page descriptor.
        return r;
    }
    // 0x555f18 reset page child count; 0x555f29 select scroll win; 0x555f33 clear;
    // 0x555f4d create scroll buttons; 0x555f56 re-select primary.
    r.builtScroll = true;
    r.returnValue = 0;            /*0x555efe*/
    return r;
}

// ---------------------------------------------------------------------------
// VIBE_Office_ApplyCandidateRatingBars (0x556ba0)
// ---------------------------------------------------------------------------
RatingBarMode RatingBarRowMode(bool headValid, bool populated,
                               u8 headCat, u8 entryCat) {
    if (!headValid)            /*0x556baf/0x556bb5*/
        return RatingBarMode::Skip;
    if (!populated)            /*0x556bcd*/
        return RatingBarMode::Skip;
    if (RatingBarBothNobility(headCat, entryCat)) /*0x556bcf/0x556c3b*/
        return RatingBarMode::MaxValue;           /*0x556beb*/
    return RatingBarMode::Favorability;           /*0x556c1d/0x556c36*/
}

// ---------------------------------------------------------------------------
// VIBE_Amt_RunCandidateSelectionWindow paging core (0x556c40)
// ---------------------------------------------------------------------------
i32 AmtFindFirstNonEmptyRole(i32 current, const i32* rowCounts, int count) {
    // 0x556d2b / 0x556fe4: only runs when the *current* role has zero rows.
    if (rowCounts[5 * current] != 0)
        return current;
    // 0x556d3c / 0x556ff4: tentatively park index at count ("none found").
    i32 idx = count;
    if (count > 0) {                       /*0x556d43 / 0x556ffb*/
        int v = 0;     // dword_63D57C row offset (steps by 5)
        int n = 0;     // candidate index
        while (rowCounts[v] == 0) {        /*0x556d60 / 0x55700f*/
            v += 5;                        /*0x557238 / 0x5572e9*/
            ++n;                           /*0x55723b / 0x5572ec*/
            if (v >= 5 * count)            /*0x55723e / 0x5572ef*/
                return idx;                // none non-empty -> stays at count
        }
        idx = n;                           /*0x556d66 / 0x557015*/
    }
    return idx;
}

SelectedRoleState AmtSelectRoleAtFrameTop(i32 index, const i32* rowCounts,
                                          int count) {
    SelectedRoleState s{};
    s.index = index;
    if (static_cast<u32>(index) < static_cast<u32>(count)) { /*0x556f6d*/
        s.inRange       = true;
        s.selectedCount = rowCounts[5 * index];              /*0x5572dd*/
    } else {
        s.inRange       = false;
        s.index         = 0;                                 /*0x556f75*/
        s.selectedCount = -1;                                // v61 = -1 (0x556f54)
    }
    return s;
}

void AmtComputeRoleButtonYs(int innerHeight, int count, i32* outYs) {
    // 0x556df1..0x556e2d
    i32 span = innerHeight + 1 - kAmtRoleButtonHeight * count; // v60 numerator
    i32 rem  = span % (count - 1);   // v63   (0x556e10)
    i32 step = span / (count - 1);   // v60   (0x556e1e, truncating)
    i32 y    = 0;                    // v62   (0x556e2d)
    for (int i = 0; i < count; ++i) {
        outYs[i] = y;
        // 0x556ee2: v62 += (--v63 >= 0) + v60 + 33  — predecrement THEN test.
        --rem;
        y += (rem >= 0 ? 1 : 0) + step + kAmtRoleButtonHeight;
    }
}

// ---------------------------------------------------------------------------
// VIBE_Amt_RunOfficeOverviewWindow per-slot core (0x5575c8)
// ---------------------------------------------------------------------------
int AmtOverviewFindClickedSlot(const i32* slotObjectIds, int slotCount,
                               i32 clickedObjId) {
    // 0x557970..0x557a03: index walks 0..slotCount; first matching object wins.
    int i = 0;                                 /*BYTE4(v38)=0 @0x557970*/
    while (slotObjectIds[i] != clickedObjId) { /*0x557980*/
        ++i;                                   /*0x5579fa*/
        if (i >= slotCount)                    /*0x557a03*/
            return slotCount;                  // not found -> apply/no-op branch
    }
    return i;                                  /*0x557982*/
}

// ---------------------------------------------------------------------------
// VIBE_Amt_HasOccupiedOffice (0x480cb4)
// ---------------------------------------------------------------------------
int AmtHasOccupiedOffice(const OfficeOccupancySlot* slots, int count) {
    // The original walks v0=7 offices (decrementing) over v1=216,210,... ; here the
    // caller supplies the already-resolved slot views in scan order. The original
    // returns 1 at the first slot that is occupied AND whose holder resolves.
    for (int i = 0; i < count; ++i) {
        const OfficeOccupancySlot& s = slots[i];
        if (s.officeTypeHiByte != 7) /*0x480cd3 -> LABEL_5 (skip)*/
            continue;
        if (s.holderId == -1)        /*0x480cde -> --v0; LABEL_5*/
            continue;
        if (s.holderResolves)        /*0x480ce2*/
            return 1;                /*0x480cf8*/
    }
    return 0;                        /*0x480cf9*/
}

} // namespace guild::world
