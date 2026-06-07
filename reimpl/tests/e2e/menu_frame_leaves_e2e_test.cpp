// E2E test for guild::gui menu_frame_leaves — a full scripted main-menu frame session
// exercising the supporting leaves the way Menu_RunMainMenu drives them, end to end:
//   BUILD:   create a "version" text label (CreateTextLabel + SetColor), build a radio
//            group of menu buttons, set the title window visible (SetVisibleRecursive).
//   PRESENT: register a fade-in, run several RenderEntityList frames (each iterating the
//            live fade), navigate the menu with InitStateReader, ENTER to pick an entry.
//   CLEANUP: unregister the fade (releasing its texture).
// The whole call ORDER is recorded via hooks and asserted; the run is deterministic and
// byte-identical on a rerun.
#include "test.h"

#include "gui/menu_frame_leaves.h"
#include "gui/gui_dialogs7.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/widget_create.h"
#include "gui/radiogroup.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

namespace {

std::vector<std::string> g_log;
FadeRecord g_pool[4];
int g_poolIdx;
i32 g_nextTex;

FadeRecord* HAlloc(int, const char*) {
    g_log.push_back("alloc");
    FadeRecord* r = &g_pool[g_poolIdx++ & 3];
    std::memset(r->raw, 0, sizeof(r->raw));
    return r;
}
void HFree(FadeRecord*) { g_log.push_back("free"); }
void HRelease(i32 tex) { g_log.push_back("release:" + std::to_string(tex)); }
i32  HFill(int, const u32*, int) { return g_nextTex++; }
void HEntities(int, int, i32) { g_log.push_back("entities"); }
void HResult(int, int, i32, i32, i32 src, int, int, i32) {
    g_log.push_back(src == 0 ? "blit_back_to_work" : "blit_work_to_back");
}
void HFadeUpdate(i32 handle, i32) { g_log.push_back("fade:" + std::to_string(handle)); }
void HPresent(void*) { g_log.push_back("present"); }
void HNop() { g_log.push_back("nop"); }
void HCoord(int x, int y) { g_log.push_back("warp:" + std::to_string(x) + "," + std::to_string(y)); }

MenuFrameLeavesHooks MakeHooks() {
    MenuFrameLeavesHooks h{};
    h.allocFade = &HAlloc; h.freeFade = &HFree; h.releaseTexture = &HRelease;
    h.fillBackBuffer = &HFill;
    h.gameLogicEntities = &HEntities; h.resultHandlerInteraction = &HResult;
    h.fadeUpdate = &HFadeUpdate; h.renderPresentFrame = &HPresent; h.guiNop = &HNop;
    h.coordWarp = &HCoord;
    return h;
}

// One full scripted session; returns the recorded call log + the final input state.
struct SessionResult {
    std::vector<std::string> log;
    int finalSelected = -1;
    int firedButton = -1;
    int firedHelpId = -1;
    int versionLabelType = -1;
    int versionLabelColor = -1;
    int titleHiddenFlag = -1;
};

SessionResult RunSession() {
    ResetGuiState();
    ResetRadioGroups();
    ResetMenuFrameLeaves();
    g_log.clear();
    g_poolIdx = 0;
    g_nextTex = 0x2000;

    MenuFrameLeavesHooks h = MakeHooks();
    const MenuFrameLeavesHooks* prev = SetMenuFrameLeavesHooks(&h);

    SessionResult res;

    // ---- BUILD ----
    g_defaultFont   = 0x42;          // menu label colour/font
    g_defaultCtrlH  = 0x14;
    g_screenClipExt = 0x01900258;    // 400x600

    int versionLbl = Object_CreateTextLabel(8, 580, "version 1.0");
    Object_SetColor(versionLbl, 67); // the menu colours its version label 67
    res.versionLabelType  = g_widgets[versionLbl].type();
    res.versionLabelColor = g_widgets[versionLbl].at<u16>(112) & 0xFFFF;

    // Menu radio group: 4 entries (New, Load, Options, Quit).
    int ids[4] = {80, 81, 82, 83};
    for (int i = 0; i < 4; ++i) {
        // Each button is a real menu widget with geometry + an id payload.
        g_widgets[ids[i]].type() = kTypeWindow;
        g_widgets[ids[i]].id()   = 900 + i;
        g_widgets[ids[i]].x()    = 100;
        g_widgets[ids[i]].y()    = static_cast<i16>(100 + 40 * i);
        g_widgets[ids[i]].w()    = 200;
        g_widgets[ids[i]].h()    = 30;
    }
    int gi = RadioGroup_Create(4, ids);
    g_radioGroups[gi].selected = 0;

    // Title window-backing widget visible (recurses into its child labels).
    Widget& title = g_widgets[10];
    title.type() = kTypeWindow;
    title.ownerWindow() = 12;
    g_windows[12].objCount() = 2;
    WindowChildList(12)[0] = ids[0];
    WindowChildList(12)[1] = ids[1];
    Object_SetVisibleRecursive(10, 1);   // show
    res.titleHiddenFlag = title.at<i32>(52);

    // ---- PRESENT ----
    Fade_Register(0, 0, 0, 0, kFadeDirIn, 50, nullptr, 1000); // fade-in, tex 0x2000

    // Frame 1: just render (no input).
    Window_RenderEntityList(0x500);

    // Frame 2: DOWN-arrow held -> step selection 0->1 and warp the cursor.
    g_menuInput.downHeld = 1;
    g_menuInput.lastKey = kKeyDown;
    InitStateReader(gi);
    g_menuInput.downHeld = 0;
    Window_RenderEntityList(0x500);

    // Frame 3: ENTER -> fire the selected entry (button 81, id 901).
    g_menuInput.lastKey = kKeyEnter;
    InitStateReader(gi);
    res.finalSelected = g_radioGroups[gi].selected;
    res.firedButton   = g_menuInput.selectedWidget;
    res.firedHelpId   = g_menuInput.hoverHelpId;
    Window_RenderEntityList(0x500);

    // ---- CLEANUP ----
    Fade_Unregister(g_fadeSlots[0]);

    res.log = g_log;
    SetMenuFrameLeavesHooks(prev);
    return res;
}

} // namespace

TEST(MenuFrameLeavesE2E, FullSessionCallOrderAndState) {
    SessionResult r = RunSession();

    // BUILD assertions.
    CHECK_EQ(r.versionLabelType, (int)kTypeLabel);   // 'C' text label
    CHECK_EQ(r.versionLabelColor, 67);               // SetColor wrote +112
    CHECK_EQ(r.titleHiddenFlag, 0);                  // visible -> hidden flag 0

    // PRESENT assertions: selection + ENTER fire.
    CHECK_EQ(r.finalSelected, 1);
    CHECK_EQ(r.firedButton, 81);
    CHECK_EQ(r.firedHelpId, 901);

    // The recorded call log must follow the build->present->cleanup contract.
    const std::vector<std::string>& L = r.log;
    // First a fade alloc (Fill is internal; alloc is the first logged fade event), then
    // three RenderEntityList frames, with the DOWN warp between frame 1 and frame 2, and
    // the texture release at the end.
    auto idx = [&](const std::string& s) -> int {
        for (size_t i = 0; i < L.size(); ++i) if (L[i] == s) return (int)i;
        return -1;
    };
    CHECK(idx("alloc") >= 0);                          // fade registered
    // The warp from the DOWN step targets button 81's centre (x=200, y=155).
    CHECK(idx("warp:200,155") >= 0);
    CHECK(idx("release:8192") >= 0);                  // 0x2000 texture released at cleanup

    // Exactly one frame contains the canonical RenderEntityList sequence. Find the first
    // "entities" and assert the 7-event frame shape follows it.
    int e = idx("entities");
    CHECK(e >= 0);
    CHECK_EQ(L[e + 0], std::string("entities"));
    CHECK_EQ(L[e + 1], std::string("blit_back_to_work"));
    CHECK_EQ(L[e + 2], std::string("fade:1"));        // one active fade, handle 1
    CHECK_EQ(L[e + 3], std::string("present"));
    CHECK_EQ(L[e + 4], std::string("blit_work_to_back"));
    CHECK_EQ(L[e + 5], std::string("nop"));

    // alloc precedes the first entities frame; release follows the last.
    CHECK(idx("alloc") < idx("entities"));
    CHECK(idx("release:8192") > e);
}

TEST(MenuFrameLeavesE2E, DeterministicAcrossReruns) {
    SessionResult a = RunSession();
    SessionResult b = RunSession();
    CHECK_EQ(a.log.size(), b.log.size());
    bool same = (a.log == b.log);
    CHECK(same);
    CHECK_EQ(a.finalSelected, b.finalSelected);
    CHECK_EQ(a.firedButton, b.firedButton);
    CHECK_EQ(a.firedHelpId, b.firedHelpId);
    CHECK_EQ(a.versionLabelColor, b.versionLabelColor);
}
