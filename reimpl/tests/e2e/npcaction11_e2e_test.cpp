// E2E: a carry-and-use NPC behaviour flow stitched across npcaction11's launchers
// and step coroutines. We model a single host NPC that:
//   1. BeginUseObject     — schedules a "use" appointment, records the carried
//                           object id, emits the equip-use command.
//   2. UseObjectStep       — runs the multi-phase use machine to completion
//                           (setup -> request17 -> host quickjump -> free).
//   3. BeginStoreObject    — schedules putting the object away.
//   4. DecrementCarryStep  — drains the carry counter and finishes (slot-reset +
//                           buildop72 for the host kind).
// All cross-module leaves are routed through one recording NpcAction11Hooks; we
// assert the observable command/lifecycle trace and the GameTime appointment
// stamps line up with the per-function golden behaviour.
#include "test.h"

#include "sim/npcaction11.h"
#include "sim/npcaction.h"
#include "sim/gametime.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct HeBuf {
    alignas(8) std::uint8_t bytes[1024];
    HeBuf() { std::memset(bytes, 0, sizeof bytes); }
    HeRecord* rec() { return reinterpret_cast<HeRecord*>(bytes); }
    void put32(int off, i32 v) { std::memcpy(bytes + off, &v, 4); }
    void put16(int off, u16 v) { std::memcpy(bytes + off, &v, 2); }
    i32 get32(int off) { i32 v; std::memcpy(&v, bytes + off, 4); return v; }
};

struct Pers { i32 id = 0; u8 kind = 0; u8 equip = 0; u8 b356 = 1; };
std::vector<Pers> g_people;
Pers* ById(i32 id) { for (auto& p : g_people) if (p.id == id) return &p; return nullptr; }

struct Trace {
    int frees = 0, args25 = 0, request17 = 0, single58 = 0, quickjumps = 0;
    int slotReset = 0, buildOp72 = 0;
    int lastArg25b = -1;
} g_tr;

i32  Free(HeRecord*) { ++g_tr.frees; return 0; }
void* Query(i32 id)  { return ById(id); }
void* Find(i32 id)   { return ById(id); }
i32  ObjId(void* r)  { return r ? static_cast<Pers*>(r)->id : 0; }
u8   Kind(void* r)   { return r ? static_cast<Pers*>(r)->kind : 0; }
u8   Equip(void* r)  { return r ? static_cast<Pers*>(r)->equip : 0; }
u8   EquipState(void* r, int idx) {
    if (!r) return 0;
    return idx == 356 ? static_cast<Pers*>(r)->b356 : 0;
}
i32  CityId(u16 ci)  { return 5000 + ci; }
u8   CityKind(u16 ci){ return ci == 0 ? 6 : 5; }
void Args25(i32, int, int b, int, int) { ++g_tr.args25; g_tr.lastArg25b = b; }
void Request17(i32, i32, int, int, u8, int) { ++g_tr.request17; }
void Single58(i32) { ++g_tr.single58; }
void Quickjump(i32, int, i32, const char*) { ++g_tr.quickjumps; }
void SlotReset28(void*) { ++g_tr.slotReset; }
void BuildOp72(i32, int) { ++g_tr.buildOp72; }
bool FastMode() { return false; }
void* GoQuery(i32, int, int, int, int) { return reinterpret_cast<void*>(1); }

NpcAction11Hooks MakeHooks() {
    NpcAction11Hooks h{};
    h.freeHandlerEntry = Free;
    h.personQueryBegin = Query;
    h.findPersonById   = Find;
    h.objId = ObjId; h.kind = Kind; h.equipFlags = Equip;
    h.personEquipState = EquipState;
    h.cityId = CityId; h.cityKind = CityKind;
    h.requestArgs25 = Args25; h.request17 = Request17; h.requestSingle58 = Single58;
    h.sendQuickjump = Quickjump; h.requestSlotReset28 = SlotReset28;
    h.requestBuildOp72 = BuildOp72; h.fastMode = FastMode;
    h.gameObjectQueryFind = GoQuery;
    return h;
}

} // namespace

TEST(NpcAction11E2E, CarryUseStoreFlow) {
    g_people = { Pers{} };
    g_people[0].id = 42; g_people[0].kind = 6; g_people[0].b356 = 2;  // host, carry 2

    GameTime clk{}; clk.day = 20; clk.hour = 12; clk.minute = 0; clk.second = 0;
    SetNpcClock(clk);

    NpcAction11Hooks h = MakeHooks();
    SetNpcAction11Hooks(&h);
    g_tr = Trace{};

    // --- 1. BeginUseObject: schedule the use, record object at +16 ----------
    HeBuf he; he.put32(172, 42); he.put16(8, 0);   // person id, city 0 (host)
    void* p = NpcAction11_BeginUseObject(he.rec());
    CHECK(p == &g_people[0]);
    CHECK_EQ(he.get32(16), 42);                     // entity recorded
    CHECK_EQ(g_tr.args25, 1);                       // arg25(...,128) equip-use
    CHECK_EQ(g_tr.lastArg25b, 128);
    // not fast mode -> +82 Advance(5,0,0): the original's 2nd arg lands on the
    // hour field (carry hours->days), so 12:00 day 20 -> 17:00 day 20.
    GameTime st; std::memcpy(&st, he.bytes + 82, sizeof st);
    CHECK_EQ(st.day, 20);
    CHECK_EQ(static_cast<int>(st.hour), 17);

    // --- 2. UseObjectStep: phase 0 (state 0 -> sw 2) emits + advances -------
    He_State(he.rec()) = 0;
    NpcAction11_UseObjectStep(he.rec());
    CHECK_EQ(g_tr.request17, 1);
    CHECK_EQ(He_State(he.rec()), 1);
    // phase 1 (state 1 -> sw 3) host quickjump + free.
    NpcAction11_UseObjectStep(he.rec());
    CHECK(g_tr.quickjumps >= 1);
    CHECK(g_tr.frees >= 1);

    // --- 3. BeginStoreObject: stow command ----------------------------------
    int args25Before = g_tr.args25;
    HeBuf st2; st2.put32(172, 42);
    NpcAction11_BeginStoreObject(st2.rec());
    CHECK_EQ(g_tr.args25, args25Before + 1);
    CHECK_EQ(g_tr.lastArg25b, 1024);

    // --- 4. DecrementCarryStep: drain the carry counter (2 -> 1 -> 0 -> -1) -
    HeBuf dc; He_State(dc.rec()) = 0; dc.put32(172, 42); dc.put32(184, 1);
    int freesBefore = g_tr.frees;
    NpcAction11_DecrementCarryStep(dc.rec());       // 1 -> 0, no finish
    CHECK_EQ(dc.get32(184), 0);
    NpcAction11_DecrementCarryStep(dc.rec());       // 0 -> -1, finish (host)
    CHECK_EQ(dc.get32(184), -1);
    CHECK_EQ(g_tr.slotReset, 1);
    CHECK_EQ(g_tr.buildOp72, 1);
    CHECK_EQ(g_tr.frees, freesBefore + 1);

    SetNpcAction11Hooks(nullptr);
}
