// Unit tests for the daily-routine director (npc_daily) and the market
// supervisor's per-slot pricing (npc_market). Golden vectors computed with the
// matching python oracle (see the report). RNG via crt::RandNext exactly.
#include "tests/framework/test.h"

#include "sim/npc_daily.h"
#include "sim/npc_market.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock
#include "crt/rand.h"

#include <cstring>

using namespace guild::sim;
using guild::i32;
using guild::u32;
using guild::i16;
using guild::u8;

// ---------------------------------------------------------------------------
// Season tables recovered byte-for-byte.
// ---------------------------------------------------------------------------
TEST(NpcDaily, SeasonTablesByteFaithful) {
    CHECK_EQ(kWorkStartHour[0], 8.0f);
    CHECK_EQ(kWorkStartHour[1], 7.0f);
    CHECK_EQ(kWorkStartHour[2], 8.0f);
    CHECK_EQ(kWorkStartHour[3], 9.0f);
    CHECK_EQ(kWorkEndHour[0], 20.0f);
    CHECK_EQ(kWorkEndHour[1], 21.0f);
    CHECK_EQ(kWorkEndHour[2], 20.0f);
    CHECK_EQ(kWorkEndHour[3], 19.0f);
    CHECK_EQ(kWorkStartSlack, -1.0f);
    CHECK_EQ(kEveningOffset, 2.0f);
    // season = day % 4
    CHECK_EQ(SeasonFromDay(0), 0);
    CHECK_EQ(SeasonFromDay(7), 3);
    CHECK_EQ(SeasonFromDay(10), 2);
}

static DailyPersonRow LiveRow(i32 home, i32 work, i32 dest, u32 bits) {
    DailyPersonRow r{};
    r.activeB = 1; r.valid = true;
    r.homeBld = home; r.workBld = work; r.destBld = dest;
    r.turnBits = bits; r.personId = 7;
    return r;
}

// ---------------------------------------------------------------------------
// The schedule RULE: morning go-to-work inside the start window.
// season 0 (spring): workStart=8, window = 8-1 = 7. hour 6 < 7 -> GoToWork.
// hour 7 NOT < 7 -> None. hour 9 -> None (past).
// ---------------------------------------------------------------------------
TEST(NpcDaily, MorningWorkWindow) {
    DailyPersonRow r = LiveRow(1, 2, 3, 0);
    CHECK(SelectDailyActivity(0, 0, 6, r, false) == DailyActivity::kGoToWork);
    CHECK(SelectDailyActivity(0, 0, 7, r, false) == DailyActivity::kNone);
    CHECK(SelectDailyActivity(0, 0, 9, r, false) == DailyActivity::kNone);
    // winter (season 3): workStart=9, window=8. hour 7 -> work; hour 8 -> none.
    CHECK(SelectDailyActivity(0, 3, 7, r, false) == DailyActivity::kGoToWork);
    CHECK(SelectDailyActivity(0, 3, 8, r, false) == DailyActivity::kNone);
    // skip-bit gates it out.
    DailyPersonRow rs = LiveRow(1, 2, 3, kDailySkip);
    CHECK(SelectDailyActivity(0, 0, 6, rs, false) == DailyActivity::kNone);
    // no building -> none.
    DailyPersonRow rn = LiveRow(0, 2, 3, 0);
    CHECK(SelectDailyActivity(0, 0, 6, rn, false) == DailyActivity::kNone);
}

// ---------------------------------------------------------------------------
// Evening RULE: past workEnd+2 -> GoHome; inside window -> tavern if eligible.
// season 0: workEnd=20, homeWindow=22. hour 23 -> GoHome.
// hour 21 (<=22): tavern-eligible -> GoToTavern; not eligible -> GoHome.
// social-already-requested bit -> None in the window.
// ---------------------------------------------------------------------------
TEST(NpcDaily, EveningHomeAndTavern) {
    DailyPersonRow r = LiveRow(1, 2, 3, 0);
    CHECK(SelectDailyActivity(1, 0, 23, r, false) == DailyActivity::kGoHome);
    CHECK(SelectDailyActivity(1, 0, 21, r, true)  == DailyActivity::kGoToTavern);
    CHECK(SelectDailyActivity(1, 0, 21, r, false) == DailyActivity::kGoHome);
    DailyPersonRow rq = LiveRow(1, 2, 3, kDailySocialReq);
    CHECK(SelectDailyActivity(1, 0, 21, rq, true) == DailyActivity::kNone);
}

// ---------------------------------------------------------------------------
// Director end-to-end (state 0): a synthetic 2-person population. Person 0 is a
// production worker inside the work window -> a "Go to work"/move dispatch;
// the work-start second sweep does not fire (hour < workStart). Verify the
// emitted command + the turn-bit write-back.
// ---------------------------------------------------------------------------
namespace {
struct DailyRec {
    int op77 = 0, chrmove = 0, str47 = 0, named53 = 0, args25 = 0;
    u32 lastBits = 0; int lastIdx = -1;
    const char* lastNamed = nullptr;
    const char* lastTag = nullptr;
};
DailyRec g_rec;
DailyPersonRow g_rows[2];

int DPCount() { return 2; }
DailyPersonRow DPRow(int i) { return g_rows[i]; }
void DPSetBits(int i, u32 b) { g_rec.lastBits = b; g_rec.lastIdx = i; g_rows[i].turnBits = b; }
int DPCarry(int, i32* u, i32* o) { *u = 100; *o = 200; return 1; }
bool DPDoor(int, i32* a, i32* b) { *a = 1; *b = 1; return true; }
bool DPProd(int) { return true; }
bool DPMesh(int) { return false; }
void DPOp77(i32) { ++g_rec.op77; }
void DPChrMove(i32, i32, i32, const char* t) { ++g_rec.chrmove; g_rec.lastTag = t; }
void DPStr47(i32, i32, i32) { ++g_rec.str47; }
void DPNamed(i32, i32, int, i32, int, const char* n) { ++g_rec.named53; g_rec.lastNamed = n; }
void DPArgs(i32, i32, int, int, int) { ++g_rec.args25; }
}

TEST(NpcDaily, DirectorState0WorkDispatch) {
    g_rec = DailyRec{};
    g_rows[0] = LiveRow(10, 20, 30, 0);
    g_rows[1] = LiveRow(0, 0, 0, 0);   // empty/no-building -> skipped

    SetNpcClock(GameTime{ /*day*/0, /*hour*/6, /*minute*/0, /*second*/0 });

    NpcDailyHooks h{};
    h.personCount = DPCount; h.personRow = DPRow; h.setTurnBits = DPSetBits;
    h.findCarryTarget = DPCarry; h.destDoorIds = DPDoor;
    h.homeIsProduction = DPProd; h.homeHasMesh = DPMesh;
    h.requestBuildOp77 = DPOp77; h.requestChrMoveToUniverse = DPChrMove;
    h.queueRequestString47 = DPStr47; h.queueRequestNamedObject53 = DPNamed;
    h.queueRequestArgs25 = DPArgs;
    SetNpcDailyHooks(&h);

    HeRecord rec{};
    He_State(&rec) = 0;
    NpcDaily_DailyRoutineStep(&rec);

    // Production worker -> the move/string path fires (op77 + chrmove + str47),
    // not the named "Go to work" (that's the non-production branch).
    CHECK_EQ(g_rec.op77, 1);
    CHECK_EQ(g_rec.chrmove, 1);
    CHECK_EQ(g_rec.str47, 1);
    CHECK_EQ(g_rec.named53, 0);
    // dispatched-to-work bit set on person 0.
    CHECK_EQ(g_rec.lastIdx, 0);
    CHECK((g_rows[0].turnBits & kDailyDispWork) != 0);
    // hour 6 < workStart(8): the work-start second sweep did not fire, state
    // stays 0.
    CHECK_EQ(He_State(&rec), 0);
    SetNpcDailyHooks(nullptr);
}

// ---------------------------------------------------------------------------
// Market per-slot recompute — UNDERSTOCK golden (seed 12345).
// Oracle: targetStk=100, eff=10, ref=50, price=40, elapsed=120, hour=10.
//   -> restock=buy, qty=24, price=51.42857360..., buyQty=24, yieldRecomp=true.
// ---------------------------------------------------------------------------
namespace {
int g_q17 = 0; i32 g_q17from = 0, g_q17to = 0, g_q17qty = 0;
float g_yieldVal = 7.5f;
float MktYield(int) { return g_yieldVal; }
void MktQ17(i32 from, i32 to, i32 qty, i16, guild::u8) {
    ++g_q17; g_q17from = from; g_q17to = to; g_q17qty = qty;
}
}

TEST(NpcMarket, RecomputeUnderstockGolden) {
    guild::crt::Srand(12345);
    g_q17 = 0;
    MarketSlot s{};
    s.active = true; s.itemType = 42;
    s.targetStk = 100; s.refValue = 50.0f; s.price = 40.0f;
    s.buyQty = 0; s.sellQty = 0;
    MarketSlotResult r = MarketRecomputeSlot(&s, /*elapsed*/120, /*eff*/10,
                                             /*idx*/0, /*hour*/10, /*market*/3,
                                             MktYield, MktQ17);
    CHECK(r.restock == MarketRestock::kBuy);
    CHECK_EQ(r.quantity, 24);
    CHECK_EQ(s.buyQty, 24);
    // price 51.42857... (compare to within fp tolerance)
    CHECK(s.price > 51.42f && s.price < 51.43f);
    CHECK(r.yieldRecomputed);
    CHECK_EQ(s.yield, 7.5f);
    CHECK_EQ(g_q17, 1);
    // understock buy: from=building(0), to=-1.
    CHECK_EQ(g_q17to, -1);
}

// ---------------------------------------------------------------------------
// Market per-slot recompute — OVERSUPPLY golden (seed 999).
// Oracle: targetStk=100, eff=90, ref=50, price=40, elapsed=120, hour=10.
//   -> restock=sell, qty=14, price=31.42857..., sellQty=14, yieldRecomp=true.
// ---------------------------------------------------------------------------
TEST(NpcMarket, RecomputeOversupplyGolden) {
    guild::crt::Srand(999);
    g_q17 = 0;
    MarketSlot s{};
    s.active = true; s.itemType = 42;
    s.targetStk = 100; s.refValue = 50.0f; s.price = 40.0f;
    MarketSlotResult r = MarketRecomputeSlot(&s, 120, 90, 0, 10, 3, MktYield, MktQ17);
    CHECK(r.restock == MarketRestock::kSell);
    CHECK_EQ(r.quantity, 14);
    CHECK_EQ(s.sellQty, 14);
    CHECK(s.price > 31.42f && s.price < 31.43f);
    CHECK(r.yieldRecomputed);
    CHECK_EQ(g_q17, 1);
    // oversupply sell: from=-1, to=building(0).
    CHECK_EQ(g_q17from, -1);
}

// Market recompute constants byte-faithful.
TEST(NpcMarket, ConstantsByteFaithful) {
    CHECK(kMktScatterBias == 80.0f);
    CHECK(kMktHalf == 0.5);
    CHECK(kMktThreeQuarter == 0.75);
    CHECK(kMktCeilFrac == 1.5f);
    CHECK(kMktResetLowFrac > 0.39f && kMktResetLowFrac < 0.41f);
    CHECK(kMktTreasuryTopUp == 5120000);
}
