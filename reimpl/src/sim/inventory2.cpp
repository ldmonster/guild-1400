// inventory2 — inventory/workstation GRID-UI cluster (gilde.exe). Faithful 1:1
// ports of the widget-coupled inventory leaves inventory.h listed as deferred:
// the slot-window builder, the slot-icon-vs-live-item reconciliation, the per-
// workstation item-grid layout, the grid surface create/destroy, and the use-object
// menu dispatch. Every renderer / window / widget / scene-graph / object / surface /
// text call is routed through Inventory2Hooks; the default hook table is inert so the
// slot reconciliation / table arithmetic is golden-testable in isolation. The one
// reconstructed leaf reused is VIBE_Inventory_FindSlotByItemId (inventory.cpp).
#include "sim/inventory2.h"

#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered string constants.
//   byte_6244B8 = "%s~ %li Stück"  (the slot count text template; modeled as an id
//                 the widgetSetCountText hook owns — the format is engine-side).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Hook table (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const Inventory2Hooks* g_hooks = nullptr;

void  DefLightGray(int, int) {}
int   DefSurfaceCreate(int, int, int) { return 0; }
int   DefSurfaceDestroy(int) { return 0; }
int   DefWindowAddChild(int, int, int, int, int, int) { return 0; }
int   DefObjectAddToWindow(int, int) { return 0; }
int   DefInputAddIcon(int, int, int, int, int) { return -1; }
void  DefWidgetDestroy(int) {}
void  DefObjectSetEnabled(int, int) {}
void  DefObjectSetColor(int, int) {}
void  DefFormSelectWindow(int, int) {}
void  DefWidgetSetCountText(int, int, int) {}
void  DefWidgetSetFill(int, float) {}
void  DefWidgetClearProgress(int) {}
void  DefWidgetMarkSlot(int) {}
void* DefObjectGetData(int) { return nullptr; }
void  DefObjectSetValue(int, void*, int) {}
void* DefQueryItems(void*, int, int, int) { return nullptr; }
void* DefIterItems() { return nullptr; }
i16   DefItemType(void*) { return 0; }
int   DefItemCount(void*) { return 0; }
int   DefItemFlag33(void*) { return 0; }
int   DefItemProgress28(void*) { return 0; }
u8    DefItemMatByte(void*) { return 0; }
void  DefDragStore(int, void*) {}
void  DefDragAdd(int, void*) {}
void  DefRenderRich(unsigned, int, int) {}
void  DefHudTiledRow(int, int, int) {}
void  DefQueueRequest(int, int, int) {}
void  DefRenderMessage(char* out, int) { if (out) out[0] = '\0'; }
void  DefShowUseObject(char*, char*) {}
float DefBuildingOutputRatio(void*) { return 0.0f; }
int   DefBuildingGroup(void*) { return 0; }
int   DefBuildingField131(void*) { return 0; }
int   DefBuildingField130(void*) { return 0; }
void* DefBuildingContainer(void*) { return nullptr; }

const Inventory2Hooks g_default = {
    DefLightGray, DefSurfaceCreate, DefSurfaceDestroy,
    DefWindowAddChild, DefObjectAddToWindow, DefInputAddIcon, DefWidgetDestroy,
    DefObjectSetEnabled, DefObjectSetColor, DefFormSelectWindow,
    DefWidgetSetCountText, DefWidgetSetFill, DefWidgetClearProgress, DefWidgetMarkSlot,
    DefObjectGetData, DefObjectSetValue,
    DefQueryItems, DefIterItems,
    DefItemType, DefItemCount, DefItemFlag33, DefItemProgress28, DefItemMatByte,
    DefDragStore, DefDragAdd,
    DefRenderRich, DefHudTiledRow,
    DefQueueRequest, DefRenderMessage, DefShowUseObject,
    DefBuildingOutputRatio, DefBuildingGroup, DefBuildingField131, DefBuildingField130,
    DefBuildingContainer,
};
}  // namespace

void SetInventory2Hooks(const Inventory2Hooks* h) { g_hooks = h; }
const Inventory2Hooks& GetInventory2Hooks() { return g_hooks ? *g_hooks : g_default; }

// ===========================================================================
// 0x5513a0 — VIBE_Inventory_CreateGridSurface
//   VIBE_Light_SetGrayColorThunk(0, 64);
//   v3 = {_, 1280, 1280}; result = VIBE_Surface_Create(v3, 11, _);
//   dword_63D1D4[0] = result; return result;
// (the surface dims live in the v3 stack frame; we pass them directly.)
// ===========================================================================
int InventoryCreateGridSurface(InvUiState& ui) {
    const Inventory2Hooks& h = GetInventory2Hooks();
    h.lightSetGray(0, kGridSurfaceGray);                 // 0x5513ae
    int result = h.surfaceCreate(kGridSurfaceDim, kGridSurfaceDim, 11); // 0x5513c7
    ui.gridSurface = result;                             // 0x5513cc
    return result;                                       // 0x5513d5
}

// ===========================================================================
// 0x5513d8 — VIBE_Inventory_DestroyGridSurface
//   if (dword_63D1D4[0]) result = VIBE_Surface_Destroy(dword_63D1D4[0], _);
//   dword_63D1D4[0] = 0; return result;
// ===========================================================================
int InventoryDestroyGridSurface(InvUiState& ui) {
    const Inventory2Hooks& h = GetInventory2Hooks();
    int result = 0;
    if (ui.gridSurface) {                                // 0x5513e2
        result = h.surfaceDestroy(ui.gridSurface);       // 0x5513f1
    }
    ui.gridSurface = 0;                                  // 0x5513e6
    return result;                                       // 0x5513ee
}

// ===========================================================================
// 0x54ebd8 — VIBE_Inventory_OpenSlotWindow
//   if (dword_63D1CC == -1) {
//     for (i=0;i!=12;i+=2){ dword_1232BF8[i]=-1; dword_1232BFC[i]=0; }  // clear slots
//     dword_63D1D0 = &word_12CE910[268*a3];                            // person base
//     dword_63D1CC = VIBE_Window_AddChildWindow(a2,a4,196,205,16,result);
//     VIBE_Object_AddToWindow(dword_63D1CC, 0);
//     VIBE_Window_AddChildWindow(0,44,30, HIWORD(dword_62D298+6), 272, dword_62D230);
//     word_67EDFC[476*dword_62D230] = 67;     // colour the active window
//     return VIBE_Text_RenderRichString(a5);
//   }
//   return result;
// The dword_1232BF8[i]=-1 / dword_1232BFC[i]=0 pair are the iconItem/iconWidget tables;
// the [i] step-by-2 maps to the 6 logical slots. We model the slot clear + the window
// build + colour mark + title render. The side-panel HIWORD()/word_67EDFC details are
// engine-internal (objectSetColor stands in for the colour write).
// ===========================================================================
int InventoryOpenSlotWindow(InvUiState& ui, int result, i16 a2, i16 a3, i16 a4,
                            unsigned a5) {
    const Inventory2Hooks& h = GetInventory2Hooks();
    int v5 = result;                                     // 0x54ebdb
    if (ui.slotWindow == -1) {                           // 0x54ebe6
        for (int i = 0; i < kInvSlotCount; ++i) {        // 0x54ebf1 (i!=12; i+=2)
            ui.iconItem[i]   = nullptr;                  // dword_1232BF8[i] = -1
            ui.iconWidget[i] = -1;                       // dword_1232BFC[i] = 0
        }
        // dword_63D1D0 = &word_12CE910[268 * a3] — record base for person a3.
        ui.personTable = reinterpret_cast<void*>(static_cast<intptr_t>(268 * a3)); // 0x54ec2a
        ui.slotWindow = h.windowAddChild(a2, a4, 196, 205, 16, v5);  // 0x54ec45
        h.objectAddToWindow(ui.slotWindow, 0);                       // 0x54ec50
        h.windowAddChild(0, 44, 30, 272, 0, ui.activeWindow);        // 0x54ec79 (side panel)
        h.objectSetColor(ui.activeWindow, 67);                       // 0x54ec92 (word_67EDFC[..]=67)
        h.renderRich(a5, 0, 0);                                      // 0x54eca1
        return v5;                                                   // 0x54eca1 (rich-string ret)
    }
    return result;                                       // 0x54eca9
}

// ===========================================================================
// 0x54ecb0 — VIBE_Inventory_RefreshSlots
// Two passes (see header). v1 == dword_63D1D0 (person base); early-out when null.
// Pass 1: for each of the 6 occupied slots, re-resolve via QueryFind(kind1,sub0,type);
//   gone  -> destroy widget, slot.item=null, slot.widget=-1.
//   live  -> set count text, find the stock slot, enable/disable + production fill,
//            and (if it has data) stage it via DragSlot_StoreItem and mark hit.
// Pass 2: iterate all container items (QueryFind kind1 sub5); skip mat-byte==9; if the
//   item isn't already in a slot AND its index >= 6, place it in the first free slot
//   with a fresh icon, count text and enable/fill state.
// The InvGridSlot stock table (findStock/stockCount) feeds FindSlotByItemId; the
// fill divisor is the dword at slot+4 (modeled via slot->pad2[2..5]).
// ===========================================================================
namespace {
// Read the dword the original takes as *((int*)slot + 1) (== slot bytes +4..+7), the
// production-capacity divisor used for the fill ratio.
int SlotCapacityDword(const InvGridSlot* slot) {
    if (!slot) return 0;
    int v = 0;
    std::memcpy(&v, slot->pad2 + 2, sizeof(int));  // +0x04
    return v;
}
}  // namespace

int InventoryRefreshSlots(InvUiState& ui, const InvGridSlot* findStock, int stockCount,
                          int /*a1*/) {
    const Inventory2Hooks& h = GetInventory2Hooks();
    if (!ui.personTable) return 0;                       // 0x54ecbf -> 0x54ee9c
    int staged = 0;                                      // v2 = 0

    // --- pass 1: reconcile the occupied slots ----------------------------------
    for (int i = 0; i < kInvSlotCount; ++i) {            // 0x54ecc9 (i!=12; i+=2)
        void* item = ui.iconItem[i];                     // dword_1232C04[i]
        if (!item) continue;                             // 0x54ecd3

        // Re-resolve the live object by its type (QueryFind kind1, sub0, *item).
        i16 type = h.itemType(item);
        if (h.queryItems(ui.personTable, 1, 0, type)) {  // 0x54eeb3 (still present)
            void* data = h.objectGetData(ui.iconWidget[i]);             // 0x54eece
            h.objectSetValue(ui.iconWidget[i], data, h.itemCount(item)); // 0x54eee4
            h.widgetSetCountText(ui.iconWidget[i], type, h.itemCount(item)); // 0x54ef20

            const InvGridSlot* slot =
                InventoryFindSlotByItemId(findStock, stockCount, type);  // 0x54ef36
            if (slot) {                                                  // 0x54ef3a
                if (h.itemFlag33(item)) {                                // 0x54ef42 (in production)
                    h.objectSetEnabled(ui.iconWidget[i], 0);             // 0x54ef54
                    int cap = SlotCapacityDword(slot);                   // *((int*)slot+1)
                    float fill = cap ? (1.0f - static_cast<float>(
                        static_cast<double>(h.itemProgress28(item)) /
                        static_cast<double>(cap))) : 0.0f;               // 0x54ef7d
                    h.widgetSetFill(ui.iconWidget[i], fill);             // 0x54ef85
                } else {
                    h.objectSetEnabled(ui.iconWidget[i], 1);             // 0x54efde
                    h.widgetClearProgress(ui.iconWidget[i]);             // 0x54eff2/0x54f004
                }
            }
            if (data) {                                                  // 0x54ef8d
                staged = 1;                                              // v2 = 1
                h.dragStore(type + kSlotIconBase, data);                 // 0x54efa8
            }
        } else {                                                        // 0x54efb2 (gone)
            ui.iconItem[i] = nullptr;
            h.widgetDestroy(ui.iconWidget[i]);                           // 0x54efc3
            ui.iconWidget[i] = -1;                                       // 0x54efc8
        }
    }

    // --- pass 2: bind not-yet-shown items into free slots ----------------------
    int seen = 0;                                        // count of items walked
    for (void* j = h.queryItems(ui.personTable, 1, 5, 0); j; j = h.iterItems(), ++seen) {
        if (h.itemMatByte(j) == 9) continue;             // 0x54ed0c

        // Is j already in a slot? (linear scan of the 6 slots.)
        int idx = 0;                                     // v6
        if (ui.iconItem[0] != j) {
            do { ++idx; } while (idx < kInvSlotCount && ui.iconItem[idx] != j); // 0x54ed2f
        }
        if (idx < kGridSlotsPerWs) continue;             // 0x54ed34 (v6 >= 6 to proceed)

        // Find the first free slot.
        int free = -1;
        for (int k = 0; k < kInvSlotCount; ++k) {        // 0x54ed48
            if (!ui.iconItem[k]) { free = k; break; }
        }
        if (free < 0) continue;

        i16 type = h.itemType(j);
        ui.iconItem[free] = j;                           // 0x54ed6b
        int widget = h.inputAddIcon(56 * (free % 3) + 21, 54 * (free / 3) + 74, 10,
                                    type + kSlotIconBase, ui.slotWindow);   // 0x54eda3
        ui.iconWidget[free] = widget;                    // 0x54eda8
        h.widgetMarkSlot(widget);                        // 0x54edb9 (+444=35)
        h.widgetClearProgress(widget);                   // 0x54edcb/0x54ede0 (+456=-1,+460=0)
        h.widgetSetCountText(widget, type, h.itemCount(j));               // 0x54ee19

        const InvGridSlot* slot = InventoryFindSlotByItemId(findStock, stockCount, type); // 0x54ee2a
        if (slot) {
            if (h.itemFlag33(j)) {                       // 0x54ee3b
                h.objectSetEnabled(widget, 0);           // 0x54ee4d
                int cap = SlotCapacityDword(slot);       // *(int*)(v14+4)
                float fill = cap ? (1.0f - static_cast<float>(
                    static_cast<double>(h.itemProgress28(j)) /
                    static_cast<double>(cap))) : 0.0f;   // 0x54ee75
                h.widgetSetFill(widget, fill);           // 0x54ee7d
            } else {
                h.objectSetEnabled(widget, 1);           // 0x54f01f
            }
        }
    }
    return staged;                                       // v2
}

// ===========================================================================
// 0x54f8dc — VIBE_Inventory_RenderItemGrid
// Per-station tables stepped by 64 bytes (v44/64 == station idx 0..31). The per-
// station 6-slot sub-tables step by 8 bytes (icon widget dword + type word). The
// three passes are described in the header. Rich-string template ids:
//   0x12BF/0x12C0 -> the two tiled-stat header rows; 0x12BE -> the station label;
//   aC1n2I "$C%1N2 %i%%" -> the output-ratio percentage row.
// ===========================================================================
int InventoryRenderItemGrid(InvUiState& ui, void* const* live, int liveCount,
                            int a1, int a2) {
    const Inventory2Hooks& h = GetInventory2Hooks();
    int staged = 0;                                      // v41 = 0
    h.formSelectWindow(a1, a2);                          // 0x54f90e
    ui.countWindow = 0;                                  // dword_63D56C[0]

    auto isLive = [&](void* b) -> bool {                 // membership in dword_11BB6A0
        if (!b) return false;
        for (int i = 0; i < liveCount && i < kGridStations; ++i)
            if (live[i] == b) return true;
        return false;
    };

    // --- pass 1: reconcile every currently-shown station -----------------------
    for (int s = 0; s < kGridStations; ++s) {            // v44: 0..2048 step 64
        void* b = ui.wsBuilding[s];
        if (!b) continue;                                // 0x54f91d
        ++ui.countWindow;                                // 0x54fa09

        if (!isLive(b)) {                                // station gone (v38 == 0)
            ui.wsBuilding[s] = nullptr;                  // 0x54fc29
            if (ui.wsLabelA[s] != -1) {                  // 0x54fc32
                h.widgetDestroy(ui.wsLabelA[s]);         // 0x54fc3b
                ui.wsLabelA[s] = -1;
            }
            if (ui.wsLabelB[s] != -1) {                  // 0x54fc53
                h.widgetDestroy(ui.wsLabelB[s]);         // 0x54fc57
                ui.wsLabelB[s] = -1;
            }
            if (ui.wsWindow[s] != -1) {                  // 0x54fc77
                h.widgetDestroy(ui.wsWindow[s]);         // 0x54fc7f
                ui.wsWindow[s] = -1;
            }
            continue;                                    // LABEL_3
        }

        ui.activeWindow = ui.wsWindow[s];                // dword_62D230 = dword_1232C3C[..]
        // Prune the 6 item slots whose item is no longer present.
        for (int k = 0; k < kGridSlotsPerWs; ++k) {      // 0x54faa5 (step 8 until v39)
            i16 type = ui.wsSlotType[s][k];
            if (k != type &&                              // (_WORD)v18 != word_1232C44[..]
                !h.queryItems(h.buildingContainer(b), 1, k, type)) {  // 0x54fa7c
                h.widgetDestroy(ui.wsSlotWidget[s][k]);  // 0x54fa8e
                ui.wsSlotWidget[s][k] = -1;              // 0x54fa93
                ui.wsSlotType[s][k] = static_cast<i16>(k); // 0x54fa99
            }
        }

        // Header: output-ratio percentage row + the two tiled stat rows.
        int pct = static_cast<int>(h.buildingOutputRatio(b) * kRatioScale); // 0x54fac5
        h.renderRich(0x12C1u /*aC1n2I*/, 0, pct);        // 0x54faef
        h.renderRich(0x12BFu, 0, 0);                     // 0x54fafc
        h.hudTiledRow(80, 22, h.buildingField131(b));    // 0x54fb29
        h.renderRich(0x12C0u, 0, 0);                     // 0x54fb33
        h.hudTiledRow(80, 37, h.buildingField130(b));    // 0x54fb60

        // Bind any newly-present item into the first free slot.
        int slotIdx = 0;
        for (void* it = h.queryItems(h.buildingContainer(b), 1, 5, 0); it;
             it = h.iterItems()) {                        // 0x54fb86
            if (h.itemMatByte(it) == 9) continue;        // 0x54fbbc
            i16 type = h.itemType(it);
            // Is it already in a slot of this station?
            int found = -1;
            for (int k = 0; k < kGridSlotsPerWs; ++k) {  // 0x54fbd3
                if (ui.wsSlotType[s][k] == type) { found = k; break; }
            }
            if (found < 0) {                             // 0x54fc9e (v22>=6)
                h.formSelectWindow(a1, a2);              // 0x54fcb8
                int free = -1;
                for (int k = 0; k < kGridSlotsPerWs; ++k) {  // 0x54fccb
                    if (ui.wsSlotType[s][k] == 0) { free = k; break; }
                }
                if (free >= 0) {                         // LABEL_44
                    ui.wsSlotWidget[s][free] = h.inputAddIcon(
                        54 * free + 80, 40, 10, type + kSlotIconBase, ui.activeWindow); // 0x54fce2
                    ui.wsSlotType[s][free] = type;       // 0x54fd11
                    found = free;
                }
            }
            if (found >= 0) {
                void* data = h.objectGetData(ui.wsSlotWidget[s][found]); // 0x54fbd9
                h.objectSetValue(ui.wsSlotWidget[s][found], data, h.itemCount(it)); // 0x54fbff
            }
            ++slotIdx; (void)slotIdx;
        }
    }

    // --- pass 2: add columns for live group-10..12 stations not yet shown ------
    for (int j = 0; j < liveCount && j < kGridStations; ++j) {  // 0x54f952
        void* b = live[j];
        if (!b) continue;                                // 0x54f95c
        int grp = h.buildingGroup(b);                    // 0x54f969
        if (grp < 10 || grp > 12) continue;              // 0x54fd1f

        // Already shown?
        bool shown = false;
        for (int s = 0; s < kGridStations; ++s)          // 0x54fd3f
            if (ui.wsBuilding[s] == b) { shown = true; break; }
        if (shown) continue;                             // LABEL_7

        // First free column.
        int free = -1;
        for (int s = 0; s < kGridStations; ++s) {        // 0x54fd72
            if (!ui.wsBuilding[s]) { free = s; break; }
        }
        if (free < 0) continue;                          // (no LABEL_54)

        h.formSelectWindow(a1, a2);                      // 0x54fd8b
        ui.wsBuilding[free] = b;                         // 0x54fdb5
        ui.wsLabelB[free] = h.objectAddToWindow(ui.activeWindow, 120 * free);     // 0x54fdc2
        ui.wsLabelA[free] = h.objectAddToWindow(ui.activeWindow, 120 * free + 5); // 0x54fe04
        ui.wsWindow[free] = h.windowAddChild(64, 120 * free, 80, 180, 16, ui.activeWindow); // 0x54fe29
        h.objectSetColor(ui.wsWindow[free], 67);         // 0x54fe45
        h.renderRich(0x12BEu, 0, h.buildingField131(b)); // 0x54fe67
    }

    // --- pass 3: re-stage every populated item slot into the drag system -------
    for (int s = 0; s < kGridStations; ++s) {            // 0x54f9e1 (v7 step 16 until 512)
        if (!ui.wsBuilding[s]) continue;                 // 0x54f98b
        for (int k = 0; k < kGridSlotsPerWs; ++k) {      // 0x54f9d6 (step 8)
            int widget = ui.wsSlotWidget[s][k];
            if (widget == -1) continue;                  // 0x54f99d
            void* data = h.objectGetData(widget);
            if (!data) continue;
            h.dragAdd(ui.wsSlotType[s][k] + kSlotIconBase, data);  // 0x54f9c4
            staged = 1;                                  // 0x54f9c9
        }
    }
    return staged;                                       // v41
}

// ===========================================================================
// 0x567538 — VIBE_Item_HandleUseChoice
//   v4 = *(menu + 12);                                // the chosen action
//   if (v4 == 1) {                                    // consume / use
//     arg = (int)(*(float*)(item+28) * 0.1f);
//     VIBE_Command_QueueRequestArgs26(*(item+4), 28, arg);
//     return 1;
//   }
//   if (v4 || *(item+2) != 6) return 1;               // only kind 6 has a panel
//   *(menu+16) = 1;
//   VIBE_Text_RenderFormattedMessage(buf, 3253);
//   VIBE_Panel_ShowUseObject(buf, _, glyphs);
//   return 1;
// The original treats item+28 as a float magnitude; we keep magnitude28 as an int and
// reproduce the *0.1f truncation faithfully.
// ===========================================================================
int ItemHandleUseChoice(const ItemUseRec* item, UseMenuRec* menu, char* glyphs) {
    const Inventory2Hooks& h = GetInventory2Hooks();
    int v4 = menu->choice12;                             // *(menu+12) — 0x56753f
    if (v4 == 1) {                                       // 0x567545
        int arg = static_cast<int>(static_cast<float>(item->magnitude28) * kUseSpeedScale); // 0x56756f
        h.queueRequest(item->actor4, 28, arg);           // 0x567575
        return 1;                                        // 0x56755d
    }
    if (v4 || item->kind2 != 6) return 1;                // 0x56754f
    menu->flag16 = 1;                                    // 0x567586
    char buf[2048];                                      // v8[2048]
    h.renderMessage(buf, 3253);                          // 0x56758d
    h.showUseObject(buf, glyphs);                        // 0x567597
    return 1;                                            // 0x56755d
}

}  // namespace guild::sim
