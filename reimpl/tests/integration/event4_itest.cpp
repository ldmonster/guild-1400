// Integration test: VIBE_Event_HarvestWageRun (0x4f3b34) wired against REAL
// reconstructed siblings — NOT mocks. The wage body makes two genuine
// cross-module calls the binary makes directly (no hook indirection):
//   * VIBE_GameTime_Compare(+82, qword_13CE852) (guild::sim::GameTimeCompare,
//     sim/gametime.cpp) gates whether the appointment is due, and
//   * VIBE_Math_RandomModulo(40*base) (guild::util::RandomModulo, which pulls the
//     real CRT LCG guild::crt::RandNext/Srand, crt/rand.cpp) draws the wage jitter.
// We seed the real LCG and the shared clock exactly as a live session would, run
// the body, and assert the wage it queued equals what the REAL siblings produce
// when replayed against the same seed. Only the entity-resolve / queue leaves
// (which cross into clusters this module does not own) are stubbed via hooks.
#include "test.h"

#include "world/event4.h"
#include "sim/he.h"
#include "sim/npcaction.h"       // NpcClock / SetNpcClock (shared game clock)
#include "sim/gametime.h"        // the REAL GameTimeCompare sibling
#include "util/math_random.h"    // the REAL RandomModulo (over the CRT LCG)
#include "crt/rand.h"            // the REAL CRT LCG

#include <cstring>
#include <vector>

using namespace guild;
using guild::world::Event4Hooks;
using guild::world::SetEvent4Hooks;
using guild::world::GetEvent4Hooks;
using guild::sim::HeRecord;
using guild::sim::HeBytes;
using guild::sim::GameTime;
using guild::sim::SetNpcClock;

namespace {
i32&      RD(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
GameTime& RT(HeRecord* h, int off) { return *reinterpret_cast<GameTime*>(HeBytes(h) + off); }
HeRecord MakeHe() { HeRecord h; std::memset(&h, 0, sizeof(h)); return h; }

struct Cap {
    std::vector<i32> wages;
    int frees = 0;
};
Cap* g_cap = nullptr;

u8 g_wageRec[600];
}  // namespace

TEST(Event4Itest, HarvestWageFromRealLcgAndRealGameTimeCompareSiblings) {
    Cap cap; g_cap = &cap;

    std::memset(g_wageRec, 0, sizeof(g_wageRec));
    const int base = 5;
    *reinterpret_cast<u16*>(g_wageRec + 54) = static_cast<u16>(base);

    SetEvent4Hooks(nullptr);
    Event4Hooks h = GetEvent4Hooks();
    h.resolveEntityById = [](void** a, i32* b, i32, i32) {
        if (a) *a = g_wageRec;
        if (b) *b = 0;
    };
    h.queueRequest17 = [](i32, i32, i32 qty, i32, i32, i32) -> i32 {
        if (g_cap) g_cap->wages.push_back(qty);
        return 1;
    };
    h.renderFormattedMessage = [](char*, i32, i32, void*, i32) {};
    h.sendQuickjumpMessage = [](i32, const char*, i32, const char*) {};
    h.freeHandlerEntry = [](HeRecord*) -> i32 { if (g_cap) g_cap->frees++; return 0; };
    SetEvent4Hooks(&h);

    // Shared clock + REAL CRT LCG seed, as a fresh session would set up.
    GameTime clk{}; clk.day = 6; clk.hour = 12; clk.minute = 0; clk.second = 0;
    SetNpcClock(clk);
    crt::Srand(20260606);

    HeRecord he = MakeHe();
    RD(&he, 112) = 1;            // the "due-poll" phase
    RD(&he, 20) = 3; RD(&he, 24) = 2;   // work amount = 5
    RD(&he, 170) = 0;
    // Appointment in the PAST so the REAL GameTimeCompare sibling reports "due".
    RT(&he, 82).day = 6; RT(&he, 82).hour = 9;

    // Sanity: the real GameTimeCompare sibling itself reports the appointment due.
    CHECK(sim::GameTimeCompare(&RT(&he, 82), &sim::NpcClock()) < 0);

    world::HarvestWageRun(&he);

    SetEvent4Hooks(nullptr); g_cap = nullptr;

    // Replay the exact draw the body made against the SAME real LCG sequence.
    crt::Srand(20260606);
    int draw = util::RandomModulo(40 * base);
    int amt = 3 + 2;
    int expectWage = amt * (40 * base + static_cast<u16>(draw));

    CHECK_EQ(cap.wages.size() == 1u, true);
    if (!cap.wages.empty()) CHECK_EQ(cap.wages[0], expectWage);
    CHECK_EQ(cap.frees, 1);            // the body cancelled + freed after paying

    // Second cross-module assertion: when the appointment is in the FUTURE the
    // REAL GameTimeCompare sibling reports "not due" (>=0) and the body returns
    // that comparison verbatim WITHOUT drawing the LCG or queuing a wage.
    Cap cap2; g_cap = &cap2;
    SetEvent4Hooks(&h);
    HeRecord he2 = MakeHe();
    RD(&he2, 112) = 1;
    RD(&he2, 20) = 4;
    RT(&he2, 82).day = 6; RT(&he2, 82).hour = 20;   // later than the day-6 12:00 clock
    i32 rv = world::HarvestWageRun(&he2);
    SetEvent4Hooks(nullptr); g_cap = nullptr;

    CHECK_EQ(rv, sim::GameTimeCompare(&RT(&he2, 82), &sim::NpcClock()));  // returns the real compare
    CHECK(rv >= 0);
    CHECK_EQ(cap2.wages.size() == 0u, true);   // no wage drawn on the not-due path
    CHECK_EQ(cap2.frees, 0);
}
