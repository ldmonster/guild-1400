// End-to-end flow across the CharAction transport coroutine: allocate a transport,
// then drive the RunTransport phase machine through begin -> work -> arrival,
// asserting the coroutine bookkeeping (state, lap, appointment, packet re-arms)
// composes the way the live game ticks a he_Transport handler. All cross-cluster
// leaves are an in-test recording bridge; the GameTime arithmetic is the real
// reconstructed sibling linked into the lib.
#include "test.h"

#include "sim/charaction_steps7.h"
#include "sim/gametime.h"
#include "sim/npcaction.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct Block { u8 b[700]; };

struct Bridge {
    int free29 = 0, lastFree29 = 99, freeHandler = 0;
    int cmd19 = 0, single49 = 0, named53 = 0, switchSlot = 0;
    HeRecord* cart = nullptr;
    HeRecord* veh = nullptr;
    HeRecord* startp = nullptr;
    HeRecord* goalp = nullptr;
    int isProdStart = 1, isProdGoal = 0;
    int prodPhase = 0;
};
Bridge g_b;

// Person/object records carry their entity id at byte +1 in the binary (disasm
// 0x4de0ae, 0x4df8a1), not the He_* +4. Model that for the golden vectors.
i32  Pid(const HeRecord* r) { i32 v; std::memcpy(&v, HeBytes(const_cast<HeRecord*>(r)) + 1, sizeof(v)); return v; }
void SetPid(HeRecord* r, i32 id) { std::memcpy(HeBytes(r) + 1, &id, sizeof(id)); }

HeRecord* bPerson(i32, int, int, i32 key) {
    if (key == 68 || key == 69) return g_b.startp;
    if (g_b.startp && key == Pid(g_b.startp)) return g_b.startp;
    if (g_b.goalp && key == Pid(g_b.goalp)) return g_b.goalp;
    return g_b.startp;
}
HeRecord* bFindObj(i32) { return g_b.cart; }
HeRecord* bObjQuery(i32, int, int, int, i32) { return g_b.cart; }
HeRecord* bResolveEnt(int, HeRecord** out, i32, int) { if (out) *out = g_b.cart; return g_b.cart; }
HeRecord* bBuilding(i32) { return nullptr; }
int  bIsProd(HeRecord*) { return (g_b.prodPhase++ % 2 == 0) ? g_b.isProdStart : g_b.isProdGoal; }
int  bIsStorage(HeRecord*) { return 0; }
int  bSumWork(HeRecord*, int, int) { return 0; }
HeRecord* bWorkProduct(HeRecord*) { return nullptr; }
int  bColVol(HeRecord*) { return 0; }
void bWalk(HeRecord*, bool (*)(HeRecord*, void*), void*) {}
int  bNameEq(HeRecord*, const char*) { return 0; }
i32  bCmd19(i32, i32, i32, i32) { g_b.cmd19++; return 99; }
void bPair51(i32, int) {}
void bArgs25(i32, int, int, int, int) {}
void bSingle49(i32) { g_b.single49++; }
void bNamed53(i32, i32, int, int, int, const char*) { g_b.named53++; }
void bQuad46(i32, int, int, int) {}
void bChrMove(i32, i32, int, int) {}
void bBuildOp74(i32) {}
void bChangeZustand(i32, int) {}
void bQuickjump(i32, i32, int) {}
void bEntityMsg(i32, int) {}
i32  bSwitch(i32, int) { g_b.switchSlot++; return 0; }
int  bNearDoor(HeRecord*, HeRecord*) { return 0; }
int  bWithin(const float*, const float*, float) { return 0; }
i32  bCityRecip(u16) { return 0; }
u8   bCityCat(u16) { return 0; }
int  bRandom(int) { return 0; }
int  bFast() { return 0; }
int  bMed() { return 0; }
u16  bSceneCity() { return 3; }

i32 bFree29(int arg, HeRecord*) { g_b.free29++; g_b.lastFree29 = arg; return 5000; }
i32 bFreeHandler(HeRecord*) { g_b.freeHandler++; return 0; }
i32 bPacketStatus(i32) { return 1; }

CharActionStep7Hooks MakeHooks() {
    CharActionStep7Hooks h{};
    h.personQueryBegin = bPerson; h.findObjectById = bFindObj;
    h.objectQueryFind = bObjQuery; h.resolveEntityById = bResolveEnt;
    h.buildingFindById = bBuilding; h.isProductionType = bIsProd;
    h.isStorageType = bIsStorage; h.sumWorkstation = bSumWork;
    h.findWorkProduct = bWorkProduct; h.collectProductionVolume = bColVol;
    h.walkScene = bWalk; h.nodeNameEquals = bNameEq;
    h.cmdRequest19 = bCmd19; h.cmdRequestPair51 = bPair51;
    h.cmdRequestArgs25 = bArgs25; h.cmdRequestSingle49 = bSingle49;
    h.cmdRequestNamedObject53 = bNamed53; h.cmdRequestQuad46 = bQuad46;
    h.cmdRequestChrMove = bChrMove; h.cmdRequestBuildOp74 = bBuildOp74;
    h.requestChangeZustand = bChangeZustand; h.sendQuickjump = bQuickjump;
    h.sendEntityMessage = bEntityMsg; h.universeSwitchSlot = bSwitch;
    h.isNearDoor = bNearDoor; h.withinTolerance = bWithin;
    h.cityRecipientId = bCityRecip; h.cityCategory = bCityCat;
    h.randomModulo = bRandom; h.fastFrameCounter = bFast;
    h.medFrameCounter = bMed; h.currentSceneCity = bSceneCity;
    return h;
}

NpcLeafHooks MakeLeaf() {
    NpcLeafHooks l{};
    l.queueRequestEntity29 = bFree29;
    l.freeHandlerEntry = bFreeHandler;
    l.packetStatus = bPacketStatus;
    return l;
}

} // namespace

TEST(CharActionSteps7E2E, AllocThenDriveCoroutine) {
    g_b = Bridge{};
    CharActionStep7Hooks h = MakeHooks();
    NpcLeafHooks l = MakeLeaf();
    SetCharActionStep7Hooks(&h);
    SetNpcLeafHooks(&l);
    GameTime clk{}; clk.day = 50; clk.hour = 12;
    SetNpcClock(clk);

    Block sb{}, gb{}, cbk{}, vbk{}, hb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 101); SetPid(gp, 202);   // person id @ +1 (binary layout)
    g_b.startp = sp; g_b.goalp = gp;
    HeRecord* cart = reinterpret_cast<HeRecord*>(&cbk);
    g_b.cart = cart;
    g_b.veh = reinterpret_cast<HeRecord*>(&vbk);
    {  // +59 is unaligned for a pointer; store via memcpy (byte-identical, no UB).
        HeRecord* _p = g_b.veh;
        std::memcpy(HeBytes(cart) + 59, &_p, sizeof(_p));
    }

    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 101; Cas7_GoalId(hp) = 202; Cas7_CartId(hp) = 303;
    // saved time (+68) drives AllocTransport's appointment snapshot.
    He_SavedTime(hp).day = 50; He_SavedTime(hp).hour = 12;

    // --- Phase A: allocate ----------------------------------------------
    g_b.isProdStart = 1; g_b.isProdGoal = 0;     // -> local variant
    HeRecord* allocd = AllocTransport(hp);
    CHECK(allocd == cart);
    if (allocd) {
        CHECK_EQ(Cas7_Variant(hp), 2);            // local
        CHECK_EQ(Cas7_Started(hp), 0);
        CHECK_EQ(He_ReqHandle(hp), -1);
        CHECK_EQ(Cas7_MovePkt(hp), -1);
        // appointment snapshot from saved-time (+1 second) -> day unchanged.
        CHECK_EQ(He_ApptTime(hp).day, 50);
        CHECK_EQ(He_ApptTime(hp).second, 1);
    }

    // --- Phase B: begin (state 0) arms the next-day wake ----------------
    He_Flags(hp) = 2;
    He_State(hp) = 0;
    He_ApptTime(hp).day = 50; He_ApptTime(hp).hour = 12; He_ApptTime(hp).second = 0;
    RunTransport(hp);
    CHECK_EQ(Cas7_Window(hp), -1);
    CHECK_EQ(He_ApptTime(hp).day, 50);            // +1 second, day unchanged
    CHECK_EQ(He_ApptTime(hp).second, 1);
    CHECK_EQ(g_b.free29, 1);
    CHECK_EQ(g_b.lastFree29, 1);                  // state+1 == 1

    // --- Phase C: working tick (state 1) dispatches the local sub-step --
    He_State(hp) = 1;
    He_ReqHandle(hp) = -1;
    Cas7_MovePkt(hp) = -1;
    Cas7_Started(hp) = 0;                         // first leg
    He_ApptTime(hp).day = 1;                      // due (clock day 50)
    int free29Before = g_b.free29;
    RunTransport(hp);
    CHECK_EQ(Cas7_Lap(hp), 1);                    // lap bumped
    CHECK_EQ(Cas7_Started(hp), 1);                // local first-leg armed the move
    CHECK_EQ(g_b.single49, 1);                    // sub-step queued the cart move
    CHECK_EQ(g_b.named53, 1);
    CHECK_EQ(g_b.cmd19, 1);
    CHECK(g_b.switchSlot >= 1);
    CHECK_EQ(g_b.free29, free29Before);           // no extra cmd29 on the first leg
    // appointment re-stamped from the clock (day 50) then +5 minutes.
    CHECK_EQ(He_ApptTime(hp).day, 50);
    CHECK_EQ(He_ApptTime(hp).minute, 5);

    // --- Phase D: arrival (state -2) re-arms when origin matches goal ---
    He_State(hp) = -2;
    He_Flags(hp) = 0;
    Cas7_OriginId(hp) = 202;                      // == goal id
    int free29D = g_b.free29;
    RunTransport(hp);
    // binary SetTargetCityRef @0x4c9484: *(h+8)=city(3), *(h+12)=cityRecipientId(3)
    // (bCityRecip returns 0). Origin(202)==Pid(goalp) -> arm cmd29(1).
    CHECK_EQ(He_CityIndex(hp), 3);
    CHECK_EQ(He_CityId(hp), 0);
    CHECK_EQ(g_b.free29, free29D + 1);
    CHECK_EQ(g_b.lastFree29, 1);                  // arrival arms cmd29(1)

    // --- Phase E: finish (state -1) clears the cart + frees the handler -
    He_State(hp) = -1;
    He_Flags(hp) = 0;
    *reinterpret_cast<u8*>(HeBytes(cart) + 19) = 0x40;       // in-transit flag set
    {  // +36 is unaligned for a 64-bit pointer; store via memcpy (no UB).
        HeRecord* _p = hp;
        std::memcpy(HeBytes(cart) + 36, &_p, sizeof(_p));     // owner == hp
    }
    RunTransport(hp);
    CHECK_EQ(*reinterpret_cast<i32*>(HeBytes(cart) + 36), 0);          // owner cleared
    CHECK_EQ(*reinterpret_cast<u8*>(HeBytes(cart) + 19) & 0x40, 0);    // flag cleared
    CHECK_EQ(g_b.freeHandler, 1);                  // handler released
}
