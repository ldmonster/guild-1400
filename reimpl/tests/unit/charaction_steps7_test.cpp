#include "test.h"
#include "sim/charaction_steps7.h"
#include "sim/charaction_steps5.h"
#include "sim/gametime.h"
#include "sim/npcaction.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// Recording mock for the CharActionStep7 + NpcLeaf hook bridges. Each function
// records the calls it makes so the golden vectors can assert the cross-cluster
// control flow without any real cluster wired in.
// ===========================================================================
namespace {

struct Rec {
    // call counters
    int personQuery = 0, findObj = 0, objQuery = 0, resolveEnt = 0;
    int cmd19 = 0, cmdPair51 = 0, cmdArgs25 = 0, cmdSingle49 = 0;
    int cmdNamed53 = 0, cmdQuad46 = 0, cmdChrMove = 0, cmdBuildOp74 = 0;
    int changeZustand = 0, quickjump = 0, entityMsg = 0, switchSlot = 0;
    int walk = 0, randCalls = 0, free29 = 0, freeHandler = 0, packetQ = 0;
    // programmable returns
    HeRecord* objToReturn = nullptr;
    HeRecord* entToReturn = nullptr;
    HeRecord* startToReturn = nullptr;
    HeRecord* goalToReturn = nullptr;
    int isProdStart = 0, isProdGoal = 0;
    int isStorage = 0, nearDoor = 0, within = 0;
    int randValue = 0;
    int packetStatusValue = 1;   // applied
    u8  cityCat = 0;
    // last cmd29 arg seen
    int lastFree29Arg = 99;
    // queue of personQuery returns by key
};

Rec g_rec;

// Person/object records store their entity id at byte offset +1 (a misaligned dword)
// in the binary — NOT at the He_* +4 used by the h-record family (disasm 0x4de0ae,
// 0x4df8a1). These helpers read/write that +1 id so the golden vectors model the
// binary's actual layout.
i32  Pid(const HeRecord* r) { i32 v; std::memcpy(&v, HeBytes(const_cast<HeRecord*>(r)) + 1, sizeof(v)); return v; }
void SetPid(HeRecord* r, i32 id) { std::memcpy(HeBytes(r) + 1, &id, sizeof(id)); }

// --- CharActionStep7Hooks --------------------------------------------------
HeRecord* mPersonQuery(i32, int, int, i32 key) {
    g_rec.personQuery++;
    // keys 68/69 -> begin/aux endpoints (reuse start); start key / goal key map.
    if (key == 68 || key == 69) return g_rec.startToReturn;
    if (g_rec.startToReturn && key == Pid(g_rec.startToReturn)) return g_rec.startToReturn;
    if (g_rec.goalToReturn && key == Pid(g_rec.goalToReturn)) return g_rec.goalToReturn;
    // first non-endpoint resolves to start, fall back to goal
    return g_rec.startToReturn ? g_rec.startToReturn : g_rec.goalToReturn;
}
HeRecord* mFindObj(i32) { g_rec.findObj++; return g_rec.objToReturn; }
HeRecord* mObjQuery(i32, int, int, int, i32) { g_rec.objQuery++; return g_rec.objToReturn; }
HeRecord* mResolveEnt(int, HeRecord** out, i32, int) {
    g_rec.resolveEnt++;
    if (out) *out = g_rec.entToReturn;
    return g_rec.entToReturn;
}
HeRecord* mBuildingFind(i32) { return nullptr; }
int  mIsProdStart(HeRecord* r) {
    // first call (start) returns isProdStart, second (goal) returns isProdGoal.
    static int phase = 0;
    int v = (phase++ % 2 == 0) ? g_rec.isProdStart : g_rec.isProdGoal;
    (void)r; return v;
}
int  mIsStorage(HeRecord*) { return g_rec.isStorage; }
int  mSumWork(HeRecord*, int, int) { return 0; }
HeRecord* mFindProduct(HeRecord*) { return nullptr; }
int  mCollectVol(HeRecord*) { return 0; }
void mWalk(HeRecord*, bool (*cb)(HeRecord*, void*), void* ctx) {
    g_rec.walk++;
    // The real scene walk feeds nodes; here we feed nothing (count stays 0).
    (void)cb; (void)ctx;
}
int  mNameEq(HeRecord*, const char*) { return 0; }
i32  mCmd19(i32, i32, i32, i32) { g_rec.cmd19++; return 4242; }
void mPair51(i32, int) { g_rec.cmdPair51++; }
void mArgs25(i32, int, int, int, int) { g_rec.cmdArgs25++; }
void mSingle49(i32) { g_rec.cmdSingle49++; }
void mNamed53(i32, i32, int, int, int, const char*) { g_rec.cmdNamed53++; }
void mQuad46(i32, int, int, int) { g_rec.cmdQuad46++; }
void mChrMove(i32, i32, int, int) { g_rec.cmdChrMove++; }
void mBuildOp74(i32) { g_rec.cmdBuildOp74++; }
void mChangeZustand(i32, int) { g_rec.changeZustand++; }
void mQuickjump(i32, i32, int) { g_rec.quickjump++; }
void mEntityMsg(i32, int) { g_rec.entityMsg++; }
i32  mSwitchSlot(i32, int) { g_rec.switchSlot++; return 7; }
int  mNearDoor(HeRecord*, HeRecord*) { return g_rec.nearDoor; }
int  mWithin(const float*, const float*, float) { return g_rec.within; }
i32  mCityRecip(u16) { return 555; }
u8   mCityCat(u16) { return g_rec.cityCat; }
int  mRandom(int n) { g_rec.randCalls++; return n ? (g_rec.randValue % n) : 0; }
int  mFast() { return 0; }
int  mMed() { return 0; }
u16  mSceneCity() { return 9; }

CharActionStep7Hooks MakeHooks() {
    CharActionStep7Hooks h{};
    h.personQueryBegin = mPersonQuery;
    h.findObjectById = mFindObj;
    h.objectQueryFind = mObjQuery;
    h.resolveEntityById = mResolveEnt;
    h.buildingFindById = mBuildingFind;
    h.isProductionType = mIsProdStart;
    h.isStorageType = mIsStorage;
    h.sumWorkstation = mSumWork;
    h.findWorkProduct = mFindProduct;
    h.collectProductionVolume = mCollectVol;
    h.walkScene = mWalk;
    h.nodeNameEquals = mNameEq;
    h.cmdRequest19 = mCmd19;
    h.cmdRequestPair51 = mPair51;
    h.cmdRequestArgs25 = mArgs25;
    h.cmdRequestSingle49 = mSingle49;
    h.cmdRequestNamedObject53 = mNamed53;
    h.cmdRequestQuad46 = mQuad46;
    h.cmdRequestChrMove = mChrMove;
    h.cmdRequestBuildOp74 = mBuildOp74;
    h.requestChangeZustand = mChangeZustand;
    h.sendQuickjump = mQuickjump;
    h.sendEntityMessage = mEntityMsg;
    h.universeSwitchSlot = mSwitchSlot;
    h.isNearDoor = mNearDoor;
    h.withinTolerance = mWithin;
    h.cityRecipientId = mCityRecip;
    h.cityCategory = mCityCat;
    h.randomModulo = mRandom;
    h.fastFrameCounter = mFast;
    h.medFrameCounter = mMed;
    h.currentSceneCity = mSceneCity;
    return h;
}

// --- NpcLeafHooks (only the three used here) -------------------------------
i32 mFree29(int arg, HeRecord*) { g_rec.free29++; g_rec.lastFree29Arg = arg; return 7777; }
i32 mFreeHandler(HeRecord*) { g_rec.freeHandler++; return 0; }
i32 mPacketStatus(i32) { g_rec.packetQ++; return g_rec.packetStatusValue; }

NpcLeafHooks MakeLeaf() {
    NpcLeafHooks l{};
    l.queueRequestEntity29 = mFree29;
    l.freeHandlerEntry = mFreeHandler;
    l.packetStatus = mPacketStatus;
    return l;
}

// Zeroed He record big enough for every +offset this module touches (the record
// tops out near +416 on the vehicle indirection; a 700-byte block is ample for the
// 'h' record itself, and we point the vehicle/cart pointers at separate blocks).
struct Block { u8 b[700]; };

void ResetEnv(CharActionStep7Hooks& h, NpcLeafHooks& l) {
    g_rec = Rec{};
    h = MakeHooks();
    l = MakeLeaf();
    SetCharActionStep7Hooks(&h);
    SetNpcLeafHooks(&l);
    GameTime clk{}; clk.day = 100; clk.hour = 8; clk.minute = 0; clk.second = 0;
    SetNpcClock(clk);
}

} // namespace

// ===========================================================================
// CollectTransporterCb — name match + cap-32 stop condition.
// ===========================================================================
TEST(CharActionSteps7, CollectCbAppendsOnMatch) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    // override name match to always-equal for this test.
    struct Local { static int eq(HeRecord*, const char*) { return 1; } };
    h.nodeNameEquals = Local::eq; SetCharActionStep7Hooks(&h);

    TransporterCollector ctx{};
    Block n0, n1;
    bool keep0 = CollectTransporterCb(reinterpret_cast<HeRecord*>(&n0), &ctx);
    bool keep1 = CollectTransporterCb(reinterpret_cast<HeRecord*>(&n1), &ctx);
    CHECK_EQ(ctx.count, 2);
    CHECK(keep0);
    CHECK(keep1);
    CHECK(ctx.nodes[0] == reinterpret_cast<HeRecord*>(&n0));
    CHECK(ctx.nodes[1] == reinterpret_cast<HeRecord*>(&n1));
}

TEST(CharActionSteps7, CollectCbNoMatchKeepsWalking) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    TransporterCollector ctx{};
    Block n0;
    bool keep = CollectTransporterCb(reinterpret_cast<HeRecord*>(&n0), &ctx);
    CHECK_EQ(ctx.count, 0);
    CHECK(keep);            // count (0) < 32
}

TEST(CharActionSteps7, CollectCbStopsAt32) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    struct Local { static int eq(HeRecord*, const char*) { return 1; } };
    h.nodeNameEquals = Local::eq; SetCharActionStep7Hooks(&h);
    TransporterCollector ctx{};
    Block n;
    bool keep = true;
    for (int i = 0; i < 32; ++i)
        keep = CollectTransporterCb(reinterpret_cast<HeRecord*>(&n), &ctx);
    CHECK_EQ(ctx.count, 32);
    CHECK(!keep);          // 32 < 32 is false -> stop
}

// ===========================================================================
// PickRandomTransporter — empty walk returns null; the gate path runs the walk.
// ===========================================================================
TEST(CharActionSteps7, PickRandomEmptyReturnsNull) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block root{};
    HeRecord* r = PickRandomTransporter(reinterpret_cast<HeRecord*>(&root));
    CHECK(r == nullptr);
    CHECK_EQ(g_rec.walk, 1);
}

TEST(CharActionSteps7, PickRandomNullRootTakesGate) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    HeRecord* r = PickRandomTransporter(nullptr);
    CHECK(r == nullptr);
    CHECK_EQ(g_rec.walk, 1);   // null root -> rawGate path, single walk
}

// ===========================================================================
// AllocTransport — variant classification + free on missing endpoint.
// ===========================================================================
TEST(CharActionSteps7, AllocFreesWhenStartAbsent) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    g_rec.startToReturn = nullptr;
    Block hb{};
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    HeRecord* r = AllocTransport(hp);
    CHECK(r == nullptr);
    CHECK_EQ(g_rec.freeHandler, 1);
}

TEST(CharActionSteps7, AllocClassifiesLocalVariant) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, vb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp;
    g_rec.goalToReturn = gp;
    // isProd(start)=1, isProd(goal)=0 -> NOT door, NOT city-city, NOT mixed -> local(2)
    g_rec.isProdStart = 1; g_rec.isProdGoal = 0;
    // cart resolve succeeds.
    HeRecord* cart = reinterpret_cast<HeRecord*>(&cb);
    HeRecord* veh = reinterpret_cast<HeRecord*>(&vb);
    { HeRecord* _p = veh; std::memcpy(HeBytes(cart) + 59, &_p, sizeof(_p)); }
    g_rec.entToReturn = cart;

    Block hb{};
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    HeRecord* r = AllocTransport(hp);
    if (r) {
        CHECK_EQ(Cas7_Variant(hp), 2);     // local
        CHECK_EQ(Cas7_Started(hp), 0);
        CHECK_EQ(He_ReqHandle(hp), -1);
        CHECK_EQ(Cas7_MovePkt(hp), -1);
    }
    CHECK(r == cart);
}

TEST(CharActionSteps7, AllocClassifiesDoorVariant) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, vb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    // isProd(start)=0, isProd(goal)=1 -> door(1)
    g_rec.isProdStart = 0; g_rec.isProdGoal = 1;
    HeRecord* cart = reinterpret_cast<HeRecord*>(&cb);
    { HeRecord* _p = reinterpret_cast<HeRecord*>(&vb); std::memcpy(HeBytes(cart) + 59, &_p, sizeof(_p)); }
    g_rec.entToReturn = cart;
    Block hb{};
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    HeRecord* r = AllocTransport(hp);
    if (r) CHECK_EQ(Cas7_Variant(hp), 1);
    CHECK(r == cart);
}

TEST(CharActionSteps7, AllocClassifiesCityCityVariant) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, vb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    // isProd(start)=0, isProd(goal)=0 -> city-city(3)
    g_rec.isProdStart = 0; g_rec.isProdGoal = 0;
    HeRecord* cart = reinterpret_cast<HeRecord*>(&cb);
    { HeRecord* _p = reinterpret_cast<HeRecord*>(&vb); std::memcpy(HeBytes(cart) + 59, &_p, sizeof(_p)); }
    g_rec.entToReturn = cart;
    Block hb{};
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    HeRecord* r = AllocTransport(hp);
    if (r) CHECK_EQ(Cas7_Variant(hp), 3);
    CHECK(r == cart);
}

// ===========================================================================
// RunTransport phase machine.
// ===========================================================================
TEST(CharActionSteps7, RunTransportState0ArmsNextDay) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, hb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    g_rec.objToReturn = reinterpret_cast<HeRecord*>(&cb);   // cart present

    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    He_Flags(hp) = 2;            // needs cmd29
    He_State(hp) = 0;
    He_ApptTime(hp).day = 100; He_ApptTime(hp).hour = 8;

    He_ApptTime(hp).second = 0;
    RunTransport(hp);
    CHECK_EQ(Cas7_Window(hp), -1);
    CHECK_EQ(He_ApptTime(hp).day, 100);     // +1 second does not roll the day
    CHECK_EQ(He_ApptTime(hp).second, 1);    // VIBE_GameTime_Advance(+82, 0, 1, 0)
    CHECK_EQ(g_rec.free29, 1);
    CHECK_EQ(g_rec.lastFree29Arg, 1);        // state+1
}

TEST(CharActionSteps7, RunTransportState0NoFlagDoesNothing) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, hb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    g_rec.objToReturn = reinterpret_cast<HeRecord*>(&cb);
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    He_Flags(hp) = 0;            // no cmd29
    He_State(hp) = 0;
    He_ApptTime(hp).day = 100;
    RunTransport(hp);
    CHECK_EQ(He_ApptTime(hp).day, 100);   // unchanged
    CHECK_EQ(g_rec.free29, 0);
}

TEST(CharActionSteps7, RunTransportMissingCartFrees) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, hb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    g_rec.objToReturn = nullptr;        // cart gone
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    He_Flags(hp) = 2; He_State(hp) = 1;
    RunTransport(hp);
    CHECK_EQ(g_rec.freeHandler, 1);
}

TEST(CharActionSteps7, RunTransportState1WaitsUntilAppointment) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, hb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    g_rec.objToReturn = reinterpret_cast<HeRecord*>(&cb);
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    He_Flags(hp) = 2; He_State(hp) = 1;
    He_ReqHandle(hp) = -1;
    // appointment in the future (day 200) -> compare >= 0 -> return early, no work.
    He_ApptTime(hp).day = 200; He_ApptTime(hp).hour = 8;
    int lapBefore = Cas7_Lap(hp);
    RunTransport(hp);
    CHECK_EQ(Cas7_Lap(hp), lapBefore);   // no sub-step ran
    CHECK_EQ(g_rec.switchSlot, 0);
}

TEST(CharActionSteps7, RunTransportState1DispatchesLocalAndBumpsLap) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, vb{}, hb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    HeRecord* cart = reinterpret_cast<HeRecord*>(&cb);
    { HeRecord* _p = reinterpret_cast<HeRecord*>(&vb); std::memcpy(HeBytes(cart) + 59, &_p, sizeof(_p)); }
    g_rec.objToReturn = cart;
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    He_Flags(hp) = 2; He_State(hp) = 1;
    He_ReqHandle(hp) = -1;
    Cas7_Variant(hp) = 2;          // local
    Cas7_MovePkt(hp) = -1;
    Cas7_Started(hp) = 0;          // first leg
    // appointment in the past (day 1) -> proceeds to work.
    He_ApptTime(hp).day = 1; He_ApptTime(hp).hour = 0;
    RunTransport(hp);
    CHECK_EQ(Cas7_Lap(hp), 1);
    CHECK_EQ(Cas7_Started(hp), 1);          // local first-leg armed it
    CHECK(g_rec.switchSlot >= 1);           // sub-step called universeSwitchSlot
    // re-stamped appointment then +5 minutes from the day-100 clock.
    CHECK_EQ(He_ApptTime(hp).day, 100);
    CHECK_EQ(He_ApptTime(hp).minute, 5);
}

TEST(CharActionSteps7, RunTransportStateMinus2ArmsOnEndpointMatch) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, hb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    // binary: RunTransport state -2 compares OriginId(+196) to *(gp+1) — the person
    // id at byte +1 (disasm 0x4df8a1..0x4df8ba), not the He_* +4 id.
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    g_rec.objToReturn = reinterpret_cast<HeRecord*>(&cb);
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    He_Flags(hp) = 0;
    He_State(hp) = -2;
    Cas7_OriginId(hp) = 22;        // matches *(gp+1) -> arm cmd29(1)
    RunTransport(hp);
    // binary VIBE_NpcAction_SetTargetCityRef @0x4c9484: *(h+8)=city (9),
    // *(h+12)=dword_12CE914[134*city] == cityRecipientId(9) (mock returns 555).
    CHECK_EQ(He_CityIndex(hp), 9);
    CHECK_EQ(He_CityId(hp), 555);
    CHECK_EQ(g_rec.free29, 1);
    CHECK_EQ(g_rec.lastFree29Arg, 1);
}

// ===========================================================================
// RunTransportDoor sub-step gating.
// ===========================================================================
TEST(CharActionSteps7, DoorFirstLegArmsMove) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, vb{}, hb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    HeRecord* cart = reinterpret_cast<HeRecord*>(&cb);
    { HeRecord* _p = reinterpret_cast<HeRecord*>(&vb); std::memcpy(HeBytes(cart) + 59, &_p, sizeof(_p)); }
    g_rec.objToReturn = cart;
    g_rec.isStorage = 0;
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    Cas7_Started(hp) = 0;
    i32 r = RunTransportDoor(hp);
    CHECK_EQ(Cas7_Started(hp), 1);
    CHECK_EQ(g_rec.cmdSingle49, 1);
    CHECK_EQ(g_rec.cmdNamed53, 1);
    CHECK_EQ(g_rec.cmd19, 1);
    CHECK_EQ(r, 7);                  // universeSwitchSlot return
}

TEST(CharActionSteps7, DoorRunningNotAtDoorJustSwitches) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, vb{}, hb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    HeRecord* cart = reinterpret_cast<HeRecord*>(&cb);
    { HeRecord* _p = reinterpret_cast<HeRecord*>(&vb); std::memcpy(HeBytes(cart) + 59, &_p, sizeof(_p)); }
    g_rec.objToReturn = cart;
    g_rec.nearDoor = 0;
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    Cas7_Started(hp) = 1;
    RunTransportDoor(hp);
    CHECK_EQ(g_rec.cmd19, 0);        // no unload move
    CHECK_EQ(g_rec.free29, 0);
}

TEST(CharActionSteps7, DoorAtDoorUnloadsAndArms) {
    CharActionStep7Hooks h; NpcLeafHooks l; ResetEnv(h, l);
    Block sb{}, gb{}, cb{}, vb{}, hb{};
    HeRecord* sp = reinterpret_cast<HeRecord*>(&sb);
    HeRecord* gp = reinterpret_cast<HeRecord*>(&gb);
    SetPid(sp, 11); SetPid(gp, 22);
    g_rec.startToReturn = sp; g_rec.goalToReturn = gp;
    HeRecord* cart = reinterpret_cast<HeRecord*>(&cb);
    { HeRecord* _p = reinterpret_cast<HeRecord*>(&vb); std::memcpy(HeBytes(cart) + 59, &_p, sizeof(_p)); }
    g_rec.objToReturn = cart;
    g_rec.nearDoor = 1;
    g_rec.cityCat = 6;
    HeRecord* hp = reinterpret_cast<HeRecord*>(&hb);
    Cas7_StartId(hp) = 11; Cas7_GoalId(hp) = 22; Cas7_CartId(hp) = 33;
    Cas7_Started(hp) = 1;
    RunTransportDoor(hp);
    CHECK_EQ(g_rec.cmd19, 1);        // unload move queued
    CHECK_EQ(g_rec.quickjump, 1);    // city category 6 -> narrative push
    CHECK_EQ(g_rec.free29, 1);
}
