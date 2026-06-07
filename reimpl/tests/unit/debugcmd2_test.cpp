// Unit tests for the second wave of VIBE_DebugCmd_* handlers (debugcmd2.cpp).
// Each test installs a recording DebugCmdHooks mock, seeds the CRT LCG, invokes
// a handler and asserts the parsed roll/gold + the emitted command/message.
#include "test.h"
#include "sim/debugcmd2.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

namespace {

// ---- Recording mock ------------------------------------------------------
struct Recorder {
    DebugCmdPerson person;
    bool personFound = true;

    int  q16Calls = 0;
    i32  q16a1 = 0, q16a2 = 0, q16amount = 0;
    u8   q16market = 0;

    int  msgCalls = 0;
    i32  msgFrom = 0, msgTo = 0;
};
Recorder* g_rec = nullptr;

bool MockFind(i32 id, DebugCmdPerson* out) {
    if (!g_rec || !g_rec->personFound) return false;
    *out = g_rec->person;
    out->id = id;            // the original reads rec.id from the record
    return true;
}
void MockMsg(i32 from, i32 to, i32 /*textId*/) {
    g_rec->msgCalls++; g_rec->msgFrom = from; g_rec->msgTo = to;
}
void MockQ16(i32 a1, i32 a2, i32 amount, u8 market) {
    g_rec->q16Calls++; g_rec->q16a1 = a1; g_rec->q16a2 = a2;
    g_rec->q16amount = amount; g_rec->q16market = market;
}

DebugCmdHooks MakeHooks() {
    DebugCmdHooks h{};
    h.findPerson = MockFind;
    h.sendEntityMessage = MockMsg;
    h.queueRequest16 = MockQ16;
    h.market = 7;
    return h;
}

// Install a mock bound to `rec`, seed the LCG, return hooks kept alive by caller.
void Setup(Recorder& rec, u32 seed) {
    g_rec = &rec;
    crt::Srand(seed);
}

// Python-oracle LCG (matches crt::RandNext) for in-test golden computation.
struct Lcg {
    u32 s;
    explicit Lcg(u32 seed) : s(seed) {}
    int next() { s = 1103515245u * s + 12345u; return (int)((s >> 16) & 0x7FFFu); }
    int mod(int n) { return n ? next() % n : 0; }
};
i32 Gold(i32 wealth, int roll, double base, double scale) {
    return (i32)((double)wealth * ((double)roll + base) * scale);
}

DebugCmdHooks g_h;   // persists for duration of each test

} // namespace

// --------------------------------------------------------------------------
// Pure helpers (no hooks): RandomModulo + ScaledGold golden vectors.
// --------------------------------------------------------------------------
TEST(DebugCmd2, RandomModuloZeroIsZero) {
    crt::Srand(12345);
    CHECK_EQ(DebugCmdRandomModulo(0), 0);     // n==0 short-circuits, no advance
}

TEST(DebugCmd2, ScaledGoldGoldenVectors) {
    // golden values computed with python3 (see implementer report).
    CHECK_EQ(DebugCmdScaledGold(10000, 0, 1.0, 0.01), 100);
    CHECK_EQ(DebugCmdScaledGold(10000, 0, 2.0, 0.01), 200);
    CHECK_EQ(DebugCmdScaledGold(10000, 1, 1.0, 0.01), 200);
    CHECK_EQ(DebugCmdScaledGold(10000, 3, 2.0, 0.01), 500);
    CHECK_EQ(DebugCmdScaledGold(10000, 0, 2.0, 1.0 / 120.0), 166);  // ScaledG
    CHECK_EQ(DebugCmdScaledGold(10000, 1, 1.0, 0.0125), 250);       // ScaledF
}

// --------------------------------------------------------------------------
// ScaledB: roll%4, base 2.0, scale 0.01, second roll%6, QR16(personId,-1).
// --------------------------------------------------------------------------
TEST(DebugCmd2, ScaledB_RollGoldEmitAndRngOrder) {
    Recorder rec;
    rec.person.wealth = 10000;
    Setup(rec, 12345);
    g_h = MakeHooks();
    SetDebugCmdHooks(&g_h);

    i32 r = DebugCmdSpawnEntityScaledB(42);

    // Golden replay: main roll%4 then gold then extra roll%6.
    Lcg o(12345);
    int roll = o.mod(4);
    i32 gold = Gold(10000, roll, 2.0, 0.01);
    int extra = o.mod(6);
    (void)extra;

    CHECK_EQ(r, (i32)kDbgHandled);
    CHECK_EQ(rec.q16Calls, 1);
    CHECK_EQ(rec.q16a1, 42);          // PersonFirst ordering
    CHECK_EQ(rec.q16a2, -1);
    CHECK_EQ(rec.q16amount, gold);
    CHECK_EQ(rec.q16market, (u8)7);
    CHECK_EQ(rec.msgCalls, 1);
    CHECK_EQ(rec.msgFrom, 42);
    CHECK_EQ(rec.msgTo, 42);
    // The handler must have advanced the LCG exactly twice (main + extra roll).
    CHECK_EQ((u32)*crt::RandStatePtr(), o.s);
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// ScaledC: roll%3, base 1.0, scale 0.01, no extra roll, QR16(personId,-1).
// --------------------------------------------------------------------------
TEST(DebugCmd2, ScaledC_SingleRollNoExtra) {
    Recorder rec; rec.person.wealth = 20000;
    Setup(rec, 7); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);

    i32 r = DebugCmdSpawnEntityScaledC(5);
    Lcg o(7); int roll = o.mod(3); i32 gold = Gold(20000, roll, 1.0, 0.01);

    CHECK_EQ(r, (i32)kDbgHandled);
    CHECK_EQ(rec.q16a1, 5);
    CHECK_EQ(rec.q16a2, -1);
    CHECK_EQ(rec.q16amount, gold);
    CHECK_EQ((u32)*crt::RandStatePtr(), o.s);   // exactly one advance
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// ScaledH: MinusOneFirst ordering, base 2.0.
// --------------------------------------------------------------------------
TEST(DebugCmd2, ScaledH_MinusOneFirstOrdering) {
    Recorder rec; rec.person.wealth = 5000;
    Setup(rec, 555); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);

    DebugCmdSpawnEntityScaledH(9);
    Lcg o(555); int roll = o.mod(3); i32 gold = Gold(5000, roll, 2.0, 0.01);

    CHECK_EQ(rec.q16a1, -1);          // MinusOneFirst
    CHECK_EQ(rec.q16a2, 9);
    CHECK_EQ(rec.q16amount, gold);
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// ScaledF (scale 0.0125) and ScaledG (scale 1/120) distinct constants.
// --------------------------------------------------------------------------
TEST(DebugCmd2, ScaledF_And_G_Constants) {
    {
        Recorder rec; rec.person.wealth = 8000;
        Setup(rec, 99); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);
        DebugCmdSpawnEntityScaledF(1);
        Lcg o(99); int roll = o.mod(2);
        CHECK_EQ(rec.q16amount, Gold(8000, roll, 1.0, 0.0125));
        SetDebugCmdHooks(nullptr);
    }
    {
        Recorder rec; rec.person.wealth = 12000;
        Setup(rec, 7); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);
        DebugCmdSpawnEntityScaledG(1);
        Lcg o(7); int roll = o.mod(3);
        CHECK_EQ(rec.q16amount, Gold(12000, roll, 2.0, 1.0 / 120.0));
        SetDebugCmdHooks(nullptr);
    }
}

// --------------------------------------------------------------------------
// ScaledM: roll%3 then extra roll%15 — two RNG advances.
// --------------------------------------------------------------------------
TEST(DebugCmd2, ScaledM_DoubleRoll) {
    Recorder rec; rec.person.wealth = 30000;
    Setup(rec, 4242); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);

    DebugCmdSpawnEntityScaledM(11);
    Lcg o(4242); int roll = o.mod(3); i32 gold = Gold(30000, roll, 1.0, 0.01);
    (void)o.mod(15);

    CHECK_EQ(rec.q16a1, -1);
    CHECK_EQ(rec.q16a2, 11);
    CHECK_EQ(rec.q16amount, gold);
    CHECK_EQ((u32)*crt::RandStatePtr(), o.s);   // two advances
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// No-person path returns 1 and emits nothing.
// --------------------------------------------------------------------------
TEST(DebugCmd2, NoPersonReturnsOneNoEmit) {
    Recorder rec; rec.personFound = false;
    Setup(rec, 1); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);

    CHECK_EQ(DebugCmdSpawnEntityScaledC(3), (i32)kDbgNoPerson);
    CHECK_EQ(rec.q16Calls, 0);
    CHECK_EQ(rec.msgCalls, 0);
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// IfNotState14 gate: officeRank==14 -> 1024, no roll consumed, no emit.
// --------------------------------------------------------------------------
TEST(DebugCmd2, IfNotState14A_GateBlocks) {
    Recorder rec; rec.person.wealth = 10000; rec.person.officeRank = 14;
    Setup(rec, 1); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);

    u32 before = (u32)*crt::RandStatePtr();
    CHECK_EQ(DebugCmdSpawnEntityIfNotState14A(2), (i32)kDbgIneligible);
    CHECK_EQ(rec.q16Calls, 0);
    CHECK_EQ((u32)*crt::RandStatePtr(), before);   // gate is before any roll
    SetDebugCmdHooks(nullptr);
}

TEST(DebugCmd2, IfNotState14A_PassWhenNot14) {
    Recorder rec; rec.person.wealth = 10000; rec.person.officeRank = 3;
    Setup(rec, 13); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);

    i32 r = DebugCmdSpawnEntityIfNotState14A(2);
    Lcg o(13); int roll = o.mod(3);
    CHECK_EQ(r, (i32)kDbgHandled);
    CHECK_EQ(rec.q16a1, 2);                         // PersonFirst
    CHECK_EQ(rec.q16amount, Gold(10000, roll, 2.0, 0.01));
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// ScaledJ gate: officeRank==0 && officeAlt==0 -> 1024; otherwise pass.
// --------------------------------------------------------------------------
TEST(DebugCmd2, ScaledJ_GateBothZeroBlocks) {
    Recorder rec; rec.person.wealth = 1000;
    rec.person.officeRank = 0; rec.person.officeAlt = 0;
    Setup(rec, 1); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);
    CHECK_EQ(DebugCmdSpawnEntityScaledJ(1), (i32)kDbgIneligible);
    CHECK_EQ(rec.q16Calls, 0);
    SetDebugCmdHooks(nullptr);
}

TEST(DebugCmd2, ScaledJ_PassWhenAltSet) {
    Recorder rec; rec.person.wealth = 1000;
    rec.person.officeRank = 0; rec.person.officeAlt = 5;
    Setup(rec, 8); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);
    i32 r = DebugCmdSpawnEntityScaledJ(1);
    Lcg o(8); int roll = o.mod(4);
    CHECK_EQ(r, (i32)kDbgHandled);
    CHECK_EQ(rec.q16a1, -1);                         // MinusOneFirst
    CHECK_EQ(rec.q16amount, Gold(1000, roll, 1.0, 0.01));
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// CheckType gate: buildType (group code) == 7 -> 1024.
// --------------------------------------------------------------------------
TEST(DebugCmd2, CheckType_GateGroup7Blocks) {
    Recorder rec; rec.person.wealth = 9000; rec.person.buildType = 7;
    Setup(rec, 1); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);
    CHECK_EQ(DebugCmdSpawnEntityCheckType(1), (i32)kDbgIneligible);
    CHECK_EQ(rec.q16Calls, 0);
    SetDebugCmdHooks(nullptr);
}

TEST(DebugCmd2, CheckType_PassWhenNot7) {
    Recorder rec; rec.person.wealth = 9000; rec.person.buildType = 2;
    Setup(rec, 21); g_h = MakeHooks(); SetDebugCmdHooks(&g_h);
    i32 r = DebugCmdSpawnEntityCheckType(1);
    Lcg o(21); int roll = o.mod(4);
    CHECK_EQ(r, (i32)kDbgHandled);
    CHECK_EQ(rec.q16amount, Gold(9000, roll, 1.0, 0.01));
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// RetFail1024 is a constant leaf.
// --------------------------------------------------------------------------
TEST(DebugCmd2, RetFail1024Constant) {
    CHECK_EQ(DebugCmdRetFail1024(), (i32)kDbgIneligible);
}

// --------------------------------------------------------------------------
// Table mapping resolves the translated indices and nothing else.
// --------------------------------------------------------------------------
TEST(DebugCmd2, NpcTableMappingResolvesTranslatedIndices) {
    CHECK(DebugCmd2NpcTableEntry(1)  == &DebugCmdSpawnEntityIfNotState14A);
    CHECK(DebugCmd2NpcTableEntry(3)  == &DebugCmdSpawnEntityIfNotState14B);
    CHECK(DebugCmd2NpcTableEntry(5)  == &DebugCmdSpawnEntityScaledB);
    CHECK(DebugCmd2NpcTableEntry(11) == &DebugCmdSpawnEntityScaledG);
    CHECK(DebugCmd2NpcTableEntry(12) == &DebugCmdSpawnEntityIfNotState14C);
    CHECK(DebugCmd2NpcTableEntry(15) == &DebugCmdSpawnEntityCheckType);
    CHECK(DebugCmd2NpcTableEntry(16) == &DebugCmdSpawnEntityScaledJ);
    CHECK(DebugCmd2NpcTableEntry(20) == &DebugCmdSpawnEntityScaledM);
    // Indices owned by debugcmd.cpp (0/4/32/33) and deferred ones -> nullptr.
    CHECK(DebugCmd2NpcTableEntry(0)  == nullptr);
    CHECK(DebugCmd2NpcTableEntry(2)  == nullptr);   // ByOfficeCategory (deferred)
    CHECK(DebugCmd2NpcTableEntry(7)  == nullptr);   // FromHandlerList (deferred)
    CHECK(DebugCmd2NpcTableEntry(99) == nullptr);
}
