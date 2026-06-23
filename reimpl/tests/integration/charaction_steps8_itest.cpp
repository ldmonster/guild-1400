// Integration test for charaction_steps8 wired against a REAL reconstructed sibling:
// the CharAction step machines' RandomModulo draws are forwarded into the genuine
// util::RandomModulo (gilde 0x58b89c), which itself runs the real crt::RandNext LCG
// (crt/rand.cpp). This is exactly how the live game wires VIBE_Math_RandomModulo, so
// the arrest wake-roll (RandomModulo(3)+4) and the herd move-roll (RandomModulo(30))
// are exercised against the genuine RNG (not a canned mock), seeded via crt::Srand
// for determinism. The GameTimeAdvance/Compare siblings are likewise the real
// reconstructed functions linked into the lib. Golden RNG draws were computed with a
// standalone probe over the same Srand/RandNext path.
#include "test.h"

#include "sim/charaction_steps8.h"
#include "sim/gametime.h"
#include "sim/npcaction.h"
#include "util/math_random.h"   // REAL sibling
#include "crt/rand.h"           // REAL LCG behind it

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct Block { u8 b[700]; };
inline HeRecord* H(Block& b) { return reinterpret_cast<HeRecord*>(b.b); }
inline void Zero(Block& b) { std::memset(b.b, 0, sizeof(b.b)); }
// Byte-exact unaligned store (avoid binding a u16& to a misaligned address, UB).
inline void Poke16(u8* p, int off, u16 v) { std::memcpy(p + off, &v, sizeof(v)); }

// The randomModulo hook -> REAL util::RandomModulo (which calls crt::RandNext).
int RealRandomModulo(int n) { return util::RandomModulo(static_cast<u16>(n)); }

struct W {
    int free = 0, args25 = 0, single49 = 0, named53 = 0;
    HeRecord* person = nullptr;
};
W w;

i32 wFree(HeRecord*) { w.free++; return 7; }
i32 wPacket(i32) { return 1; }
i32 wEnt29Leaf(int, HeRecord*) { return 0; }
i32 wLaw() { return 0; }
int wInv(int, i32*) { return 0; }
void wShuf(int, i32*) {}
void wB93(i32,int,i32,u8) {}
NpcLeafHooks wLeaf = { wEnt29Leaf, wFree, wPacket, wLaw, wInv, wShuf, wB93 };

HeRecord* wBegin(i32, int, int, i32) { return w.person; }
HeRecord* wById(i32) { return w.person; }
HeRecord* wActive(HeRecord*) { return nullptr; }
HeRecord* wObj(i32, int, int, int, i32) { return nullptr; }
HeRecord* wResolve(int, HeRecord** o, i32, int) { if (o) *o = nullptr; return nullptr; }
int wMoney(i32) { return 0; }
HeRecord* wN1(i32) { return nullptr; }
HeRecord* wNR(HeRecord*) { return nullptr; }
int wSum(HeRecord*, int, int) { return 0; }
int wRoom(HeRecord*, int, i32) { return 0; }
int wProd(GameTime*, const GameTime*) { return 0; }
HeRecord* wFF(int, int, int) { return nullptr; }
HeRecord* wFN() { return nullptr; }
void wArgs25(i32, int, int, int, int) { w.args25++; }
void wSingle49(i32) { w.single49++; }
void wNamed53(i32, i32, int, int, int, const char*) { w.named53++; }
void wFlag55(i32, int) {}
void wReq17(i32, int, int, int, int, int) {}
void wSlot28(void*, int) {}
i32  wEnt29(i32, void*) { return 0; }
void wState22() {}
void wBegin2(i32, i32) {}
void wRaw(unsigned, unsigned, void*, int) {}
void wDelta(unsigned, unsigned, void*, int) {}
void wEnq15(i32, i32, int, u8) {}
void wChangeZ(i32, int, i32) {}
void wRender(char* d, int, int, int, int) { if (d) d[0] = 0; }
void wQuick(i32, i32, i32, const char*, int) {}
void wErr(const char*) {}
void wAction(i32, int, int, int) {}
int  wDestroy(i32) { return 0; }
int  wMesh() { return 0; }
void wRel(int) {}
void* wAttach(i32, bool, int) { return nullptr; }
i32  wCity(u16) { return 0; }
u8   wCat(u16) { return 0; }

CharActionStep8Hooks MakeWired() {
    CharActionStep8Hooks h{};
    h.personQueryBegin = wBegin; h.personFindRecordById = wById;
    h.personFindActiveByEntity = wActive; h.objectQueryFind = wObj;
    h.resolveEntityById = wResolve; h.sumChildMoney = wMoney;
    h.buildingFindById = wN1; h.findWorkProduct = wNR; h.findStorable = wNR;
    h.sumWorkstation = wSum; h.computeRoomWorth = wRoom; h.productionOutput = wProd;
    h.heFindFirst = wFF; h.heFindNext = wFN;
    h.cmdRequestArgs25 = wArgs25; h.cmdRequestSingle49 = wSingle49;
    h.cmdRequestNamedObject53 = wNamed53; h.cmdRequestFlag55 = wFlag55;
    h.cmdRequest17 = wReq17; h.cmdRequestSlotReset28 = wSlot28;
    h.cmdRequestEntity29 = wEnt29; h.cmdRequestState22 = wState22;
    h.cmdBeginDeltaPacket = wBegin2; h.cmdAppendRawField = wRaw;
    h.cmdAppendDeltaField = wDelta; h.cmdEnqueue15 = wEnq15;
    h.requestChangeZustand = wChangeZ; h.renderFormatted = wRender;
    h.sendQuickjump = wQuick; h.reportError = wErr; h.changePlayerAction = wAction;
    h.characterDestroy = wDestroy; h.animFindFreeMeshSlot = wMesh;
    h.animReleaseMeshData = wRel; h.attachAni = wAttach;
    h.randomModulo = RealRandomModulo;   // <-- REAL sibling wiring
    h.cityRecipientId = wCity; h.cityCategory = wCat;
    h.poolSlot = nullptr; h.poolClear = nullptr;
    return h;
}

GameTime MkClock(int d, int hr, int mn, int sc) {
    GameTime t{}; t.day = d; t.hour = static_cast<u16>(hr); t.minute = mn; t.second = sc;
    return t;
}

} // namespace

// Seed 7: the real RNG's first RandomModulo(3) == 1, so InitArrest wakes +5 hours.
TEST(CharActionSteps8Itest, InitArrestRealRngWake) {
    w = W{};
    SetNpcLeafHooks(&wLeaf);
    CharActionStep8Hooks h = MakeWired();
    Block person; Zero(person);
    person.b[0] = 3;                       // ordinary class
    Poke16(person.b, 37, 0xFFFF);
    w.person = reinterpret_cast<HeRecord*>(person.b);
    SetCharActionStep8Hooks(&h);

    crt::Srand(7);                         // deterministic: first mod3 == 1
    SetNpcClock(MkClock(40, 6, 0, 0));
    Block blk; Zero(blk);
    He_Flags(H(blk)) = 0;
    HeRecord* r = InitArrestPerson(H(blk));
    CHECK(r == H(blk));
    CHECK_EQ(w.args25, 1);                 // arrest cmd armed through the wired path
    // +5h (1 + 4) from 6:00 -> hour 11 via the REAL GameTimeAdvance.
    CHECK_EQ(static_cast<int>(He_ApptTime(H(blk)).hour), 11);
    SetCharActionStep8Hooks(nullptr);
}

// Seed 1: first mod3 == 2, so InitArrest wakes +6 hours.
TEST(CharActionSteps8Itest, InitArrestRealRngWakeSeed1) {
    w = W{};
    SetNpcLeafHooks(&wLeaf);
    CharActionStep8Hooks h = MakeWired();
    Block person; Zero(person);
    person.b[0] = 3;
    Poke16(person.b, 37, 0xFFFF);
    w.person = reinterpret_cast<HeRecord*>(person.b);
    SetCharActionStep8Hooks(&h);

    crt::Srand(1);
    SetNpcClock(MkClock(40, 6, 0, 0));
    Block blk; Zero(blk);
    He_Flags(H(blk)) = 0;
    InitArrestPerson(H(blk));
    CHECK_EQ(static_cast<int>(He_ApptTime(H(blk)).hour), 12);  // 6 + (2+4)
    SetCharActionStep8Hooks(nullptr);
}

// Herd move phase draws exactly one RandomModulo(30) into the +minute wake. The
// real RNG (seed 12345) yields a first RandomModulo(30) draw of 18 (probe-confirmed).
TEST(CharActionSteps8Itest, HerdRealRngMoveWake) {
    w = W{};
    SetNpcLeafHooks(&wLeaf);
    CharActionStep8Hooks h = MakeWired();
    Block animal; Zero(animal);
    *reinterpret_cast<i32*>(animal.b + 91 * 4) = 9;
    w.person = reinterpret_cast<HeRecord*>(animal.b);
    SetCharActionStep8Hooks(&h);

    crt::Srand(12345);                     // probe: first RandomModulo(30) == 18
    SetNpcClock(MkClock(50, 8, 0, 0));
    Block blk; Zero(blk);
    blk.b[172] = 1;                        // 1 animal
    Cas8_Member(H(blk), 0) = 5;
    He_State(H(blk)) = 0;                  // move phase
    i32 r = RunHerdAnimals(H(blk), 0);
    CHECK_EQ(w.single49, 1);
    CHECK_EQ(w.named53, 1);
    // wake = +2 days(=hours) + RandomModulo(30) minutes. Seed 12345 first mod30 = 18.
    // 8:00 + 2h + 18min = 10:18 via the REAL GameTimeAdvance.
    CHECK_EQ(static_cast<int>(He_ApptTime(H(blk)).hour), 10);
    CHECK_EQ(He_ApptTime(H(blk)).minute, 18);
    CHECK_EQ(r, 10);
    SetCharActionStep8Hooks(nullptr);
}
