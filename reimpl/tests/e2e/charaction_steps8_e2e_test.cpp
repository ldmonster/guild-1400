// End-to-end flow for charaction_steps8: drive a full CharAction coroutine across
// its phases using the installable hook table, asserting the cross-phase state
// machine and GameTime scheduling behave as a unit. Two flows are exercised:
//   (1) the arrest lifecycle: InitArrestPerson -> RunArrestPerson (work*N) ->
//       RunArrestPerson(finish), tracking the iteration counter and the appointment
//       clock advancing each tick.
//   (2) the herd lifecycle: RunHerdAnimals through the move (state0->2) and produce
//       (state2->3) phases, checking the state transitions and the produced-goods
//       command on the third phase.
// GameTime advances use the real reconstructed GameTimeAdvance; leaves are inert
// recordings. CHECK does not abort; every deref is guarded.
#include "test.h"

#include "sim/charaction_steps8.h"
#include "sim/gametime.h"
#include "sim/npcaction.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct Block { u8 b[700]; };
inline HeRecord* H(Block& b) { return reinterpret_cast<HeRecord*>(b.b); }
inline void Zero(Block& b) { std::memset(b.b, 0, sizeof(b.b)); }

struct E2E {
    int freeHandler = 0, args25 = 0, single49 = 0, named53 = 0, req17 = 0;
    HeRecord* person = nullptr;
    HeRecord* pen = nullptr;
    int rng = 0;
};
E2E e;

i32 eFree(HeRecord*) { e.freeHandler++; return 7; }
i32 ePacket(i32) { return 1; }
i32 eEnt29Leaf(int, HeRecord*) { return 0; }
i32 eLaw() { return 0; }
int eInv(int, i32*) { return 0; }
void eShuf(int, i32*) {}
void eB93(i32,int,i32,u8) {}
NpcLeafHooks eLeaf = { eEnt29Leaf, eFree, ePacket, eLaw, eInv, eShuf, eB93 };

HeRecord* ePersonBegin(i32, int, int, i32) { return e.person; }
HeRecord* ePersonById(i32) { return e.person; }
HeRecord* eActive(HeRecord*) { return nullptr; }
HeRecord* eObjQuery(i32, int, int, int, i32 key) { return (key == 42) ? e.pen : nullptr; }
HeRecord* eResolve(int, HeRecord** o, i32, int) { if (o) *o = nullptr; return nullptr; }
int  eChildMoney(i32) { return 0; }
HeRecord* eNull1(i32) { return nullptr; }
HeRecord* eNullR(HeRecord*) { return nullptr; }
int  eSum(HeRecord*, int, int) { return 0; }
int  eRoom(HeRecord*, int, i32) { return 0; }
int  eProd(GameTime*, const GameTime*) { return 0; }
HeRecord* eFF(int, int, int) { return nullptr; }
HeRecord* eFN() { return nullptr; }
void eArgs25(i32, int, int, int, int) { e.args25++; }
void eSingle49(i32) { e.single49++; }
void eNamed53(i32, i32, int, int, int, const char*) { e.named53++; }
void eFlag55(i32, int) {}
void eReq17(i32, int, int, int, int, int) { e.req17++; }
void eSlot28(void*, int) {}
i32  eEnt29(i32, void*) { return 0; }
void eState22() {}
void eBegin(i32, i32) {}
void eRaw(unsigned, unsigned, void*, int) {}
void eDelta(unsigned, unsigned, void*, int) {}
void eEnq15(i32, i32, int, u8) {}
void eChangeZ(i32, int, i32) {}
void eRender(char* d, int, int, int, int) { if (d) d[0] = 0; }
void eQuick(i32, i32, i32, const char*, int) {}
void eErr(const char*) {}
void eAction(i32, int, int, int) {}
int  eDestroy(i32) { return 0; }
int  eMesh() { return 0; }
void eRel(int) {}
void* eAttach(i32, bool, int) { return nullptr; }
int  eRng(int) { return e.rng; }
i32  eCity(u16) { return 0; }
u8   eCat(u16) { return 0; }

CharActionStep8Hooks MakeE2E() {
    CharActionStep8Hooks h{};
    h.personQueryBegin = ePersonBegin; h.personFindRecordById = ePersonById;
    h.personFindActiveByEntity = eActive; h.objectQueryFind = eObjQuery;
    h.resolveEntityById = eResolve; h.sumChildMoney = eChildMoney;
    h.buildingFindById = eNull1; h.findWorkProduct = eNullR; h.findStorable = eNullR;
    h.sumWorkstation = eSum; h.computeRoomWorth = eRoom; h.productionOutput = eProd;
    h.heFindFirst = eFF; h.heFindNext = eFN;
    h.cmdRequestArgs25 = eArgs25; h.cmdRequestSingle49 = eSingle49;
    h.cmdRequestNamedObject53 = eNamed53; h.cmdRequestFlag55 = eFlag55;
    h.cmdRequest17 = eReq17; h.cmdRequestSlotReset28 = eSlot28;
    h.cmdRequestEntity29 = eEnt29; h.cmdRequestState22 = eState22;
    h.cmdBeginDeltaPacket = eBegin; h.cmdAppendRawField = eRaw;
    h.cmdAppendDeltaField = eDelta; h.cmdEnqueue15 = eEnq15;
    h.requestChangeZustand = eChangeZ; h.renderFormatted = eRender;
    h.sendQuickjump = eQuick; h.reportError = eErr; h.changePlayerAction = eAction;
    h.characterDestroy = eDestroy; h.animFindFreeMeshSlot = eMesh;
    h.animReleaseMeshData = eRel; h.attachAni = eAttach; h.randomModulo = eRng;
    h.cityRecipientId = eCity; h.cityCategory = eCat;
    h.poolSlot = nullptr; h.poolClear = nullptr;
    return h;
}

GameTime MkClock(int d, int hr, int mn, int sc) {
    GameTime t{}; t.day = d; t.hour = static_cast<u16>(hr); t.minute = mn; t.second = sc;
    return t;
}

} // namespace

TEST(CharActionSteps8E2E, ArrestLifecycle) {
    e = E2E{};
    SetNpcLeafHooks(&eLeaf);
    CharActionStep8Hooks h = MakeE2E();
    Block person; Zero(person);
    person.b[0] = 3;                    // ordinary class
    {  // +37 is unaligned for u16; store via memcpy (byte-identical, no UB).
        u16 _v = 0xFFFF;
        std::memcpy(person.b + 37, &_v, sizeof(_v));
    }
    e.person = reinterpret_cast<HeRecord*>(person.b);
    e.rng = 0;                          // every roll 0 -> +4h wakes, no escape branch
    SetCharActionStep8Hooks(&h);

    Block blk; Zero(blk);
    He_Flags(H(blk)) = 0;
    SetNpcClock(MkClock(20, 6, 0, 0));

    // setup: arms the arrest cmd, iter cleared, packet handle -1.
    HeRecord* r0 = InitArrestPerson(H(blk));
    CHECK(r0 == H(blk));
    CHECK_EQ(g_walkTargetA == g_walkTargetA, true);  // touch a symbol to keep linked
    CHECK_EQ(Cas8_Iter(H(blk)), 0);
    CHECK_EQ(He_ReqHandle(H(blk)), -1);
    CHECK_EQ(static_cast<int>(He_ApptTime(H(blk)).hour), 10);  // 6 + 4

    // drive the working phase three times; iter should climb 1,2,3 and the
    // appointment hour should re-advance +4 each tick from the live clock.
    He_State(H(blk)) = 0;
    for (int i = 1; i <= 3; ++i) {
        SetNpcClock(MkClock(20, 6, 0, 0));   // reset clock each tick for determinism
        i32 rr = RunArrestPerson(H(blk), 0, e.person);
        CHECK_EQ(Cas8_Iter(H(blk)), i);
        CHECK_EQ(rr, 10);                    // hour after +4
        He_State(H(blk)) = 0;                // stay in work
    }

    // finish phase frees the handler exactly once.
    He_State(H(blk)) = -2;
    i32 rf = RunArrestPerson(H(blk), 0, e.person);
    CHECK_EQ(rf, 7);
    CHECK_EQ(e.freeHandler, 1);
    SetCharActionStep8Hooks(nullptr);
}

TEST(CharActionSteps8E2E, HerdLifecycle) {
    e = E2E{};
    SetNpcLeafHooks(&eLeaf);
    CharActionStep8Hooks h = MakeE2E();
    Block animal; Zero(animal);
    *reinterpret_cast<i32*>(animal.b + 91 * 4) = 7;  // animal char nonzero
    e.person = reinterpret_cast<HeRecord*>(animal.b);
    Block pen; Zero(pen);
    e.pen = reinterpret_cast<HeRecord*>(pen.b);
    e.rng = 0;
    SetCharActionStep8Hooks(&h);
    SetNpcClock(MkClock(30, 9, 0, 0));

    Block blk; Zero(blk);
    blk.b[172] = 3;                    // 3 animals
    Cas8_Member(H(blk), 0) = 10;
    Cas8_Member(H(blk), 1) = 11;
    Cas8_Member(H(blk), 2) = 12;

    // move phase (state 0 -> +2 == 2): issue 3 moves, ++state to 1.
    He_State(H(blk)) = 0;
    i32 r2 = RunHerdAnimals(H(blk), 0);
    CHECK_EQ(e.single49, 3);
    CHECK_EQ(e.named53, 3);
    CHECK_EQ(He_State(H(blk)), 1);
    CHECK_EQ(static_cast<int>(He_ApptTime(H(blk)).hour), 11);  // 9 + 2
    CHECK_EQ(r2, 11);

    // produce phase (state 1 -> +2 == 3): the appointment is now in the future
    // relative to the live clock, so GameTimeCompare(appt, clock) < 0 enters the
    // produce branch and emits one cmd17, then resets state to -1.
    SetNpcClock(MkClock(30, 20, 0, 0));   // clock now past the appointment
    RunHerdAnimals(H(blk), 0);
    CHECK_EQ(e.req17, 1);
    CHECK_EQ(He_State(H(blk)), -1);
    SetCharActionStep8Hooks(nullptr);
}
