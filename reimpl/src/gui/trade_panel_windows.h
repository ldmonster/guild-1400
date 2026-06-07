#pragma once
// guild::gui — TradePanel production-window tree assembly (deferred from trade_panel.cpp).
//
// The in-game "production" panel (form "Handel\Handel_PRODUKTION") is built by one of
// four near-identical modal openers and two child-window builders:
//
//   VIBE_TradePanel_BuildProductionWindow  @0x5087bc — generic production window; the OK
//       button gfx is chosen by the building's category (a switch on the 589-byte
//       building record's type byte).  title text id 180.  Builds item-row child windows
//       (BuildItemRowWindows mode 0).
//   VIBE_TradePanel_OpenProductionWindowA  @0x510084 — variant; title 196; OK gfx 1661;
//       builds SLOT child windows (BuildSlotChildWindows).
//   VIBE_TradePanel_OpenProductionWindowB  @0x510a80 — variant; title 199; OK gfx 1661;
//       BuildItemRowWindows mode 1.
//   VIBE_TradePanel_OpenProductionWindowC  @0x511170 — variant; title 200; OK gfx 1661;
//       BuildItemRowWindows mode 2.
//   VIBE_TradePanel_BuildSlotChildWindows  @0x509328 — per item: an 87x205 child window at
//       (x,y) + a 1723 background + the item-icon object + up to 4 ingredient animated
//       objects + a 136-wide inner child window; sets the status sprite (1727/1728/1729)
//       per ingredient availability and the row sprite (1730+state).
//   VIBE_TradePanel_BuildItemRowWindows    @0x509a8c — like BuildSlotChildWindows but with
//       per-ingredient color/size and a richer set of localized labels.
//
// And three near-identical inventory column refreshers (shared 14-dword/56-byte item
// slot table dword_122E08C, capacity 16):
//   VIBE_TradePanel_RefreshItemColumns @0x50b1c4 (col table dword_507C58)
//   VIBE_TradePanel_RefreshSellColumns @0x50bf08 (col table dword_507D68 + sell guard)
//   VIBE_TradePanel_RebuildItemTable   @0x50c444 (col table dword_507E48)
//
// This module translates 1:1 the *window-tree build + content/layout* of these: which
// windows/widgets each creates, their ids/positions, the per-row sprite-id selection,
// the localized line set, and the column-select tables (recovered byte-for-byte).  The
// modal frame loop, the glyph/text engine, the widget-alloc leaves and the command codec
// live in other clusters and are forward-declared / routed through mockable hooks.  Item
// slot population reuses the existing trade_panel.cpp slot tables.

#include "gui/types.h"
#include "gui/trade_panel.h"
#include <array>
#include <vector>

namespace guild::gui {

// ---------------------------------------------------------------------------
// The production form + the four openers' title text ids.
// ---------------------------------------------------------------------------
inline constexpr const char* kFormProduction = "Handel\\Handel_PRODUKTION";

inline constexpr int kProdTitleGeneric = 180; // BuildProductionWindow  RenderRichString(180)
inline constexpr int kProdTitleA       = 196; // OpenProductionWindowA   RenderRichString(196)
inline constexpr int kProdTitleB       = 199; // OpenProductionWindowB   RenderRichString(199)
inline constexpr int kProdTitleC       = 200; // OpenProductionWindowC   RenderRichString(200)

// The action-panel "cancel/right" gfx is always 1716; the "OK/left" gfx varies.
inline constexpr int kProdActionPanelRight = 1716;
inline constexpr int kProdActionPanelOkDefault = 1661;  // A/B/C + generic default

// The slider/info panel the openers build: Hud_BuildSliderPanel(624, 350, win2, curWin,
// 120, ...).  The resulting info widget's color is set to 67 (+112 = 67).
inline constexpr int kProdSliderPanelW    = 624;
inline constexpr int kProdSliderPanelH    = 350;
inline constexpr int kProdSliderPanelStep = 120;
inline constexpr int kProdInfoColor       = 67;
inline constexpr int kProdActionWindowSlot = 0;  // SelectWindow(form, 0) for action panel
inline constexpr int kProdContentWindowSlot = 2; // SelectWindow(form, 2) for item rows
inline constexpr int kProdTitleWindowSlot = 3;   // SelectWindow(form, 3) for title text

// gilde.exe 0x5087bc — VIBE_TradePanel_BuildProductionWindow's per-category OK gfx select.
// Switch on the building's category byte (dword_13CE294 + 589*type):
//   19,4,16        -> 1697
//   8,14           -> 1665
//   6              -> 1669
//   22,13,12,11    -> 1673
//   default        -> 1661
int TradePanel_OkGfxForCategory(int category);

// ---------------------------------------------------------------------------
// The window-tree the openers assemble (the recovered build sequence, as data).
// ---------------------------------------------------------------------------
struct ProductionWindowPlan {
    const char* form = kFormProduction;
    int titleTextId = 0;       // RenderRichString in window slot 3
    int okGfx = 0;             // Hud_BuildObjectActionPanel(curWin, okGfx, 1716)
    int cancelGfx = kProdActionPanelRight;
    int sliderPanelW = kProdSliderPanelW;
    int sliderPanelH = kProdSliderPanelH;
    // Which child-window builder the frame loop drives, and its mode arg:
    enum Builder { kSlotChildWindows, kItemRowWindows } builder = kItemRowWindows;
    int itemRowMode = 0;       // BuildItemRowWindows a4 mode (0/1/2); ignored for slot builder
};

// The four openers' assembly plans (the part that is byte-stable regardless of building).
//   buildCategory: only used by BuildProductionWindow to pick the OK gfx; -1 => default.
ProductionWindowPlan TradePanel_PlanBuildProduction(int buildCategory);
ProductionWindowPlan TradePanel_PlanOpenProductionA();
ProductionWindowPlan TradePanel_PlanOpenProductionB();
ProductionWindowPlan TradePanel_PlanOpenProductionC();

// ---------------------------------------------------------------------------
// Per-item child-window assembly (BuildSlotChildWindows / BuildItemRowWindows).
//
// Each visible production item occupies one 72-byte (18-dword) record in the item table
// dword_122E94E.  For each item the builder creates:
//   - an 87x205 child window at (x,y) = HIWORD(dword_122E962[i]), HIWORD(dword_122E966[i]),
//     flag 16, of the content window;  its backing color is 67.
//   - a background object (gfx 1723 for slots, 1726 for item-rows; or 1724 when first).
//   - the item icon object at (x=13, y=5).
//   - up to 4 ingredient animated objects (gfx 58) — one per non-zero ingredient slot.
//   - a 136-wide inner child window at (x=50, y=4), flag 16, color 67.
// Per-ingredient availability picks a status sprite id stored at widget +464:
//   enough         -> 1727
//   partial        -> 1728  (and bumps the row state to >=1)
//   missing        -> 1729  (row state := 2)
// The row's summary sprite is 1730 + rowState.
// ---------------------------------------------------------------------------
inline constexpr int kItemRecordStrideDwords = 18;   // 72-byte record in dword_122E94E
inline constexpr int kItemRecordStrideBytes  = 72;
inline constexpr int kMaxItemRows = 16;              // capacity guard (v89 < 16)
inline constexpr int kIngredientsPerItem = 4;        // inner loop bound

inline constexpr int kChildWinW = 87;     // AddChildWindow(.., 87, 205, 16, ..)
inline constexpr int kChildWinH = 205;
inline constexpr int kChildWinFlags = 16;
inline constexpr int kInnerWinX = 50;     // inner AddChildWindow(67, 4, 50, 136, 16, ..)
inline constexpr int kInnerWinY = 4;
inline constexpr int kInnerWinW = 136;
inline constexpr int kChildColor = 67;

inline constexpr int kBgGfxSlot      = 1723; // BuildSlotChildWindows background
inline constexpr int kBgGfxItemRow   = 1726; // BuildItemRowWindows background
inline constexpr int kBgGfxFirstSlot = 1724; // BuildSlotChildWindows first-row variant
inline constexpr int kIconObjX = 13;   // item-icon object x (AddToWindow(.,5,13,..))
inline constexpr int kIconObjY = 5;    // item-icon object y
inline constexpr int kIngredientGfx = 58;

// Status sprite ids (widget +464).
inline constexpr int kSpriteEnough  = 1727; // ingredient fully available
inline constexpr int kSpritePartial = 1728; // partially available
inline constexpr int kSpriteMissing = 1729; // missing
inline constexpr int kSpriteRowBase = 1730; // row summary = 1730 + rowState

// One ingredient requirement of an item (recovered from dword_13CE27C + 65*prot + 38/46).
struct Ingredient {
    i16 protId = 0;    // ingredient prototype id (word at +46); 0 => empty slot (skipped)
    i32 needed = 0;    // required quantity (word at +38)
    i32 available = 0; // current stock for this ingredient
};
// One production item row.
struct ProductionItem {
    i16 protId = 0;             // HIWORD(dword_122E94E[i]) item prototype id
    int x = 0;                  // child-window x  (HIWORD dword_122E962)
    int y = 0;                  // child-window y  (HIWORD dword_122E966)
    std::array<Ingredient, kIngredientsPerItem> ingredients{};
};

// The widget set one item child window expands to.
struct ItemChildWindow {
    int childWindow = -1;       // 87x205 outer child window
    int x = 0, y = 0;
    int bgObject = -1;          // background gfx object
    int iconObject = -1;        // item icon object
    std::array<int, kIngredientsPerItem> ingredientObjects{ {-1, -1, -1, -1} };
    std::array<int, kIngredientsPerItem> ingredientSprites{ {-1, -1, -1, -1} }; // 1727/1728/1729
    int innerWindow = -1;       // 136-wide inner child window
    int rowSprite = -1;         // 1730 + rowState
    int rowState = 0;           // 0=enough, 1=partial, 2=missing
};

// gilde.exe 0x509328 — BuildSlotChildWindows: assemble the child-window tree for `items`.
// `firstRowVariant` mirrors the original's `v52` (the first row uses bg gfx 1724 instead
// of 1723).  Returns one ItemChildWindow per item, with the status/row sprites resolved
// from each ingredient's available-vs-needed comparison.
std::vector<ItemChildWindow> TradePanel_BuildSlotChildWindows(
    const std::vector<ProductionItem>& items);

// gilde.exe 0x509a8c — BuildItemRowWindows: same tree, bg gfx 1726, used by the A/B/C
// openers (mode 0/1/2 selects the localized label set; the window tree is identical).
std::vector<ItemChildWindow> TradePanel_BuildItemRowWindows(
    const std::vector<ProductionItem>& items, int mode);

// Per-row sprite resolution shared by both builders (the +464 / 1730+state logic).
//   For each ingredient: available >= needed -> 1727 ; 0 < available < needed -> 1728
//   (rowState := max(1, rowState)) ; available == 0 (missing) -> 1729 (rowState := 2).
// Fills `out.ingredientSprites` + `out.rowState` + `out.rowSprite`.
void TradePanel_ResolveItemSprites(const ProductionItem& item, ItemChildWindow& out);

// ---------------------------------------------------------------------------
// Inventory column refreshers — shared 14-dword/56-byte item slot table.
//
// The 16-entry item slot table (dword_122E08C base) is the same buy/sell grid the
// populate routine fills.  RefreshItemColumns / RefreshSellColumns / RebuildItemTable
// first copy a 17x4 column-select table (recovered byte-for-byte below) indexed by the
// window's "page" byte (window +28) into the visible-columns scratch dword_122EDF0, then
// re-scan the building's objects to (a) update each existing slot's effective stock, and
// (b) append any newly-seen object id to a free slot.  They return 1 if any slot widget
// still holds a live data ptr ('A'-type object), else 0.
// ---------------------------------------------------------------------------
inline constexpr int kColTableRows = 17;     // 272 bytes / (4*4)
inline constexpr int kColTableCols = 4;
// gilde.exe dword_507C58 — RefreshItemColumns column-select table (17 rows x 4).
extern const i32 kColTableItem[kColTableRows][kColTableCols];
// gilde.exe dword_507D68 — RefreshSellColumns column-select table (== item table).
extern const i32 kColTableSell[kColTableRows][kColTableCols];
// gilde.exe dword_507E48 — RebuildItemTable column-select table (shifted).
extern const i32 kColTableRebuild[kColTableRows][kColTableCols];

// Copy the 4 visible-column selectors for window-page `page` from `table` into `out[0..3]`.
// (the qmemcpy + `v23[4*page + i]` loop, byte-exact).  `page` is the window +28 byte.
void TradePanel_SelectColumns(const i32 table[kColTableRows][kColTableCols], int page,
                              i32 out[kColTableCols]);

// A live object seen during a refresh scan (the GameObject_QueryFind enumeration result).
struct RefreshObject {
    i16 protId = 0;
    i32 stock = 0;       // VIBE_Inventory_GetEffectiveStock
    i32 capacity = 0;    // VIBE_Inventory_GetSlotCapacity
    bool sellEligible = true; // RefreshSellColumns slot guard (FindSlotByProt[1] != 0)
};

// Refresh-mode selector (which of the three near-identical refreshers).
enum class RefreshMode { kItem, kSell, kRebuild };

// gilde.exe 0x50b1c4 / 0x50bf08 / 0x50c444 — the shared refresh body.
// Updates `g_buySlots` (the 16-entry item grid) from the scanned objects: existing item
// ids get their stock refreshed; new ids fill the first free slot; the sell variant
// additionally drops objects failing the slot-eligibility guard.  Returns the number of
// slots that hold a live value (>=1 => the original's "1" return).
int TradePanel_RefreshColumns(RefreshMode mode, const std::vector<RefreshObject>& objects);

} // namespace guild::gui
