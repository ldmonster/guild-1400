#include "test.h"

// Integration: drive the charaction_steps4 BuyObjectStep coroutine against a
// REAL reconstructed sibling — VIBE_Building_MapTypeToCategory
// (building_type.cpp 0x5878b0, the kind->UI-category switch) — no mock category
// map. The live wiring routes a candidate building's type/kind byte through the
// building-category classifier; BuyObjectStep keeps only category-6 (market)
// sellers. Here the CharActionStep4Hooks.buildingCategory hook forwards into the
// genuine Building_MapKindToCategory, so the seller scan's market filter is the
// real sibling's verdict (kind 2 -> category 6), end to end.
//
// freeHandlerEntry / packetStatus (the shared NpcLeafHooks) and the remaining
// step-4 effects are recorded by local mocks so the cross-module flow is
// observable; the load-bearing classifier is the real one.
#include "sim/charaction_steps4.h"
#include "sim/building_type.h"   // real Building_MapKindToCategory (0x5878b0)
#include "sim/npcaction.h"       // NpcLeafHooks / SetNpcLeafHooks

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A real He record sized to the full struct (extends past +361).
struct HeBuf {
    alignas(8) std::uint8_t bytes[sizeof(HeRecord)];
    HeBuf() { std::memset(bytes, 0, sizeof bytes); }
    HeRecord* rec() { return reinterpret_cast<HeRecord*>(bytes); }
    void SetKindByte(std::uint8_t v) { bytes[0] = v; }   // +0 type/kind byte
};

// --- recording state for the non-classifier effects ---------------------
int   g_freeCalls   = 0;
int   g_moodTarget  = -999;
int   g_moodDelta   = 0;
i32   g_quickjumpTo = -999;
int   g_quickjumpText = -999;

i32  RecFree(HeRecord*)              { ++g_freeCalls; return 7; }
i32  RecPacketStatus(i32)            { return 1; }
i32  RecQueue29(int, HeRecord*)      { return 0; }

const NpcLeafHooks kNpcHooks = {
    /*queueRequestEntity29*/ RecQueue29,
    /*freeHandlerEntry*/     RecFree,
    /*packetStatus*/         RecPacketStatus,
    /*lawBaseTextId*/        nullptr,
    /*findInventorySlot*/    nullptr,
    /*shuffleDwords*/        nullptr,
    /*requestBuildOp93*/     nullptr,
};

// The seller candidates the person query iterates. Each carries a kind byte at
// +0 that the REAL classifier maps to a UI category.
std::vector<HeBuf*> g_sellers;
std::size_t         g_iter = 0;

// --- step-4 hooks: buildingCategory is the REAL sibling -------------------
int RealBuildingCategory(u8 typeByte) {
    return Building_MapKindToCategory(typeByte);   // REAL sibling (0x5878b0)
}
HeRecord* QueryBegin(int, int, int) {
    g_iter = 0;
    return g_sellers.empty() ? nullptr : g_sellers[g_iter++]->rec();
}
HeRecord* IterNext() {
    return (g_iter < g_sellers.size()) ? g_sellers[g_iter++]->rec() : nullptr;
}
int  Rand0(int) { return 0; }   // deterministic pick: first market seller
void RecAdjustMood(HeRecord* rec, int delta) {
    g_moodTarget = static_cast<int>(*reinterpret_cast<std::uint8_t*>(rec)); // its kind
    g_moodDelta  = delta;
}
u8   CityCat6(u16)  { return 6; }
i32  CityPerson(u16){ return 314; }
void RecQuickjump(i32 to, int text) { g_quickjumpTo = to; g_quickjumpText = text; }

CharActionStep4Hooks MakeHooks() {
    CharActionStep4Hooks h;
    std::memset(&h, 0, sizeof h);
    h.buildingCategory     = RealBuildingCategory;   // <-- real wiring
    h.personQueryBegin     = QueryBegin;
    h.personIterNext       = IterNext;
    h.randomModulo         = Rand0;
    h.adjustMood           = RecAdjustMood;
    h.cityCategory         = CityCat6;
    h.cityPersonId         = CityPerson;
    h.sendQuickjumpMessage = RecQuickjump;
    return h;
}

void ResetRec() {
    g_freeCalls = 0; g_moodTarget = -999; g_moodDelta = 0;
    g_quickjumpTo = -999; g_quickjumpText = -999;
    g_sellers.clear(); g_iter = 0;
}

} // namespace

// state 0: scan persons; the REAL classifier keeps only kind-2 (category-6,
// market) candidates. With sellers {kind 5, kind 2, kind 7}, only the kind-2 one
// is a market seller; RandomModulo(0)=0 picks it; its mood is adjusted by -50.
TEST(CharactionSteps4Itest, BuyObjectKeepsRealMarketCategory) {
    ResetRec();
    // Sanity-pin the real sibling: kind 2 -> category 6 (market), others not.
    CHECK_EQ(static_cast<int>(Building_MapKindToCategory(2)), 6);
    CHECK(Building_MapKindToCategory(5) != 6);
    CHECK(Building_MapKindToCategory(7) != 6);

    HeBuf s0, s1, s2;
    s0.SetKindByte(5);   // not market
    s1.SetKindByte(2);   // market (category 6)
    s2.SetKindByte(7);   // not market
    g_sellers = {&s0, &s1, &s2};

    HeBuf h;
    He_State(h.rec())     = 0;
    He_CityIndex(h.rec()) = 11;

    const CharActionStep4Hooks hooks = MakeHooks();
    SetCharActionStep4Hooks(&hooks);
    SetNpcLeafHooks(&kNpcHooks);

    i32 r = BuyObjectStep(h.rec());

    CHECK_EQ(r, 7);                 // returns freeHandlerEntry's code
    CHECK_EQ(g_freeCalls, 1);       // freed once
    CHECK_EQ(g_moodTarget, 2);      // mood adjusted on the kind-2 market seller
    CHECK_EQ(g_moodDelta, -50);
    CHECK_EQ(g_quickjumpTo, 314);   // city category 6 -> buyer quickjump
    CHECK_EQ(g_quickjumpText, 3355);

    SetCharActionStep4Hooks(nullptr);
    SetNpcLeafHooks(nullptr);
}

// When NO candidate maps (via the REAL classifier) to category 6, the seller
// list is empty: no mood adjust, no quickjump, just free.
TEST(CharactionSteps4Itest, BuyObjectNoRealMarketMeansNoSeller) {
    ResetRec();
    HeBuf s0, s1;
    s0.SetKindByte(5);   // category 8 (real)
    s1.SetKindByte(7);   // category 4 (real)
    g_sellers = {&s0, &s1};

    HeBuf h;
    He_State(h.rec())     = 0;
    He_CityIndex(h.rec()) = 3;

    const CharActionStep4Hooks hooks = MakeHooks();
    SetCharActionStep4Hooks(&hooks);
    SetNpcLeafHooks(&kNpcHooks);

    i32 r = BuyObjectStep(h.rec());

    CHECK_EQ(r, 7);
    CHECK_EQ(g_freeCalls, 1);
    CHECK_EQ(g_moodTarget, -999);   // no seller adjusted
    CHECK_EQ(g_quickjumpTo, -999);  // no quickjump

    SetCharActionStep4Hooks(nullptr);
    SetNpcLeafHooks(nullptr);
}

// state -2 is the terminal free path: the step frees immediately without ever
// consulting the classifier (the seller scan is skipped).
TEST(CharactionSteps4Itest, BuyObjectTerminalStateFreesImmediately) {
    ResetRec();
    HeBuf h;
    He_State(h.rec()) = -2;

    const CharActionStep4Hooks hooks = MakeHooks();
    SetCharActionStep4Hooks(&hooks);
    SetNpcLeafHooks(&kNpcHooks);

    i32 r = BuyObjectStep(h.rec());
    CHECK_EQ(r, 7);
    CHECK_EQ(g_freeCalls, 1);
    CHECK_EQ(g_moodTarget, -999);

    SetCharActionStep4Hooks(nullptr);
    SetNpcLeafHooks(nullptr);
}
