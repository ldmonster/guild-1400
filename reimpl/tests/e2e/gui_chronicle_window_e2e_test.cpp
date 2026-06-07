// End-to-end flow for the chronicle scroll window: dispatch -> build a multi-page
// scroll session over the real world chronicle -> verify the page schedule.
//
//   * Headless path: a full dispatch+build flow over a synthetic real chronicle
//     (always runs).
//   * Real-asset path (GUARDED on GUILD_GAME_DIR): would load the shipped chronicle
//     file; skip-passes cleanly when the env var is unset (no game install in CI).
#include "gui/chronicle_window.h"
#include "world/history_chronicle.h"
#include "world/history.h"
#include "tests/framework/test.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace guild::gui;

namespace {

struct FlowHost : ChronicleHost {
    const guild::world::Chronicle* chron = nullptr;
    int cursor = 0;
    int pending = 0;
    int ScanNextEvent(bool /*real*/, ChronicleEvent* out) override {
        if (cursor >= chron->Count()) return 4;
        const auto& e = chron->At(cursor);
        char buf[16];
        out->title = chron->FormatDate(cursor, buf);
        out->body = e.text ? e.text : "";
        out->kind = out->body.empty() ? 6 : 1;
        ++cursor;
        return 1;
    }
    bool PageSignal() override {
        if (pending > 0) { --pending; return true; }
        return false;
    }
};

} // namespace

TEST(GuiChronicleE2E, FullDispatchAndScrollFlow) {
    // Build a small real chronicle.
    guild::world::Chronicle chron;
    for (int i = 0; i < 4; ++i) {
        guild::world::ChronicleEntry e{};
        e.day = 100 + i * 2; e.month = 6; e.year = 1402;
        e.textId = 7319 + i;
        e.text = (i == 2) ? "" : "An event occurred."; // one empty-body event (kind 6)
        chron.Add(e);
    }

    // Dispatch decides which builder to run; mode "forward" with availability.
    int mode = Chronicle_DispatchMode(/*available=*/true, kChronicleModeForward);
    CHECK_EQ(mode, kChronicleModeForward);

    // Build the scroll session, advancing through every page.
    FlowHost host;
    host.chron = &chron;
    host.pending = 10;
    bool real = (mode == kChronicleModeReal);
    ChronicleScrollPlan plan = Chronicle_BuildScrollPlan(host, real);

    CHECK(plan.opened);
    CHECK_EQ((int)plan.pages.size(), 4); // all four entries surfaced
    // Every page carries the recovered slot ids + scroll-button id + window flag.
    for (const auto& p : plan.pages) {
        CHECK_EQ(p.headerId, kChronicleHeaderText);  // 3347
        CHECK_EQ(p.scrollBtns, kChronicleScrollBtns); // 1753
        CHECK_EQ(p.winFlag, kChronicleWinFlag);       // 24
        CHECK(p.bodyText.rfind("$C", 0) == 0);        // body starts with the "$C" token
        CHECK(p.titleText.rfind("$C$Z$[", 0) == 0);   // title boxed markup
    }
    // The empty-body event (index 2) renders just the "$C" prefix.
    CHECK(plan.pages[2].bodyText == "$C");
    CHECK_EQ(plan.loopForm, kChronicleLoopForm);     // RunFrameLoop 417927
}

TEST(GuiChronicleE2E, RealAssetGuarded) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir) {
        // No real "Die Gilde" install available — clean skip-pass.
        CHECK(true);
        return;
    }
    // With a real install the shipped chronicle file would be loaded and scanned;
    // we only assert the dispatch gate accepts the forward mode here (the file loader
    // is an engine leaf outside this module's scope).
    CHECK_EQ(Chronicle_DispatchMode(true, kChronicleModeForward), kChronicleModeForward);
}
