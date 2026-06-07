// Integration tests: the modal message-box family against its REAL siblings —
// window.cpp (Window_Create / Object_AddToWindow), radiogroup.cpp
// (RadioGroup_Create / AddButton / Selection_Update), dialog.cpp
// (Dialog_BuildButtonGroup) and the Form/Window/Widget data model.
//
// Only the OS/cross-cluster boundary (form load, text render, frame loop) is mocked;
// everything below MessageBox_* runs the genuine translated code, so we verify that the
// box really builds a radio group from the window's children and that the group's live
// selection (dword_676588[group]) drives the returned value (selection + 1).

#include "test.h"

#include "gui/message_box.h"
#include "gui/window.h"
#include "gui/radiogroup.h"
#include "gui/object.h"
#include "gui/input.h"
#include "gui/dialog.h"

#include <vector>

using namespace guild::gui;

using FrameInput = MessageBoxHost::FrameInput;

namespace {

struct CapturingHost : MessageBoxHost {
    std::vector<FrameInput> frames;
    size_t i = 0;
    int formId = 3;
    int LoadForm(const char*, bool) override { return formId; }
    bool RunFrame(int, guild::i32, unsigned, FrameInput& in) override {
        if (i >= frames.size()) return false;
        in = frames[i++];
        return true;
    }
};

// Real window with `n` clickable buttons (genuine Object_AddToWindow children).
int MakeRealButtonWindow(int n) {
    ResetWindows();
    ResetRadioGroups();
    ResetInputState();
    int slot = Window_Create(0, 0, 320, 200, 0);
    for (int k = 0; k < n; ++k) {
        int w = Object_AddToWindow(slot, 16, 16, 0);
        CHECK(w >= 0);
        g_widgets[w].type() = kTypeLabel; // non-'@' so Dialog_BuildButtonGroup adds it
        g_widgets[w].btnFlagA() = 1;      // clickable
    }
    return slot;
}

} // namespace

// The box builds a REAL radio group whose button count equals the window's non-'@'
// children, and OK returns (live selection + 1).  Default selection is button 0 -> 1.
TEST(GuiMessageBoxItest, RealGroupBuiltFromChildren) {
    int slot = MakeRealButtonWindow(4);

    CapturingHost h;
    FrameInput ok{}; ok.clickedId = kIdOk; ok.clickedWindow = slot;
    h.frames.push_back(ok);

    int before = 0;
    for (auto& g : g_radioGroups) (void)g; // ensure array visible
    (void)before;

    int r = MessageBox_Show(h, 0, 0, 1);
    CHECK_EQ(r, 1);  // selection 0 + 1

    // Exactly one group was created with 4 buttons (the backing '@' was skipped).
    int found = -1;
    for (int g = 0; g < (int)(sizeof(g_radioGroups) / sizeof(g_radioGroups[0])); ++g) {
        if (g_radioGroups[g].count == 4) { found = g; break; }
    }
    CHECK(found >= 0);
}

// If a later button is pre-selected via the REAL Selection_Update, OK reflects it.
// We script two frames: frame 1 selects button 2 in the group; frame 2 presses OK.
TEST(GuiMessageBoxItest, SelectionDrivesResult) {
    int slot = MakeRealButtonWindow(3);

    // First, run a throwaway box just to learn which group id gets allocated next.
    // Simpler: drive selection mid-loop via a host that mutates the group on frame 1.
    struct SelHost : MessageBoxHost {
        int slot_;
        int frame = 0;
        explicit SelHost(int s) : slot_(s) {}
        int LoadForm(const char*, bool) override { return 5; }
        bool RunFrame(int, guild::i32, unsigned, FrameInput& in) override {
            if (frame == 0) {
                // Find the group the box just built (count == 3) and select button 2.
                for (int g = 0; g < (int)(sizeof(g_radioGroups)/sizeof(g_radioGroups[0])); ++g)
                    if (g_radioGroups[g].count == 3) { Selection_Update(g, 2); break; }
                in = FrameInput{};            // no click this frame
                in.clickedWindow = slot_;
                ++frame;
                return true;
            }
            FrameInput ok{}; ok.clickedId = kIdOk; ok.clickedWindow = slot_;
            in = ok; ++frame;
            return frame <= 2; // one OK frame, then stop
        }
    } host(slot);

    int r = MessageBox_Show(host, 0, 0, 1);
    CHECK_EQ(r, 3);  // selection 2 + 1
}

// The reentrancy guard composes with the real build: a blocked call leaves the radio
// table untouched.
TEST(GuiMessageBoxItest, GuardLeavesGroupsUntouched) {
    MakeRealButtonWindow(2);
    g_msgGuardNormal = 1;
    CapturingHost h;
    int r = MessageBox_Show(h, 0, 0, 1);
    g_msgGuardNormal = 0;
    CHECK_EQ(r, 0);
    // No group with count 2 should have been created.
    bool any = false;
    for (auto& g : g_radioGroups) if (g.count == 2) any = true;
    CHECK(!any);
}

// Big and normal use independent guards: a normal box open does not block a Big box.
TEST(GuiMessageBoxItest, IndependentGuards) {
    int slot = MakeRealButtonWindow(2);
    g_msgGuardNormal = 1; // pretend a normal box is open
    CapturingHost h;
    FrameInput ok{}; ok.clickedId = kIdOk; ok.clickedWindow = slot;
    h.frames.push_back(ok);
    int r = MessageBox_ShowBig(h, kMsgFlagBig, 0, 1); // Big has its own guard
    g_msgGuardNormal = 0;
    CHECK_EQ(r, 1);
    CHECK_EQ(g_msgGuardBig, (guild::u8)0);
}
