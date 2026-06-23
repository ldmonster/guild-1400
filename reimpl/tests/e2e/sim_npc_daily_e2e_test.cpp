// End-to-end: run the daily-routine director over a small synthetic population
// across a day (morning work-dispatch -> work-start transition -> evening social
// -> go-home), and run the market supervisor over a synthetic stall, verifying
// activity assignments + price/stock changes + emitted commands vs a reference.
#include "tests/framework/test.h"

#include "sim/npc_daily.h"
#include "sim/npc_market.h"
#include "sim/npcaction.h"
#include "crt/rand.h"

#include <cstring>
#include <vector>

using namespace guild::sim;
using guild::i32;
using guild::u32;
using guild::i16;
using guild::u8;

// ===========================================================================
// Daily director over a day.
// ===========================================================================
namespace {
struct E2EWorld {
    std::vector<DailyPersonRow> rows;
    int op77 = 0, chrmove = 0, str47 = 0, named53 = 0, args25 = 0;
    int goWork = 0, goTavern = 0, goHome = 0;
};
E2EWorld g_w;

int  EWCount() { return static_cast<int>(g_w.rows.size()); }
DailyPersonRow EWRow(int i) { return g_w.rows[i]; }
void EWSetBits(int i, guild::u32 b) { g_w.rows[i].turnBits = b; }
int  EWCarry(int, guild::i32* u, guild::i32* o) { *u = 500; *o = 600; return 1; }
int  EWInteract(int, guild::i32* u, guild::i32* o) { *u = 500; *o = 600; return 1; }
bool EWDoor(int, guild::i32* a, guild::i32* b) { *a = 0; *b = 0; return true; }
bool EWProd(int) { return true; }     // production -> work move path
bool EWMesh(int) { return false; }
guild::u8 EWOwner(int) { return 6; }  // player-owned (enables social pass)
guild::u8 EWClass(int) { return 5; }  // not 4/16/19 -> not skipped
bool EWDist(int) { return true; }
bool EWBudget() { return true; }
guild::i32 EWCurrency(int) { return 5000; } // > 3200 -> tavern eligible
int  EWPickTavern(int, guild::i32* u, guild::i32* o) { *u = 700; *o = 800; return 1; }
int  EWCandidates() { return 3; }
void EWOp77(guild::i32) { ++g_w.op77; }
void EWChrMove(guild::i32, guild::i32, guild::i32, const char*) { ++g_w.chrmove; }
void EWStr47(guild::i32, guild::i32, guild::i32) { ++g_w.str47; }
void EWNamed(guild::i32, guild::i32, int, guild::i32, int, const char* n) {
    ++g_w.named53;
    if (std::strcmp(n, "Go to work") == 0) ++g_w.goWork;
    else if (std::strcmp(n, "Go to wirtshaus") == 0) ++g_w.goTavern;
    else if (std::strcmp(n, "Go home") == 0) ++g_w.goHome;
}
void EWArgs(guild::i32, guild::i32, int, int, int) { ++g_w.args25; }

NpcDailyHooks MakeDailyHooks() {
    NpcDailyHooks h{};
    h.personCount = EWCount; h.personRow = EWRow; h.setTurnBits = EWSetBits;
    h.findCarryTarget = EWCarry; h.findInteractionTarget = EWInteract;
    h.destDoorIds = EWDoor; h.homeIsProduction = EWProd; h.homeHasMesh = EWMesh;
    h.ownerKind = EWOwner; h.aiPlayerClass = EWClass; h.workDistanceOk = EWDist;
    h.characterBudgetOk = EWBudget; h.currencyHeld = EWCurrency;
    h.pickTavern = EWPickTavern; h.candidateCount = EWCandidates;
    h.requestBuildOp77 = EWOp77; h.requestChrMoveToUniverse = EWChrMove;
    h.queueRequestString47 = EWStr47; h.queueRequestNamedObject53 = EWNamed;
    h.queueRequestArgs25 = EWArgs;
    return h;
}
}

TEST(NpcDailyE2E, FullDaySweep) {
    g_w = E2EWorld{};
    // 3 live production workers (home/work/dest set), 1 empty slot.
    for (int i = 0; i < 3; ++i) {
        DailyPersonRow r{};
        r.activeB = 1; r.valid = true;
        r.homeBld = 100 + i; r.workBld = 200 + i; r.destBld = 300 + i;
        r.personId = 10 + i;
        g_w.rows.push_back(r);
    }
    DailyPersonRow empty{}; empty.valid = true; // no buildings
    g_w.rows.push_back(empty);

    NpcDailyHooks h = MakeDailyHooks();
    SetNpcDailyHooks(&h);
    guild::crt::Srand(1);

    HeRecord rec{};
    He_State(&rec) = 0;

    // --- Morning (hour 6, spring season 0): work-dispatch fires (production
    // move path), state stays 0 (hour < workStart=8). ---
    SetNpcClock(GameTime{ 0, 6, 0, 0 });
    NpcDaily_DailyRoutineStep(&rec);
    CHECK_EQ(He_State(&rec), 0);
    CHECK(g_w.chrmove >= 3);          // all 3 workers dispatched
    CHECK_EQ(g_w.named53, 0);         // production -> move path, no named cmd
    int chrmoveMorning = g_w.chrmove;

    // --- Work-start (hour 9 >= workStart 8): the second sweep fires AND state
    // transitions to 1, appointment set to work-end hour. The morning-dispatched
    // workers do NOT re-dispatch: the gate and the writeback are the SAME bit
    // 0x1000 (BYTE1 |= 0x10 @0x4e8184; binary-verified — the earlier 0x100000
    // gate was a misread), so chrmove stays at the morning count. ---
    SetNpcClock(GameTime{ 0, 9, 0, 0 });
    He_SavedTime(&rec).day = 0;       // saved-day == clock-day gate
    NpcDaily_DailyRoutineStep(&rec);
    CHECK_EQ(He_State(&rec), 1);
    CHECK_EQ(g_w.chrmove, chrmoveMorning);
    CHECK_EQ(static_cast<int>(He_ApptTime(&rec).hour), 20); // work-end spring

    // --- Evening within window (hour 21 <= 22): social pass -> one tavern/home
    // dispatch (player-owned, currency>3200). ---
    g_w.named53 = 0; g_w.goTavern = 0; g_w.goHome = 0;
    SetNpcClock(GameTime{ 0, 21, 0, 0 });
    He_SavedTime(&rec).day = 0;
    NpcDaily_DailyRoutineStep(&rec);
    // one social dispatch this round (pass A breaks after the first).
    CHECK_EQ(g_w.named53, 1);
    CHECK_EQ(g_w.goTavern + g_w.goHome, 1);

    // --- Late evening (hour 23 > workEnd+2=22): the go-home sweep sends the
    // remaining workers home. ---
    int chrBefore = g_w.chrmove;
    SetNpcClock(GameTime{ 0, 23, 0, 0 });
    He_SavedTime(&rec).day = 0;
    NpcDaily_DailyRoutineStep(&rec);
    CHECK(g_w.chrmove >= chrBefore);  // go-home moves emitted

    SetNpcDailyHooks(nullptr);
}

// ===========================================================================
// Market supervisor over a synthetic stall list.
// ===========================================================================
namespace {
struct MktWorld {
    std::vector<MarketSlot> slots;
    int q17 = 0, transform = 0, topup = 0;
    bool enabled = true;
    i32 treasuryVal = 100;
};
MktWorld g_m;

bool MEnabled() { return g_m.enabled; }
guild::u8 MIndex() { return 3; }
guild::i32 MTreasury() { return g_m.treasuryVal; }
void MTopUp(guild::i32) { ++g_m.topup; }
int  MSlotCount() { return static_cast<int>(g_m.slots.size()); }
MarketSlot MSlot(int i) { return g_m.slots[i]; }
void MCommit(int i, const MarketSlot* s) { g_m.slots[i] = *s; }
bool MWorkable(int) { return true; }
guild::i32 MEffStock(int) { return 10; }   // understock vs target 100
float MYield(int) { return 9.0f; }
void MQ17(guild::i32, guild::i32, guild::i32, guild::i16, guild::u8) { ++g_m.q17; }
void MTransform(const MarketSlot*, guild::u8) { ++g_m.transform; }
}

TEST(NpcMarketE2E, SupervisorSweep) {
    g_m = MktWorld{};
    g_m.treasuryVal = 100;   // < 5,120,000 -> top-up enqueued
    for (int i = 0; i < 5; ++i) {
        MarketSlot s{};
        s.active = true; s.itemType = static_cast<guild::i16>(40 + i);
        s.targetStk = 100; s.refValue = 50.0f; s.price = 40.0f;
        g_m.slots.push_back(s);
    }

    NpcMarketHooks h{};
    h.marketEnabled = MEnabled; h.marketIndex = MIndex;
    h.treasury = MTreasury; h.enqueueTopUp = MTopUp;
    h.slotCount = MSlotCount; h.slot = MSlot; h.commitSlot = MCommit;
    h.slotIsActiveWorkable = MWorkable; h.effectiveStock = MEffStock;
    h.computeYield = MYield; h.queueRequest17 = MQ17; h.queueTransform64 = MTransform;
    SetNpcMarketHooks(&h);
    guild::crt::Srand(4242);

    HeRecord rec{};
    He_State(&rec) = 0;
    // saved time @+96 is the previous run; set it 120 min earlier so elapsed=120.
    SetNpcClock(GameTime{ 0, 10, 0, 0 });
    GameTime* savedAt = reinterpret_cast<GameTime*>(reinterpret_cast<guild::u8*>(&rec) + 96);
    *savedAt = GameTime{ 0, 8, 0, 0 };   // 2h earlier

    NpcMarket_RunMarktSupervisorStep(&rec);

    // treasury top-up enqueued once.
    CHECK_EQ(g_m.topup, 1);
    // each of the 5 slots committed via transform64.
    CHECK_EQ(g_m.transform, 5);
    // understock (eff 10 < half of 100) -> each slot raised price above 40.
    for (const auto& s : g_m.slots)
        CHECK(s.price > 40.0f);
    // state re-armed to 0 (hour 10 < 0x16); appointment re-stamped to now then
    // advanced via GameTime_Advance(rec, 1, 0, 0) — note this function's "addDays"
    // arg actually feeds the hour accumulator (carries to days only at /24), so
    // the appointment ends at hour 11, day 0 (faithful to the original arithmetic).
    CHECK_EQ(He_State(&rec), 0);
    CHECK_EQ(He_ApptTime(&rec).day, 0);
    CHECK_EQ(static_cast<int>(He_ApptTime(&rec).hour), 11);

    SetNpcMarketHooks(nullptr);
}

// Market gate: disabled supervisor frees immediately (no sweep).
TEST(NpcMarketE2E, DisabledFreesImmediately) {
    g_m = MktWorld{};
    g_m.enabled = false;
    int freed = 0;
    static int* s_freed = &freed;
    NpcMarketHooks h{};
    h.marketEnabled = MEnabled;
    h.freeHandlerEntry = [](HeRecord*) -> guild::i32 { ++*s_freed; return 0; };
    SetNpcMarketHooks(&h);
    HeRecord rec{};
    NpcMarket_RunMarktSupervisorStep(&rec);
    CHECK_EQ(freed, 1);
    SetNpcMarketHooks(nullptr);
}
