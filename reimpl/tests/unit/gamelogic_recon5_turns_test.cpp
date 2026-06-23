#include "test.h"
#include "sim/gamelogic_recon5_turns.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct TurnFixture {
    i32 minutes = 0;
    i32 playerCount = 0;
    bool guardSuppressed = false;
    std::vector<int> refilled;
    std::vector<int> guardUpdated;
    std::vector<int> deltaSlots;
    std::vector<i32> deltaValues;
    // per-slot config:
    bool occupied[768];
    bool owned[768];
    i32  balance[768];
    TurnFixture() {
        for (int i = 0; i < 768; ++i) { occupied[i] = false; owned[i] = false; balance[i] = 0; }
    }
};
TurnFixture g_t;

i32  TDiff(const TurnClock&, const TurnClock&) { return g_t.minutes; }
i32  TCount() { return g_t.playerCount; }
bool TOcc(int s) { return g_t.occupied[s]; }
void TRefill(int s) { g_t.refilled.push_back(s); }
void TGuard(int s) { g_t.guardUpdated.push_back(s); }
bool TGuardSup() { return g_t.guardSuppressed; }
bool TOwned(int s) { return g_t.owned[s]; }
i32  TBal(int s) { return g_t.balance[s]; }
void TEmit(int s, i32 d) { g_t.deltaSlots.push_back(s); g_t.deltaValues.push_back(d); }

void InstallTurns() {
    Recon5TurnHooks h{};
    h.diffMinutes = &TDiff;
    h.totalPlayerCount = &TCount;
    h.slotOccupied = &TOcc;
    h.refillTavernStock = &TRefill;
    h.updateGuardBehavior = &TGuard;
    h.guardSuppressed = &TGuardSup;
    h.ownedGate = &TOwned;
    h.recordBalance = &TBal;
    h.emitBalanceDelta = &TEmit;
    SetRecon5TurnHooks(&h);
}

void ResetWindow() {
    Recon5TurnWindow() = TurnWindowState{};
}

} // namespace

// ===========================================================================
// Gate: period byte (word2) out of [6,0x16] -> no work.
// ===========================================================================
TEST(GameLogicRecon5, Turns_GateRejectsBadPeriod) {
    g_t = TurnFixture{}; g_t.minutes = 1; g_t.playerCount = 1;
    InstallTurns(); ResetWindow();
    TurnClock c{}; c.word2 = 5; // below 6
    GameLogic_UpdatePlayerTurns(c);
    CHECK_EQ((int)g_t.refilled.size(), 0);
    c.word2 = 0x17; // above 0x16
    GameLogic_UpdatePlayerTurns(c);
    CHECK_EQ((int)g_t.refilled.size(), 0);
}

TEST(GameLogicRecon5, Turns_GateRejectsZeroPlayers) {
    g_t = TurnFixture{}; g_t.minutes = 1; g_t.playerCount = 0;
    InstallTurns(); ResetWindow();
    TurnClock c{}; c.word2 = 10;
    GameLogic_UpdatePlayerTurns(c);
    CHECK_EQ((int)g_t.refilled.size(), 0);
}

// ===========================================================================
// Backwards clock (snap.lo > committed.lo) -> recommit only.
// ===========================================================================
TEST(GameLogicRecon5, Turns_BackwardsClockRecommitsOnly) {
    g_t = TurnFixture{}; g_t.minutes = 5; g_t.playerCount = 2;
    InstallTurns(); ResetWindow();
    Recon5TurnWindow().committed.lo = 100;
    TurnClock c{}; c.word2 = 10; c.lo = 200; // 200 > 100
    GameLogic_UpdatePlayerTurns(c);
    CHECK_EQ((int)g_t.refilled.size(), 0);
    CHECK_EQ(Recon5TurnWindow().committed.lo, 200);
}

// ===========================================================================
// >60 players -> v13 forced to 768, processes all occupied slots.
// ===========================================================================
TEST(GameLogicRecon5, Turns_ManyPlayersProcessesFullTable) {
    g_t = TurnFixture{}; g_t.minutes = 1; g_t.playerCount = 61;
    g_t.occupied[0] = true; g_t.occupied[5] = true; g_t.occupied[767] = true;
    InstallTurns(); ResetWindow();
    TurnClock c{}; c.word2 = 10; c.lo = 0; // not > committed.lo (0)
    GameLogic_UpdatePlayerTurns(c);
    // 768 iterations from cursor 0 -> visits every slot once; 3 occupied refilled.
    CHECK_EQ((int)g_t.refilled.size(), 3);
    CHECK_EQ(Recon5TurnWindow().startCursor, 0); // wraps fully back to 0
}

// ===========================================================================
// Count derivation: v13 = (int)(minutes*12.8), small player count branch.
//   minutes=10 -> 128.0 -> v13=128. v14=128 - 128 = 0, frac stays 0.
// ===========================================================================
TEST(GameLogicRecon5, Turns_GoldenCountFromMinutes) {
    g_t = TurnFixture{}; g_t.minutes = 10; g_t.playerCount = 2;
    for (int i = 0; i < 128; ++i) g_t.occupied[i] = true;
    InstallTurns(); ResetWindow();
    TurnClock c{}; c.word2 = 10; c.lo = 0;
    GameLogic_UpdatePlayerTurns(c);
    CHECK_EQ((int)g_t.refilled.size(), 128); // exactly 128 slots processed
    CHECK_EQ(Recon5TurnWindow().startCursor, 128);
}

// ===========================================================================
// Fractional accumulator carry: minutes=1 -> 12.8 -> v13=12, v14=12.8-12=0.8.
//   frac 0 + 0.8 = 0.8 (< 1.0 bits) -> no carry first tick.
//   second tick: frac 0.8 + 0.8 = 1.6 >= 1.0 -> ++v13 (13), frac -> 0.6.
// ===========================================================================
TEST(GameLogicRecon5, Turns_GoldenFractionalCarry) {
    InstallTurns(); ResetWindow();
    g_t = TurnFixture{}; g_t.minutes = 1; g_t.playerCount = 2;
    for (int i = 0; i < 768; ++i) g_t.occupied[i] = true;
    // tick 1
    TurnClock c{}; c.word2 = 10; c.lo = 0;
    GameLogic_UpdatePlayerTurns(c);
    CHECK_EQ((int)g_t.refilled.size(), 12);
    int afterTick1 = Recon5TurnWindow().startCursor; // 12
    CHECK_EQ(afterTick1, 12);
    // tick 2: fractional accumulator should now trigger the carry -> 13 processed
    g_t.refilled.clear();
    GameLogic_UpdatePlayerTurns(c);
    CHECK_EQ((int)g_t.refilled.size(), 13);
    CHECK_EQ(Recon5TurnWindow().startCursor, 25); // 12 + 13
}

// ===========================================================================
// Balance delta: delta = (int)(balance*0.89) - balance.
//   balance 1000 -> 890 - 1000 = -110.
// ===========================================================================
TEST(GameLogicRecon5, Turns_GoldenBalanceDelta) {
    g_t = TurnFixture{}; g_t.minutes = 10; g_t.playerCount = 2;
    g_t.occupied[0] = true; g_t.owned[0] = true; g_t.balance[0] = 1000;
    InstallTurns(); ResetWindow();
    TurnClock c{}; c.word2 = 10; c.lo = 0;
    GameLogic_UpdatePlayerTurns(c);
    CHECK_EQ((int)g_t.deltaSlots.size(), 1);
    // 1000 * 0.89f (flt 0.89000000953) -> 889 ; delta = 889 - 1000 = -111
    CHECK_EQ(g_t.deltaValues[0], -111);
}

// ===========================================================================
// Guard update gated by word2 in (6,0x16) exclusive and !suppressed.
// ===========================================================================
TEST(GameLogicRecon5, Turns_GuardUpdateGate) {
    g_t = TurnFixture{}; g_t.minutes = 10; g_t.playerCount = 2;
    g_t.occupied[0] = true;
    InstallTurns(); ResetWindow();
    // word2 == 6 -> guard NOT updated (needs > 6)
    TurnClock c{}; c.word2 = 6; c.lo = 0;
    GameLogic_UpdatePlayerTurns(c);
    CHECK_EQ((int)g_t.guardUpdated.size(), 0);
    // word2 == 7, not suppressed -> updated
    g_t.guardUpdated.clear(); ResetWindow();
    c.word2 = 7;
    GameLogic_UpdatePlayerTurns(c);
    CHECK(g_t.guardUpdated.size() >= 1);
    // suppressed -> not updated
    g_t.guardUpdated.clear(); g_t.guardSuppressed = true; ResetWindow();
    GameLogic_UpdatePlayerTurns(c);
    CHECK_EQ((int)g_t.guardUpdated.size(), 0);
}

// ===========================================================================
// Selection_ClearAll: writes offsets 536..411648, zeroes globals, returns 411648.
// ===========================================================================
namespace {
int g_clearCount = 0;
int g_firstOff = -1;
int g_lastOff = -1;
void ClearByte(int off) {
    if (g_clearCount == 0) g_firstOff = off;
    g_lastOff = off;
    ++g_clearCount;
}
}

TEST(GameLogicRecon5, SelectionClearAll_Golden) {
    g_clearCount = 0; g_firstOff = -1; g_lastOff = -1;
    SelectionGlobals g; g.g11BC270 = 9; g.g631740 = 9; g.g6317B0 = 9;
    int r = Selection_ClearAll(&ClearByte, g);
    CHECK_EQ(r, 411648);
    CHECK_EQ(g_clearCount, 768);      // 536..411648 step 536
    CHECK_EQ(g_firstOff, 536);
    CHECK_EQ(g_lastOff, 411648);
    CHECK_EQ(g.g11BC270, 0);
    CHECK_EQ(g.g631740, 0);
    CHECK_EQ(g.g6317B0, 0);
}

// ===========================================================================
// DebugFlag_SetReload.
// ===========================================================================
TEST(GameLogicRecon5, DebugFlagSetReload_Golden) {
    i32 flag = 0;
    DebugFlag_SetReload(flag);
    CHECK_EQ(flag, 1);
}
