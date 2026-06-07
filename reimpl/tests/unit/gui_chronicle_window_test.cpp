// Unit tests for the chronicle / history-event scroll window builder.
//   gilde.exe 0x4fed20 VIBE_History_DisplayCurrentEvent (dispatch core)
//   gilde.exe 0x4fe74c VIBE_History_ShowEventScrollForward / 0x4fea88 ..Real (build)
// Golden vectors: the markup-format expansions and the dispatch/page-advance rules.
#include "gui/chronicle_window.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A scripted host: emits a fixed list of events, returns the page-signal a fixed
// number of times. Models ScanNextEventReal/Forward + dword_75BF38.
struct ScriptHost : ChronicleHost {
    std::vector<ChronicleEvent> events;
    size_t idx = 0;
    int signals = 0;     // how many more times PageSignal() returns true
    bool lastReal = false;

    int ScanNextEvent(bool real, ChronicleEvent* out) override {
        lastReal = real;
        if (idx >= events.size())
            return 4; // end-of-chronicle (not 1) -> stop
        *out = events[idx++];
        return out->kind; // the event carries its own scan-result code
    }
    bool PageSignal() override {
        if (signals > 0) { --signals; return true; }
        return false;
    }
};

ChronicleEvent Ev(const std::string& title, const std::string& body, int kind = 1) {
    ChronicleEvent e{};
    e.title = title;
    e.body = body;
    e.kind = kind;
    return e;
}

} // namespace

// --- dispatch core (0x4fed20) ---------------------------------------------
TEST(GuiChronicle, DispatchMode) {
    // available && mode 1 -> forward; mode 2 -> real; else idle.
    CHECK_EQ(Chronicle_DispatchMode(true, kChronicleModeForward), kChronicleModeForward);
    CHECK_EQ(Chronicle_DispatchMode(true, kChronicleModeReal), kChronicleModeReal);
    CHECK_EQ(Chronicle_DispatchMode(true, kChronicleModeIdle), kChronicleModeIdle);
    CHECK_EQ(Chronicle_DispatchMode(true, 99), kChronicleModeIdle);   // unknown mode -> idle
    // not available -> always idle (the !dword_764CF0 gate).
    CHECK_EQ(Chronicle_DispatchMode(false, kChronicleModeForward), kChronicleModeIdle);
    CHECK_EQ(Chronicle_DispatchMode(false, kChronicleModeReal), kChronicleModeIdle);
}

// --- per-event format (the three RenderRichString markup expansions) -------
TEST(GuiChronicle, FormatPageMarkup) {
    ChroniclePage p = Chronicle_FormatPage(Ev("01.01.1400", "A guild was founded."));
    // body uses "$C%s"; title uses "$C$Z$[%s$]".
    CHECK(p.bodyText == "$CA guild was founded.");
    CHECK(p.titleText == "$C$Z$[01.01.1400$]");
    CHECK_EQ(p.bodySlot, kChronicleBodySlot);
    CHECK_EQ(p.headerSlot, kChronicleHeaderSlot);
    CHECK_EQ(p.titleSlot, kChronicleTitleSlot);
    CHECK_EQ(p.headerId, kChronicleHeaderText);  // 3347
    CHECK_EQ(p.scrollBtns, kChronicleScrollBtns); // 1753
    CHECK_EQ(p.winFlag, kChronicleWinFlag);       // 24
}

TEST(GuiChronicle, FormatPageEmptyBody) {
    // an empty body still expands cleanly (the kind-6 "empty body" scan case).
    ChroniclePage p = Chronicle_FormatPage(Ev("Title", "", 6));
    CHECK(p.bodyText == "$C");
    CHECK(p.titleText == "$C$Z$[Title$]");
}

// --- build / page-advance (0x4fe74c / 0x4fea88) ----------------------------
TEST(GuiChronicle, BailWhenFirstScanMisses) {
    ScriptHost host; // no events -> first scan returns 4 (!= 1)
    ChronicleScrollPlan plan = Chronicle_BuildScrollPlan(host, /*real=*/false);
    CHECK(!plan.opened);              // early `return result;`
    CHECK(plan.pages.empty());
}

TEST(GuiChronicle, OpensAndRendersFirstPage) {
    ScriptHost host;
    host.events.push_back(Ev("D1", "Body1"));
    host.signals = 0;                 // never advance
    ChronicleScrollPlan plan = Chronicle_BuildScrollPlan(host, /*real=*/true);
    CHECK(plan.opened);
    CHECK_EQ((int)plan.pages.size(), 1);
    CHECK(plan.pages[0].bodyText == "$CBody1");
    CHECK(plan.pages[0].titleText == "$C$Z$[D1$]");
    CHECK(host.lastReal);             // real=true selected ScanNextEventReal
    CHECK_EQ(plan.loopForm, kChronicleLoopForm); // 417927
}

TEST(GuiChronicle, AdvancesPagesOnSignal) {
    ScriptHost host;
    host.events.push_back(Ev("D1", "B1"));
    host.events.push_back(Ev("D2", "B2"));
    host.events.push_back(Ev("D3", "B3"));
    host.signals = 5;                 // signal more than there are events
    ChronicleScrollPlan plan = Chronicle_BuildScrollPlan(host, /*real=*/false);
    CHECK(plan.opened);
    // page0 + 2 advances, then the 3rd advance scan misses (end) -> loop breaks.
    CHECK_EQ((int)plan.pages.size(), 3);
    CHECK(plan.pages[0].bodyText == "$CB1");
    CHECK(plan.pages[1].bodyText == "$CB2");
    CHECK(plan.pages[2].bodyText == "$CB3");
}

TEST(GuiChronicle, MaxPagesBound) {
    ScriptHost host;
    for (int i = 0; i < 50; ++i) host.events.push_back(Ev("D", "B"));
    host.signals = 1000;
    ChronicleScrollPlan plan = Chronicle_BuildScrollPlan(host, false, /*maxPages=*/4);
    CHECK(plan.opened);
    // page0 + at most 4 advances.
    CHECK((int)plan.pages.size() <= 5);
}
