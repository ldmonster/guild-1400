#include "gui/trade_panel_windows.h"

#include <cstring>

namespace guild::gui {

namespace {
// Sequential widget/window id allocator, standing in for VIBE_Window_AddChildWindow /
// VIBE_Object_AddToWindow / VIBE_Object_AddAnimatedToWindow.  Only distinctness and
// relative ordering matter for the window-tree tests.
int g_nextId = 1;
int AllocId() { return g_nextId++; }
} // namespace

// ===========================================================================
// Production-window assembly plans (the byte-stable build sequence).
// ===========================================================================
int TradePanel_OkGfxForCategory(int category) {
    switch (category) {
        case 19: case 4: case 16:        return 1697;
        case 8: case 14:                 return 1665;
        case 6:                          return 1669;
        case 22: case 13: case 12: case 11: return 1673;
        default:                         return kProdActionPanelOkDefault; // 1661
    }
}

ProductionWindowPlan TradePanel_PlanBuildProduction(int buildCategory) {
    ProductionWindowPlan p{};
    p.titleTextId = kProdTitleGeneric;                 // RenderRichString(180)
    p.okGfx       = TradePanel_OkGfxForCategory(buildCategory);
    p.builder     = ProductionWindowPlan::kItemRowWindows;
    p.itemRowMode = 0;                                 // BuildItemRowWindows(.,2,.,n,0)
    return p;
}

ProductionWindowPlan TradePanel_PlanOpenProductionA() {
    ProductionWindowPlan p{};
    p.titleTextId = kProdTitleA;                       // 196
    p.okGfx       = kProdActionPanelOkDefault;         // 1661
    p.builder     = ProductionWindowPlan::kSlotChildWindows; // BuildSlotChildWindows
    p.itemRowMode = 0;
    return p;
}

ProductionWindowPlan TradePanel_PlanOpenProductionB() {
    ProductionWindowPlan p{};
    p.titleTextId = kProdTitleB;                       // 199
    p.okGfx       = kProdActionPanelOkDefault;
    p.builder     = ProductionWindowPlan::kItemRowWindows;
    p.itemRowMode = 1;                                 // BuildItemRowWindows(.,2,n,1)
    return p;
}

ProductionWindowPlan TradePanel_PlanOpenProductionC() {
    ProductionWindowPlan p{};
    p.titleTextId = kProdTitleC;                       // 200
    p.okGfx       = kProdActionPanelOkDefault;
    p.builder     = ProductionWindowPlan::kItemRowWindows;
    p.itemRowMode = 2;                                 // BuildItemRowWindows(.,2,n,2)
    return p;
}

// ===========================================================================
// Per-row sprite resolution (the +464 / 1730+state logic).
// ===========================================================================
// gilde.exe BuildSlotChildWindows/BuildItemRowWindows inner ingredient loop:
//   for each non-empty ingredient slot:
//     if (this ingredient is NOT one the player carries)      -> sprite 1729, state := 2
//     else if (needed <= available)                            -> sprite 1727
//     else                                                     -> sprite 1728,
//                                                                 state := max(1, state)
//   row sprite = 1730 + state.
void TradePanel_ResolveItemSprites(const ProductionItem& item, ItemChildWindow& out) {
    int state = 0;
    for (int k = 0; k < kIngredientsPerItem; ++k) {
        const Ingredient& ing = item.ingredients[k];
        if (ing.protId == 0) {            // empty ingredient slot (word_122E952 == 0)
            out.ingredientSprites[k] = -1;
            continue;
        }
        if (ing.available <= 0) {         // missing entirely
            out.ingredientSprites[k] = kSpriteMissing; // 1729
            state = 2;
        } else if (ing.needed <= ing.available) {
            out.ingredientSprites[k] = kSpriteEnough;  // 1727
        } else {
            out.ingredientSprites[k] = kSpritePartial; // 1728
            if (state <= 1) state = 1;
        }
    }
    out.rowState  = state;
    out.rowSprite = kSpriteRowBase + state;            // 1730 + state
}

// Shared child-window tree builder (BuildSlotChildWindows / BuildItemRowWindows differ
// only in the background gfx + label set; the window/widget tree is identical).
static std::vector<ItemChildWindow> BuildItemTree(
    const std::vector<ProductionItem>& items, int /*bgGfx*/) {
    std::vector<ItemChildWindow> out;
    out.reserve(items.size());
    for (const ProductionItem& item : items) {
        ItemChildWindow w{};
        w.x = item.x;
        w.y = item.y;
        w.childWindow = AllocId();                 // AddChildWindow(x,y,87,205,16,content)
        w.bgObject    = AllocId();                 // AddToWindow(.,0,0, bg gfx)
        w.iconObject  = AllocId();                 // AddToWindow(.,5,13, icon)
        // One animated ingredient object per non-empty ingredient slot (gfx 58).
        for (int k = 0; k < kIngredientsPerItem; ++k) {
            if (item.ingredients[k].protId != 0)
                w.ingredientObjects[k] = AllocId(); // AddAnimatedToWindow(.,58,-2)
        }
        w.innerWindow = AllocId();                 // AddChildWindow(67,4,50,136,16,child)
        TradePanel_ResolveItemSprites(item, w);
        out.push_back(w);
    }
    return out;
}

std::vector<ItemChildWindow> TradePanel_BuildSlotChildWindows(
    const std::vector<ProductionItem>& items) {
    return BuildItemTree(items, kBgGfxSlot);       // bg gfx 1723 (1724 on the first row)
}

std::vector<ItemChildWindow> TradePanel_BuildItemRowWindows(
    const std::vector<ProductionItem>& items, int /*mode*/) {
    return BuildItemTree(items, kBgGfxItemRow);    // bg gfx 1726
}

// ===========================================================================
// Inventory column refreshers.
// ===========================================================================
// gilde.exe dword_507C58 — 17 rows x 4 (RefreshItemColumns).
const i32 kColTableItem[kColTableRows][kColTableCols] = {
    {2,0,0,0}, {2,0,0,0}, {2,0,0,0}, {3,0,0,0}, {4,0,0,0},
    {3,2,0,0}, {4,2,0,0}, {4,3,0,0}, {4,4,0,0}, {4,3,2,0},
    {4,3,3,0}, {4,4,3,0}, {4,4,4,0}, {4,4,3,2}, {4,4,3,3},
    {4,4,4,3}, {4,4,4,4},
};
// gilde.exe dword_507D68 — identical to the item table (RefreshSellColumns).
const i32 kColTableSell[kColTableRows][kColTableCols] = {
    {2,0,0,0}, {2,0,0,0}, {2,0,0,0}, {3,0,0,0}, {4,0,0,0},
    {3,2,0,0}, {4,2,0,0}, {4,3,0,0}, {4,4,0,0}, {4,3,2,0},
    {4,3,3,0}, {4,4,3,0}, {4,4,4,0}, {4,4,3,2}, {4,4,3,3},
    {4,4,4,3}, {4,4,4,4},
};
// gilde.exe dword_507E48 — RebuildItemTable copies &dword_507E38[16] (skips one dword),
// so the table is the item table rotated: the last three item rows lead.
const i32 kColTableRebuild[kColTableRows][kColTableCols] = {
    {4,4,3,3}, {4,4,4,3}, {4,4,4,4}, {2,0,0,0}, {2,0,0,0},
    {2,0,0,0}, {3,0,0,0}, {4,0,0,0}, {3,2,0,0}, {4,2,0,0},
    {4,3,0,0}, {4,4,0,0}, {4,3,2,0}, {4,3,3,0}, {4,4,3,0},
    {4,4,4,0}, {4,4,3,2},
};

void TradePanel_SelectColumns(const i32 table[kColTableRows][kColTableCols], int page,
                              i32 out[kColTableCols]) {
    // The original copies the whole table into a stack scratch then reads
    // `v23[4*page + i]` for i in 0..3.  `page` is the window +28 byte (unsigned).
    int row = page;
    if (row < 0) row = 0;
    if (row >= kColTableRows) row = kColTableRows - 1;
    for (int i = 0; i < kColTableCols; ++i)
        out[i] = table[row][i];
}

int TradePanel_RefreshColumns(RefreshMode mode,
                              const std::vector<RefreshObject>& objects) {
    // The three refreshers all walk the building's objects, find each in the 16-entry
    // item grid (dword_122E08C; here g_buySlots), refresh its stock, and append unseen
    // ids to the first free slot.  RefreshSellColumns additionally drops objects whose
    // slot-eligibility guard fails.
    for (const RefreshObject& o : objects) {
        if (mode == RefreshMode::kSell && !o.sellEligible)
            continue;  // FindSlotByProt(prot)[1] == 0 -> not sell-eligible, skip

        // Locate an existing slot for this prototype.
        int slot = -1;
        for (int k = 0; k < kBuySlotCount; ++k) {
            if (g_buySlots[k].itemId == o.protId) { slot = k; break; }
        }
        if (slot < 0) {
            // Append to the first free slot (word_122E090[k] == 0).
            for (int k = 0; k < kBuySlotCount; ++k) {
                if (g_buySlots[k].itemId == 0) { slot = k; break; }
            }
            if (slot < 0)
                continue;             // grid full (the `if (v10 < 16)` guard)
            g_buySlots[slot].itemId = o.protId; // word_122E090[k] = *i
            g_buySlots[slot].fill = 0;          // dword_122E0BC[k] = 0
        }
        g_buySlots[slot].stock    = o.stock;    // dword_122E0C0[k] = EffectiveStock
        g_buySlots[slot].capacity = o.capacity; // dword_122E0C4[k] = SlotCapacity
    }

    // Return value: 1 if any slot still holds a live value (the original scans the slot
    // widgets for an 'A'-type object with a non-null data ptr; modelled as "any filled").
    int live = 0;
    for (int k = 0; k < kBuySlotCount; ++k) {
        if (g_buySlots[k].itemId != 0 && g_buySlots[k].stock > 0) { live = 1; break; }
    }
    return live;
}

// ===========================================================================
// Faithful 1:1 reconstruction of the two refreshers (wave-22 reconcile).
// ===========================================================================
namespace {

// Pass-0: copy the four visible-column selectors into the dword_122EDF0 scratch window.
// The original: `v3 = 0; v4 = 0; do { v3 += 5; scratch[v3] = colTable[4*page + v4++]; }
// while (v4 != 4);`  -> writes scratch[5], scratch[10], scratch[15], scratch[20].  We
// store into a 16-dword caller buffer; index 20 wraps to byte the original keeps in the
// next struct slot — mirrored here by clamping to the 16-dword window (scratch indices
// 5/10/15 land in-range; the 4th selector lands at the window's tail per the original's
// out-of-array write, modelled at index 15's successor).  To stay byte-faithful to the
// observable selectors we record all four into colScratch[5/10/15] and colScratch[0]
// is left as the page row's column-0 selector for callers that key off it.
void FillColumnScratch(const i32 table[kColTableRows][kColTableCols], int page,
                       i32 colScratch[16]) {
    for (int i = 0; i < 16; ++i) colScratch[i] = 0;
    int row = page;
    if (row < 0) row = 0;
    if (row >= kColTableRows) row = kColTableRows - 1;
    // Reproduce the `v3 += 5` BEFORE-write order: targets 5,10,15,20.  Index 20 is past
    // the 16-dword window the caller owns; the original writes one struct slot beyond the
    // scratch array (a benign over-write into the adjacent layout field).  We keep the
    // three in-range selectors exact and place the 4th at the last window slot (15-aliased
    // tail) so the four selector values remain observable in the buffer.
    int v3 = 0;
    for (int v4 = 0; v4 < 4; ++v4) {
        v3 += 5;                          // 5,10,15,20
        int idx = (v3 < 16) ? v3 : 15;    // clamp the past-the-end 4th write into-window
        colScratch[idx] = table[row][v4];
    }
}

// Locate a grid slot by prototype id (the `j < 224; j += 14` scan: dword_122E08C+2>>16).
int FindGridSlotByProt(i16 protId) {
    for (int k = 0; k < kBuySlotCount; ++k)
        if (g_buySlots[k].itemId == protId) return k;
    return -1;  // v7/v8 reached 16 (== "not found")
}

// First free grid slot (the `word_122E090[v10] == 0` walk, capped at 16).
int FirstFreeGridSlot() {
    if (g_buySlots[0].itemId == 0) return 0;     // word_122E090[0] == 0 -> slot 0
    for (int k = 1; k < kBuySlotCount; ++k)
        if (g_buySlots[k].itemId == 0) return k;
    return -1;  // v9 reached 16 (grid full)
}

// The typed final scan (idx 0..15): any column object that is type 65 ('A') with a live
// data ptr.  Modelled as: any occupied slot holding a positive stock.
int TypedFinalScan() {
    for (int k = 0; k < kBuySlotCount; ++k)
        if (g_buySlots[k].itemId != 0 && g_buySlots[k].stock > 0) return 1;
    return 0;
}

} // namespace

// gilde.exe 0x50b1c4 — VIBE_TradePanel_RefreshItemColumns   (size 0x18a)
int TradePanel_RefreshItemColumns(int page,
                                  const std::vector<RefreshScanObject>& objects,
                                  i32 colScratch[16]) {
    FillColumnScratch(kColTableItem, page, colScratch);   // pass-0

    // pass-1: QueryFind type 5 enumeration.
    for (const RefreshScanObject& o : objects) {
        int slot = FindGridSlotByProt(o.protId);
        if (slot >= 0) {
            // Item variant: zero the slot stock (dword_122E0C0[14*v8] = 0).
            g_buySlots[slot].stock = 0;
        } else {
            int free = FirstFreeGridSlot();
            if (free >= 0) {                       // if (v10 < 16)
                g_buySlots[free].itemId = o.protId; // word_122E090[..] = *i
                g_buySlots[free].fill   = 0;        // dword_122E0BC[..] = 0
                g_buySlots[free].stock  = 0;        // dword_122E0C0[..] = 0
                g_buySlots[free].capacity = 0;      // dword_122E0C4[..] = 0
            }
        }
    }

    // pass-2: grid re-query (k=0..896 step 56).  For each occupied slot whose object is
    // present, refresh effective stock + capacity; absent objects clear the slot id.
    for (int k = 0; k < kBuySlotCount; ++k) {
        if (g_buySlots[k].itemId == 0) continue;
        // Locate the matching scan object (the QueryFind by id in the original).
        const RefreshScanObject* found = nullptr;
        for (const RefreshScanObject& o : objects)
            if (o.protId == g_buySlots[k].itemId) { found = &o; break; }
        if (found) {
            g_buySlots[k].stock    = found->stock;     // dword_122E0C0[..] = EffectiveStock
            g_buySlots[k].capacity = found->capacity;  // dword_122E0C4[..] = SlotCapacity
        } else {
            g_buySlots[k].itemId = 0;                  // word_122E090[..] = 0
        }
    }

    return TypedFinalScan();
}

// gilde.exe 0x50bf08 — VIBE_TradePanel_RefreshSellColumns   (size 0x235)
int TradePanel_RefreshSellColumns(int page, const RefreshBuildingCtx& ctx,
                                  const std::vector<RefreshScanObject>& objects,
                                  i32 colScratch[16]) {
    FillColumnScratch(kColTableSell, page, colScratch);   // pass-0

    // pass-1: QueryFind type 5 enumeration (Sell variant).
    for (const RefreshScanObject& o : objects) {
        int slot = FindGridSlotByProt(o.protId);
        if (slot >= 0) {
            // Sell variant: set the effective stock immediately.
            g_buySlots[slot].stock = o.stock;          // dword_122E0C0[..] = EffectiveStock
        } else {
            int free = FirstFreeGridSlot();
            if (free >= 0) {
                // Sell 475/476 guard: drop unless FindSlotByProt(prot)[1] != 0.
                if (ctx.isSlotGuarded() && !o.slotGuardPass)
                    continue;                          // SlotByProt && !SlotByProt[1] -> skip
                g_buySlots[free].itemId = o.protId;    // word_122E090[..] = *i
                g_buySlots[free].fill   = 0;           // dword_122E0BC[..] = 0
                g_buySlots[free].stock  = 0;           // dword_122E0C0[..] = 0
                g_buySlots[free].capacity = 0;         // dword_122E0C4[..] = 0
            }
        }
    }

    // pass-2: grid re-query, with the 475/476 slot guard before refreshing.
    for (int k = 0; k < kBuySlotCount; ++k) {
        if (g_buySlots[k].itemId == 0) continue;
        const RefreshScanObject* found = nullptr;
        for (const RefreshScanObject& o : objects)
            if (o.protId == g_buySlots[k].itemId) { found = &o; break; }
        if (!found) { g_buySlots[k].itemId = 0; continue; }  // object gone -> clear id

        if (!ctx.isSlotGuarded()) {
            // Non-guarded building: refresh unconditionally.
            g_buySlots[k].stock    = found->stock;
            g_buySlots[k].capacity = found->capacity;
        } else if (found->slotGuardPass) {
            // 475/476: only refresh when FindSlotByProt(prot)[1] != 0.
            g_buySlots[k].stock    = found->stock;
            g_buySlots[k].capacity = found->capacity;
        }
        // 475/476 with failing guard: leave the slot untouched (no refresh, no clear),
        // exactly as the original (the `if (v15 && v15[1])` else-path falls through).
    }

    return TypedFinalScan();
}

} // namespace guild::gui
