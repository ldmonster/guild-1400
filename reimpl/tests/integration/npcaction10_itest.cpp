// Integration: drive npcaction10's RunCreditStep debt-collection coroutine against
// a REAL reconstructed sibling — VIBE_Person_SumCurrencyHeld (inventory_wealth.cpp
// 0x59152c), the currency-stack aggregator. This is the live wiring: RunCreditStep
// decides "can the creditor afford the charge?" by calling VIBE_Person_SumCurrencyHeld
// on the creditor, reached here through the module's `sumCurrencyHeld` hook. The
// reconstructed SumCurrencyHeld operates on a ContainerView (it scans the person's
// currency child stacks), so the hook adapts the creditor record into that view and
// forwards into the genuine inventory_wealth::PersonSumCurrencyHeld — the affordable
// vs short fork is then decided by the REAL aggregator, end to end. The currency
// proto table + active player are seeded through the real WealthSet* API so the real
// function's proto-match is byte-faithful.
//
// The remaining RunCreditStep leaves (queueRequest16 / distributeCredit / message)
// are recording stubs so the cross-module verdict is observable.
#include "test.h"

#include "sim/npcaction10.h"
#include "sim/inventory_wealth.h"   // REAL PersonSumCurrencyHeld + Wealth* API

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

constexpr i16 kCurrencyProto = 7;   // player 0's currency good id (proto table)

// The creditor's currency stacks (level == amount). The hook below aggregates
// these through the REAL PersonSumCurrencyHeld via a ContainerView, exactly as the
// running game would scan the person's child currency nodes.
std::vector<StockChild> g_creditorStacks;

// --- the load-bearing hook: forward into the REAL sibling ----------------
int RealSumCurrencyHeld(void* /*creditor*/) {
    ContainerView cv;
    cv.children = g_creditorStacks;            // creditor's currency child stacks
    return PersonSumCurrencyHeld(cv);          // REAL inventory_wealth aggregator
}

// --- opaque records the resolves return ----------------------------------
int g_bldg = 0xB;       // any non-null sentinel
int g_creditor = 0xC;

void* QueryBegin(i32 /*key*/) { return &g_bldg; }
void* FindPerson(i32 /*id*/)  { return &g_creditor; }
u16   MarkerWord(void*)       { return 0; }     // city index 0
void* CityRecord(u16)         { return nullptr; }
i32   ObjId(void* r)          { return r == &g_bldg ? 0xB000 : 0xC000; }

// --- recorders for the two outcome branches ------------------------------
struct Rec {
    int q16Calls = 0; i32 q16From = -1, q16To = -1, q16Amt = -1;
    int distCalls = 0; i32 distAmt = -1;
} g_rec;

void QueueRequest16(i32 from, i32 to, i32 amt) {
    ++g_rec.q16Calls; g_rec.q16From = from; g_rec.q16To = to; g_rec.q16Amt = amt;
}
i32 DistributeCredit(i32 /*payer*/, void* /*payee*/, i32 amt) {
    ++g_rec.distCalls; g_rec.distAmt = amt; return amt;
}

NpcAction10Hooks MakeHooks() {
    NpcAction10Hooks h{};
    h.sumCurrencyHeld   = RealSumCurrencyHeld;    // <-- real sibling wiring
    h.personQueryBegin  = QueryBegin;
    h.findPersonById    = FindPerson;
    h.markerWord        = MarkerWord;
    h.cityPersonRecord  = CityRecord;
    h.objId             = ObjId;
    h.queueRequest16    = QueueRequest16;
    h.distributeCredit  = DistributeCredit;
    return h;
}

// A full He record. RunCreditStep reaches +192; size generously.
struct HeBuf {
    alignas(8) std::uint8_t bytes[1024];
    HeBuf() { std::memset(bytes, 0, sizeof bytes); }
    HeRecord* rec() { return reinterpret_cast<HeRecord*>(bytes); }
    void put32(int off, i32 v) { std::memcpy(bytes + off, &v, 4); }
};

void SeedWealth() {
    // player 0 -> currency proto kCurrencyProto; active player 0.
    WealthSetCurrencyProtoTable({ kCurrencyProto });
    WealthSetActivePlayer(0);
    // Pin the real proto resolver.
    CHECK_EQ(static_cast<int>(WealthCurrencyProto(0)), kCurrencyProto);
}

} // namespace

// charge = +192 * 0.01 * +180 = 200 * 0.01 * 50 = 100. When the REAL aggregator
// reports the creditor holds >= 100 currency, RunCreditStep takes the AFFORDABLE
// branch (queueRequest16), not the short/repossess branch.
TEST(NpcAction10Itest, CreditStepAffordableViaRealCurrencySum) {
    SeedWealth();
    g_rec = Rec{};
    // 150 in matching-proto stacks (60 + 90), plus a non-currency stack ignored.
    g_creditorStacks = { {kCurrencyProto, 60}, {kCurrencyProto, 90}, {99, 1000} };
    // Sanity: the REAL sum == 150 (only proto-7 stacks).
    CHECK_EQ(RealSumCurrencyHeld(&g_creditor), 150);

    NpcAction10Hooks h = MakeHooks();
    SetNpcAction10Hooks(&h);

    HeBuf he;
    He_ReqHandle(he.rec()) = -1;   // +132 no pending packet
    He_State(he.rec())     = 0;
    He_Flags(he.rec())     = 2;    // flag&2 -> charge path
    he.put32(172, 0xB);            // building id (resolved)
    he.put32(176, 0xC);            // creditor id (resolved)
    he.put32(180, 50);             // rate term
    he.put32(192, 200);            // rate
    he.put32(188, 1);              // retry counter (>0 so no settle this pass)

    NpcAction10_RunCreditStep(he.rec());

    CHECK_EQ(g_rec.q16Calls, 1);          // affordable -> transfer issued
    CHECK_EQ(g_rec.distCalls, 0);         // no repossession
    CHECK_EQ(g_rec.q16Amt, 100);          // (int)charge
    CHECK_EQ(g_rec.q16To, 0xC);           // to the debtor slot (+176)

    SetNpcAction10Hooks(nullptr);
}

// When the REAL aggregator reports too little currency (< charge), RunCreditStep
// takes the SHORT branch (distributeCredit / repossess) instead.
TEST(NpcAction10Itest, CreditStepShortViaRealCurrencySum) {
    SeedWealth();
    g_rec = Rec{};
    g_creditorStacks = { {kCurrencyProto, 40} };   // only 40 < 100 charge
    CHECK_EQ(RealSumCurrencyHeld(&g_creditor), 40);

    NpcAction10Hooks h = MakeHooks();
    SetNpcAction10Hooks(&h);

    HeBuf he;
    He_ReqHandle(he.rec()) = -1;
    He_State(he.rec())     = 0;
    He_Flags(he.rec())     = 2;
    he.put32(172, 0xB);
    he.put32(176, 0xC);
    he.put32(180, 50);
    he.put32(192, 200);
    he.put32(188, 1);

    NpcAction10_RunCreditStep(he.rec());

    CHECK_EQ(g_rec.q16Calls, 0);          // not affordable -> no transfer
    CHECK_EQ(g_rec.distCalls, 1);         // repossession issued
    CHECK_EQ(g_rec.distAmt, 100);         // (int)charge requested

    SetNpcAction10Hooks(nullptr);
}
