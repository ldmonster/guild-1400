#pragma once
// inventory2 — the inventory/workstation GRID-UI cluster from gilde.exe. These are
// the widget-coupled inventory leaves that inventory.h explicitly LISTED AS DEFERRED
// (the "VIBE_Inventory_OpenSlotWindow / RefreshSlots / RenderItemGrid / *GridSurface"
// note). They orchestrate the on-screen slot windows: build the slot-icon window,
// reconcile the visible icon set against the scene-tree's live item objects, and lay
// out the per-workstation item grid. plus VIBE_Item_HandleUseChoice (the use-object
// menu dispatch).
//
// The control flow / table arithmetic / slot reconciliation is pure and faithfully
// 1:1; every renderer / window / widget / scene-graph / object call lives in OTHER
// (unreconstructed) modules and is routed through an installable Inventory2Hooks
// dispatch table with inert default implementations (mirrors CharRender2Hooks). The
// large flat engine slot tables (dword_1232BF8..C44, dword_11BB6A0, the per-widget
// dword_69FFB4 record array, dword_63D1CC/D0/D4, dword_63D56C, dword_62D230) are
// modeled as an explicit InvUiState the caller owns, so the loops are testable
// against a recording mock without touching global engine memory.
//
// The one reconstructed leaf reused here is VIBE_Inventory_FindSlotByItemId
// (inventory.h) — a pure scan over the InvGridSlot stock table.
//
// Translated functions (this TU):
//   VIBE_Inventory_CreateGridSurface   0x5513a0  (create the 1280x1280 grid surface)
//   VIBE_Inventory_DestroyGridSurface  0x5513d8  (destroy + clear the grid surface)
//   VIBE_Inventory_OpenSlotWindow      0x54ebd8  (build the 6-slot inventory window)
//   VIBE_Inventory_RefreshSlots        0x54ecb0  (reconcile slot icons vs live items)
//   VIBE_Inventory_RenderItemGrid      0x54f8dc  (lay out the per-workstation grid)
//   VIBE_Item_HandleUseChoice          0x567538  (dispatch the use-object menu choice)
#include "guild/common/types.h"
#include "sim/inventory.h"   // InvGridSlot, InventoryFindSlotByItemId
#include "sim/types.h"

namespace guild::sim {

// ===========================================================================
// Recovered constants.
//   kGridSurfaceDim   = 1280              (CreateGridSurface: 1280x1280, fmt 11)
//   kGridSurfaceGray  = 64                (Light_SetGrayColor argument)
//   kSlotCountFmt     = "%s~ %li Stück"   (byte_6244B8, sprintf into widget+184)
//   kUseSpeedScale    = 0.1f              (flt_624E28, HandleUseChoice eat/drink)
//   kRatioScale       = 100.0             (dbl_624510, RenderItemGrid output ratio %)
//   kSlotIconBase     = 206               (item type -> icon id bias)
//   kRichGroupHdr     = 0x12BF/0x12C0/0x12BE/0x12C1 rich-string template ids
// ===========================================================================
inline constexpr int   kGridSurfaceDim  = 1280;
inline constexpr int   kGridSurfaceGray = 64;
inline constexpr float kUseSpeedScale   = 0.1f;     // flt_624E28
inline constexpr double kRatioScale     = 100.0;    // dbl_624510
inline constexpr int   kSlotIconBase    = 206;
inline constexpr int   kInvSlotCount    = 6;        // 6 inventory icon slots
inline constexpr int   kGridStations    = 32;       // dword_11BB6A0 workstation cap
inline constexpr int   kGridSlotsPerWs  = 6;        // per-workstation item slots

// ===========================================================================
// InvUiState — the engine's flat inventory-window slot tables, modeled explicitly.
//
// OpenSlotWindow / RefreshSlots maintain the 6-slot icon window. The originals use
// two parallel 6-entry tables (stepped by 2 dwords each: dword_1232C00[i] = widget
// id, dword_1232C04[i] = live item-object ptr). We model those as `iconWidget[]`
// and `iconItem[]` (the item is a generic handle into the caller's object world).
//   gridSurface  <- dword_63D1D4[0]   (surface handle from CreateGridSurface)
//   slotWindow   <- dword_63D1CC      (the AddChildWindow id; -1 == not open)
//   personTable  <- dword_63D1D0      (&word_12CE910[268*city]; opaque base here)
//   countWindow  <- dword_63D56C[0]   (RenderItemGrid station-count accumulator)
//   activeWindow <- dword_62D230      (current selected child-window id)
// ===========================================================================
struct InvUiState {
    int  gridSurface  = 0;     // dword_63D1D4[0]
    int  slotWindow   = -1;    // dword_63D1CC  (-1 == closed)
    void* personTable = nullptr; // dword_63D1D0
    int  countWindow  = 0;     // dword_63D56C[0]
    int  activeWindow = 0;     // dword_62D230

    // 6 inventory icon slots (RefreshSlots / OpenSlotWindow).
    int   iconWidget[kInvSlotCount];   // dword_1232C00[2*i]; -1 == empty
    void* iconItem[kInvSlotCount];     // dword_1232C04[2*i]; nullptr == empty

    // Per-workstation grid tables (RenderItemGrid). One column per station; each
    // station owns a 6-entry item-slot sub-table.
    //   wsBuilding[s] <- dword_1232C30[16*s]  (the workstation building ptr; 0 == free)
    //   wsLabelA[s]   <- dword_1232C34[16*s]   wsLabelB[s] <- dword_1232C38[16*s]
    //   wsWindow[s]   <- dword_1232C3C[16*s]  (the station's child window)
    //   wsSlotWidget[s][k] <- dword_1232C40[..] (-1 == empty)
    //   wsSlotType[s][k]   <- word_1232C44[..]  (the item type word)
    void* wsBuilding[kGridStations];
    int   wsLabelA[kGridStations];
    int   wsLabelB[kGridStations];
    int   wsWindow[kGridStations];
    int   wsSlotWidget[kGridStations][kGridSlotsPerWs];
    i16   wsSlotType[kGridStations][kGridSlotsPerWs];

    InvUiState() {
        for (int i = 0; i < kInvSlotCount; ++i) { iconWidget[i] = -1; iconItem[i] = nullptr; }
        for (int s = 0; s < kGridStations; ++s) {
            wsBuilding[s] = nullptr;
            wsLabelA[s] = -1; wsLabelB[s] = -1; wsWindow[s] = -1;
            for (int k = 0; k < kGridSlotsPerWs; ++k) { wsSlotWidget[s][k] = -1; wsSlotType[s][k] = 0; }
        }
    }
};

// ===========================================================================
// Cross-module dispatch hooks (window / object / widget / scene-graph / surface /
// text / drag-slot). Every call the originals make is a slot here; the default
// table is inert (or returns 0/null/false) so the slot reconciliation is testable.
// ===========================================================================
struct Inventory2Hooks {
    // --- Surface (CreateGridSurface / DestroyGridSurface) -----------------------
    void (*lightSetGray)(int a, int gray);                 // VIBE_Light_SetGrayColorThunk
    int  (*surfaceCreate)(int w, int h, int fmt);          // VIBE_Surface_Create
    int  (*surfaceDestroy)(int surface);                   // VIBE_Surface_Destroy

    // --- Window / object / widget management ------------------------------------
    int  (*windowAddChild)(int x, int y, int w, int h, int kind, int parent); // VIBE_Window_AddChildWindow
    int  (*objectAddToWindow)(int window, int slot);       // VIBE_Object_AddToWindow
    int  (*inputAddIcon)(int x, int y, int z, int iconId, int window);        // VIBE_Input_AddIconToWindow
    void (*widgetDestroy)(int widget);                     // VIBE_Widget_DestroyByType
    void (*objectSetEnabled)(int widget, int enabled);     // VIBE_Object_SetEnabled
    void (*objectSetColor)(int widget, int color);         // VIBE_Object_SetColor
    void (*formSelectWindow)(int a1, int a2);              // VIBE_Form_SelectWindow

    // --- Per-widget record fields (the dword_69FFB4 + 740*id record) ------------
    // The originals poke fields at widget+76/184/444/456/460/480 in the global
    // record array. We expose them as setters so the control flow is faithful.
    void (*widgetSetCountText)(int widget, int textId, int count); // sprintf -> +184
    void (*widgetSetFill)(int widget, float fill);         // +480 progress fill
    void (*widgetClearProgress)(int widget);               // +76=0,+480=0 / +456/+460
    void (*widgetMarkSlot)(int widget);                    // +444=35 marker

    // --- Item / object data -----------------------------------------------------
    void* (*objectGetData)(int widget);                    // VIBE_Object_GetDataPtr
    void (*objectSetValue)(int widget, void* data, int value);  // VIBE_Object_SetValueOrText

    // --- Scene-tree item iteration (VIBE_GameObject_QueryFind / IterNext) -------
    // queryItems(personTable, kind, sub, type) starts an iteration over the live
    // item objects of a container; iterItems advances it. Each yields an opaque item
    // handle, or null at the end. The hook owns the iterator state.
    void* (*queryItems)(void* personTable, int kind, int sub, int type); // QueryFind
    void* (*iterItems)();                                  // VIBE_GameObject_IterNext

    // Per-item accessors the loops read:
    i16  (*itemType)(void* item);     // *item       (the type word)
    int  (*itemCount)(void* item);    // *(item+14)  (count/stock dword)
    int  (*itemFlag33)(void* item);   // *(item+33)  (production-in-progress dword)
    int  (*itemProgress28)(void* item); // *(item+28) (production elapsed dword)
    u8   (*itemMatByte)(void* item);  // dword_13CE27C[65*type] != 9 guard

    // --- Drag-slot bridge -------------------------------------------------------
    void (*dragStore)(int iconId, void* data); // VIBE_DragSlot_StoreItem
    void (*dragAdd)(int iconId, void* data);   // VIBE_DragSlot_AddItem

    // --- Text / hud (RenderItemGrid / OpenSlotWindow) ---------------------------
    void (*renderRich)(unsigned templateId, int a, int b); // VIBE_Text_RenderRichString
    void (*hudTiledRow)(int x, int y, int n);              // VIBE_Hud_BuildTiledRow

    // --- HandleUseChoice -------------------------------------------------------
    void (*queueRequest)(int actor, int code, int arg);    // VIBE_Command_QueueRequestArgs26
    void (*renderMessage)(char* out, int msgId);           // VIBE_Text_RenderFormattedMessage
    void (*showUseObject)(char* text, char* glyphs);       // VIBE_Panel_ShowUseObject

    // --- RenderItemGrid (workstation grid) --------------------------------------
    // VIBE_Building_ComputeOutputRatio(building) -> 0..1 output efficiency.
    float (*buildingOutputRatio)(void* building);          // VIBE_Building_ComputeOutputRatio
    // VIBE_BuildingType_GroupFromPairCode(code) on building+354 high byte; the grid
    // shows stations whose group is in [10,12]. We model it as a per-building query.
    int  (*buildingGroup)(void* building);                 // group via GroupFromPairCode
    // The two byte fields the grid header rows tile: building+131 / building+130.
    int  (*buildingField131)(void* building);
    int  (*buildingField130)(void* building);
    // building+376 == its item-container base (passed to queryItems).
    void* (*buildingContainer)(void* building);
};
void SetInventory2Hooks(const Inventory2Hooks* hooks);
const Inventory2Hooks& GetInventory2Hooks();

// ===========================================================================
// Translated functions.
// ===========================================================================

// gilde.exe 0x5513a0 — VIBE_Inventory_CreateGridSurface. Sets the grey backdrop
// colour, then creates a 1280x1280 format-11 surface and records it as the grid
// surface (dword_63D1D4[0]). Returns the new surface handle.
int InventoryCreateGridSurface(InvUiState& ui);

// gilde.exe 0x5513d8 — VIBE_Inventory_DestroyGridSurface. Destroys the grid surface
// if present and clears the handle. Returns the surfaceDestroy result (0 if none).
int InventoryDestroyGridSurface(InvUiState& ui);

// gilde.exe 0x54ebd8 — VIBE_Inventory_OpenSlotWindow. If the slot window is not open
// (slotWindow == -1): clears the 6 icon slots, resolves the person/scene record base,
// builds the child window + its object, adds the side panel window (+ marks colour 67
// on the active window), and renders the title rich-string. No-op if already open.
// `result` is the parent window passed through; `a3` selects the person record;
// `a2`/`a4` are the window placement coords; `a5` is the title rich-string template.
// Returns the (possibly unchanged) `result`.
int InventoryOpenSlotWindow(InvUiState& ui, int result, i16 a2, i16 a3, i16 a4,
                            unsigned a5);

// gilde.exe 0x54ecb0 — VIBE_Inventory_RefreshSlots. Reconciles the 6 visible icon
// slots against the container's live item objects:
//   pass 1: for each occupied slot, re-resolve the item; if it's gone, destroy its
//           widget and free the slot; else refresh its count text, enable/disable and
//           set the production fill bar, and re-stage it into the drag system.
//   pass 2: iterate the container's items; any item not already shown (and past the
//           first 6) gets bound into the first free slot with a fresh icon widget.
// Returns 1 if any item was (re)staged, else 0. `findStock` is the InvGridSlot stock
// table used by FindSlotByItemId (model of the global UI table); `stockCount` its size.
int InventoryRefreshSlots(InvUiState& ui, const InvGridSlot* findStock, int stockCount,
                          int a1);

// gilde.exe 0x54f8dc — VIBE_Inventory_RenderItemGrid. Lays out / reconciles the
// per-workstation item grid against the live workstation list `live` (dword_11BB6A0,
// `liveCount` entries):
//   pass 1: for each currently-shown station, if it is no longer live, tear down its
//           widgets; else prune the item-slot widgets whose item is gone, render the
//           output-ratio header + the two tiled stat rows, and bind any newly-present
//           item into the first free slot of the station.
//   pass 2: for each live station of group 10..12 not yet shown, add a new station
//           column (two labels + a child window + a header) into the first free column.
//   pass 3: re-stage every populated item slot into the drag system.
// Returns 1 if any slot was staged into the drag system, else 0. `a1`/`a2` are passed
// to formSelectWindow (the window-select coords).
int InventoryRenderItemGrid(InvUiState& ui, void* const* live, int liveCount,
                            int a1, int a2);

// gilde.exe 0x567538 — VIBE_Item_HandleUseChoice. Dispatches a use-object menu choice
// (the +12 dword of the menu record):
//   choice 1            -> queue a "consume" command with arg = item->field28 * 0.1.
//   choice 0 & kind==6  -> render message 3253 and show the use-object panel.
// Returns 1 (the original always returns 1). `item` is the carried item record (reads
// field4 actor handle, field28 magnitude, byte2 kind); `menu` the menu record (field12
// choice, field16 set to 1 on the panel branch); `glyphs` the panel glyph buffer.
struct ItemUseRec { int actor4; u8 kind2; int magnitude28; };
struct UseMenuRec { int choice12; int flag16; };
int ItemHandleUseChoice(const ItemUseRec* item, UseMenuRec* menu, char* glyphs);

}  // namespace guild::sim
