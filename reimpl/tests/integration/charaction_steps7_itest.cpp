// Integration test for the CharAction transport coroutine (charaction_steps7)
// wired against a REAL reconstructed sibling: the transport's RandomModulo draws
// are forwarded into util::RandomModulo (gilde 0x58b89c), which itself runs the
// real crt::RandNext LCG (crt/rand.cpp). This is exactly how the live game wires
// VIBE_Math_RandomModulo, so the highwayman-ambush check in RunTransport's working
// phase is exercised against the genuine RNG (not a canned mock), seeded via
// crt::Srand for determinism. The two GameTime siblings (GameTimeAdvance/Compare)
// are likewise the real reconstructed functions linked into the lib.
#include "test.h"

#include "sim/charaction_steps7.h"
#include "sim/gametime.h"
#include "sim/npcaction.h"
#include "util/math_random.h"   // REAL sibling
#include "crt/rand.h"           // REAL LCG behind it

#include <cstring>              // std::memcpy (alignment-safe field stores)

using namespace guild;
using namespace guild::sim;

namespace {

struct Block { u8 b[700]; };

// Only the leaves the working-phase path touches; everything else inert default.
struct Wire {
    int free29 = 0, lastFree29 = 99, freeHandler = 0, entityMsg = 0;
    int buildOp74 = 0, changeZustand = 0, resolveEnt = 0;
    HeRecord* cart = nullptr;
    HeRecord* startp = nullptr;
    HeRecord* goalp = nullptr;
};
Wire g_w;

// The randomModulo hook -> REAL util::RandomModulo (which calls crt::RandNext).
int RealRandomModulo(int n) {
    return util::RandomModulo(static_cast<u16>(n));
}

HeRecord* wPerson(i32, int, int, i32 key) {
    if (key == 68 || key == 69) return g_w.startp;
    if (g_w.startp && key == He_Id(g_w.startp)) return g_w.startp;
    if (g_w.goalp && key == He_Id(g_w.goalp)) return g_w.goalp;
    return g_w.startp;
}
HeRecord* wFindObj(i32) { return g_w.cart; }
HeRecord* wObjQuery(i32, int, int, int, i32) { return g_w.cart; }
HeRecord* wResolveEnt(int, HeRecord** out, i32, int) {
    g_w.resolveEnt++;
    if (out) *out = g_w.cart;
    return g_w.cart;
}
HeRecord* wBuilding(i32) { return nullptr; }
int  wRet0a(HeRecord*) { return 0; }
int  wRet0b(HeRecord*) { return 0; }
int  wSumWork(HeRecord*, int, int) { return 0; }
HeRecord* wWorkProduct(HeRecord*) { return nullptr; }
int  wColVol(HeRecord*) { return 0; }
void wWalk(HeRecord*, bool (*)(HeRecord*, void*), void*) {}
int  wNameEq(HeRecord*, const char*) { return 0; }
i32  wCmd19(i32, i32, i32, i32) { return 1; }
void wPair51(i32, int) {}
void wArgs25(i32, int, int, int, int) {}
void wSingle49(i32) {}
void wNamed53(i32, i32, int, int, int, const char*) {}
void wQuad46(i32, int, int, int) {}
void wChrMove(i32, i32, int, int) {}
void wBuildOp74(i32) { g_w.buildOp74++; }
void wChangeZustand(i32, int) { g_w.changeZustand++; }
void wQuickjump(i32, i32, int) {}
void wEntityMsg(i32, int) { g_w.entityMsg++; }
i32  wSwitch(i32, int) { return 0; }
int  wNearDoor(HeRecord*, HeRecord*) { return 0; }
int  wWithin(const float*, const float*, float) { return 0; }
i32  wCityRecip(u16) { return 555; }
u8   wCityCat(u16) { return 0; }
int  wFast() { return 0; }
int  wMed() { return 0; }
u16  wSceneCity() { return 0; }

i32 wFree29(int arg, HeRecord*) { g_w.free29++; g_w.lastFree29 = arg; return 1; }
i32 wFreeHandler(HeRecord*) { g_w.freeHandler++; return 0; }
i32 wPacketStatus(i32) { return 1; }   // applied

CharActionStep7Hooks MakeHooks() {
    CharActionStep7Hooks h{};
    h.personQueryBegin = wPerson; h.findObjectById = wFindObj;
    h.objectQueryFind = wObjQuery; h.resolveEntityById = wResolveEnt;
    h.buildingFindById = wBuilding; h.isProductionType = wRet0a;
    h.isStorageType = wRet0b; h.sumWorkstation = wSumWork;
    h.findWorkProduct = wWorkProduct; h.collectProductionVolume = wColVol;
    h.walkScene = wWalk; h.nodeNameEquals = wNameEq;
    h.cmdRequest19 = wCmd19; h.cmdRequestPair51 = wPair51;
    h.cmdRequestArgs25 = wArgs25; h.cmdRequestSingle49 = wSingle49;
    h.cmdRequestNamedObject53 = wNamed53; h.cmdRequestQuad46 = wQuad46;
    h.cmdRequestChrMove = wChrMove; h.cmdRequestBuildOp74 = wBuildOp74;
    h.requestChangeZustand = wChangeZustand; h.sendQuickjump = wQuickjump;
    h.sendEntityMessage = wEntityMsg; h.universeSwitchSlot = wSwitch;
    h.isNearDoor = wNearDoor; h.withinTolerance = wWithin;
    h.cityRecipientId = wCityRecip; h.cityCategory = wCityCat;
    h.randomModulo = RealRandomModulo;       // <-- REAL sibling
    h.fastFrameCounter = wFast; h.medFrameCounter = wMed;
    h.currentSceneCity = wSceneCity;
    return h;
}

NpcLeafHooks MakeLeaf() {
    NpcLeafHooks l{};
    l.queueRequestEntity29 = wFree29;
    l.freeHandlerEntry = wFreeHandler;
    l.packetStatus = wPacketStatus;
    return l;
}

// Drive RunTransport one working tick at lap 4 -> the ++lap makes it 5, triggering
// the ambush roll against the real RNG. Returns the record for inspection.
void RunOneWorkTickAtLap4(HeRecord* hp, Block& sb, Block& gb, Block& cb, Block& vb) {
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    He_Id(sp) = 11; He_Id(gp) = 22;
    g_w.startp = sp; g_w.goalp = gp;
    g_w.cart = reinterpret_cast<HeRecord*>(&cb);
    // cart class byte (+18) == 0 so the ambush threshold is `randMod(30) > 0`.
    *reinterpret_cast<u8*>(HeBytes(g_w.cart) + 18) = 0;
    // the variant-2 sub-step's ApplyTransportSpeed dereferences cart+59 (vehicle).
    { HeRecord* _p = reinterpret_cast<HeRecord*>(&vb); std::memcpy(HeBytes(g_w.cart) + 59, &_p, sizeof(_p)); }

    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    He_Flags(hp) = 2; He_State(hp) = 1;
    He_ReqHandle(hp) = -1;
    Cas7_Variant(hp) = 2; Cas7_MovePkt(hp) = -1; Cas7_Started(hp) = 1;
    Cas7_Lap(hp) = 4;                 // ++lap -> 5 inside RunTransport
    Cas7_Delivered(hp) = 0;
    He_ApptTime(hp).day = 1;          // due (clock day 100) -> proceeds
}

} // namespace

// Seed 12345: real RandNext()%30 == 18 (> 0) -> ambush fires at lap 5.
TEST(CharActionSteps7Itest, AmbushFiresWithRealRng) {
    g_w = Wire{};
    CharActionStep7Hooks h = MakeHooks();
    NpcLeafHooks l = MakeLeaf();
    SetCharActionStep7Hooks(&h);
    SetNpcLeafHooks(&l);
    GameTime clk{}; clk.day = 100; clk.hour = 8;
    SetNpcClock(clk);
    crt::Srand(12345);

    // sanity: confirm the real sibling produces the predicted draw before the run.
    // (consume + re-seed so the run sees the same first draw)
    CHECK_EQ(util::RandomModulo(30), 18);
    crt::Srand(12345);

    Block sb{}, gb{}, cb{}, vb{}, hb{};
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    RunOneWorkTickAtLap4(hp, sb, gb, cb, vb);

    RunTransport(hp);

    CHECK_EQ(Cas7_Lap(hp), 5);
    // ambush path: entity-message + build-op-74 + cmd29(-1) re-arm, then return.
    CHECK_EQ(g_w.entityMsg, 1);
    CHECK_EQ(g_w.buildOp74, 1);
    CHECK_EQ(g_w.free29, 1);
    CHECK_EQ(g_w.lastFree29, -1);
    // ambush returns before the arrival-probability branch sets Delivered.
    CHECK_EQ(Cas7_Delivered(hp), 0);
}

// Seed 13: real RandNext()%30 == 0 (NOT > 0) -> no ambush; falls through to the
// arrival-probability branch which (inert frame counters + workstation 0 + cls 0)
// sets Delivered. Exercises the genuine RNG taking the other branch.
TEST(CharActionSteps7Itest, NoAmbushFallsThroughWithRealRng) {
    g_w = Wire{};
    CharActionStep7Hooks h = MakeHooks();
    NpcLeafHooks l = MakeLeaf();
    SetCharActionStep7Hooks(&h);
    SetNpcLeafHooks(&l);
    GameTime clk{}; clk.day = 100; clk.hour = 8;
    SetNpcClock(clk);
    crt::Srand(13);

    CHECK_EQ(util::RandomModulo(30), 0);   // real sibling: no ambush
    crt::Srand(13);

    Block sb{}, gb{}, cb{}, vb{}, hb{};
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    RunOneWorkTickAtLap4(hp, sb, gb, cb, vb);

    RunTransport(hp);

    CHECK_EQ(Cas7_Lap(hp), 5);
    CHECK_EQ(g_w.buildOp74, 0);            // ambush did NOT fire
    // arrival-probability branch ran: threshold = (i16)cls(0) * prob == 0; the
    // second real draw (RandNext()%105) is > 0 with overwhelming likelihood, so the
    // "robbed" (Delivered=2) branch is taken (roll > threshold).
    CHECK_EQ(Cas7_Delivered(hp), 2);
    CHECK_EQ(g_w.changeZustand, 1);        // RequestChangeZustand(-15) on the robbed path
}
