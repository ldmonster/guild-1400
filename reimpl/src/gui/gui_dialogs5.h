#pragma once
// guild::gui — gui_dialogs5: a slice of the retained-mode GUI's panel / form
// builders, translated 1:1 from gilde.exe. These are the deterministic
// VIBE_Panel_* layout/builder routines plus one VIBE_Window_* caption helper.
// They walk the game's city/person/building tables, lay out form windows and
// child widgets via the already-reconstructed Form/Window/Object data model,
// and (for the interactive variants) spin a per-frame loop until the engine
// signals exit.
//
// Functions recovered here:
//
//   VIBE_Window_SetCityNameCaption  @0x503e3c  swap the active city caption into the
//                                              "choose player" sub-menu and restore it.
//   VIBE_Panel_BuildLawSeals        @0x552904  lay out a law's wax-seal row + tiled bar.
//   VIBE_Panel_BuildBuildingList    @0x551b5c  load the geb_liste_2 form, price rows,
//                                              slider panel (build-only half).
//   VIBE_Panel_BuildMoneyInfo       @0x5517b4  load geld_info_2, money/building rows.
//   VIBE_Panel_BuildMasterList      @0x551e7c  load meister_liste_2, per-master rows
//                                              (name + tax field), slider panel.
//   VIBE_Panel_RunBuildingDetail    @0x551c2c  building stats panel + frame loop.
//   VIBE_Panel_RunBuildingList      @0x552fc4  building list panel + frame loop.
//   VIBE_Panel_RunApBuy             @0x54d9d8  apprenticeship-buy panel + frame loop.
//   VIBE_Panel_RunUseObject         @0x54f3e8  use-object panel + frame loop.
//   VIBE_Panel_RunChooseWappen      @0x551404  coat-of-arms chooser + frame loop.
//   VIBE_Panel_ChooseProfession     @0x566b9c  profession chooser + frame loop.
//   VIBE_Panel_RunPlayerStats       @0x552234  player-stats meta panel (composes the
//                                              three Build* panels) + frame loop.
//
// Cross-module / sibling leaves (the form loader, text renderer, the HUD layout
// helpers, the per-frame game loop, the command queue, the engine input-state
// globals, etc.) are routed through an installable GuiDialogs5Hooks struct with
// inert defaults defined in gui_dialogs5.cpp — the house pattern (GuiDialogs4Hooks
// / CutsceneMiscHooks). Tests install their own hooks; the integration test
// forwards a hook into a REAL reconstructed sibling. The widget pool
// (g_widgets / Widget_AllocSlot) and the current-window id (g_currentWindowId)
// are reused from their owning modules via extern.

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// Module-owned engine tables this slice walks/writes. In the original these are
// flat BSS arrays addressed by absolute pointer arithmetic; we model the ones no
// reconstructed sibling owns as plain arrays so the 1:1 index math reads cleanly.
// ===========================================================================

// dword_1231EA4 / dword_1231EB0 — BuildMoneyInfo per-row scratch (16 rows * 7 i32).
extern i32 g_moneyInfoRows[16 * 7];      // dword_1231EA4 base (rows of 7)
// dword_12312A8.. — BuildMasterList per-master row table (128 rows * 6 i32).
extern i32 g_masterRows[128 * 6];        // dword_12312A8 base (rows of 6)
// dword_1233430 / dword_1233434 / dword_1233438 — slider-panel scratch triple.
extern i32 g_sliderScratch[3];           // dword_1233430 / 4 / 8
// dword_12310C0[] — building-detail row id list (BuildBuildingList / RunPlayerStats).
extern i32 g_buildingRowIds[128];        // dword_12310C0
// dword_63D570 — building-row count produced by BuildBuildingList.
extern i32 g_buildingRowCount;           // dword_63D570
// dword_63D574 — RunPlayerStats remembered radio-tab selection.
extern i32 g_playerStatsTab;             // dword_63D574

// Engine control-state globals that drive the frame loops. These are owned by
// input.cpp / gui_dialogs4 / window.cpp; we re-declare them here as extern and
// REUSE them — never redefine. The hooks own the frame-loop continuation and the
// last-clicked id so tests get a deterministic, terminating loop.
extern i32 g_forceQuitLatch;             // dword_631614 — force-advance / quit latch (owned HERE)

// ===========================================================================
// Hooks: cross-module / sibling leaves with inert defaults.
// Signatures mirror the raw decompiled ABI so each call translates 1:1.
// ===========================================================================
struct GuiDialogs5Hooks {
    // --- form loader / form window selection -------------------------------
    int  (*gameTickFinalize)(i16 a, i16 b, const char* formName); // VIBE_GameTick_Finalize @0x41beb8
    void (*formCenterChildWindows)(int formId);                   // VIBE_Form_CenterChildWindows @0x41d6ac
    void (*formSelectWindow)(int formId, int winSlot);            // VIBE_Form_SelectWindow @0x41e4cc
    int  (*formGetWindowId)(int formId, int slot);                // VIBE_Form_GetWindowId @0x41e544
    int  (*formGetChildObjectId)(int formId, int group, int childIdx); // VIBE_Form_GetChildObjectId
    int  (*formDestroy)(int formId);                              // VIBE_Form_Destroy @0x41da04
    void (*formSetObjectsVisible)(int formId, int visible);      // VIBE_Form_SetObjectsVisible
    void (*formSetChildrenVisible)(int formId, int visible);     // VIBE_Form_SetChildrenVisible @0x41d568

    // --- text / rich-string rendering --------------------------------------
    int  (*textRenderRichString)(unsigned id, int a, int b, int c, int d); // VIBE_Text_RenderRichString @0x59d6e8
    void (*textRenderFormattedMessage)(char* out, const char* fmt, int a, int b, int c); // VIBE_Text_RenderFormattedMessage

    // --- HUD layout helpers ------------------------------------------------
    void (*hudSyncWindowColors)(int curWinId);                   // VIBE_Hud_SyncWindowColors @0x4bd5dc
    void (*hudBuildSliderPanel)(int x, int y, int winId, int curWinId, int count,
                                i32* a, i32* b, i32* c);         // VIBE_Hud_BuildSliderPanel @0x4bd388
    int  (*hudBuildBuildingPriceRows)(const void* cityRooms, int cap, i32* outIds); // VIBE_Hud_BuildBuildingPriceRows @0x552b80
    int  (*hudBuildScaledTiledBar)(int cur, int base, int step, const void* cityRooms, int baseGfx); // VIBE_Hud_BuildScaledTiledBar @0x4bd758
    void (*hudBuildButtonRow)(int curWinId, int a, i32* outIds, int n, const void* labels); // VIBE_Hud_BuildButtonRow

    // --- object / widget adders & setters ----------------------------------
    int  (*objectAddToWindow)(int curWinId, int yArg);           // VIBE_Object_AddToWindow @0x41ae10
    int  (*objectAddTextLabel)(i16 x, i16 y, int curWinId, const char* text); // VIBE_Object_AddTextLabel @0x41b288
    int  (*inputAddFieldToWindow)(i16 x, i16 y, i16 w, i16 h, unsigned flags, int curWinId); // VIBE_Input_AddFieldToWindow @0x410030
    void (*objectSetColor)(int widgetId, int color);            // VIBE_Object_SetColor @0x41e614
    void (*objectSetEnabled)(int widgetId, int enabled);        // VIBE_Object_SetEnabled
    void (*objectSetValueOrText)(int widgetId, int aText, int a3, int value, int a5); // VIBE_Object_SetValueOrText @0x41dfec
    int  (*objectGetDataPtr)(int widgetId);                     // VIBE_Object_GetDataPtr @0x41db9c
    int  (*windowAddChildWindow)(int x, i16 y, i16 w, int hcoord, int kind, int curWinId); // VIBE_Window_AddChildWindow @0x41a598
    void (*windowRemoveIfActive)(int widget, int prev, int formId);   // VIBE_Window_RemoveIfActive @0x41a7a8
    int  (*widgetDestroyByType)(int widget, int a, int formId);  // VIBE_Widget_DestroyByType @0x414f98
    void (*windowScroll)(int a, int dy, int winId);             // VIBE_Window_Scroll @0x41a024

    // --- radio group -------------------------------------------------------
    int  (*radioGroupCreate)(int kind, int seed);               // VIBE_RadioGroup_Create
    void (*radioGroupAddButton)(int group, int widgetId);       // VIBE_RadioGroup_AddButton
    void (*radioGroupFreeSurface)();                            // VIBE_RadioGroup_FreeSurface_Thunk

    // --- person / building / money queries ---------------------------------
    const void* (*personQueryBegin)(int n, int a, int b, unsigned short faction); // VIBE_Person_QueryBegin @0x586c20
    const void* (*personIterNext)();                            // VIBE_Person_IterNext @0x586a6c
    const short* (*personFindActiveByEntity)(const short* rec); // VIBE_Person_FindActiveByEntity @0x5920b0
    int  (*personComputeTotalWealth)(unsigned short personId, const short* rec); // VIBE_Person_ComputeTotalWealth
    int  (*moneyConvertToDisplayCoord)(int money, unsigned char unit); // VIBE_Money_ConvertToDisplayCoord
    int  (*buildingCollectByCityHandle)(unsigned short city, int a, i32* out); // VIBE_Building_CollectByCityHandle @0x591870
    int  (*buildingFindById)(int id);                           // VIBE_Building_FindById @0x587b20
    int  (*buildingMapTypeToCategory)(int type);               // VIBE_Building_MapTypeToCategory
    int  (*buildingGetUpgradeLevel)(int rec);                  // VIBE_Building_GetUpgradeLevel
    void (*buildingValueComputeWorth)(const void* rec, unsigned short city, int* out); // VIBE_BuildingValue_ComputeProductionWorth
    int  (*gesetzGetRecord)(unsigned char id, void* out);      // VIBE_Gesetz_GetRecord
    int  (*buildingTypeComputeVariantIndex)(int a, int b);     // VIBE_BuildingType_ComputeVariantIndex

    // --- inventory / item / trade ------------------------------------------
    int  (*inventoryFindSlotIndexByItemId)(short item, void* out); // VIBE_Inventory_FindSlotIndexByItemId
    int  (*inventoryFindSlotByItemId)(short item);             // VIBE_Inventory_FindSlotByItemId
    void (*itemUseObjectAction)(int cityTable, void* args);    // VIBE_Item_UseObjectAction
    void (*tradePopulateItemSlots)(int a, i32* out, int b, int formId, int c); // VIBE_Trade_PopulateItemSlots @0x51b57c
    int  (*formPopulateObjectList)(int formId, int a);         // VIBE_Form_PopulateObjectList @0x54d8fc

    // --- command queue -----------------------------------------------------
    void (*cmdBeginDeltaPacket)(int a, int b);                 // VIBE_Command_BeginDeltaPacket
    void (*cmdAppendCopiedField)(unsigned a, unsigned b, const void* p, int off); // VIBE_Command_AppendCopiedField
    int  (*cmdQueueRequestState23)();                          // VIBE_Command_QueueRequestState23
    int  (*cmdGetPacketStatusById)(int id);                    // VIBE_Command_GetPacketStatusById
    void (*cmdQueueRequestFlagBlob32)(int a, void* p);         // VIBE_Command_QueueRequestFlagBlob32
    void (*amtRefreshGuildState)();                            // VIBE_Amt_RefreshGuildState
    void (*cmdQueueRequestSlotReset28)(void* p, int a);        // VIBE_Command_QueueRequestSlotReset28

    // --- drag / cursor / coord / light -------------------------------------
    void (*dragSlotBeginDragText)();                           // VIBE_DragSlot_BeginDragText
    void (*dragSlotResetTable)();                              // VIBE_DragSlot_ResetTable
    void (*dragCursorSetSprite)(int a, int b);                 // VIBE_DragCursor_SetSprite
    void (*coordConvertX)();                                   // VIBE_Coord_ConvertX @0x5c6b08
    void (*lightSetGrayThunk)(int a, int b, void* p);          // VIBE_Light_SetGrayColorThunk

    // --- interaction / selection / menu ------------------------------------
    int  (*interactionTestHandlerFlagDword)(int flag);         // VIBE_Interaction_TestHandlerFlagDword
    int  (*interactionDispatchPanelEvent)(int a, int b, int c, int d); // VIBE_Interaction_DispatchPanelEvent
    void (*selectionUpdate)(int group, int sel);               // VIBE_Selection_Update
    void (*menuRunChoosePlayer)(int a, int b);                 // VIBE_Menu_RunChoosePlayer
    void (*menuChooseProfession)(int a, int b);                // VIBE_Menu_ChooseProfession

    // --- per-frame game loop (returns 0 to exit the loop) ------------------
    int  (*gameLogicRunFrameLoop)(int a, int b, const void* self); // VIBE_GameLogic_RunFrameLoop @0x4c09a0

    // --- engine control-state reads (mouse / last-click) -------------------
    int  (*readMouseRelease)();   // dword_672230 (right-click / cancel edge)
    int  (*readMouseWheelUp)();   // dword_672228
    int  (*readLastClickedId)();  // dword_75BF38
    int  (*readHoverObject)();    // dword_62D22C
};

// Install a hooks struct (nullptr restores the inert defaults). Returns previous.
const GuiDialogs5Hooks* SetGuiDialogs5Hooks(const GuiDialogs5Hooks* hooks);
const GuiDialogs5Hooks* GuiDialogs5Hooks_Default();
const GuiDialogs5Hooks& GuiDialogs5HooksActive();

// Reset module-owned tables + restore default hooks (deterministic test start).
void ResetGuiDialogs5();

// ===========================================================================
// Recovered functions.
// ===========================================================================

// gilde.exe 0x503e3c — VIBE_Window_SetCityNameCaption (winRec@eax, name@edx).
// winRec points at a window record; +48 is its caption (a UTF-16-ish 2-byte/char
// string, copied char-by-char in the original). dstCity receives the chosen-player
// caption, scratch holds the restored original.
int Window_SetCityNameCaption(char* winRec, char* name);

// gilde.exe 0x552904 — VIBE_Panel_BuildLawSeals (lawRow@eax).
// lawRow is a 4-int header [id, winA, winB(label), winC(bar)] followed by packed
// fields. Returns the lawRow ptr (or the tiled-bar result for early laws).
int* Panel_BuildLawSeals(int* lawRow);

// gilde.exe 0x551b5c — VIBE_Panel_BuildBuildingList. Returns the loaded form id.
int Panel_BuildBuildingList(unsigned short city);

// gilde.exe 0x5517b4 — VIBE_Panel_BuildMoneyInfo. Returns the loaded form id.
int Panel_BuildMoneyInfo(unsigned short city);

// gilde.exe 0x551e7c — VIBE_Panel_BuildMasterList. Returns form id, or -1 if empty.
int Panel_BuildMasterList(unsigned short city);

// gilde.exe 0x551c2c — VIBE_Panel_RunBuildingDetail (buildingRec@eax).
int Panel_RunBuildingDetail(char* buildingRec, unsigned short city);

// gilde.exe 0x552fc4 — VIBE_Panel_RunBuildingList.
int Panel_RunBuildingList(unsigned short city);

// gilde.exe 0x54d9d8 — VIBE_Panel_RunApBuy.
int Panel_RunApBuy(unsigned short city);

// gilde.exe 0x54f3e8 — VIBE_Panel_RunUseObject (itemRec@eax). itemRec[0] is the item id.
int Panel_RunUseObject(short* itemRec, unsigned short city);

// gilde.exe 0x551404 — VIBE_Panel_RunChooseWappen.
int Panel_RunChooseWappen(unsigned short city);

// gilde.exe 0x566b9c — VIBE_Panel_ChooseProfession (a1@eax, a2@edx, a3@esi).
int Panel_ChooseProfession(unsigned short* a1, unsigned short* a2, short* a3);

// gilde.exe 0x552234 — VIBE_Panel_RunPlayerStats. Returns the dispatch byte.
unsigned char Panel_RunPlayerStats(unsigned short city);

} // namespace guild::gui
