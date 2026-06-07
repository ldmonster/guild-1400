// Integration tests for guild::gui menu_frame_leaves — compose the leaves into the
// small pipelines the main menu actually runs:
//   (1) Fade register/iterate/unregister across a RenderEntityList frame.
//   (2) A radio group navigated by InitStateReader, driving the REAL Selection_Update
//       (radiogroup.cpp) which mutates the button widgets' value words.
// Deterministic; host edges recorded via hooks.
#include "test.h"

#include "gui/menu_frame_leaves.h"
#include "gui/gui_dialogs7.h"   // g_fadeSlots
#include "gui/object.h"         // g_widgets, ResetGuiState
#include "gui/window.h"
#include "gui/widget_create.h"
#include "gui/radiogroup.h"     // g_radioGroups, ResetRadioGroups, Selection_Update

#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {

// Fade-record store the hooks hand out.
FadeRecord g_pool[8];
int g_poolIdx;
int g_fadeUpdated[8];   // fade handles passed to fadeUpdate, in order
int g_fadeUpdN;
int g_releasedTex[8];
int g_releasedN;

FadeRecord* HAlloc(int, const char*) {
    FadeRecord* r = &g_pool[g_poolIdx++ & 7];
    std::memset(r->raw, 0, sizeof(r->raw));
    return r;
}
void HFree(FadeRecord*) {}
void HRelease(i32 tex) { if (g_releasedN < 8) g_releasedTex[g_releasedN++] = tex; }
i32  g_nextTex = 0x1000;
i32  HFill(int, const u32*, int) { return g_nextTex++; }
void HFadeUpdate(i32 handle, i32) { if (g_fadeUpdN < 8) g_fadeUpdated[g_fadeUpdN++] = handle; }
void HEntities(int, int, i32) {}
void HResult(int, int, i32, i32, i32, int, int, i32) {}
void HPresent(void*) {}
void HNop() {}
void HCoord(int, int) {}

MenuFrameLeavesHooks MakeHooks() {
    MenuFrameLeavesHooks h{};
    h.allocFade = &HAlloc;
    h.freeFade = &HFree;
    h.releaseTexture = &HRelease;
    h.fillBackBuffer = &HFill;
    h.gameLogicEntities = &HEntities;
    h.resultHandlerInteraction = &HResult;
    h.fadeUpdate = &HFadeUpdate;
    h.renderPresentFrame = &HPresent;
    h.guiNop = &HNop;
    h.coordWarp = &HCoord;
    return h;
}

void Reset() {
    ResetGuiState();
    ResetRadioGroups();
    ResetMenuFrameLeaves();
    g_poolIdx = 0; g_fadeUpdN = 0; g_releasedN = 0; g_nextTex = 0x1000;
    SetMenuFrameLeavesHooks(nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Fade lifecycle across a frame: register 3 fades, RenderEntityList iterates
//     exactly the active ones, then unregister each (releasing its texture).
// ---------------------------------------------------------------------------
TEST(MenuFrameLeavesIT, FadeFramePipeline) {
    Reset();
    MenuFrameLeavesHooks h = MakeHooks();
    SetMenuFrameLeavesHooks(&h);

    i32 f0 = Fade_Register(0, 0, 0, 0, kFadeDirIn, 50, nullptr, 100);   // tex 0x1000
    i32 f1 = Fade_Register(0, 0, 0, 0, kFadeDirOut, 60, nullptr, 100);  // tex 0x1001
    i32 f2 = Fade_Register(0, 0, 0, 0, kFadeDirIn, 70, nullptr, 100);   // tex 0x1002
    CHECK(f0 && f1 && f2);

    // A frame: RenderEntityList must call fadeUpdate for each active slot in index order.
    Window_RenderEntityList(0x123);
    CHECK_EQ(g_fadeUpdN, 3);
    CHECK_EQ(g_fadeUpdated[0], f0);
    CHECK_EQ(g_fadeUpdated[1], f1);
    CHECK_EQ(g_fadeUpdated[2], f2);

    // Tear down the middle fade first: its texture (0x1001) is released.
    CHECK_EQ(Fade_Unregister(f1), 0);
    CHECK_EQ(g_releasedN, 1);
    CHECK_EQ(g_releasedTex[0], 0x1001);
    CHECK_EQ(g_fadeSlots[1], 0);

    // Next frame only iterates the two survivors (slot1 is the hole, skipped).
    g_fadeUpdN = 0;
    Window_RenderEntityList(0x123);
    CHECK_EQ(g_fadeUpdN, 2);
    CHECK_EQ(g_fadeUpdated[0], f0);
    CHECK_EQ(g_fadeUpdated[1], f2);

    CHECK_EQ(Fade_Unregister(f0), 0);
    CHECK_EQ(Fade_Unregister(f2), 0);
    CHECK_EQ(g_releasedN, 3);
    SetMenuFrameLeavesHooks(nullptr);
}

// ---------------------------------------------------------------------------
// (2) Radio navigation pipeline: a 4-button group, repeatedly stepped by
//     InitStateReader; the REAL Selection_Update mutates the button widgets'
//     value(+36)/mirror(+40) words (mutual exclusion). Assert the live widget state
//     after each step matches the selection, deterministically.
// ---------------------------------------------------------------------------
TEST(MenuFrameLeavesIT, RadioNavigationDrivesSelectionUpdate) {
    Reset();
    MenuFrameLeavesHooks h = MakeHooks();
    SetMenuFrameLeavesHooks(&h);

    int ids[4] = {60, 61, 62, 63};
    // Use RadioGroup_Create so the buttons get btnFlagA set (so Selection_Update's
    // Object_SetButtonValue actually writes value/mirror).
    int gi = RadioGroup_Create(4, ids);
    CHECK(gi >= 0);
    CHECK_EQ(g_radioGroups[gi].count, 4);
    g_radioGroups[gi].selected = 0;
    // Seed widget value words so we can see them change.
    for (int id : ids) { g_widgets[id].value() = -1; g_widgets[id].type() = kTypeWindow; }

    auto checkSelected = [&](int sel) {
        for (int i = 0; i < 4; ++i) {
            int want = (i == sel) ? 1 : 0;
            CHECK_EQ((int)(unsigned char)g_widgets[ids[i]].value(), want);
            CHECK_EQ((int)(unsigned char)g_widgets[ids[i]].valueMirror(), want);
        }
    };

    // DOWN x3: 0 -> 1 -> 2 -> 3
    for (int step = 1; step <= 3; ++step) {
        g_menuInput.lastKey = kKeyDown;
        InitStateReader(gi);
        CHECK_EQ(g_radioGroups[gi].selected, step);
        checkSelected(step);
    }
    // One more DOWN wraps 3 -> (3+1)%4 == 0
    g_menuInput.lastKey = kKeyDown;
    InitStateReader(gi);
    CHECK_EQ(g_radioGroups[gi].selected, 0);
    checkSelected(0);

    // UP wraps 0 -> last (3)
    g_menuInput.lastKey = kKeyUp;
    InitStateReader(gi);
    CHECK_EQ(g_radioGroups[gi].selected, 3);
    checkSelected(3);

    SetMenuFrameLeavesHooks(nullptr);
}

// ---------------------------------------------------------------------------
// (3) ENTER after navigation commits the click edge against the right button id.
// ---------------------------------------------------------------------------
TEST(MenuFrameLeavesIT, NavigateThenEnterFiresCorrectButton) {
    Reset();
    MenuFrameLeavesHooks h = MakeHooks();
    SetMenuFrameLeavesHooks(&h);

    int ids[3] = {70, 71, 72};
    int gi = RadioGroup_Create(3, ids);
    g_radioGroups[gi].selected = 0;
    for (int i = 0; i < 3; ++i) { g_widgets[ids[i]].type() = kTypeWindow; g_widgets[ids[i]].id() = 800 + i; }

    g_menuInput.lastKey = kKeyDown; InitStateReader(gi); // -> sel 1
    CHECK_EQ(g_radioGroups[gi].selected, 1);

    g_menuInput.lastKey = kKeyEnter; InitStateReader(gi); // fire button 71
    CHECK_EQ(g_menuInput.clickFlag, 1);
    CHECK_EQ(g_menuInput.selectedWidget, 71);
    CHECK_EQ(g_menuInput.hoverHelpId, 801);
    CHECK_EQ(g_menuInput.lastKey, 0);
    SetMenuFrameLeavesHooks(nullptr);
}
