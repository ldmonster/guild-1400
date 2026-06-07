// Unit tests for the P6 (Wave 29) GUI coverage slice — gui_widgetn, hud_actionsn,
// menu_dialogsn, action_target_pickn. Pure deterministic cores get golden vectors;
// the runtime-coupled functions are exercised with installed hooks that record what
// the form/text/world runtime would have been asked to do.
#include "test.h"

#include "gui/gui_widgetn.h"
#include "gui/hud_actionsn.h"
#include "gui/menu_dialogsn.h"
#include "gui/action_target_pickn.h"
#include "gui/object.h"   // g_widgets

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::gui;

// ---------------------------------------------------------------------------
// gui_widgetn — StatusText_ClearTable + Widget_SetTextColor + InfoPanel_Destroy
// ---------------------------------------------------------------------------
TEST(GuiP6Widget, StatusTextClearTableReturnsAndZeros) {
    ResetGuiWidgetN();
    for (int i = 0; i < kStatusTextCountN; ++i) g_statusTextMarker[i] = i + 1;
    int rv = StatusText_ClearTable();
    CHECK_EQ(rv, 6400);  // result ends at 1600, *4
    bool allZero = true;
    for (int i = 0; i < kStatusTextCountN; ++i) if (g_statusTextMarker[i] != 0) allZero = false;
    CHECK(allZero);
}

TEST(GuiP6Widget, WidgetSetTextColorWritesOffset20) {
    ResetGuiWidgetN();
    // No text flag (+88 == 0): SetTextColor still writes +20, RefreshText is a no-op.
    int idx = 3;
    std::memset(g_widgets[idx].raw, 0, sizeof(g_widgets[idx].raw));
    int r = Widget_SetTextColor(idx, 0x1234);
    CHECK_EQ((int)g_widgets[idx].at<std::int16_t>(20), 0x1234);
    CHECK_EQ(r, 0);  // RefreshText returns 0 when +88 flag clear
}

TEST(GuiP6Widget, WidgetRefreshTextCentersOnScreenWhenNoParent) {
    ResetGuiWidgetN();
    int idx = 4;
    Widget& w = g_widgets[idx];
    std::memset(w.raw, 0, sizeof(w.raw));
    w.at<std::int32_t>(88) = 1;            // text-present flag set
    w.at<std::int32_t>(44) = 0;            // no parent
    // text width: hiword of dword at +18 = 40 -> halfW 20.
    std::int32_t d18 = (40 << 16);
    std::memcpy(w.raw + 18, &d18, 4);
    // y: hiword of dword at +16 = 7.
    std::int32_t d16 = (7 << 16);
    std::memcpy(w.raw + 16, &d16, 4);
    g_screenSizeDword = (200u << 16);      // screen width 200 -> midX 100

    int capturedX = -999, capturedY = -999, capturedIdx = -1;
    static int cx, cy, ci; cx = cy = -999; ci = -1;
    GuiWidgetNHooks h = *GuiWidgetNHooks_Default();
    h.widgetLayoutBounds = [](int x, int y, int wi){ cx = x; cy = y; ci = wi; };
    SetGuiWidgetNHooks(&h);
    int rv = Widget_RefreshTextN(idx);
    capturedX = cx; capturedY = cy; capturedIdx = ci;
    SetGuiWidgetNHooks(nullptr);

    CHECK_EQ(rv, 1);
    CHECK_EQ(capturedX, 100 - 20);  // screen midX - halfW
    CHECK_EQ(capturedY, 7);
    CHECK_EQ(capturedIdx, idx);
}

TEST(GuiP6Widget, InfoPanelDestroyNoopWhenNotOpen) {
    ResetGuiWidgetN();
    g_infoPanelForm = -1;
    InfoPanel_Destroy();  // must not crash / touch anything
    CHECK_EQ(g_infoPanelForm, -1);
}

TEST(GuiP6Widget, InfoPanelDestroyTearsDownChildrenAndResets) {
    ResetGuiWidgetN();
    g_infoPanelForm = 5;
    g_infoPanelChild[0] = 11;
    g_infoPanelChild[1] = -1;   // already free
    g_infoPanelChild[2] = 13;
    g_infoPanelChild[3] = 14;
    for (int i = 0; i < 10; ++i) g_infoPanelHandle[i] = i + 100;

    InfoPanel_Destroy();

    CHECK_EQ(g_infoPanelForm, -1);
    for (int i = 0; i < kInfoPanelChildren; ++i) CHECK_EQ(g_infoPanelChild[i], -1);
    for (int i = 0; i < 10; ++i) CHECK_EQ(g_infoPanelHandle[i], -1);
}

TEST(GuiP6Widget, ScrollRunAnimationLoopPumpsAndCloses) {
    ResetGuiWidgetN();
    static int frames; frames = 3;
    static int opens, updates, closes; opens = updates = closes = 0;
    GuiWidgetNHooks h = *GuiWidgetNHooks_Default();
    h.scrollOpen = [](void*){ opens++; };
    h.scrollUpdateAnimation = [](){ updates++; };
    h.scrollClose = [](){ closes++; return 77; };
    h.gameLogicRunFrameLoop = [](int, int, const void*){ return frames-- > 0 ? 1 : 0; };
    h.readMouseRelease = [](){ return 1; };  // latch quit each frame
    SetGuiWidgetNHooks(&h);
    int rv = Scroll_RunAnimationLoop(nullptr, 0, "scroll");
    SetGuiWidgetNHooks(nullptr);
    CHECK_EQ(rv, 77);
    CHECK_EQ(opens, 1);
    CHECK_EQ(updates, 3);
    CHECK_EQ(closes, 1);
}

// ---------------------------------------------------------------------------
// hud_actionsn — classifiers + book loops + contact updater
// ---------------------------------------------------------------------------
TEST(GuiP6Hud, TradeContactTypeClassifier) {
    CHECK(Hud_IsTradeContactType(30));
    CHECK(Hud_IsTradeContactType(31));
    CHECK(Hud_IsTradeContactType(32));
    CHECK(!Hud_IsTradeContactType(29));
    CHECK(!Hud_IsTradeContactType(33));
    CHECK(!Hud_IsTradeContactType(0));
}

TEST(GuiP6Hud, EnterableContactClassClassifier) {
    CHECK(Hud_IsEnterableContactClass(2));
    CHECK(Hud_IsEnterableContactClass(6));
    CHECK(!Hud_IsEnterableContactClass(1));
    CHECK(!Hud_IsEnterableContactClass(3));
}

TEST(GuiP6Hud, MeisterBookLoopDispatchesClickedEntry) {
    ResetHudActionsN();
    static int frames; frames = 2;
    static int regCalls, meister, staff; regCalls = meister = staff = 0;
    HudActionsNHooks h = *HudActionsNHooks_Default();
    // First register returns 100 (meister), second 200 (personnel).
    h.statusTextRegister = [](const char*, int){ return (++regCalls % 2 == 1) ? 100 : 200; };
    h.gameLogicRunFrameLoop = [](int, int, const void*){ return frames-- > 0 ? 1 : 0; };
    h.meisterRunMasterCertificate = [](){ meister++; };
    h.personnelRunStaffBook = [](){ staff++; };
    SetHudActionsNHooks(&h);
    g_clickedStatusId = 100;  // meister entry clicked
    Hud_RunMeisterPersonalBookLoop(0);
    SetHudActionsNHooks(nullptr);
    CHECK_EQ(meister, 2);  // one dispatch per of the 2 frames
    CHECK_EQ(staff, 0);
}

TEST(GuiP6Hud, SoeldnerBookLoopGatedOnFlag) {
    ResetHudActionsN();
    static int frames; frames = 1;
    static int regCalls; regCalls = 0;
    HudActionsNHooks h = *HudActionsNHooks_Default();
    h.statusTextRegister = [](const char*, int){ regCalls++; return 0; };
    h.gameLogicRunFrameLoop = [](int, int, const void*){ return frames-- > 0 ? 1 : 0; };
    SetHudActionsNHooks(&h);
    g_hudFlags = 0;            // flag bit clear -> no registration
    Hud_RunSoeldnerBookLoop();
    SetHudActionsNHooks(nullptr);
    CHECK_EQ(regCalls, 0);
}

TEST(GuiP6Hud, SoeldnerBookLoopRegistersWhenFlagSet) {
    ResetHudActionsN();
    static int frames; frames = 1;
    static int regCalls; regCalls = 0;
    HudActionsNHooks h = *HudActionsNHooks_Default();
    h.statusTextRegister = [](const char*, int){ regCalls++; return 0; };
    h.gameLogicRunFrameLoop = [](int, int, const void*){ return frames-- > 0 ? 1 : 0; };
    SetHudActionsNHooks(&h);
    g_hudFlags = kHudFlagSoeldnerBook;  // 0x200 set -> two registers
    Hud_RunSoeldnerBookLoop();
    SetHudActionsNHooks(nullptr);
    CHECK_EQ(regCalls, 2);
}

TEST(GuiP6Hud, ContactUpdaterRegistersTradeContact) {
    ResetHudActionsN();
    static int trade; trade = 0;
    HudActionsNHooks h = *HudActionsNHooks_Default();
    h.tradeRegisterEinkaufContact = [](int){ trade++; };
    SetHudActionsNHooks(&h);
    g_selectedObject = 0x100 | 31;  // type byte 31 -> trade contact
    Hud_UpdateSelectedObjectContact(0);
    g_selectedObject = 0x100 | 10;  // type byte 10 -> not a contact
    Hud_UpdateSelectedObjectContact(0);
    SetHudActionsNHooks(nullptr);
    CHECK_EQ(trade, 1);
}

// ---------------------------------------------------------------------------
// menu_dialogsn — clamps + dialogs
// ---------------------------------------------------------------------------
TEST(GuiP6Menu, ClampPlayerCount) {
    CHECK_EQ((int)Menu_ClampPlayerCount(0), 2);
    CHECK_EQ((int)Menu_ClampPlayerCount(1), 2);
    CHECK_EQ((int)Menu_ClampPlayerCount(2), 2);
    CHECK_EQ((int)Menu_ClampPlayerCount(5), 5);
    CHECK_EQ((int)Menu_ClampPlayerCount(255), 255);
}

TEST(GuiP6Menu, ShouldShowMissionWarningGate) {
    CHECK(!Menu_ShouldShowMissionWarning(0));
    CHECK(!Menu_ShouldShowMissionWarning(1));
    CHECK(Menu_ShouldShowMissionWarning(2));
    CHECK(Menu_ShouldShowMissionWarning(4));
}

TEST(GuiP6Menu, ChoosePlayerCountClampsAndReturnsOk) {
    ResetMenuDialogsN();
    static int frames; frames = 1;
    static int seedCount; seedCount = -1;
    MenuDialogsNHooks h = *MenuDialogsNHooks_Default();
    h.gameTickFinalize = [](i16, i16, const char*){ return 9; };  // form id 9
    h.formGetChildObjectId = [](int, int, int){ return 5; };
    h.objectSetValueOrText = [](int, int, int, int c){ seedCount = c; };
    h.gameLogicRunFrameLoop = [](int, int, const void*){ return frames-- > 0 ? 1 : 0; };
    h.objectGetDataPtr = [](int){ return 4; };  // OK -> count becomes 4
    SetMenuDialogsNHooks(&h);
    g_playerCount = 1;            // below min
    g_lastMenuAction = kActionOk; // OK pressed
    int rv = Menu_ChoosePlayerCount();
    SetMenuDialogsNHooks(nullptr);
    CHECK_EQ(seedCount, 2);       // clamped to 2 before the loop
    CHECK_EQ(rv, 1);             // OK pressed
    CHECK_EQ((int)g_playerCount, 4); // updated from widget data ptr
}

TEST(GuiP6Menu, ChoosePlayerCountCancelReturnsZero) {
    ResetMenuDialogsN();
    static int frames; frames = 1;
    MenuDialogsNHooks h = *MenuDialogsNHooks_Default();
    h.gameTickFinalize = [](i16, i16, const char*){ return 3; };
    h.formGetChildObjectId = [](int, int, int){ return 5; };
    h.gameLogicRunFrameLoop = [](int, int, const void*){ return frames-- > 0 ? 1 : 0; };
    SetMenuDialogsNHooks(&h);
    g_playerCount = 3;
    g_lastMenuAction = kActionCancel;
    int rv = Menu_ChoosePlayerCount();
    SetMenuDialogsNHooks(nullptr);
    CHECK_EQ(rv, 0);  // cancel -> OK not pressed
}

TEST(GuiP6Menu, ShowMissionWarningGateAndResult) {
    ResetMenuDialogsN();
    static int warnShown; warnShown = 0;
    static int failRun; failRun = 0;
    MenuDialogsNHooks h = *MenuDialogsNHooks_Default();
    h.missionPickRandomByType = [](std::uint8_t){ return 7; };  // a mission was picked
    h.gameTickFinalize = [](i16, i16, const char*){ warnShown++; return -1; };
    h.missionRunFailureDialog = [](int, int){ failRun++; };
    SetMenuDialogsNHooks(&h);
    // single player -> no warning dialog, but mission still runs failure dialog.
    g_playerCount = 1;
    int rv = Menu_ShowMissionWarning();
    CHECK_EQ(warnShown, 0);
    CHECK_EQ(failRun, 1);
    CHECK_EQ(rv, 7);
    // multi player -> warning dialog shown.
    warnShown = 0; failRun = 0;
    g_playerCount = 3;
    rv = Menu_ShowMissionWarning();
    SetMenuDialogsNHooks(nullptr);
    CHECK_EQ(warnShown, 1);
    CHECK_EQ(failRun, 1);
    CHECK_EQ(rv, 7);
}

TEST(GuiP6Menu, ShowMissionWarningNoMissionReturnsZero) {
    ResetMenuDialogsN();
    MenuDialogsNHooks h = *MenuDialogsNHooks_Default();
    h.missionPickRandomByType = [](std::uint8_t){ return 0; };  // no mission
    SetMenuDialogsNHooks(&h);
    g_playerCount = 1;
    int rv = Menu_ShowMissionWarning();
    SetMenuDialogsNHooks(nullptr);
    CHECK_EQ(rv, 0);
}

// ---------------------------------------------------------------------------
// action_target_pickn — config-record builder + dialogs
// ---------------------------------------------------------------------------
TEST(GuiP6ActionPick, BuildTargetPickConfig) {
    TargetPickConfig c = ActionDialog_BuildTargetPickConfig(40, 0xABCD);
    CHECK_EQ(c.overlayColor, 40);
    CHECK_EQ(c.flag, 1024);
    CHECK_EQ(c.payload, 0xABCD);
    CHECK_EQ((int)c.kind, 6);
}

TEST(GuiP6ActionPick, BeginAbductTargetPickPacksRecord) {
    ResetActionTargetPick();
    static TargetPickConfig seen; std::memset(&seen, 0, sizeof(seen));
    static int gotCfg; gotCfg = 0;
    ActionTargetPickHooks h = *ActionTargetPickHooks_Default();
    h.amtRunOfficeOverviewWindow = [](const TargetPickConfig* cfg, const void*, const void*) {
        if (cfg) { seen = *cfg; gotCfg = 1; }
        return 42;
    };
    SetActionTargetPickHooks(&h);
    int rv = ActionDialog_BeginAbductTargetPick(0x55);
    SetActionTargetPickHooks(nullptr);
    CHECK_EQ(rv, 42);
    CHECK(gotCfg);
    CHECK_EQ(seen.overlayColor, 40);   // gray level 40 from the thunk
    CHECK_EQ(seen.flag, 1024);
    CHECK_EQ(seen.payload, 0x55);
    CHECK_EQ((int)seen.kind, 6);
}

TEST(GuiP6ActionPick, PromptTargetSelectFormatsAndScrolls) {
    ResetActionTargetPick();
    static int textId; textId = 0;
    static int scrollA, scrollB; scrollA = scrollB = -1;
    static TargetPickConfig seen; std::memset(&seen, 0, sizeof(seen));
    ActionTargetPickHooks h = *ActionTargetPickHooks_Default();
    h.textRenderFormattedMessage = [](char* buf, int id){ textId = id; if (buf) buf[0]='\0'; };
    h.amtRunOfficeOverviewWindow = [](const TargetPickConfig* cfg, const void*, const void*){ if (cfg) seen=*cfg; return 0; };
    h.hudUpdateEdgeScroll = [](int, int b, int c){ scrollA = b; scrollB = c; };
    SetActionTargetPickHooks(&h);
    ActionDialog_PromptTargetSelect(0, 0, 11, 22);
    SetActionTargetPickHooks(nullptr);
    CHECK_EQ(textId, 4962);
    CHECK_EQ(seen.flag, 1024);
    CHECK_EQ((int)seen.kind, 6);
    CHECK_EQ(seen.payload, 0);
    CHECK_EQ(scrollA, 11);
    CHECK_EQ(scrollB, 22);
}
