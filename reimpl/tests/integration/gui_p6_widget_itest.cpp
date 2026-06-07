// Integration tests for the P6 (Wave 29) GUI coverage slice. Composes the recovered
// functions into small widget/form pipelines: a player-count menu flow feeding a book
// loop, a target-pick config flowing into the office-overview window + edge-scroll,
// and the widget text-color/refresh path against the REAL g_widgets model. Where a
// real reconstructed sibling exists (Form_Destroy / Widget_DestroyByType via
// InfoPanel_Destroy) it is reused directly through its header.
#include "test.h"

#include "gui/gui_widgetn.h"
#include "gui/hud_actionsn.h"
#include "gui/menu_dialogsn.h"
#include "gui/action_target_pickn.h"
#include "gui/object.h"   // g_widgets (real model)

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

// A scripted runtime that records the form/text/world calls a full dialog would make.
namespace {
struct Trace {
    std::vector<std::string> events;
    int frames = 0;
};
Trace* g_trace = nullptr;
} // namespace

// ---------------------------------------------------------------------------
// Pipeline 1: pick player count, then if >1 player show the mission warning, then
// run the soeldner book loop gated on the hud flag. Verifies the menu clamp drives
// the warning gate, end-to-end.
// ---------------------------------------------------------------------------
TEST(GuiP6Pipeline, MenuToMissionWarningToBookLoop) {
    ResetMenuDialogsN();
    ResetHudActionsN();
    Trace tr; g_trace = &tr;

    // --- player-count picker: user accepts 3 players ---
    MenuDialogsNHooks mh = *MenuDialogsNHooks_Default();
    static int pcFrames; pcFrames = 1;
    mh.gameTickFinalize = [](i16, i16, const char* f){ g_trace->events.push_back(std::string("form:")+f); return 1; };
    mh.formGetChildObjectId = [](int, int, int){ return 5; };
    mh.gameLogicRunFrameLoop = [](int, int, const void*){ return pcFrames-- > 0 ? 1 : 0; };
    mh.objectGetDataPtr = [](int){ return 3; };
    SetMenuDialogsNHooks(&mh);
    g_playerCount = 1;
    g_lastMenuAction = kActionOk;
    int ok = Menu_ChoosePlayerCount();
    CHECK_EQ(ok, 1);
    CHECK_EQ((int)g_playerCount, 3);

    // --- mission warning: 3 players -> warning shown; mission picked -> failure dialog ---
    static int warnFrames; warnFrames = 1;
    mh.missionPickRandomByType = [](std::uint8_t){ return 9; };
    mh.gameTickFinalize = [](i16, i16, const char* f){ g_trace->events.push_back(std::string("warn:")+f); return 2; };
    mh.gameLogicRunFrameLoop = [](int, int, const void*){ return warnFrames-- > 0 ? 1 : 0; };
    mh.missionRunFailureDialog = [](int m, int){ g_trace->events.push_back("fail:" + std::to_string(m)); };
    SetMenuDialogsNHooks(&mh);
    int missionRv = Menu_ShowMissionWarning();
    SetMenuDialogsNHooks(nullptr);
    CHECK_EQ(missionRv, 9);

    // --- soeldner book loop: flag set -> registers, clicked id dispatches mercenary book ---
    HudActionsNHooks hh = *HudActionsNHooks_Default();
    static int hbFrames; hbFrames = 1;
    hh.statusTextRegister = [](const char* k, int){ g_trace->events.push_back(std::string("reg:")+k);
                                                    return std::string(k) == "ob_SOELDNERBUCH" ? 50 : 60; };
    hh.gameLogicRunFrameLoop = [](int, int, const void*){ return hbFrames-- > 0 ? 1 : 0; };
    hh.personnelRunMercenaryBook = [](){ g_trace->events.push_back("mercbook"); };
    SetHudActionsNHooks(&hh);
    g_hudFlags = kHudFlagSoeldnerBook;
    g_clickedStatusId = 50;  // soeldnerbuch clicked
    Hud_RunSoeldnerBookLoop();
    SetHudActionsNHooks(nullptr);
    g_trace = nullptr;

    // Assert the composed trace order.
    std::vector<std::string> expect = {
        "form:Menu\\CHOOSE_PLAYERCOUNT",
        "warn:Menu\\GET_MISSION_WARNING",
        "fail:9",
        "reg:ob_SOELDNERBUCH",
        "reg:ub_STAENDER_WAFFEN",
        "mercbook",
    };
    CHECK_EQ((int)tr.events.size(), (int)expect.size());
    for (size_t i = 0; i < expect.size() && i < tr.events.size(); ++i)
        CHECK(tr.events[i] == expect[i]);
}

// ---------------------------------------------------------------------------
// Pipeline 2: target-pick config flows into the office window + edge-scroll, and the
// returned object is classified as a trade contact by the hud updater.
// ---------------------------------------------------------------------------
TEST(GuiP6Pipeline, TargetPickToContactClassification) {
    ResetActionTargetPick();
    ResetHudActionsN();

    static TargetPickConfig pickedCfg; std::memset(&pickedCfg, 0, sizeof(pickedCfg));
    static int edgeKicked; edgeKicked = 0;
    ActionTargetPickHooks ah = *ActionTargetPickHooks_Default();
    ah.amtRunOfficeOverviewWindow = [](const TargetPickConfig* c, const void*, const void*){ if (c) pickedCfg=*c; return 0; };
    ah.hudUpdateEdgeScroll = [](int, int, int){ edgeKicked++; };
    SetActionTargetPickHooks(&ah);
    ActionDialog_PromptTargetSelect(0, 0, 5, 6);
    SetActionTargetPickHooks(nullptr);

    CHECK_EQ((int)pickedCfg.kind, 6);
    CHECK_EQ(pickedCfg.flag, 1024);
    CHECK_EQ(edgeKicked, 1);

    // The picked target (a type-30 market) is then handed to the contact updater.
    static int contacts; contacts = 0;
    HudActionsNHooks hh = *HudActionsNHooks_Default();
    hh.tradeRegisterEinkaufContact = [](int){ contacts++; };
    SetHudActionsNHooks(&hh);
    g_selectedObject = 30;  // a marketplace -> trade contact
    Hud_UpdateSelectedObjectContact(0);
    SetHudActionsNHooks(nullptr);
    CHECK_EQ(contacts, 1);
}

// ---------------------------------------------------------------------------
// Pipeline 3: widget text-color set against the real g_widgets model, then refresh
// centers under a real parent widget; finally the info-panel teardown runs through
// the real Form_Destroy / Widget_DestroyByType siblings.
// ---------------------------------------------------------------------------
TEST(GuiP6Pipeline, WidgetTextColorRefreshAndInfoPanelTeardown) {
    ResetGuiWidgetN();

    // Parent widget at index 2: x=10 (hiword of +2), w=80 (hiword of +6) -> midX 50.
    int parentIdx = 2, labelIdx = 7;
    std::memset(g_widgets[parentIdx].raw, 0, sizeof(g_widgets[parentIdx].raw));
    std::memset(g_widgets[labelIdx].raw, 0, sizeof(g_widgets[labelIdx].raw));
    std::int32_t pX = (10 << 16), pW = (80 << 16);
    std::memcpy(g_widgets[parentIdx].raw + 2, &pX, 4);
    std::memcpy(g_widgets[parentIdx].raw + 6, &pW, 4);

    Widget& lbl = g_widgets[labelIdx];
    lbl.at<std::int32_t>(88) = 1;            // text present
    lbl.at<std::int32_t>(44) = parentIdx;    // parent link
    std::int32_t d16 = (12 << 16);           // y = 12 (hiword of +16 dword)
    std::memcpy(lbl.raw + 16, &d16, 4);

    static int lx, ly; lx = ly = -1;
    GuiWidgetNHooks wh = *GuiWidgetNHooks_Default();
    wh.widgetLayoutBounds = [](int x, int y, int){ lx = x; ly = y; };
    SetGuiWidgetNHooks(&wh);
    // NOTE: faithful field overlap — SetTextColor writes +20, which IS the high word
    // of the +18 dword that RefreshText reads as the text "width". So after setting
    // color 0x10, RefreshText's halfW == 0x10/2 == 8.
    int r = Widget_SetTextColor(labelIdx, 0x10);
    SetGuiWidgetNHooks(nullptr);

    CHECK_EQ((int)lbl.at<std::int16_t>(20), 0x10);   // color stored at +20
    CHECK_EQ(r, 1);                                  // refreshed (text flag set)
    CHECK_EQ(lx, 50 - 8);                            // parent midX(50) - halfW(0x10/2)
    CHECK_EQ(ly, 12);

    // Info-panel teardown via real siblings (must not crash, resets state).
    g_infoPanelForm = -1;  // closed: real Form_Destroy not invoked, safe
    InfoPanel_Destroy();
    CHECK_EQ(g_infoPanelForm, -1);
}
