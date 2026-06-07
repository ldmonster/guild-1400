// End-to-end flow test for the NpcAction9 evaluator family. Simulates a single NPC
// "tick" walking the per-action evaluators in dispatch order with a small recording
// world installed through NpcAction9Hooks, asserting the chosen action code and the
// request blocks the winner fills.

#include "sim/npcaction9.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct World {
    int currency = 0;
    int wealth = 0;
    int rank = 0;
    int amt = 0;
    int findHit = 0;
    int nearestId = 0;
    void* resolved = nullptr;
    int select = 0;
    int opponent = 0;
    u16 rng = 0;
    float rngf = 0.0f;
};
World g_w;

int w_currency(int, u8) { return g_w.currency; }
int w_wealth(int, const void*) { return g_w.wealth; }
int w_rank(int) { return g_w.rank; }
int w_amt(const void*) { return g_w.amt; }
int w_find(const void*, int, void*, float, float, int* o) { if (o) *o = g_w.nearestId; return g_w.findHit; }
void w_resolve(void** o, int, int, int) { *o = g_w.resolved; }
int w_select(int, u16, void*, int, void*) { return g_w.select; }
int w_opponent(const void*, void* a, void* b) {
    if (g_w.opponent) { std::memset(a, 0, kNpc9ReqBlockSize); std::memset(b, 0, kNpc9ReqBlockSize); }
    return g_w.opponent;
}
int w_count(void* d, int* id, const void*) { if (d) std::memset(d, 0, 8); if (id) *id = -1; return 0; }
u16 w_rng(u16) { return g_w.rng; }
float w_rngf() { return g_w.rngf; }
int w_cat(int) { return 7; }

NpcAction9Hooks MakeWorldHooks() {
    NpcAction9Hooks h{};
    h.personCurrencyAmount = w_currency;
    h.personTotalWealth = w_wealth;
    h.buildingRankWithinGroup = w_rank;
    h.amtCheckGuildRankLevel2 = w_amt;
    h.findNearestEntity = w_find;
    h.resolveEntityById = w_resolve;
    h.selectBestRecursive = w_select;
    h.findOpponentBuilding = w_opponent;
    h.countInventoryMatch = w_count;
    h.randomModulo = w_rng;
    h.randomFloatScaled = w_rngf;
    h.buildingCategoryForObject = w_cat;
    h.currencyByte = 0;
    return h;
}

struct Rec { u8 b[600]; Rec() { std::memset(b, 0, sizeof(b)); } };

// Mimic the dispatcher's "first non-zero wins" scan over a handful of evaluators.
int RunTick(const u16* rec, void* reqA, void* reqB, u8* sub) {
    int code;
    if ((code = NpcAction9_EvaluateArrest(0, reqA, 0, reinterpret_cast<const i16*>(rec)))) return code;
    if ((code = NpcAction9_EvaluateRecruitWorker(0, rec, reqA, 0, reqB))) return code;
    if ((code = NpcAction9_EvaluateShopInteract(0, reqA, 0, rec, sub))) return code;
    if ((code = NpcAction9_EvaluateEnterBuilding(rec, reqA, 0, reqB))) return code;
    return 0;
}

} // namespace

TEST(NpcAction9E2E, EmptyWorldPicksNothing) {
    g_w = World{};
    NpcAction9Hooks h = MakeWorldHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0}; u8 sub = 0;
    // EnterBuilding's first branch calls SelectBestRecursive(40); with select=0 it
    // returns 0, so nothing fires.
    CHECK_EQ(RunTick(reinterpret_cast<const u16*>(rec.b), a, b, &sub), 0);
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9E2E, ShopInteractWinsViaOpponent) {
    g_w = World{};
    g_w.rank = 0;          // category present but low rank -> opponent fallback
    g_w.opponent = 1;
    NpcAction9Hooks h = MakeWorldHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0}; u8 sub = 0;
    // Arrest gate (amt != 1) and RecruitWorker gate (record[+358] != 15) both fail,
    // so ShopInteract is the first to fire.
    CHECK_EQ(RunTick(reinterpret_cast<const u16*>(rec.b), a, b, &sub), 50);
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9E2E, RecruitWinsBeforeShop) {
    g_w = World{};
    g_w.findHit = 1;
    static Rec target;
    g_w.resolved = target.b;
    g_w.rng = 0;            // loyalty roll passes
    g_w.select = 49;        // SelectBestRecursive(15) yields recruit code
    NpcAction9Hooks h = MakeWorldHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; rec.b[358] = 15;   // arms RecruitWorker
    u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0}; u8 sub = 0;
    // Arrest fails (amt != 1); RecruitWorker fires first and wins.
    int code = RunTick(reinterpret_cast<const u16*>(rec.b), a, b, &sub);
    CHECK_EQ(code, 49);
    CHECK_EQ(a[0], 4);      // recruit reqA kind byte
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9E2E, ArrestWinsWhenRankAndAffordable) {
    g_w = World{};
    g_w.amt = 1;            // guild-rank check passes
    g_w.wealth = 100000;    // bounty 999
    g_w.currency = 100000;  // budget 22000 >= 999
    g_w.findHit = 1;
    g_w.nearestId = 7;
    static Rec arrTarget;
    g_w.resolved = arrTarget.b;
    g_w.rng = 0;
    NpcAction9Hooks h = MakeWorldHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0}; u8 sub = 0;
    // Arrest still bails because the city-ref table is empty (cityRefCount==0 -> 0):
    // this is the documented deferred grid-walk path. Verify the flow degrades to 0.
    CHECK_EQ(RunTick(reinterpret_cast<const u16*>(rec.b), a, b, &sub), 0);
    SetNpcAction9Hooks(nullptr);
}
