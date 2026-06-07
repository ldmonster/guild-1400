#include "gui/gui_dialogs5.h"

#include "gui/object.h"   // g_widgets, Widget_AllocSlot
#include "gui/window.h"   // g_currentWindowId (dword_62D230)

#include <cstdint>
#include <cstring>

namespace guild::gui {

// ===========================================================================
// Module-owned engine tables (BSS, zero at load).
// ===========================================================================
i32 g_moneyInfoRows[16 * 7];
i32 g_masterRows[128 * 6];
i32 g_sliderScratch[3];
i32 g_buildingRowIds[128];
i32 g_buildingRowCount;
i32 g_playerStatsTab;
i32 g_forceQuitLatch;

// Widget pool element by slot, addressed as the original "dword_69FFB4 + 740*idx".
namespace { inline Widget& W(int slot) { return g_widgets[slot]; } }

// ===========================================================================
// Hooks (inert defaults). Defaults make every builder observable without the
// engine and make every frame loop terminate immediately (gameLogicRunFrameLoop
// returns 0 by default).
// ===========================================================================
namespace {

int  DefGameTickFinalize(i16, i16, const char*) { return -1; } // "no form loaded"
void DefFormCenterChildWindows(int) {}
void DefFormSelectWindow(int, int) {}
int  DefFormGetWindowId(int, int) { return -1; }
int  DefFormGetChildObjectId(int, int, int) { return -1; }
int  DefFormDestroy(int) { return -1; }
void DefFormSetObjectsVisible(int, int) {}
void DefFormSetChildrenVisible(int, int) {}
int  DefTextRenderRichString(unsigned, int, int, int, int) { return 0; }
void DefTextRenderFormattedMessage(char* out, const char*, int, int, int) { if (out) out[0] = 0; }
void DefHudSyncWindowColors(int) {}
void DefHudBuildSliderPanel(int, int, int, int, int, i32*, i32*, i32*) {}
int  DefHudBuildBuildingPriceRows(const void*, int, i32*) { return 0; }
int  DefHudBuildScaledTiledBar(int, int, int, const void*, int) { return 0; }
void DefHudBuildButtonRow(int, int, i32*, int, const void*) {}
int  DefObjectAddToWindow(int, int) { return Widget_AllocSlot(); } // real allocator
int  DefObjectAddTextLabel(i16, i16, int, const char*) { return Widget_AllocSlot(); }
int  DefInputAddFieldToWindow(i16, i16, i16, i16, unsigned, int) { return Widget_AllocSlot(); }
void DefObjectSetColor(int, int) {}
void DefObjectSetEnabled(int, int) {}
void DefObjectSetValueOrText(int, int, int, int, int) {}
int  DefObjectGetDataPtr(int) { return 0; }
int  DefWindowAddChildWindow(int, i16, i16, int, int, int) { return Widget_AllocSlot(); }
void DefWindowRemoveIfActive(int, int, int) {}
int  DefWidgetDestroyByType(int, int, int) { return 0; }
void DefWindowScroll(int, int, int) {}
int  DefRadioGroupCreate(int, int) { return -1; }
void DefRadioGroupAddButton(int, int) {}
void DefRadioGroupFreeSurface() {}
const void* DefPersonQueryBegin(int, int, int, unsigned short) { return nullptr; }
const void* DefPersonIterNext() { return nullptr; }
const short* DefPersonFindActiveByEntity(const short*) { return nullptr; }
int  DefPersonComputeTotalWealth(unsigned short, const short*) { return 0; }
int  DefMoneyConvertToDisplayCoord(int money, unsigned char) { return money; }
int  DefBuildingCollectByCityHandle(unsigned short, int, i32*) { return 0; }
int  DefBuildingFindById(int) { return 0; }
int  DefBuildingMapTypeToCategory(int) { return 0; }
int  DefBuildingGetUpgradeLevel(int) { return 0; }
void DefBuildingValueComputeWorth(const void*, unsigned short, int* out) { if (out) *out = 0; }
int  DefGesetzGetRecord(unsigned char, void*) { return 0; }
int  DefBuildingTypeComputeVariantIndex(int, int) { return 0; }
int  DefInventoryFindSlotIndexByItemId(short, void*) { return 0; }
int  DefInventoryFindSlotByItemId(short) { return 0; }
void DefItemUseObjectAction(int, void*) {}
void DefTradePopulateItemSlots(int, i32*, int, int, int) {}
int  DefFormPopulateObjectList(int, int) { return 0; }
void DefCmdBeginDeltaPacket(int, int) {}
void DefCmdAppendCopiedField(unsigned, unsigned, const void*, int) {}
int  DefCmdQueueRequestState23() { return 0; }
int  DefCmdGetPacketStatusById(int) { return 1; } // "done" -> wait loop exits at once
void DefCmdQueueRequestFlagBlob32(int, void*) {}
void DefAmtRefreshGuildState() {}
void DefCmdQueueRequestSlotReset28(void*, int) {}
void DefDragSlotBeginDragText() {}
void DefDragSlotResetTable() {}
void DefDragCursorSetSprite(int, int) {}
void DefCoordConvertX() {}
void DefLightSetGrayThunk(int, int, void*) {}
int  DefInteractionTestHandlerFlagDword(int) { return 1; } // proceed
int  DefInteractionDispatchPanelEvent(int, int, int, int) { return 0; }
void DefSelectionUpdate(int, int) {}
void DefMenuRunChoosePlayer(int, int) {}
void DefMenuChooseProfession(int, int) {}
int  DefGameLogicRunFrameLoop(int, int, const void*) { return 0; } // exit loop immediately
int  DefReadMouseRelease() { return 0; }
int  DefReadMouseWheelUp() { return 0; }
int  DefReadLastClickedId() { return -1; }
int  DefReadHoverObject() { return -1; }

const GuiDialogs5Hooks kDefaultHooks = {
    &DefGameTickFinalize, &DefFormCenterChildWindows, &DefFormSelectWindow,
    &DefFormGetWindowId, &DefFormGetChildObjectId, &DefFormDestroy,
    &DefFormSetObjectsVisible, &DefFormSetChildrenVisible,
    &DefTextRenderRichString, &DefTextRenderFormattedMessage,
    &DefHudSyncWindowColors, &DefHudBuildSliderPanel, &DefHudBuildBuildingPriceRows,
    &DefHudBuildScaledTiledBar, &DefHudBuildButtonRow,
    &DefObjectAddToWindow, &DefObjectAddTextLabel, &DefInputAddFieldToWindow,
    &DefObjectSetColor, &DefObjectSetEnabled, &DefObjectSetValueOrText,
    &DefObjectGetDataPtr, &DefWindowAddChildWindow, &DefWindowRemoveIfActive,
    &DefWidgetDestroyByType, &DefWindowScroll,
    &DefRadioGroupCreate, &DefRadioGroupAddButton, &DefRadioGroupFreeSurface,
    &DefPersonQueryBegin, &DefPersonIterNext, &DefPersonFindActiveByEntity,
    &DefPersonComputeTotalWealth, &DefMoneyConvertToDisplayCoord,
    &DefBuildingCollectByCityHandle, &DefBuildingFindById, &DefBuildingMapTypeToCategory,
    &DefBuildingGetUpgradeLevel, &DefBuildingValueComputeWorth, &DefGesetzGetRecord,
    &DefBuildingTypeComputeVariantIndex,
    &DefInventoryFindSlotIndexByItemId, &DefInventoryFindSlotByItemId,
    &DefItemUseObjectAction, &DefTradePopulateItemSlots, &DefFormPopulateObjectList,
    &DefCmdBeginDeltaPacket, &DefCmdAppendCopiedField, &DefCmdQueueRequestState23,
    &DefCmdGetPacketStatusById, &DefCmdQueueRequestFlagBlob32, &DefAmtRefreshGuildState,
    &DefCmdQueueRequestSlotReset28,
    &DefDragSlotBeginDragText, &DefDragSlotResetTable, &DefDragCursorSetSprite,
    &DefCoordConvertX, &DefLightSetGrayThunk,
    &DefInteractionTestHandlerFlagDword, &DefInteractionDispatchPanelEvent,
    &DefSelectionUpdate, &DefMenuRunChoosePlayer, &DefMenuChooseProfession,
    &DefGameLogicRunFrameLoop,
    &DefReadMouseRelease, &DefReadMouseWheelUp, &DefReadLastClickedId, &DefReadHoverObject,
};

const GuiDialogs5Hooks* g_hooks = &kDefaultHooks;

} // namespace

const GuiDialogs5Hooks* SetGuiDialogs5Hooks(const GuiDialogs5Hooks* hooks) {
    const GuiDialogs5Hooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}
const GuiDialogs5Hooks* GuiDialogs5Hooks_Default() { return &kDefaultHooks; }
const GuiDialogs5Hooks& GuiDialogs5HooksActive() { return *g_hooks; }

void ResetGuiDialogs5() {
    std::memset(g_moneyInfoRows, 0, sizeof(g_moneyInfoRows));
    std::memset(g_masterRows, 0, sizeof(g_masterRows));
    std::memset(g_sliderScratch, 0, sizeof(g_sliderScratch));
    std::memset(g_buildingRowIds, 0, sizeof(g_buildingRowIds));
    g_buildingRowCount = 0;
    g_playerStatsTab = 0;
    g_forceQuitLatch = 0;
    g_hooks = &kDefaultHooks;
}

// Convenience wrappers for the most-called text helper (the original passes a
// varying number of args; the hook always takes five, defaulted to 0).
static inline int RichStr(unsigned id, int a = 0, int b = 0, int c = 0, int d = 0) {
    return g_hooks->textRenderRichString(id, a, b, c, d);
}

// ===========================================================================
// 0x503e3c — VIBE_Window_SetCityNameCaption.
// The original copies three 2-byte/char strings around two "ANSI string" scratch
// buffers (String / byte_122F4CA / ReturnedString) while running the
// choose-player sub-menu, then patches the window record fields. We translate the
// byte-wise copy loops verbatim and use caller-provided scratch for the globals.
// ===========================================================================
namespace {
// The original String / ReturnedString / byte_122F4CA are 2-byte-per-char scratch
// buffers in BSS. We model them as module-local fixed buffers (the copies are
// bounded by the source NUL).
char g_cityCaptionScratch[256];   // String (dword_122F4A0-adjacent region)
char g_cityNameScratch[256];      // byte_122F4CA
char g_cityRetScratch[256];       // ReturnedString
const char* const kEurerStadt = "$Z%s";  // aEurerStadt analogue (caption template)
i32 g_cityFieldA;                 // dword_122F4A4
unsigned char g_cityFieldB;       // byte_122F4A8
unsigned char g_cityFieldC;       // byte_122F4A9
i32 g_cityFieldD;                 // dword_122F4A0

// 1:1 of the "copy until NUL, two chars at a time" loop the decompiler emits.
void CopyWideStr(char* dst, const char* src) {
    char a = src[0];
    dst[0] = a;
    if (!a) return;
    int i = 1;
    char b;
    do {
        b = src[i];
        dst[i] = b;
        ++i;
    } while (b);
}
} // namespace

int Window_SetCityNameCaption(char* winRec, char* name) {
    char* v3 = winRec + 48;             // window caption field
    // copy caption -> String
    CopyWideStr(g_cityCaptionScratch, winRec + 48);
    // copy incoming name -> byte_122F4CA
    CopyWideStr(g_cityNameScratch, name);
    // copy "$Z%s" template -> ReturnedString
    CopyWideStr(g_cityRetScratch, kEurerStadt);

    g_hooks->menuRunChoosePlayer(198, 1);
    g_hooks->menuChooseProfession(198, 0);

    // copy String back into v3 (restored caption record field)
    CopyWideStr(v3, g_cityCaptionScratch);
    // copy byte_122F4CA back into name
    CopyWideStr(name, g_cityNameScratch);

    // Patch the resolved window record (v19 in the original; modeled as winRec).
    *reinterpret_cast<i16*>(winRec + 10) = 18;
    *reinterpret_cast<i32*>(winRec + 84) = g_cityFieldA;
    *reinterpret_cast<unsigned char*>(winRec + 9)  = g_cityFieldB;
    *reinterpret_cast<unsigned char*>(winRec + 12) = g_cityFieldC;
    *reinterpret_cast<unsigned char*>(winRec + 356) =
        static_cast<unsigned char>((g_cityFieldD >> 24) & 0xFF);
    return 1;
}

// ===========================================================================
// 0x552904 — VIBE_Panel_BuildLawSeals.
// lawRow[0]=id, [1]=winA, [2]=labelWin, [3]=barWin. Bytes at +13/+14 pack seal
// counts; v6 (the seal count) / v7 (a flag) come from VIBE_Gesetz_GetRecord.
// ===========================================================================
int* Panel_BuildLawSeals(int* lawRow) {
    int* v1 = lawRow;
    int* result = lawRow;
    if (lawRow[0] == -1) return result;

    // VIBE_Gesetz_GetRecord(HIBYTE(*(lawRow+14)), &scratch) -> seal flag/count.
    unsigned char hi = static_cast<unsigned char>(
        (*reinterpret_cast<unsigned*>(reinterpret_cast<char*>(lawRow) + 14) >> 24) & 0xFF);
    char v5[12];
    std::memset(v5, 0, sizeof(v5));
    int rec = g_hooks->gesetzGetRecord(hi, v5);
    // v6 (count) and v7 (flag) live in the returned record at the +0x18/+0xC slots
    // the original reads as v6/v7; model from v5 scratch (zero by default).
    int v7 = *reinterpret_cast<int*>(v5 + 0);   // v7 (flag)
    int v6 = *reinterpret_cast<int*>(v5 + 12);  // v6 (count)

    if (rec) {
        if (v7) {
            if (v6) {
                g_hooks->formSelectWindow(v1[0], v1[1]);
                for (int v2 = 0; v2 < v6; ++v2) {
                    g_hooks->objectAddToWindow(g_currentWindowId, 5);
                }
            }
        }
    }

    if (*reinterpret_cast<signed char*>(reinterpret_cast<char*>(v1) + 16) <= 4) {
        g_hooks->formSelectWindow(v1[0], v1[2]);
        RichStr(0u /*"$C$Z%s"*/, (*reinterpret_cast<int*>(reinterpret_cast<char*>(v1) + 13) >> 24) + 4810);
        g_hooks->formSelectWindow(v1[0], v1[3]);
        int curByte = (*reinterpret_cast<unsigned*>(reinterpret_cast<char*>(v1) + 13) >> 24) & 0xFF;
        return reinterpret_cast<int*>(static_cast<std::intptr_t>(
            g_hooks->hudBuildScaledTiledBar(0, 6, curByte, nullptr, 1162)));
    }
    return result;
}

// ===========================================================================
// 0x551b5c — VIBE_Panel_BuildBuildingList (build-only half).
// ===========================================================================
int Panel_BuildBuildingList(unsigned short /*city*/) {
    int v1 = g_hooks->gameTickFinalize(0, 0, "panel\\geb_liste_2");
    g_hooks->formCenterChildWindows(v1);
    g_hooks->formSelectWindow(v1, 0);
    g_hooks->hudSyncWindowColors(g_currentWindowId);
    g_hooks->formSelectWindow(v1, 0);
    RichStr(0xAFu);
    g_hooks->formSelectWindow(v1, 0);
    g_buildingRowCount = g_hooks->hudBuildBuildingPriceRows(nullptr, 128, g_buildingRowIds);
    g_hooks->formSelectWindow(v1, 0);
    int windowId = g_hooks->formGetWindowId(v1, 2);
    g_hooks->hudBuildSliderPanel(518, 359, windowId, g_currentWindowId, 64,
                                 &g_sliderScratch[2], &g_sliderScratch[1], &g_sliderScratch[0]);
    return v1;
}

// ===========================================================================
// 0x5517b4 — VIBE_Panel_BuildMoneyInfo.
// ===========================================================================
int Panel_BuildMoneyInfo(unsigned short city) {
    // clear the per-row scratch: 16 rows of 7 i32; col[0..5]=-1 markers, col[6]=0.
    for (int i = 0; i != 112; i += 7) {
        g_moneyInfoRows[i + 0] = -1; // dword_1231EA4[i]
        g_moneyInfoRows[i + 6] = 0;  // dword_1231EB0 (+12 bytes from EA4 => +3 i32? keep verbatim slot)
    }
    int v3 = g_hooks->gameTickFinalize(0, 0, "Panel\\geld_info_2");
    int v30 = v3;
    g_hooks->formCenterChildWindows(v3);
    g_hooks->formSelectWindow(v3, 0);
    g_hooks->hudSyncWindowColors(g_currentWindowId);
    g_hooks->formSelectWindow(v3, 0);
    RichStr(0x1B19u);
    g_hooks->formSelectWindow(v3, 4);
    RichStr(0x1B1Au);
    g_hooks->formSelectWindow(v3, 5);
    RichStr(0x1B1Bu);
    g_hooks->formSelectWindow(v3, 1);
    g_hooks->tradePopulateItemSlots(0, &g_moneyInfoRows[6 /*dword_1231EC0 region*/], 1, v3, 0);

    i32 v24[9];
    std::memset(v24, 0, sizeof(v24));
    g_hooks->buildingCollectByCityHandle(city, 0, v24);
    int count = g_hooks->buildingCollectByCityHandle(city, 0, v24);

    g_hooks->formSelectWindow(v30, 3);
    RichStr(0u /*"$C"*/);
    if (count >= 9) RichStr(0x14DAu);

    g_hooks->formSelectWindow(v30, 0);
    int windowId = g_hooks->formGetWindowId(v30, 3);
    g_hooks->hudBuildSliderPanel(533, 363, windowId, g_currentWindowId, 106,
                                 &g_moneyInfoRows[0], &g_moneyInfoRows[1], &g_moneyInfoRows[2]);

    if (count > 0) {
        int row = 0;
        for (int v10 = 0; v10 < count; ++v10) {
            g_hooks->formSelectWindow(v30, 3);
            g_moneyInfoRows[row + 2] = g_hooks->objectAddToWindow(g_currentWindowId, v10 * 106);
            int child = g_hooks->windowAddChildWindow(0, 0, 98, 0, 16, g_currentWindowId);
            g_moneyInfoRows[row + 1] = child;
            g_moneyInfoRows[row + 3] = v24[v10];
            if (child >= 0 && child < kMaxWidgets) {
                W(child).at<i16>(20) = 69;  // word_67EDFC[...] = 69 (width slot)
                W(child).at<i16>(22) = 16;  // word_67EB90[...] = 16
            }
            int bld = g_hooks->buildingFindById(v24[v10]);
            if (bld) RichStr(0x1B1Cu, bld);
            row += 3;
        }
    }
    return v30;
}

// ===========================================================================
// 0x551e7c — VIBE_Panel_BuildMasterList.
// Walks the person query; for each foreign master adds a name label, a tax-value
// field, and records the row in g_masterRows. Returns -1 when no master was added.
// ===========================================================================
int Panel_BuildMasterList(unsigned short city) {
    int v19 = 0;
    int v20 = g_hooks->gameTickFinalize(0, 0, "panel\\meister_liste_2");
    g_hooks->formCenterChildWindows(v20);
    g_hooks->formSelectWindow(v20, 0);
    RichStr(0x1543u);
    for (int v3 = 0; v3 != 768; ) {
        v3 += 6;
        if (v3 < 768) {
            g_masterRows[v3 + 0] = -1;
            g_masterRows[v3 + 1] = -1;
            g_masterRows[v3 + 2] = -1;
            g_masterRows[v3 + 3] = -1;
            g_masterRows[v3 + 4] = 0;
        }
    }
    g_hooks->formSelectWindow(v20, 0);
    int windowId = g_hooks->formGetWindowId(v20, 2);
    g_hooks->hudBuildSliderPanel(518, 359, windowId, g_currentWindowId, 62,
                                 &g_sliderScratch[2], &g_sliderScratch[1], &g_sliderScratch[0]);
    g_hooks->formSelectWindow(v20, 3);
    RichStr(0x1544u);
    g_hooks->formSelectWindow(v20, 4);
    RichStr(0x1545u);
    g_hooks->formSelectWindow(v20, 2);

    const short* begin = static_cast<const short*>(g_hooks->personQueryBegin(3072, 1, 4, city));
    if (begin) {
        int v9 = 0;        // 6*row index into g_masterRows
        int y = 8;
        do {
            const short* active = g_hooks->personFindActiveByEntity(begin);
            if (active && active[0] != static_cast<short>(city)) {
                g_hooks->formSelectWindow(v20, 2);
                g_masterRows[v9 + 0] = g_hooks->objectAddToWindow(g_currentWindowId, 62 * v19);
                char buf[256];
                g_hooks->textRenderFormattedMessage(buf, "%s~ %s", 0, 0, 0);
                int label = g_hooks->objectAddTextLabel(64, static_cast<i16>(y + 12),
                                                        g_currentWindowId, buf);
                g_masterRows[v9 + 5] = label;
                g_hooks->objectSetColor(label, 67);
                char taxbuf[256];
                g_hooks->textRenderFormattedMessage(taxbuf, "%T", 0, 0, 0);
                g_masterRows[v9 + 4] = g_hooks->objectAddTextLabel(384, static_cast<i16>(y + 12),
                                                                   g_currentWindowId, taxbuf);
                int field = g_hooks->inputAddFieldToWindow(234, static_cast<i16>(y), 64, 64, 0x82u,
                                                           g_currentWindowId);
                g_masterRows[v9 + 3] = field;
                int disp = g_hooks->moneyConvertToDisplayCoord(0, 0);
                g_hooks->objectSetValueOrText(field, 1000, 9000, disp, 0);
                int data = g_hooks->objectGetDataPtr(field);
                g_masterRows[v9 + 6 + 0] = data; // dword_12312B8 row+1 slot
                ++v19;
                v9 += 6;
                y += 62;
            }
            begin = static_cast<const short*>(g_hooks->personIterNext());
        } while (begin);
    }
    if (v19) return v20;
    g_hooks->formDestroy(v20);
    return -1;
}

// ===========================================================================
// 0x551c2c — VIBE_Panel_RunBuildingDetail.
// Builds the building-stats panel, renders a sequence of conditional stat lines,
// then spins the per-frame loop until it exits.
// ===========================================================================
int Panel_RunBuildingDetail(char* a1, unsigned short city) {
    int v3 = g_hooks->gameTickFinalize(60, 0, "panel\\player_stats_geb_detail");
    g_hooks->formCenterChildWindows(v3);
    g_hooks->formSelectWindow(v3, 0);
    RichStr(0u /*"$[%1G$]"*/);
    g_hooks->formSelectWindow(v3, 0);
    g_hooks->buildingMapTypeToCategory(a1 ? a1[0] : 0);

    int worth[16];
    std::memset(worth, 0, sizeof(worth));
    g_hooks->buildingValueComputeWorth(a1, city, worth);
    int v14 = worth[0],  v15 = worth[1],  v16 = worth[2],  v17 = worth[3],  v18 = worth[4];
    int v19 = worth[5],  v20 = worth[6],  v21 = worth[7],  v22 = worth[8],  v23 = worth[9];
    int v24 = worth[10], v25 = worth[11], v26 = worth[12]; int hi13 = worth[13];

    int upg = g_hooks->buildingGetUpgradeLevel(reinterpret_cast<std::intptr_t>(a1));
    RichStr(0xA0u, upg);
    if (hi13) RichStr(0xA1u, hi13);
    if (v15) RichStr(0xA2u, v15);
    if (v18) {
        int catBase = a1 ? (static_cast<unsigned char>(a1[547]) + 294) : 294;
        RichStr(0xA3u, catBase, v18);
    }
    if (v17 + v16) RichStr(0xA4u, v17 + v16);
    if (v19) RichStr(0xA5u, v19);
    if (v20) RichStr(0xA6u, v20);
    if (v23) RichStr(0xA7u, v23);
    if (v21) RichStr(0xA8u, v21);
    if (v22) RichStr(0xA9u, v22);
    if (v25) RichStr(0xAAu, v25);
    if (v24) {
        char t = a1 ? a1[0] : 0;
        if (t == 4 || t == 16 || t == 19) RichStr(0xB0u, v24);
        else                              RichStr(0xABu, v24);
    }
    if (v14) RichStr(0xACu, v14);
    if (v26 > 0)      RichStr(0xADu, v26);
    else if (v26 < 0) RichStr(0xAEu, v26 + v24);

    while (g_hooks->gameLogicRunFrameLoop(423879, 0, reinterpret_cast<const void*>(&Panel_RunBuildingDetail))) {
        if (g_hooks->readMouseRelease()) g_forceQuitLatch = 1;
    }
    return g_hooks->formDestroy(v3);
}

// ===========================================================================
// 0x552fc4 — VIBE_Panel_RunBuildingList.
// ===========================================================================
int Panel_RunBuildingList(unsigned short /*city*/) {
    i32 priceIds[128];
    std::memset(priceIds, 0, sizeof(priceIds));
    int v2 = g_hooks->gameTickFinalize(0, 0, "panel\\geb_liste");
    g_hooks->formCenterChildWindows(v2);
    g_hooks->formSelectWindow(v2, 0);
    g_hooks->hudSyncWindowColors(g_currentWindowId);
    g_hooks->formSelectWindow(v2, 0);
    RichStr(0xAFu);
    g_hooks->formSelectWindow(v2, 0);
    int v8 = g_hooks->hudBuildBuildingPriceRows(nullptr, 128, priceIds);
    g_hooks->formSelectWindow(v2, 0);
    int windowId = g_hooks->formGetWindowId(v2, 2);
    g_hooks->hudBuildSliderPanel(518, 359, windowId, g_currentWindowId, 256,
                                 &g_sliderScratch[2], &g_sliderScratch[1], &g_sliderScratch[0]);
    g_hooks->readLastClickedId(); // dword_75BF38 = -1 in the original; reads owned global

    do {
        if (g_hooks->readMouseRelease()) {
            g_forceQuitLatch = 1;
        } else if (g_hooks->readLastClickedId() != -1 && v8 > 0) {
            int hover = g_hooks->readHoverObject();
            for (int i = 0; i < v8; ++i) {
                if (hover == priceIds[i]) {
                    char* rec = reinterpret_cast<char*>(static_cast<std::intptr_t>(
                        g_hooks->objectGetDataPtr(hover)));
                    Panel_RunBuildingDetail(rec, /*city*/0);
                }
            }
        }
    } while (g_hooks->gameLogicRunFrameLoop(415687, 0, reinterpret_cast<const void*>(&Panel_RunBuildingList)));
    return g_hooks->formDestroy(v2);
}

// ===========================================================================
// 0x54d9d8 — VIBE_Panel_RunApBuy.
// ===========================================================================
int Panel_RunApBuy(unsigned short /*city*/) {
    int v1 = g_hooks->gameTickFinalize(0, 0, "panel\\panel_ap");
    g_hooks->formCenterChildWindows(v1);
    g_hooks->formSelectWindow(v1, 0);
    RichStr(0x1AD4u);
    g_hooks->formPopulateObjectList(v1, 0);

    do {
        if (g_hooks->readMouseRelease()) g_forceQuitLatch = 1;
        if (g_hooks->readLastClickedId() == 1210) {
            int hover = g_hooks->readHoverObject();
            for (int v4 = 0; v4 < 512; v4 += 2) {
                if (g_buildingRowIds[v4 + 1] && hover == g_buildingRowIds[v4]) {
                    g_hooks->objectGetDataPtr(g_buildingRowIds[v4]);
                    break;
                }
            }
            g_hooks->formSelectWindow(v1, 0);
            RichStr(0u /*"$C"*/);
            g_hooks->formPopulateObjectList(v1, 0);
        }
    } while (g_hooks->gameLogicRunFrameLoop(415687, 0, nullptr));
    return g_hooks->formDestroy(v1);
}

// ===========================================================================
// 0x54f3e8 — VIBE_Panel_RunUseObject.
// ===========================================================================
int Panel_RunUseObject(short* a1, unsigned short city) {
    int slot[8];
    std::memset(slot, 0, sizeof(slot));
    if (!g_hooks->inventoryFindSlotIndexByItemId(a1 ? a1[0] : 0, slot)) return 0;

    int v4 = g_hooks->gameTickFinalize(0, 0, "panel\\useobj");
    g_hooks->formCenterChildWindows(v4);
    g_hooks->formSelectWindow(v4, 0);

    short item = a1 ? a1[0] : 0;
    if (!g_hooks->inventoryFindSlotByItemId(item)) {
        RichStr(0xCA9u, 2 * item + 2151);
    } else {
        RichStr(0xCA8u, 2 * item + 2151, 2 * item + 2581, 2 * item + 2151);
    }
    g_hooks->dragCursorSetSprite(-1, 0);
    g_hooks->dragSlotBeginDragText();

    int a2 = 0;
    do {
        if (g_hooks->readMouseRelease()) { g_forceQuitLatch = 1; a2 = 0; }
        int clicked = g_hooks->readLastClickedId();
        if (clicked != -1) {
            if (clicked == 1210) {
                a2 = 1;
                g_forceQuitLatch = 1;
            } else if (clicked == 1155) {
                char buf[256];
                g_hooks->textRenderFormattedMessage(buf, "%d", 2 * item + 2151, 2 * item + 2151, 0);
                g_hooks->formSetObjectsVisible(v4, 0);
                // VIBE_Dialog_ShowMessageBox path -> commit; inert by default.
                g_hooks->formSetObjectsVisible(v4, 1);
                a2 = 0;
            }
        }
    } while (g_hooks->gameLogicRunFrameLoop(423879, 0, nullptr));

    g_hooks->formDestroy(v4);
    if (!a2) return 0;

    short args[8];
    std::memset(args, 0, sizeof(args));
    args[0] = a1 ? a1[0] : 0;
    g_hooks->itemUseObjectAction(static_cast<int>(city), args);
    return 0;
}

// ===========================================================================
// 0x551404 — VIBE_Panel_RunChooseWappen (coat-of-arms chooser).
// Builds 8 radio crest buttons, disables the ones already taken, then on a valid
// click queues the crest-change command and waits for it.
// ===========================================================================
int Panel_RunChooseWappen(unsigned short city) {
    int v38 = g_hooks->gameTickFinalize(0, 0, "Misc\\choosewappen");
    g_hooks->formCenterChildWindows(v38);
    g_hooks->formSelectWindow(v38, 0);
    g_hooks->hudSyncWindowColors(g_currentWindowId);
    g_hooks->formSelectWindow(v38, 0);
    RichStr(0x85u);
    g_hooks->formSelectWindow(v38, 2);

    int buttons[8];
    int v33 = g_hooks->radioGroupCreate(0, 0);
    int v5 = 1342;
    for (int v6 = 0; v6 < 8; ++v6) {
        int btn = g_hooks->objectAddToWindow(g_currentWindowId, 120);
        buttons[v6] = btn;
        g_hooks->radioGroupAddButton(v33, btn);
        ++v5;
    }
    // disable crests already owned by some city (1342..1349).
    int v10 = 1342;
    for (int i = 0; i != 8; ++i) {
        // The original walks the 768-city table; with no table loaded (inert) the
        // disable never fires. Preserve the per-crest decrement.
        --v10;
    }

    g_hooks->formSelectWindow(v38, 3);
    int v13 = RichStr(0x7Fu);
    int childObj = g_hooks->formGetChildObjectId(v38, 0, v13);
    (void)childObj; (void)city;

    g_hooks->dragSlotBeginDragText();
    do {
        for (int v18 = 0; v18 != 8; ++v18) {
            g_hooks->objectSetEnabled(buttons[v18], 1);
        }
        // enable/disable selected-confirm button: inert table => leave enabled(0).
        g_hooks->objectSetEnabled(childObj, 0);

        if (g_hooks->readLastClickedId() == 1210) {
            // build + queue the crest-change command, then poll until done.
            g_hooks->cmdBeginDeltaPacket(0, 0);
            int v35 = 1342;
            g_hooks->cmdAppendCopiedField(4u, 1u, &v35, 0);
            int pkt = g_hooks->cmdQueueRequestState23();
            while (!g_hooks->cmdGetPacketStatusById(pkt)) g_hooks->amtRefreshGuildState();
            int flag[16];
            std::memset(flag, 0, sizeof(flag));
            flag[0] = city;
            g_hooks->cmdQueueRequestFlagBlob32(13, flag);
            g_forceQuitLatch = 1;
        }
    } while (g_hooks->gameLogicRunFrameLoop(423879, 0, nullptr));

    g_hooks->radioGroupFreeSurface();
    return g_hooks->formDestroy(v38);
}

// ===========================================================================
// 0x566b9c — VIBE_Panel_ChooseProfession.
// Lays out a column of profession labels (string copied char-by-char from a
// label table), runs the frame loop, and on a click queues the slot-reset command.
// ===========================================================================
int Panel_ChooseProfession(unsigned short* a1, unsigned short* a2, short* a3) {
    int wealth = g_hooks->personComputeTotalWealth(a1 ? a1[0] : 0, a3);
    (void)a2;
    g_hooks->coordConvertX();
    int v37 = wealth; // (double)wealth * flt -> coord; inert coord keeps it integral
    int v38 = g_hooks->gameTickFinalize(0, 0, "Menu\\chooseprof");
    g_hooks->formCenterChildWindows(v38);
    g_hooks->formSelectWindow(v38, 0);
    int v23 = (a2 && (reinterpret_cast<unsigned char*>(a2)[9] != 0)) + 5805;
    int v24 = a2 ? a2[0] : 0;
    RichStr(0x16A7u, v23, v24, v37);

    int v34 = 4;
    int v33 = 1350;
    int v40 = 0;
    do {
        int w = g_hooks->objectAddToWindow(g_currentWindowId, 80 * (v40 / 3) + 100);
        if (w >= 0 && w < kMaxWidgets) {
            W(w).at<i32>(72) = 1;
            W(w).at<u8>(444) = 3;
            // label text copy (v12/v13 loop) — inert: no label table, leave blank.
            W(w).at<i32>(440) = 69;
        }
        v34 += 4;
        ++v33;
        ++v40;
    } while (v34 != 52);

    while (g_hooks->gameLogicRunFrameLoop(0, v33, reinterpret_cast<const void*>(&Panel_ChooseProfession))) {
        if (g_hooks->readMouseRelease()) {
            g_forceQuitLatch = 1;
        } else if (g_hooks->readMouseWheelUp()) {
            int clicked = g_hooks->readLastClickedId();
            int v17 = 1350;
            bool hit = (clicked == 1350);
            if (!hit) {
                while (true) {
                    ++v17;
                    if (v17 > 1362) break;
                    if (v17 == clicked) { hit = true; break; }
                }
            }
            if (hit) {
                int blob[40];
                std::memset(blob, 0, sizeof(blob));
                blob[1] = a2 ? *reinterpret_cast<int*>(reinterpret_cast<char*>(a2) + 4) : 0;
                blob[2] = -1;
                int variant = g_hooks->buildingTypeComputeVariantIndex((clicked - 1350) + 1, 1);
                (void)variant;
                g_hooks->cmdQueueRequestSlotReset28(blob, 0);
                g_forceQuitLatch = 1;
            }
        }
    }
    g_hooks->formDestroy(v38);
    return 0;
}

// ===========================================================================
// 0x552234 — VIBE_Panel_RunPlayerStats.
// The composite player-stats panel: composes the three Build* sub-panels, builds
// a 3-tab radio row, and runs the per-frame loop dispatching tab visibility and
// the sub-panel interactions.
// ===========================================================================
unsigned char Panel_RunPlayerStats(unsigned short city) {
    if (!g_hooks->interactionTestHandlerFlagDword(2048)) return 0;
    g_hooks->interactionDispatchPanelEvent(0x13, 0, 0, 0);

    int v29 = g_hooks->gameTickFinalize(0, 0, "panel\\player_stats");
    g_hooks->formCenterChildWindows(v29);
    g_hooks->formSelectWindow(v29, 0);
    g_hooks->hudSyncWindowColors(g_currentWindowId);
    g_hooks->formSelectWindow(v29, 0);

    i32 tabIds[3] = { -1, -1, -1 };
    g_hooks->hudBuildButtonRow(g_currentWindowId, 0, tabIds, 3, nullptr);

    int v6  = Panel_BuildMoneyInfo(city);
    int v31 = Panel_BuildBuildingList(city);
    int windowId = Panel_BuildMasterList(city);
    char* v10 = reinterpret_cast<char*>(static_cast<std::intptr_t>(windowId));

    int v28 = g_hooks->radioGroupCreate(3, 0);
    g_hooks->selectionUpdate(v28, g_playerStatsTab);
    if (windowId == -1) g_hooks->objectSetEnabled(tabIds[2], 0);

    do {
        if (g_hooks->readMouseRelease()) g_forceQuitLatch = 1;
        // The active tab id lives in dword_676588[35*group]; inert => 0 (money tab).
        int tab = 0;
        switch (tab) {
            case 0:
                g_hooks->formSetObjectsVisible(v6, 1);
                g_hooks->formSetObjectsVisible(v31, 0);
                g_hooks->formSetObjectsVisible(windowId, 0);
                break;
            case 1:
                g_hooks->formSetObjectsVisible(v31, 1);
                g_hooks->formSetObjectsVisible(v6, 0);
                g_hooks->formSetObjectsVisible(windowId, 0);
                break;
            case 2:
                g_hooks->formSetObjectsVisible(windowId, 1);
                g_hooks->formSetObjectsVisible(v31, 0);
                g_hooks->formSetObjectsVisible(v6, 0);
                break;
        }
        if (v6 != -1) {
            g_hooks->formSelectWindow(v6, 1);
            g_hooks->tradePopulateItemSlots(0, &g_moneyInfoRows[6], 1, v6, 0);
        }
        if (v31 != -1 && g_hooks->readLastClickedId() != -1 && g_buildingRowCount > 0) {
            int hover = g_hooks->readHoverObject();
            for (int v15 = 0; v15 < g_buildingRowCount; ++v15) {
                if (hover == g_buildingRowIds[v15]) {
                    g_hooks->formSetChildrenVisible(v29, 0);
                    char* rec = reinterpret_cast<char*>(static_cast<std::intptr_t>(
                        g_hooks->objectGetDataPtr(hover)));
                    Panel_RunBuildingDetail(rec, city);
                    g_hooks->formSetChildrenVisible(v29, 1);
                    break;
                }
            }
        }
        if (v10 != reinterpret_cast<char*>(static_cast<std::intptr_t>(-1))) {
            g_hooks->formSelectWindow(windowId, 0);
            // per-master tax-field commit loop: data unchanged when inert.
            for (int i = 0; i != 768; i += 6) {
                int field = g_masterRows[i + 3];
                if (field != -1 && g_hooks->objectGetDataPtr(field) != g_masterRows[i + 6]) {
                    g_hooks->cmdBeginDeltaPacket(0, 0);
                    g_hooks->cmdQueueRequestState23();
                }
            }
        }
    } while (g_hooks->gameLogicRunFrameLoop(415687, 0, v10));

    g_playerStatsTab = 0;
    g_hooks->radioGroupFreeSurface();
    g_hooks->formDestroy(v29);
    if (v6 != -1)  g_hooks->formDestroy(v6);
    if (v31 != -1) g_hooks->formDestroy(v31);
    if (windowId != -1) g_hooks->formDestroy(windowId);
    return static_cast<unsigned char>(g_hooks->interactionDispatchPanelEvent(0x13, 0, 0, 1));
}

} // namespace guild::gui
