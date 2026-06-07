// Integration tests: the chronicle scroll-window builder driven by the REAL world
// chronicle data core (world::Chronicle::ScanNextForward + FormatDate +
// world::HistoryClassifyEntry), not a stand-in. Verifies the gui builder consumes the
// actual dated-event scan the original window-builder fed from.
#include "gui/chronicle_window.h"
#include "world/history_chronicle.h"
#include "world/history.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A host that scans the REAL chronicle forward from a moving "current day", emitting
// each "yesterday" entry exactly as VIBE_History_ScanNextEventForward drives the loop.
struct RealChronicleHost : ChronicleHost {
    const guild::world::Chronicle* chron = nullptr;
    guild::i32 day = 0;   // the current game day; advances as entries are consumed
    int pendingSignals = 0;

    int ScanNextEvent(bool /*real*/, ChronicleEvent* out) override {
        // Walk forward looking for the entry dated to (day - 1) via the real classifier.
        for (int probe = 0; probe < 4000; ++probe) {
            int idx = chron->ScanNextForward(day);
            if (idx < 0) {
                // No "yesterday" entry at this day; advance one day until we pass the log.
                if (day > LastDay() + 1)
                    return 4; // end of chronicle
                ++day;
                continue;
            }
            const auto& e = chron->At(idx);
            char buf[16];
            char* d = chron->FormatDate(idx, buf);
            out->title = d;                 // the real "DD.MM.YYYY" label
            out->body = e.text ? e.text : "";
            out->kind = out->body.empty() ? 6 : 1;
            ++day;                          // move on so the next scan finds the next entry
            return 1;
        }
        return 4;
    }
    bool PageSignal() override {
        if (pendingSignals > 0) { --pendingSignals; return true; }
        return false;
    }
    guild::i32 LastDay() const {
        guild::i32 last = 0;
        for (int i = 0; i < chron->Count(); ++i)
            if (chron->At(i).day > last) last = chron->At(i).day;
        return last;
    }
};

} // namespace

TEST(GuiChronicleItest, ScansRealChronicleForward) {
    guild::world::Chronicle chron;
    guild::world::ChronicleEntry e1{};
    e1.day = 10; e1.month = 3; e1.year = 1400; e1.textId = 7313; e1.text = "An arrest was made.";
    guild::world::ChronicleEntry e2{};
    e2.day = 12; e2.month = 3; e2.year = 1400; e2.textId = 7316; e2.text = "A relic was used.";
    chron.Add(e1);
    chron.Add(e2);

    RealChronicleHost host;
    host.chron = &chron;
    host.day = 11;            // "yesterday" == day 10 -> first scan emits e1
    host.pendingSignals = 8;

    ChronicleScrollPlan plan = Chronicle_BuildScrollPlan(host, /*real=*/false);
    CHECK(plan.opened);
    CHECK((int)plan.pages.size() >= 2); // both events surface across page advances

    // The first page body wraps the real entry text with "$C%s".
    CHECK(plan.pages[0].bodyText == "$CAn arrest was made.");
    // The title wraps the REAL FormatDate label "10.03.1400" with "$C$Z$[%s$]".
    CHECK(plan.pages[0].titleText == "$C$Z$[10.03.1400$]");
    // The second emitted event is e2 (dated 12.03.1400).
    bool sawSecond = false;
    for (const auto& p : plan.pages)
        if (p.titleText == "$C$Z$[12.03.1400$]") sawSecond = true;
    CHECK(sawSecond);
}

TEST(GuiChronicleItest, EmptyChronicleBails) {
    guild::world::Chronicle chron; // no entries
    RealChronicleHost host;
    host.chron = &chron;
    host.day = 5;
    host.pendingSignals = 4;
    ChronicleScrollPlan plan = Chronicle_BuildScrollPlan(host, false);
    CHECK(!plan.opened);          // first scan never returns 1 -> early bail
    CHECK(plan.pages.empty());
}
