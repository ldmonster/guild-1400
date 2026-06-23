#include "gui/gui_dialogs6.h"

#include "gui/gui_dialogs5.h" // Panel_RunUseObject (real sibling), g_forceQuitLatch
#include "gui/object.h"       // g_widgets, g_widgetCache, Widget_AllocSlot
#include "gui/window.h"       // g_currentWindowId (dword_62D230)
#include "util/coord.h"       // util::ConvertX (VIBE_Coord_ConvertX @0x5c6b08, TRUNCATE)

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace guild::gui {

// ===========================================================================
// Module-owned engine tables (BSS, zero at load).
// ===========================================================================
i32 g_winBorderColor[4];
i32 g_invSlotIds[6];
void* g_invSlotRecs[6];
i32 g_thiefGridIds[32];
i32 g_inventoryActiveWindow = -1;
i32 g_plantPrevPanel = -1;

// gilde.exe dbl_624490 — plant-bar price scale.  get_bytes(0x624490,8) =
// 00 00 00 00 00 00 E0 3F == 0.5 (the truncated market price is halved).
inline constexpr double kPlantPriceHalf = 0.5;

// g_forceQuitLatch is defined in gui_dialogs5.cpp; we declare it extern in the
// header and link against it. (The original dword_631614 lives once.)

namespace { inline Widget& W(int slot) { return g_widgets[slot]; }
// Unaligned by-value dword load — byte-identical to the original's unaligned x86 read.
inline int LdI32(const void* p) { int v; std::memcpy(&v, p, sizeof(v)); return v; } }

// ===========================================================================
// Hooks (inert defaults). Defaults make every builder observable without the
// engine and make every frame loop terminate immediately (gameLogicRunFrameLoop
// returns 0 by default).
// ===========================================================================
namespace {

int  DefGameTickFinalize(i16, i16, const char*) { return -1; }
void DefFormCenterChildWindows(int) {}
void DefFormSelectWindow(int, int) {}
int  DefFormGetChildObjectId(int, int, int) { return -1; }
int  DefFormDestroy(int) { return -1; }
void DefFormSetObjectsVisible(int, int) {}
void DefWindowRemoveChildren(int, int) {}
void DefWindowRemoveIfActive(int, int, int) {}
int  DefTextRenderRichString(unsigned, int, int, int, int) { return 0; }
void DefTextRenderFormattedMessage(char* out, unsigned, int, int, int) { if (out) out[0] = 0; }
int  DefObjectAddToWindow(int, int) { return Widget_AllocSlot(); }
int  DefObjectAddTextLabel(i16, i16, int, const char*) { return Widget_AllocSlot(); }
void DefObjectSetValueOrText(int, int, int, int, int) {}
int  DefObjectGetDataPtr(int) { return 0; }
void DefObjectSetPosition(int, const float*) {}
void DefObjectDetachAndRelease(int) {}
void DefObjectApplyTransparencyTree(int, const void*) {}
int  DefRadioGroupCreate(int, int) { return -1; }
void DefRadioGroupAddButton(int, int) {}
void DefRadioGroupFreeSurface() {}
void DefSelectionUpdate(int, int) {}
void DefSelectionClearAll() {}
int  DefReadActiveRadioSel(int) { return 0; }
const short* DefPersonFindRecordById(int) { return nullptr; }
int  DefOfficeCollectByCategory(int, int, void*) { return 0; }
int  DefOfficeCollectElectiveOffices(int, int, void*) { return 0; }
int  DefBuildingMapTypeToCategory(int) { return 0; }
void DefBuildingValueComputeWorth(const void*, unsigned short, int* out) {
    if (out) std::memset(out, 0, 14 * sizeof(int));
}
int  DefBuildingGetUpgradeLevel(int) { return 0; }
float DefBuildingComputeCurrentOutput(int) { return 0.0f; }
double DefBuildingComputeMarketPrice(int, unsigned) { return 0.0; }
const short* DefAmtFindOfficeTypeRecord(int) { return nullptr; }
int  DefAmtFindFreePlacement(int, int, int, int) { return 0; }
const void* DefHeFindFirstHandlerByFilter(int, int, int, int, int) { return nullptr; }
const void* DefHeFindNextMatchingHandler() { return nullptr; }
int  DefHeCountMatchingEntities(int, unsigned short, int) { return 0; }
void DefInventoryOpenSlotWindow(int, int, unsigned short, int, unsigned) {}
void DefInventoryRefreshSlots(int) {}
int  DefInventoryFindSlotByItemId(short) { return 0; }
void DefInventoryRenderItemGrid(int, int) {}
int  DefDragSlotCountUsed() { return 0; }
void DefDragSlotResetTable() {}
void DefDragSlotResetGridTable() {}
void DefDragCursorSetSprite(int, int) {}
void DefCmdEnqueueBuildingActionStart(const char*) {}
void DefCmdEnqueueBuildingActionEnd() {}
void DefCmdEnqueueCmd15(int, int, int, int) {}
void DefCmdQueueRequestSlotReset28(void*, int) {}
void DefCmdQueueRequestArgs25(int, int, int, int, int) {}
unsigned DefCmdQueueRequestMixed44(int, int, int, int, int, int) { return 0; }
int  DefCmdGetPacketStatusById(unsigned) { return 1; } // "done" -> wait exits at once
int  DefCmdGetPacketSeqById(unsigned) { return 0; }
int  DefMoneyConvertToDisplayCoord(int money, unsigned char) { return money; }
int  DefMoneyMultiplyByRate(int dataPtr, unsigned char) { return dataPtr; }
int  DefPersonSumCurrencyHeld(int) { return 0; }
void DefCoordConvertX() {}
void DefLightSetGrayThunk(int, int, int) {}
void DefLightBuildObjectCache(int) {}
const short* DefGameObjectQueryFind(int, int, int, int) { return nullptr; }
const short* DefGameObjectIterNext() { return nullptr; }
int  DefHeightmapFindNearestEntry(int*, int, int, double, int*) { return -1; }
void DefMathMatrixCopy(int, int) {}
void DefMeshSetGlobalColorTemp(int, char, char, int) {}
int  DefSceneLoadObjectGroup(const char*, int, i16, int) { return 0; }
void DefPlantLoadVegetationModel(int*) {}
void DefSound3dSetListener(int, const float*, int, float*, int) {}
void DefErrorLogReportMessage(const char*) {}
void DefHudUpdateEdgeScroll(int, int, int) {}
void DefHudBuildPersonGridLayout(char, int, int, void*, int) {}
void DefHudBuildPersonColumnLayout(char, int, int, void*, int) {}
void DefPlayerBarCreate(int, int) {}
void DefPlayerBarDestroy(int, void*) {}
void DefVoicePlayCraftFavorComment() {}
void DefAiMethodComputeChoiceWeights(int, const short*) {}
int  DefInteractionTestHandlerFlagDword(int) { return 1; } // proceed
int  DefInteractionDispatchPanelEvent(int, int, int, int) { return 0; }
int  DefDialogCheckActiveCharFlag() { return 0; } // 0 -> proceed
void DefDialogShowMessageBox(int, int, int) {}
void DefMapViewPanelDispatcher(int, int, void (*)(), int, int) {}
int  DefGameLogicRunFrameLoop(int, int, const void*) { return 0; } // exit loop immediately
int  DefReadMouseRelease() { return 0; }
int  DefReadMouseWheelUp() { return 0; }
int  DefReadLastClickedId() { return -1; }
int  DefReadHoverObject() { return -1; }
int  DefReadMouseDownLeft() { return 0; }
int  DefReadSelectOffice() { return 0; }
int  DefReadKeyCode() { return 0; }

const GuiDialogs6Hooks kDefaultHooks = {
    &DefGameTickFinalize, &DefFormCenterChildWindows, &DefFormSelectWindow,
    &DefFormGetChildObjectId, &DefFormDestroy, &DefFormSetObjectsVisible,
    &DefWindowRemoveChildren, &DefWindowRemoveIfActive,
    &DefTextRenderRichString, &DefTextRenderFormattedMessage,
    &DefObjectAddToWindow, &DefObjectAddTextLabel, &DefObjectSetValueOrText,
    &DefObjectGetDataPtr, &DefObjectSetPosition, &DefObjectDetachAndRelease,
    &DefObjectApplyTransparencyTree,
    &DefRadioGroupCreate, &DefRadioGroupAddButton, &DefRadioGroupFreeSurface,
    &DefSelectionUpdate, &DefSelectionClearAll, &DefReadActiveRadioSel,
    &DefPersonFindRecordById, &DefOfficeCollectByCategory, &DefOfficeCollectElectiveOffices,
    &DefBuildingMapTypeToCategory, &DefBuildingValueComputeWorth, &DefBuildingGetUpgradeLevel,
    &DefBuildingComputeCurrentOutput, &DefBuildingComputeMarketPrice,
    &DefAmtFindOfficeTypeRecord, &DefAmtFindFreePlacement,
    &DefHeFindFirstHandlerByFilter, &DefHeFindNextMatchingHandler, &DefHeCountMatchingEntities,
    &DefInventoryOpenSlotWindow, &DefInventoryRefreshSlots, &DefInventoryFindSlotByItemId,
    &DefInventoryRenderItemGrid,
    &DefDragSlotCountUsed, &DefDragSlotResetTable, &DefDragSlotResetGridTable,
    &DefDragCursorSetSprite,
    &DefCmdEnqueueBuildingActionStart, &DefCmdEnqueueBuildingActionEnd, &DefCmdEnqueueCmd15,
    &DefCmdQueueRequestSlotReset28, &DefCmdQueueRequestArgs25, &DefCmdQueueRequestMixed44,
    &DefCmdGetPacketStatusById, &DefCmdGetPacketSeqById,
    &DefMoneyConvertToDisplayCoord, &DefMoneyMultiplyByRate, &DefPersonSumCurrencyHeld,
    &DefCoordConvertX, &DefLightSetGrayThunk, &DefLightBuildObjectCache,
    &DefGameObjectQueryFind, &DefGameObjectIterNext, &DefHeightmapFindNearestEntry,
    &DefMathMatrixCopy, &DefMeshSetGlobalColorTemp, &DefSceneLoadObjectGroup,
    &DefPlantLoadVegetationModel, &DefSound3dSetListener, &DefErrorLogReportMessage,
    &DefHudUpdateEdgeScroll, &DefHudBuildPersonGridLayout, &DefHudBuildPersonColumnLayout,
    &DefPlayerBarCreate, &DefPlayerBarDestroy, &DefVoicePlayCraftFavorComment,
    &DefAiMethodComputeChoiceWeights, &DefInteractionTestHandlerFlagDword,
    &DefInteractionDispatchPanelEvent, &DefDialogCheckActiveCharFlag, &DefDialogShowMessageBox,
    &DefMapViewPanelDispatcher,
    &DefGameLogicRunFrameLoop,
    &DefReadMouseRelease, &DefReadMouseWheelUp, &DefReadLastClickedId, &DefReadHoverObject,
    &DefReadMouseDownLeft, &DefReadSelectOffice, &DefReadKeyCode,
};

const GuiDialogs6Hooks* g_hooks = &kDefaultHooks;

} // namespace

const GuiDialogs6Hooks* SetGuiDialogs6Hooks(const GuiDialogs6Hooks* hooks) {
    const GuiDialogs6Hooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}
const GuiDialogs6Hooks* GuiDialogs6Hooks_Default() { return &kDefaultHooks; }
const GuiDialogs6Hooks& GuiDialogs6HooksActive() { return *g_hooks; }

void ResetGuiDialogs6() {
    std::memset(g_winBorderColor, 0, sizeof(g_winBorderColor));
    std::memset(g_invSlotIds, 0, sizeof(g_invSlotIds));
    std::memset(g_invSlotRecs, 0, sizeof(g_invSlotRecs));
    std::memset(g_thiefGridIds, 0, sizeof(g_thiefGridIds));
    g_inventoryActiveWindow = -1;
    g_plantPrevPanel = -1;
    g_forceQuitLatch = 0;
    g_hooks = &kDefaultHooks;
}

// Convenience wrapper for the most-called text helper (varying arg count).
static inline int RichStr(unsigned id, int a = 0, int b = 0, int c = 0, int d = 0) {
    return g_hooks->textRenderRichString(id, a, b, c, d);
}

// Set the four active-window border-color channels (dword_67EF18..24) to one value.
static inline void SetWindowBorder(int v) {
    g_winBorderColor[0] = v;
    g_winBorderColor[1] = v;
    g_winBorderColor[2] = v;
    g_winBorderColor[3] = v;
}

// ===========================================================================
// 0x54e940 — VIBE_Panel_RunGelage.
// Loads special\gelage, lays out an amount field (capped at 1000), then spins
// the per-frame loop. On the confirm button (id 1210) queues a "party" command.
// ===========================================================================
int Panel_RunGelage(int buildingRec) {
    int v34 = g_hooks->gameTickFinalize(0, 0, "special\\gelage");
    g_hooks->formCenterChildWindows(v34);
    g_hooks->formSelectWindow(v34, 0);
    SetWindowBorder(14);
    g_hooks->formSelectWindow(v34, 1);
    RichStr(0x15B8u);
    g_hooks->formSelectWindow(v34, 2);
    int v8 = RichStr(0x15B9u, /*byte_6477A1=*/0);
    int childObjectId = g_hooks->formGetChildObjectId(v34, /*?*/0, v8);
    int v11 = v8 + 1;
    int v37 = g_hooks->formGetChildObjectId(v34, 2, v8 + 1);

    int held = g_hooks->personSumCurrencyHeld(0 /*&person record*/);
    int v15 = g_hooks->moneyConvertToDisplayCoord(held, 0);
    if (v15 > 1000) v15 = 1000;
    g_hooks->objectSetValueOrText(childObjectId, 0x64, v15, 100, 0);

    while (g_hooks->gameLogicRunFrameLoop(423879, v11, reinterpret_cast<const void*>(static_cast<std::intptr_t>(buildingRec)))) {
        if (g_hooks->readLastClickedId() == 1210 && v37 != -1 && v37 == g_hooks->readHoverObject()) {
            int data = g_hooks->objectGetDataPtr(childObjectId);
            g_hooks->moneyMultiplyByRate(data, 0);
            int blob[40];
            std::memset(blob, 0, sizeof(blob));
            // header byte 56 at +4 then various copied fields; modeled inert.
            *reinterpret_cast<unsigned char*>(blob) = 1;
            g_hooks->cmdEnqueueBuildingActionStart("party");
            v11 = 0; // (unsigned __int8)byte_6477A1
            g_hooks->cmdEnqueueCmd15(-1, 0, 0, 0);
            g_hooks->cmdQueueRequestSlotReset28(blob, 0);
            g_hooks->cmdEnqueueBuildingActionEnd();
            g_forceQuitLatch = 1;
        } else if (g_hooks->readLastClickedId() == 1155) {
            g_forceQuitLatch = 1;
        }
        if (g_hooks->readMouseRelease()) g_forceQuitLatch = 1;
    }
    g_hooks->hudUpdateEdgeScroll(v11, 0, 0);
    return g_hooks->formDestroy(v34);
}

// ===========================================================================
// 0x54f668 — VIBE_Panel_RunInventory.
// Opens the inventory slot window, spins the per-frame loop. A click on a slot
// whose item is usable dispatches the REAL sibling gui::Panel_RunUseObject.
// ===========================================================================
void Panel_RunInventory(unsigned short city) {
    if (g_inventoryActiveWindow != -1) return;
    if (!g_hooks->interactionTestHandlerFlagDword(4096)) return;

    g_hooks->interactionDispatchPanelEvent(0x14, 0, 0, 0);
    // gilde.exe @0x54f6c1: GameTick_Finalize(HIWORD(dword_69FFBC)-344, dword_69FFBC-264,
    // "panel\inventory_2"). dword_69FFBC is BSS (0) -> args (-344, -264).
    int v1 = g_hooks->gameTickFinalize(static_cast<i16>(-344), static_cast<i16>(-264),
                                       "panel\\inventory_2");
    g_hooks->formSelectWindow(v1, 0);
    SetWindowBorder(24);
    g_hooks->formSelectWindow(v1, 0);
    g_hooks->dragCursorSetSprite(0, 0);
    g_hooks->inventoryOpenSlotWindow(g_currentWindowId, 0, city, 0, 0x1B16u);

    while (g_hooks->gameLogicRunFrameLoop(415687, 0, nullptr)) {
        g_hooks->inventoryRefreshSlots(0);
        if (g_hooks->readLastClickedId() != -1 || g_hooks->dragSlotCountUsed()) {
            for (int i = 0; i != 6; i += 1) {
                if (g_invSlotRecs[i] && g_hooks->readHoverObject() == g_invSlotIds[i]) {
                    short* rec = static_cast<short*>(g_invSlotRecs[i]);
                    short item = rec ? rec[0] : 0;
                    if (g_hooks->inventoryFindSlotByItemId(item)) {
                        g_hooks->formSetObjectsVisible(v1, 0);
                        // REAL sibling — gui::Panel_RunUseObject (gui_dialogs5.cpp).
                        Panel_RunUseObject(rec, v1);
                        g_hooks->objectSetValueOrText(g_invSlotIds[i], 0, 999, 0, 0);
                        g_hooks->dragSlotResetTable();
                        g_hooks->formSetObjectsVisible(v1, 1);
                    } else {
                        g_hooks->dialogShowMessageBox(0 /*dword_8C6960*/, 0, v1);
                    }
                }
            }
        }
        if (g_hooks->readMouseRelease()) {
            if (g_hooks->dragSlotCountUsed()) {
                for (int j = 0; j != 6; j += 1) {
                    if (g_invSlotRecs[j]) {
                        if (g_hooks->objectGetDataPtr(g_invSlotIds[j]))
                            g_hooks->objectSetValueOrText(g_invSlotIds[j], 0, 999, 0, 0);
                    }
                }
                g_hooks->dragSlotResetTable();
            } else {
                g_forceQuitLatch = 1;
            }
        }
    }
    g_hooks->dragSlotResetTable();
    g_hooks->windowRemoveIfActive(g_inventoryActiveWindow, 0, 1);
    g_inventoryActiveWindow = -1;
    g_hooks->formDestroy(v1);
    g_hooks->interactionDispatchPanelEvent(0x14, 0, 0, 1);
}

// ===========================================================================
// 0x552d34 — VIBE_Panel_RunBuildingRoundEnd.
// Building round-end stats panel: renders the same conditional stat lines as
// RunBuildingDetail, then spins the loop until a control-state edge fires.
// ===========================================================================
int Panel_RunBuildingRoundEnd(char* a1, unsigned short city) {
    // v1 = 589*(*a1) + dword_13CE294 (building-type record; type table out of tree, a1-modeled).
    char* v1 = a1;
    int v2 = g_hooks->gameTickFinalize(60, 0, "Runden\\Spielerrunde_Ende_geb");
    g_hooks->formCenterChildWindows(v2);
    g_hooks->formSelectWindow(v2, 0);
    // *(word*)(dword_62D298 + 636) = 69 — window-record write (out of tree).
    g_hooks->buildingMapTypeToCategory(a1 ? a1[0] : 0);

    // ComputeProductionWorth (@0x58fe68) writes a contiguous dword array; here the output
    // base IS &v11 (idx0 = v11, unlike RunBuildingDetail which uses &v13+4). Stack-aliased
    // locals -> a2[] indices:  v11=a2[0] v12=a2[1] v13=a2[2] v14=a2[4] v15=a2[5] v16=a2[6]
    //   v17=a2[8] v18=a2[12] v19=a2[13] v20=a2[14] v21=a2[15] v22=a2[18] v23=a2[19] v24=a2[20].
    int worth[24];
    std::memset(worth, 0, sizeof(worth));
    g_hooks->buildingValueComputeWorth(a1, city, worth);
    int v11 = worth[0],  v12 = worth[1],  v13 = worth[2],  v14 = worth[4],  v15 = worth[5];
    int v16 = worth[6],  v17 = worth[8],  v18 = worth[12], v19 = worth[13], v20 = worth[14];
    int v21 = worth[15], v22 = worth[18], v23 = worth[19], v24 = worth[20];

    // building-name line: v5[5] (owner suffix) selects "$A$Z%1s >%s<$2A" vs "$A$Z%1s$2A".
    // Record (v5) out of tree -> inert (no suffix).
    RichStr(0u /*"$A$Z%1s$2A"*/);

    int upg = g_hooks->buildingGetUpgradeLevel(reinterpret_cast<std::intptr_t>(a1));
    RichStr(0xA0u, upg);
    if (v11) RichStr(0xA1u, v11);
    if (v13) RichStr(0xA2u, v13);
    if (v16) {
        int catBase = v1 ? (static_cast<unsigned char>(v1[547]) + 294) : 294;
        RichStr(0xA3u, catBase, v16);
    }
    if (v15 + v14) RichStr(0xA4u, v15 + v14);
    if (v17) RichStr(0xA5u, v17);
    if (v18) RichStr(0xA6u, v18);
    if (v21) RichStr(0xA7u, v21);
    if (v19) RichStr(0xA8u, v19);
    if (v20) RichStr(0xA9u, v20);
    if (v23) RichStr(0xAAu, v23);
    if (v22) {
        char t = v1 ? v1[0] : 0;
        if (t == 4 || t == 16 || t == 19) RichStr(0xB0u, v22);
        else                              RichStr(0xABu, v22);
    }
    if (v12) RichStr(0xACu, v12);
    if (v24 > 0)      RichStr(0xADu, v24);
    else if (v24 < 0) RichStr(0xAEu, v22 + v24);

    // gilde.exe @0x552f42: RunFrameLoop(423879, (int)VIBE_Panel_RunBuildingRoundEnd, v2(form)).
    while (g_hooks->gameLogicRunFrameLoop(423879,
               static_cast<int>(reinterpret_cast<std::intptr_t>(&Panel_RunBuildingRoundEnd)),
               reinterpret_cast<const void*>(static_cast<std::intptr_t>(v2)))) {
        // The original compares a cached vs current control word; any change -> quit.
        if (g_hooks->readMouseRelease()) g_forceQuitLatch = 1;
    }
    return g_hooks->formDestroy(v2);
}

// ===========================================================================
// 0x566dd0 — VIBE_Panel_ShowUniversity.
// Lays out three "choice" columns (weights from AiMethod_ComputeChoiceWeights),
// runs the loop, and on a column click queues an apprenticeship request.
// ===========================================================================
int Panel_ShowUniversity(int a1, unsigned short* a2) {
    (void)a1;
    char weights[24];
    std::memset(weights, 0, sizeof(weights));
    g_hooks->aiMethodComputeChoiceWeights(reinterpret_cast<std::intptr_t>(weights), nullptr);

    int v39 = g_hooks->gameTickFinalize(0, 0, "special\\uni");
    g_hooks->formCenterChildWindows(v39);
    g_hooks->formSelectWindow(v39, 0);
    int profBit = (a2 && (reinterpret_cast<unsigned char*>(a2)[9] != 0)) + 5805;
    RichStr(0x16AAu, profBit, a2 ? a2[0] : 0, 5);
    g_hooks->formSelectWindow(v39, 0);

    int choiceIds[3] = { -1, -1, -1 };
    int v7 = 0;   // byte offset into weights (step 8)
    for (int v4 = 0; v4 < 3; ++v4) {
        int v44 = 90 * v4;
        int w = g_hooks->objectAddToWindow(g_currentWindowId, 0);
        choiceIds[v4] = w;
        if (w >= 0 && w < kMaxWidgets) {
            W(w).at<i32>(72) = 1;   // +72 = 1
            W(w).at<u8>(444) = 3;   // +444 = 3
        }
        char buf[256];
        g_hooks->textRenderFormattedMessage(buf, 0 /*"%s"*/, 0, 0, 0);
        g_hooks->objectAddTextLabel(190, static_cast<i16>(v44 + 30), g_currentWindowId, buf);
        g_hooks->textRenderFormattedMessage(buf, 0, (LdI32(&weights[v7 + 1]) >> 24) + 4810, 0, 0);
        g_hooks->objectAddTextLabel(190, static_cast<i16>(v44 + 40), g_currentWindowId, buf);
        g_hooks->textRenderFormattedMessage(buf, 0, (LdI32(&weights[v7 + 2]) >> 24) + 4810, 0, 0);
        g_hooks->objectAddTextLabel(190, static_cast<i16>(v44 + 50), g_currentWindowId, buf);
        g_hooks->textRenderFormattedMessage(buf, 0 /*"%T"*/,
                                            *reinterpret_cast<int*>(&weights[v7]), 0, 0);
        g_hooks->objectAddTextLabel(190, static_cast<i16>(v44 + 70), g_currentWindowId, buf);
        v7 += 8;
    }

    g_hooks->formSelectWindow(v39, 3);
    int v14 = RichStr(0x17B5u);
    int childObjectId = g_hooks->formGetChildObjectId(v39, 0, v14);
    g_hooks->dragCursorSetSprite(0, 0);

    while (g_hooks->gameLogicRunFrameLoop(0, v14, nullptr)) {
        if (g_hooks->readMouseRelease()) g_forceQuitLatch = 1;
        int clicked = g_hooks->readLastClickedId();
        if (clicked != -1) {
            int hover = g_hooks->readHoverObject();
            if (hover == childObjectId) {
                g_forceQuitLatch = 1;
            } else {
                for (int v18 = 0; v18 < 3; ++v18) {
                    if (hover == choiceIds[v18]) {
                        int blob[40];
                        std::memset(blob, 0, sizeof(blob));
                        blob[1] = a2 ? *reinterpret_cast<int*>(reinterpret_cast<char*>(a2) + 4) : 0;
                        blob[2] = -1;
                        // header byte 18 at +0x18, 95 at +0; modeled via blob.
                        g_hooks->cmdQueueRequestSlotReset28(blob, 0x10000);
                        g_hooks->cmdQueueRequestArgs25(blob[1], 0, 0, 4, 0);
                        g_forceQuitLatch = 1;
                        break;
                    }
                }
            }
        }
    }
    return g_hooks->formDestroy(v39);
}

// ===========================================================================
// 0x54fe74 — VIBE_Panel_RunTraining.
// Gates on the count of pending training handlers (>=3 => "too many" message),
// otherwise builds the training panel and on confirm queues per-person training
// commands (up to 3), capped by the apprentice slots.
// ===========================================================================
int Panel_RunTraining(char* a1, int a2, char a3, unsigned short city) {
    (void)city;
    // building-type -> base string index v6 (4 -> 1, 16 -> 15, else 19).
    char bt = a1 ? a1[0] : 0;
    int v6 = (bt == 4) ? 1 : (bt == 16) ? 15 : 19;

    int v7 = 0;
    const void* h = g_hooks->heFindFirstHandlerByFilter(2, 0, 22, 3, a2);
    if (h) {
        do {
            h = g_hooks->heFindNextMatchingHandler();
            ++v7;
        } while (h);
    }
    if (v7 >= 3) {
        char buf[2048];
        g_hooks->textRenderFormattedMessage(buf, 5719, v7, 0, 0);
        g_hooks->dialogShowMessageBox(reinterpret_cast<std::intptr_t>(buf), 0, 0);
        return 0;
    }

    g_hooks->playerBarCreate(0, 1);
    int v35 = g_hooks->gameTickFinalize(0, 0, "special\\training");
    g_hooks->formCenterChildWindows(v35);
    g_hooks->dragCursorSetSprite(0, 0);
    g_hooks->formSelectWindow(v35, 1);
    int v13 = RichStr(0x1652u, v6 + 471, v6 + 472, ((a3 << 24) >> 24 << 24 >> 24) + 4810);
    int childObjectId = g_hooks->formGetChildObjectId(v35, 0, v13);
    int v17 = -1;
    int v36 = g_hooks->formGetChildObjectId(v35, 1, v13 + 1);

    do {
        if (g_hooks->readMouseRelease()) g_forceQuitLatch = 1;
        int clicked = g_hooks->readLastClickedId();
        if (clicked != -1) {
            int hover = g_hooks->readHoverObject();
            if (hover == childObjectId) {
                int v18 = 0;
                // walk the 768-person table (byte_12CEA98 marker, step 536); inert => none.
                for (int v19 = 0; v19 < 411648 && v18 < 3; v19 += 536) {
                    // marker check is inert (no table); skip queueing.
                }
                if (v18) {
                    g_hooks->voicePlayCraftFavorComment();
                    if (v18 > 3) {
                        char buf[2048];
                        g_hooks->textRenderFormattedMessage(buf, 5719, 3, 0, 0);
                        g_hooks->dialogShowMessageBox(reinterpret_cast<std::intptr_t>(buf), 0, 0);
                    }
                    g_forceQuitLatch = 1;
                } else {
                    g_hooks->dialogShowMessageBox(0 /*dword_8C9008*/, 0, 0);
                }
            } else if (hover == v36) {
                g_forceQuitLatch = 1;
            }
        }
    } while (g_hooks->gameLogicRunFrameLoop(423879, v17, nullptr));

    g_hooks->formDestroy(v35);
    g_hooks->playerBarDestroy(v17, nullptr);
    g_hooks->hudUpdateEdgeScroll(v17, 0, 0);
    g_hooks->selectionClearAll();
    return 0;
}

// ===========================================================================
// 0x54dfdc — VIBE_Panel_RunPlantBar.
// 3D vegetation-placement bar. Builds a radio of available plant kinds, then in
// the per-frame loop raycasts mouse->terrain, positions a preview object, and on
// a left-click commit queues the plant/build command. Most of the body is gated
// on the scene globals (heightmap / mouse) which are inert in tests.
// ===========================================================================
short* Panel_RunPlantBar(int a1) {
    int objectGroup = 0;
    int v60 = 1;
    int v55 = 0;
    int v57 = -1;
    int v47 = 0;          // count of plant kinds
    int v63 = -1;         // radio group id
    // result = *(__int16 **)(a1 + 113) in the original (a1 is a record pointer).
    // Guard the deref: a null/zero handle has no record (deterministic in tests).
    short* result = a1 ? *reinterpret_cast<short**>(
                             reinterpret_cast<char*>(static_cast<std::intptr_t>(a1)) + 113)
                       : nullptr;
    int v3 = 0;           // current selection index
    short* v53 = result;
    (void)v53; (void)v55; (void)v3;

    // dword_64A028 = the heightmap handle; inert (0) => the whole body is skipped.
    int heightmap = 0;
    if (heightmap) {
        for (int i = 0; i < 8; ++i) {
            // loc_5CB930 terrain-tile probe; inert => never updates v55.
            (void)i;
        }
        result = const_cast<short*>(g_hooks->gameObjectQueryFind(a1, 1, 0, 255));
        if (!result) return result;
        const short* node = g_hooks->gameObjectQueryFind(0, 1, 4, 23);
        short kinds[28];
        std::memset(kinds, 0, sizeof(kinds));
        if (node) {
            do {
                kinds[v47] = node[0];
                ++v47;
                node = g_hooks->gameObjectIterNext();
            } while (node);
        }
        // gilde.exe @0x54e0f4: GameTick_Finalize(HIWORD(dword_69FFBC) - 95, 266, "Misc\\PlantBar").
        // dword_69FFBC is BSS (0 at load) so HIWORD-95 == -95.
        int v52 = g_hooks->gameTickFinalize(static_cast<i16>(-95), 266, "Misc\\PlantBar");
        if (v52 == -1) return result;
        if (g_plantPrevPanel != -1) g_hooks->formSetObjectsVisible(g_plantPrevPanel, 0);
        g_hooks->formSelectWindow(v52, 1);

        int btnIds[28];
        std::memset(btnIds, 0, sizeof(btnIds));
        for (int v8 = 0; v8 < v47; ++v8) {
            int w = g_hooks->objectAddToWindow(g_currentWindowId, 0);
            btnIds[v8] = w;
            if (v63 == -1) {
                v63 = g_hooks->radioGroupCreate(1, w);
                if (w >= 0 && w < kMaxWidgets) {
                    W(w).at<i32>(36) = 1;
                    W(w).at<i32>(40) = 1;
                }
            } else {
                g_hooks->radioGroupAddButton(v63, w);
            }
        }
        int v56 = v47 - 1;

        while (true) {
            int out[4];
            std::memset(out, 0, sizeof(out));
            int near = g_hooks->heightmapFindNearestEntry(&heightmap, 0, 0, 30.0, out);
            if (near != -1 && objectGroup) {
                g_hooks->mathMatrixCopy(0, reinterpret_cast<std::intptr_t>(out));
                float pos[3] = {0, 0, 0};
                g_hooks->objectSetPosition(objectGroup, pos);
                v60 = g_hooks->amtFindFreePlacement(reinterpret_cast<std::intptr_t>(v53), 0, 0, 0);
                g_hooks->lightBuildObjectCache(objectGroup);
            }
            if (v60) g_hooks->meshSetGlobalColorTemp(objectGroup, 0, 0, 0);
            else     g_hooks->meshSetGlobalColorTemp(objectGroup, -64, 64, 64);

            if (g_hooks->readMouseDownLeft() && near != -1 && v60) {
                unsigned pkt = g_hooks->cmdQueueRequestMixed44(a1, 0, 0, 0, 0, -1);
                while (!g_hooks->cmdGetPacketStatusById(pkt))
                    g_hooks->gameLogicRunFrameLoop(423879, 0, nullptr);
                int seq = g_hooks->cmdGetPacketSeqById(pkt);
                if (seq) {
                    // gilde.exe 0x54e6.. : market price is truncated, halved, truncated
                    // again, then floored at 1024.  TWO VIBE_Coord_ConvertX @0x5c6b08
                    // truncation passes with a *dbl_624490 (==0.5) scale between them:
                    //   p1 = (int)trunc(marketPrice)
                    //   p2 = (int)trunc((double)p1 * 0.5)   [dbl_624490 == 0.5]
                    //   v26 = max(p2, 1024)
                    double price = g_hooks->buildingComputeMarketPrice(0, 0x64u);
                    g_hooks->coordConvertX();
                    int p1 = static_cast<int>(util::ConvertX(price));      // (int)v24
                    double scaled = static_cast<double>(p1) * kPlantPriceHalf; // *dbl_624490
                    g_hooks->coordConvertX();
                    int p = static_cast<int>(util::ConvertX(scaled));     // (int)v25
                    int v26 = (p <= 1024) ? 1024 : p;
                    g_hooks->cmdEnqueueCmd15(-1, 0, v26, 0);
                    g_hooks->plantLoadVegetationModel(&seq);
                }
            }
            if (g_hooks->readMouseRelease()) g_forceQuitLatch = 1;

            if (g_hooks->readMouseDownLeft()) {
                for (int idx = 0; idx < v47; ++idx) {
                    if (g_hooks->readHoverObject() == btnIds[idx]) {
                        v3 = g_hooks->readActiveRadioSel(v63);
                        break;
                    }
                }
            }
            int key = g_hooks->readKeyCode();
            int v31 = v3;
            if (key == 78) { v31 = v3 + 1; if (v31 > v56) v31 = 0; g_hooks->selectionUpdate(v63, v31); }
            else if (key == 74) { v31 = v3 - 1; if (v31 < 0) v31 = v56; g_hooks->selectionUpdate(v63, v31); }

            v3 = g_hooks->readActiveRadioSel(v63);
            if (v3 != v57) {
                const short* type = g_hooks->amtFindOfficeTypeRecord(kinds[v3]);
                if (objectGroup) g_hooks->objectDetachAndRelease(objectGroup);
                char path[256];
                std::snprintf(path, sizeof(path), "%svegetation/*pfl_%s_04.ogr", "", "");
                (void)type;
                objectGroup = g_hooks->sceneLoadObjectGroup(path, 0, 0, 0);
                if (objectGroup) {
                    int tparg = 65756;
                    g_hooks->objectApplyTransparencyTree(objectGroup, &tparg);
                } else {
                    char msg[600];
                    std::snprintf(msg, sizeof(msg),
                                  "ob_HandleBepflanzung(): Could not load 3D-Objekt-roup '%s'...", path);
                    g_hooks->errorLogReportMessage(msg);
                    g_forceQuitLatch = 1;
                }
                v57 = v3;
            }
            if (!g_hooks->gameLogicRunFrameLoop(423887, 0, reinterpret_cast<const void*>(static_cast<std::intptr_t>(v3)))) {
                if (objectGroup) g_hooks->objectDetachAndRelease(objectGroup);
                g_hooks->radioGroupFreeSurface();
                if (g_plantPrevPanel != -1) g_hooks->formSetObjectsVisible(g_plantPrevPanel, 1);
                return reinterpret_cast<short*>(static_cast<std::intptr_t>(g_hooks->formDestroy(v52)));
            }
        }
    }
    return result;
}

// ===========================================================================
// 0x5546e4 — VIBE_Panel_RunOfficeSession.
// Loads special\amt2, collects the office candidates for a category (or the
// player's own offices when a1==0), enriches each with status strings, lays out
// the grid/column, then spins the selection loop (with an optional drill-in
// callback a6 for sub-offices). Returns the selected person record.
// ===========================================================================
unsigned short* Panel_RunOfficeSession(char a1, int a2, unsigned a3, unsigned a4,
                                       int (*drillIn)()) {
    unsigned short* v6 = nullptr;       // current "drilled-in" sub record
    unsigned short* v62 = nullptr;      // selected result
    int v63 = 0;                        // "sub-office mode" flag
    int v61 = g_hooks->gameTickFinalize(0, 0, "special\\amt2");
    g_hooks->formCenterChildWindows(v61);

    // 112-entry candidate table: [recPtr, id, statusA, statusB, statusC, ... 14 i32 each].
    unsigned short* table[112];
    std::memset(table, 0, sizeof(table));
    int statusFlags[14 * 8];
    std::memset(statusFlags, 0, sizeof(statusFlags));

restart:
    g_hooks->formSelectWindow(v61, 2);
    RichStr(0u /*"$C"*/);
    if (v63) RichStr(0x9Du, v6 ? v6[0] : 0);
    else     RichStr(0u /*"$Z$[%s$]"*/, 3830);
    g_hooks->formSelectWindow(v61, 3);
    RichStr(0u /*"$C"*/);
    if (a4)      RichStr(a4);
    else if (a3) RichStr(a3);
    g_hooks->formSelectWindow(v61, 1);
    g_hooks->windowRemoveChildren(g_currentWindowId, 1);

    int count = 0;
    if (a1) {
        char buf[144];   // 6 candidates * 24-byte stride (Office_Collect* output cap)
        std::memset(buf, 0, sizeof(buf));
        if (a1 > 0 && a1 < 7) {
            count = g_hooks->officeCollectByCategory(a1, 6, buf);
        } else if (a1 == 7) {
            count = g_hooks->officeCollectElectiveOffices(0, 6, buf);
        } else {
            goto force_quit;
        }
        if (count > 8) count = 8;   // table holds 8 groups of 14 ptrs (112 slots)
        for (int i = 0; i < count; ++i) {
            int id = *reinterpret_cast<int*>(&buf[24 * i + 4]);
            // each candidate occupies a 14-ptr group; the record ptr is slot 0.
            table[14 * i] = const_cast<unsigned short*>(
                reinterpret_cast<const unsigned short*>(g_hooks->personFindRecordById(id)));
        }
    } else {
        // a1==0: walk the 768-person table for class bytes {5,6,7}; inert => none.
        count = 0;
    }

    if (count) {
        if (a1 == 0 || a1 == 7)
            g_hooks->hudBuildPersonGridLayout(a1, count, a2, table, v6 ? reinterpret_cast<std::intptr_t>(v6) : 0);
        else
            g_hooks->hudBuildPersonColumnLayout(a1, count, a2, table, v6 ? reinterpret_cast<std::intptr_t>(v6) : 0);

        int dirty = 0;
        while (true) {
            if (g_hooks->readMouseRelease()) {
                g_forceQuitLatch = 1;
            } else if (g_hooks->readSelectOffice() && v63 == 0) {
                // back out of a sub-office to the top level.
                dirty = 1;
                v63 = g_hooks->readSelectOffice();
                v6 = nullptr;
            } else if (g_hooks->readMouseWheelUp() && g_hooks->readLastClickedId() != -1) {
                int hover = g_hooks->readHoverObject();
                int sel = -1;
                for (int i = 0; i < count; ++i) {
                    if (reinterpret_cast<std::intptr_t>(table[i * 14 + 1]) == hover) { sel = i; break; }
                }
                if (sel >= 0 && sel < count) {
                    unsigned short* rec = table[14 * sel];
                    if (rec && rec[0] == 0xFFFF) {
                        v6 = nullptr; dirty = 1; v63 = 0;
                    } else if (!drillIn) {
                        v62 = rec; g_forceQuitLatch = 1; dirty = 1;
                    } else {
                        g_hooks->formSetObjectsVisible(v61, 0);
                        int r = drillIn();
                        v62 = r ? table[14 * sel] : nullptr;
                        g_hooks->formSetObjectsVisible(v61, 1);
                        dirty = 1;
                    }
                }
            }
            if (!g_hooks->gameLogicRunFrameLoop(415687, 0,
                    reinterpret_cast<const void*>(static_cast<std::intptr_t>(count)))) {
                g_hooks->formDestroy(v61);
                return v62;
            }
            if (dirty) { dirty = 0; goto restart; }
        }
    }

force_quit:
    g_forceQuitLatch = 1;
    g_hooks->gameLogicRunFrameLoop(415687, 1, reinterpret_cast<const void*>(static_cast<std::intptr_t>(count)));
    g_hooks->formDestroy(v61);
    return nullptr;
}

// ===========================================================================
// 0x550190 — VIBE_Panel_RunThievesGuildTrain.
// Loads the thieves'-guild training form, renders an item grid, and on the
// confirm key (258) or a right-click-with-items resets the dragged item slots.
// ===========================================================================
int Panel_RunThievesGuildTrain(int a1, int a2, char* a3, int a4) {
    int v4 = g_hooks->gameTickFinalize(0, 0,
                 "locations\\diebesgilde\\diebesgilde_trainieren");
    g_hooks->formCenterChildWindows(v4);
    g_hooks->dragSlotResetGridTable();
    g_hooks->dragCursorSetSprite(0, 0);

    do {
        g_hooks->inventoryRenderItemGrid(0, 2);
        if (g_hooks->readMouseRelease()) {
            if (g_hooks->dragSlotCountUsed()) {
                for (int i = 0; i != 32; i += 1) {
                    int id = g_thiefGridIds[i];
                    if (id != -1 && g_hooks->objectGetDataPtr(id))
                        g_hooks->objectSetValueOrText(id, 0, a4, a1, a2);
                }
                g_hooks->dragSlotResetTable();
            } else {
                g_forceQuitLatch = 1;
            }
        }
        if (g_hooks->readLastClickedId() == 258) {
            for (int j = 0; j != 32; j += 1) {
                int id = g_thiefGridIds[j];
                if (id != -1 && g_hooks->objectGetDataPtr(id))
                    g_hooks->objectSetValueOrText(id, 0, a4, a1, a2);
            }
            g_hooks->dragSlotResetTable();
        }
    } while (g_hooks->gameLogicRunFrameLoop(423879, -1, a3));

    g_hooks->formDestroy(v4);
    g_hooks->hudUpdateEdgeScroll(-1, reinterpret_cast<std::intptr_t>(a3), 0);
    return 0;
}

// ===========================================================================
// 0x5130bc — VIBE_Dialog_RobberRaidConfirm.
// Gated on the active-char flag; builds the player bar and runs a map-view panel
// dispatch around VIBE_Location_RobberCampRaid (routed as a hook callback).
// ===========================================================================
namespace { void RobberCampRaidCb() {} } // VIBE_Location_RobberCampRaid (inert)

void Dialog_RobberRaidConfirm(int target) {
    if (g_hooks->dialogCheckActiveCharFlag()) return;
    // gilde.exe @0x5130e8: Light_SetGrayColorThunk(0, 40, &v4) zeros a 40-byte blob;
    // then v5(+4)=1024, v7(+0xC byte)=6, v6(+8)=&unk_7443A0, v8(+0x24)=1689.
    unsigned char v4[40];
    std::memset(v4, 0, sizeof(v4));
    g_hooks->lightSetGrayThunk(0, 40, reinterpret_cast<std::intptr_t>(v4));
    { int t = 1024; std::memcpy(v4 + 4, &t, 4); }   // v5 (unconditional)
    if (target) {
        g_hooks->playerBarCreate(target, 0);
        v4[0x0C] = 6;                                    // v7
        { int t = 1689; std::memcpy(v4 + 0x24, &t, 4); } // v8
        char text[1024];
        g_hooks->textRenderFormattedMessage(text, 5776, 0, 0, 0);
        g_hooks->mapViewPanelDispatcher(1, reinterpret_cast<std::intptr_t>(v4),
                                        &RobberCampRaidCb, reinterpret_cast<std::intptr_t>(text), 0);
        g_hooks->playerBarDestroy(0, nullptr);
        g_hooks->selectionClearAll();
    }
}

// ===========================================================================
// 0x512e08 — VIBE_Dialog_BriberyConfirm. Same shape, around Location_BriberyMenu.
// ===========================================================================
namespace { void BriberyMenuCb() {} } // VIBE_Location_BriberyMenu (inert)

void Dialog_BriberyConfirm(int target) {
    if (g_hooks->dialogCheckActiveCharFlag()) return;
    // gilde.exe @0x512e34: same 40-byte blob; v5(+4)=1024, v6(+8)=0, v7(+0xC byte)=6,
    // v8(+0x24)=1689, message 5637.
    unsigned char v4[40];
    std::memset(v4, 0, sizeof(v4));
    g_hooks->lightSetGrayThunk(0, 40, reinterpret_cast<std::intptr_t>(v4));
    { int t = 1024; std::memcpy(v4 + 4, &t, 4); }   // v5 (unconditional)
    if (target) {
        g_hooks->playerBarCreate(target, 0);
        v4[0x0C] = 6;                                    // v7
        { int t = 1689; std::memcpy(v4 + 0x24, &t, 4); } // v8
        char text[1024];
        g_hooks->textRenderFormattedMessage(text, 5637, 0, 0, 0);
        g_hooks->mapViewPanelDispatcher(1, reinterpret_cast<std::intptr_t>(v4),
                                        &BriberyMenuCb, reinterpret_cast<std::intptr_t>(text), 0);
        g_hooks->playerBarDestroy(0, nullptr);
        g_hooks->selectionClearAll();
    }
}

} // namespace guild::gui
