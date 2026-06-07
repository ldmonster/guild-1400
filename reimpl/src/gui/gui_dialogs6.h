#pragma once
// guild::gui — gui_dialogs6: a further slice of the retained-mode GUI's
// panel / dialog "run" routines, translated 1:1 from gilde.exe. These are the
// interactive VIBE_Panel_* / VIBE_Dialog_* loops that build a form window, lay
// out child widgets via the already-reconstructed Form/Window/Object data
// model, and spin a per-frame loop until the engine signals exit.
//
// Functions recovered here:
//
//   VIBE_Panel_RunGelage            @0x54e940  party/feast panel: amount field +
//                                              frame loop, queues a "party" action.
//   VIBE_Panel_RunInventory         @0x54f668  inventory panel: per-slot use-object
//                                              dispatch (calls the REAL sibling
//                                              gui::Panel_RunUseObject) + frame loop.
//   VIBE_Panel_RunBuildingRoundEnd  @0x552d34  building round-end stats panel +
//                                              frame loop (sibling of RunBuildingDetail).
//   VIBE_Panel_ShowUniversity       @0x566dd0  university 3-choice panel: lays out
//                                              three choice columns + frame loop,
//                                              queues an apprenticeship request.
//   VIBE_Panel_RunTraining          @0x54fe74  craft-training panel: handler-count
//                                              gate, per-person training command loop.
//   VIBE_Panel_RunPlantBar          @0x54dfdc  3D vegetation-placement bar: radio of
//                                              plant kinds, raycast-to-terrain place,
//                                              queues the plant/build command + frame loop.
//   VIBE_Panel_RunOfficeSession     @0x5546e4  office/council session: builds a person
//                                              grid/column for an office category and
//                                              spins the selection loop.
//   VIBE_Panel_RunThievesGuildTrain @0x550190  thieves-guild training item grid + loop.
//   VIBE_Dialog_RobberRaidConfirm   @0x5130bc  robber-raid confirm: playerbar + a
//                                              map-view panel dispatch.
//   VIBE_Dialog_BriberyConfirm      @0x512e08  bribery confirm: playerbar + a map-view
//                                              panel dispatch.
//
// Cross-module / sibling leaves (form loader, text renderer, command queue,
// 3D scene / heightmap / object helpers, the per-frame game loop, the engine
// input-state globals, etc.) are routed through an installable GuiDialogs6Hooks
// struct with inert defaults defined in gui_dialogs6.cpp — the house pattern
// (GuiDialogs5Hooks / GuiDialogs4Hooks / CutsceneMiscHooks). Tests install their
// own hooks; the integration test forwards RunInventory's use-object leaf into
// the REAL reconstructed sibling gui::Panel_RunUseObject (gui_dialogs5.cpp). The
// widget pool (g_widgets / Widget_AllocSlot) and the current-window id
// (g_currentWindowId) are reused from their owning modules via extern, and the
// force-quit latch (g_forceQuitLatch / dword_631614) is reused from gui_dialogs5.

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// Module-owned engine tables this slice walks/writes (BSS, zero at load).
// ===========================================================================
// dword_67EF18.. — per-window border-color table (14*winColor + 224*winColor
// stride in the original). We model the four channels written by RunGelage /
// RunInventory as a small array; only the active window-color slot is touched.
extern i32 g_winBorderColor[4];          // dword_67EF18 / 1C / 20 / 24 (active slot)
// dword_1232C00 / dword_1232C04 — RunInventory per-slot (id,recPtr) pair table.
// The original stores 32-bit pointers in dword_1232C04; we widen to host pointer
// width so the use-object dispatch can carry a real item-record pointer.
extern i32 g_invSlotIds[6];              // dword_1232C00 (6 ids, step 2 over 12)
extern void* g_invSlotRecs[6];           // dword_1232C04 (6 rec ptrs)
// dword_1232C38 — RunThievesGuildTrain item-grid id table (32 ids, step 16 over 512).
extern i32 g_thiefGridIds[32];           // dword_1232C38
// dword_63D1CC — RunInventory "active inventory window" latch (-1 = none).
extern i32 g_inventoryActiveWindow;      // dword_63D1CC
// dword_631768 — RunPlantBar "previous panel to hide/restore" window (-1 = none).
extern i32 g_plantPrevPanel;             // dword_631768

// Engine control-state globals owned by gui_dialogs5 — REUSED here via extern.
extern i32 g_forceQuitLatch;             // dword_631614 (owned by gui_dialogs5.cpp)

// ===========================================================================
// Hooks: cross-module / sibling leaves with inert defaults.
// Signatures mirror the raw decompiled ABI so each call translates 1:1.
// ===========================================================================
struct GuiDialogs6Hooks {
    // --- form loader / form window selection -------------------------------
    int  (*gameTickFinalize)(i16 a, i16 b, const char* formName); // VIBE_GameTick_Finalize @0x41beb8
    void (*formCenterChildWindows)(int formId);                   // VIBE_Form_CenterChildWindows @0x41d6ac
    void (*formSelectWindow)(int formId, int winSlot);            // VIBE_Form_SelectWindow @0x41e4cc
    int  (*formGetChildObjectId)(int formId, int group, int childIdx); // VIBE_Form_GetChildObjectId @0x41dea8
    int  (*formDestroy)(int formId);                              // VIBE_Form_Destroy @0x41da04
    void (*formSetObjectsVisible)(int formId, int visible);      // VIBE_Form_SetObjectsVisible @0x41d634
    void (*windowRemoveChildren)(int curWinId, int a);           // VIBE_Window_RemoveChildren @0x41a7dc
    void (*windowRemoveIfActive)(int widget, int prev, int formId);   // VIBE_Window_RemoveIfActive @0x41a7a8

    // --- text / rich-string rendering --------------------------------------
    int  (*textRenderRichString)(unsigned id, int a, int b, int c, int d); // VIBE_Text_RenderRichString @0x59d6e8
    void (*textRenderFormattedMessage)(char* out, unsigned fmtId, int a, int b, int c); // VIBE_Text_RenderFormattedMessage

    // --- object / widget adders & setters ----------------------------------
    int  (*objectAddToWindow)(int curWinId, int yArg);           // VIBE_Object_AddToWindow @0x41ae10
    int  (*objectAddTextLabel)(i16 x, i16 y, int curWinId, const char* text); // VIBE_Object_AddTextLabel @0x41b288
    void (*objectSetValueOrText)(int widgetId, int aText, int a3, int value, int a5); // VIBE_Object_SetValueOrText @0x41dfec
    int  (*objectGetDataPtr)(int widgetId);                      // VIBE_Object_GetDataPtr @0x41db9c
    void (*objectSetPosition)(int objGroup, const float* pos);   // VIBE_Object_SetPosition
    void (*objectDetachAndRelease)(int objGroup);                // VIBE_Object_DetachAndRelease
    void (*objectApplyTransparencyTree)(int objGroup, const void* p); // VIBE_Object_ApplyTransparencyTree

    // --- radio group / selection -------------------------------------------
    int  (*radioGroupCreate)(int kind, int seed);                // VIBE_RadioGroup_Create
    void (*radioGroupAddButton)(int group, int widgetId);        // VIBE_RadioGroup_AddButton
    void (*radioGroupFreeSurface)();                             // VIBE_RadioGroup_FreeSurface_Thunk
    void (*selectionUpdate)(int group, int sel);                 // VIBE_Selection_Update
    void (*selectionClearAll)();                                 // VIBE_Selection_ClearAll
    int  (*readActiveRadioSel)(int group);                       // dword_676588[35*group]

    // --- person / office / building queries --------------------------------
    const short* (*personFindRecordById)(int id);               // VIBE_Person_FindRecordById
    int  (*officeCollectByCategory)(int cat, int cap, void* outBuf); // VIBE_Office_CollectByCategory
    int  (*officeCollectElectiveOffices)(int a, int cap, void* outBuf); // VIBE_Office_CollectElectiveOffices
    int  (*buildingMapTypeToCategory)(int type);                // VIBE_Building_MapTypeToCategory
    void (*buildingValueComputeWorth)(const void* rec, unsigned short city, int* out); // VIBE_BuildingValue_ComputeProductionWorth
    int  (*buildingGetUpgradeLevel)(int rec);                   // VIBE_Building_GetUpgradeLevel
    float (*buildingComputeCurrentOutput)(int rec);             // VIBE_Building_ComputeCurrentOutput
    double (*buildingComputeMarketPrice)(int item, unsigned amount); // VIBE_Building_ComputeMarketPrice
    const short* (*amtFindOfficeTypeRecord)(int kind);          // VIBE_Amt_FindOfficeTypeRecord
    int  (*amtFindFreePlacement)(int rec, int x, int kind, int z); // VIBE_Amt_FindFreePlacement

    // --- handler-entity queries (He_*) -------------------------------------
    const void* (*heFindFirstHandlerByFilter)(int a, int b, int c, int d, int e); // VIBE_He_FindFirstHandlerByFilter
    const void* (*heFindNextMatchingHandler)();                 // VIBE_He_FindNextMatchingHandler
    int  (*heCountMatchingEntities)(int a, unsigned short city, int c); // VIBE_He_CountMatchingEntities

    // --- inventory / item / trade ------------------------------------------
    void (*inventoryOpenSlotWindow)(int curWin, int a, unsigned short city, int c, unsigned fmtId); // VIBE_Inventory_OpenSlotWindow
    void (*inventoryRefreshSlots)(int a);                       // VIBE_Inventory_RefreshSlots
    int  (*inventoryFindSlotByItemId)(short item);              // VIBE_Inventory_FindSlotByItemId
    void (*inventoryRenderItemGrid)(int a, int b);              // VIBE_Inventory_RenderItemGrid

    // --- drag slot / cursor ------------------------------------------------
    int  (*dragSlotCountUsed)();                                // VIBE_DragSlot_CountUsed
    void (*dragSlotResetTable)();                               // VIBE_DragSlot_ResetTable
    void (*dragSlotResetGridTable)();                           // VIBE_DragSlot_ResetGridTable
    void (*dragCursorSetSprite)(int a, int b);                  // VIBE_DragCursor_SetSprite

    // --- command queue -----------------------------------------------------
    void (*cmdEnqueueBuildingActionStart)(const char* name);    // VIBE_Command_EnqueueBuildingActionStart
    void (*cmdEnqueueBuildingActionEnd)();                      // VIBE_Command_EnqueueBuildingActionEnd
    void (*cmdEnqueueCmd15)(int a, int b, int c, int d);        // VIBE_Command_EnqueueCmd15
    void (*cmdQueueRequestSlotReset28)(void* p, int a);         // VIBE_Command_QueueRequestSlotReset28
    void (*cmdQueueRequestArgs25)(int a, int b, int c, int d, int e); // VIBE_Command_QueueRequestArgs25
    unsigned (*cmdQueueRequestMixed44)(int a, int x, int kind, int z, int d, int e); // VIBE_Command_QueueRequestMixed44
    int  (*cmdGetPacketStatusById)(unsigned id);               // VIBE_Command_GetPacketStatusById
    int  (*cmdGetPacketSeqById)(unsigned id);                  // VIBE_Command_GetPacketSeqById

    // --- money / coord / light ---------------------------------------------
    int  (*moneyConvertToDisplayCoord)(int money, unsigned char unit); // VIBE_Money_ConvertToDisplayCoord
    int  (*moneyMultiplyByRate)(int dataPtr, unsigned char unit); // VIBE_Money_MultiplyByRate
    int  (*personSumCurrencyHeld)(int rec);                     // VIBE_Person_SumCurrencyHeld
    void (*coordConvertX)();                                    // VIBE_Coord_ConvertX
    void (*lightSetGrayThunk)(int a, int b, int p);             // VIBE_Light_SetGrayColorThunk
    void (*lightBuildObjectCache)(int objGroup);               // VIBE_Light_BuildObjectCache

    // --- 3D scene / heightmap / mesh ---------------------------------------
    const short* (*gameObjectQueryFind)(int a, int b, int c, int d); // VIBE_GameObject_QueryFind
    const short* (*gameObjectIterNext)();                       // VIBE_GameObject_IterNext
    int  (*heightmapFindNearestEntry)(int* hm, int x, int z, double r, int* out); // VIBE_Heightmap_FindNearestEntryToPoint
    void (*mathMatrixCopy)(int src, int dst);                   // VIBE_Math_MatrixCopy
    void (*meshSetGlobalColorTemp)(int a, char r, char g, int b);  // VIBE_Mesh_SetGlobalColorTemp
    int  (*sceneLoadObjectGroup)(const char* path, int a, i16 b, int c); // VIBE_Scene_LoadObjectGroup
    void (*plantLoadVegetationModel)(int* seq);                 // VIBE_Plant_LoadVegetationModel
    void (*sound3dSetListener)(int a, const float* p, int c, float* d, int e); // VIBE_Sound3d_SetListenerFromVectors
    void (*errorLogReportMessage)(const char* msg);            // VIBE_ErrorLog_ReportMessage

    // --- HUD layout / player bar / voice -----------------------------------
    void (*hudUpdateEdgeScroll)(int a, int b, int c);          // VIBE_Hud_UpdateEdgeScroll
    void (*hudBuildPersonGridLayout)(char cat, int n, int a, void* table, int recBase); // VIBE_Hud_BuildPersonGridLayout
    void (*hudBuildPersonColumnLayout)(char cat, int n, int a, void* table, int recBase); // VIBE_Hud_BuildPersonColumnLayout
    void (*playerBarCreate)(int a, int b);                     // VIBE_PlayerBar_Create
    void (*playerBarDestroy)(int a, void* p);                  // VIBE_PlayerBar_Destroy
    void (*voicePlayCraftFavorComment)();                      // VIBE_Voice_PlayCraftFavorComment

    // --- AI / interaction / dialog -----------------------------------------
    void (*aiMethodComputeChoiceWeights)(int outBuf, const short* rec); // VIBE_AiMethod_ComputeChoiceWeights
    int  (*interactionTestHandlerFlagDword)(int flag);         // VIBE_Interaction_TestHandlerFlagDword
    int  (*interactionDispatchPanelEvent)(int a, int b, int c, int d); // VIBE_Interaction_DispatchPanelEvent
    int  (*dialogCheckActiveCharFlag)();                       // VIBE_Dialog_CheckActiveCharFlag
    void (*dialogShowMessageBox)(int textPtr, int a, int formId); // VIBE_Dialog_ShowMessageBox
    void (*mapViewPanelDispatcher)(int a, int b, void (*cb)(), int textPtr, int e); // VIBE_MapView_PanelDispatcher

    // --- per-frame game loop (returns 0 to exit the loop) ------------------
    int  (*gameLogicRunFrameLoop)(int a, int b, const void* self); // VIBE_GameLogic_RunFrameLoop @0x4c09a0

    // --- engine control-state reads ----------------------------------------
    int  (*readMouseRelease)();   // dword_672230 (right-click / cancel edge)
    int  (*readMouseWheelUp)();   // dword_672228
    int  (*readLastClickedId)();  // dword_75BF38
    int  (*readHoverObject)();    // dword_62D22C
    int  (*readMouseDownLeft)();  // dword_67221C (left button held / placement commit)
    int  (*readSelectOffice)();   // dword_672238 (office sub-select / drill-in)
    int  (*readKeyCode)();        // byte_67225C (N / J keyboard nav)
};

// Install a hooks struct (nullptr restores the inert defaults). Returns previous.
const GuiDialogs6Hooks* SetGuiDialogs6Hooks(const GuiDialogs6Hooks* hooks);
const GuiDialogs6Hooks* GuiDialogs6Hooks_Default();
const GuiDialogs6Hooks& GuiDialogs6HooksActive();

// Reset module-owned tables + restore default hooks (deterministic test start).
void ResetGuiDialogs6();

// ===========================================================================
// Recovered functions.
// ===========================================================================

// gilde.exe 0x54e940 — VIBE_Panel_RunGelage (a1@eax = building record).
int Panel_RunGelage(int buildingRec);

// gilde.exe 0x54f668 — VIBE_Panel_RunInventory. Calls the REAL sibling
// gui::Panel_RunUseObject for the clicked slot.
void Panel_RunInventory(unsigned short city);

// gilde.exe 0x552d34 — VIBE_Panel_RunBuildingRoundEnd (a1@eax = &buildingIdx).
int Panel_RunBuildingRoundEnd(char* a1, unsigned short city);

// gilde.exe 0x566dd0 — VIBE_Panel_ShowUniversity (a1@ecx, a2@edx = person rec).
int Panel_ShowUniversity(int a1, unsigned short* a2);

// gilde.exe 0x54fe74 — VIBE_Panel_RunTraining (a1@eax = building record).
int Panel_RunTraining(char* a1, int a2, char a3, unsigned short city);

// gilde.exe 0x54dfdc — VIBE_Panel_RunPlantBar (a1@eax = building record).
short* Panel_RunPlantBar(int a1);

// gilde.exe 0x5546e4 — VIBE_Panel_RunOfficeSession.
// a1=office category byte, a2=layout arg, a3/a4=caption ids, a6=drill-in callback.
unsigned short* Panel_RunOfficeSession(char a1, int a2, unsigned a3, unsigned a4,
                                       int (*drillIn)());

// gilde.exe 0x550190 — VIBE_Panel_RunThievesGuildTrain.
int Panel_RunThievesGuildTrain(int a1, int a2, char* a3, int a4);

// gilde.exe 0x5130bc — VIBE_Dialog_RobberRaidConfirm (this@ecx = selected target).
void Dialog_RobberRaidConfirm(int target);

// gilde.exe 0x512e08 — VIBE_Dialog_BriberyConfirm (this@ecx = selected target).
void Dialog_BriberyConfirm(int target);

} // namespace guild::gui
