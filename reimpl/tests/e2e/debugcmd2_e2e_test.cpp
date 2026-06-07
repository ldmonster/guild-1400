// End-to-end flow for the second-wave VIBE_DebugCmd_* handlers:
//   parse a console line ("<cmd> <personId>") -> resolve the handler via the
//   funcs_5766CB index map -> dispatch -> observe the emitted command/message.
// Exercises the gated, ungated and double-roll handlers in one pass with a
// shared recording mock, confirming RNG order and gold are consistent across
// the family.
#include "test.h"
#include "sim/debugcmd2.h"
#include "crt/rand.h"
#include <cstring>
#include <cstdlib>

using namespace guild;
using namespace guild::sim;

namespace {

struct Effect {
    bool emitted = false;
    i32 a1 = 0, a2 = 0, amount = 0;
    u8  market = 0;
    bool messaged = false;
};
Effect* g_eff = nullptr;
DebugCmdPerson g_person;
bool g_found = true;

bool E2eFind(i32 id, DebugCmdPerson* out) {
    if (!g_found) return false;
    *out = g_person; out->id = id; return true;
}
void E2eMsg(i32 from, i32 to, i32) { (void)from; (void)to; g_eff->messaged = true; }
void E2eQ16(i32 a1, i32 a2, i32 amount, u8 market) {
    g_eff->emitted = true; g_eff->a1 = a1; g_eff->a2 = a2;
    g_eff->amount = amount; g_eff->market = market;
}

// Console command name -> funcs_5766CB table index (the engine's action codes).
struct NameMap { const char* name; int index; };
const NameMap kCommands[] = {
    {"scaledB", 5}, {"scaledC", 6}, {"scaledH", 13}, {"scaledM", 20},
    {"ifnot14a", 1}, {"checktype", 15}, {"scaledJ", 16},
};

int IndexForName(const char* name) {
    for (const auto& c : kCommands)
        if (std::strcmp(c.name, name) == 0) return c.index;
    return -1;
}

// Parse "<cmd> <id>" and dispatch through the translated table.
i32 RunConsoleLine(const char* line, Effect& eff) {
    char cmd[32]; int id = 0;
    if (std::sscanf(line, "%31s %d", cmd, &id) != 2) return kDbgNoPerson;
    int idx = IndexForName(cmd);
    if (idx < 0) return kDbgNoPerson;
    auto fn = DebugCmd2NpcTableEntry(idx);
    if (!fn) return kDbgNoPerson;
    g_eff = &eff;
    return fn(id);
}

DebugCmdHooks MakeHooks() {
    DebugCmdHooks h{};
    h.findPerson = E2eFind;
    h.sendEntityMessage = E2eMsg;
    h.queueRequest16 = E2eQ16;
    h.market = 4;
    return h;
}

// In-test LCG oracle (matches crt::RandNext).
struct Lcg {
    u32 s; explicit Lcg(u32 seed) : s(seed) {}
    int next() { s = 1103515245u * s + 12345u; return (int)((s >> 16) & 0x7FFFu); }
    int mod(int n) { return n ? next() % n : 0; }
};
i32 Gold(i32 w, int roll, double b, double sc) {
    return (i32)((double)w * ((double)roll + b) * sc);
}

DebugCmdHooks g_h;

} // namespace

// --------------------------------------------------------------------------
// Full flow: several console lines dispatched in sequence; verify each parses,
// hits the right handler, and emits the golden gold with the right arg order.
// --------------------------------------------------------------------------
TEST(DebugCmd2E2E, ConsoleDispatchFlow) {
    g_h = MakeHooks();
    SetDebugCmdHooks(&g_h);
    g_found = true;
    g_person = DebugCmdPerson{};
    g_person.wealth = 50000;
    g_person.officeRank = 1;     // passes ifnot14a + scaledJ gates
    g_person.officeAlt  = 0;
    g_person.buildType  = 3;     // passes checktype gate (!= 7)

    crt::Srand(2024);
    Lcg o(2024);

    // 1) scaledC 100 -> PersonFirst, roll%3, base1, scale0.01.
    {
        Effect e; i32 r = RunConsoleLine("scaledC 100", e);
        int roll = o.mod(3);
        CHECK_EQ(r, (i32)kDbgHandled);
        CHECK(e.emitted);
        CHECK_EQ(e.a1, 100);
        CHECK_EQ(e.a2, -1);
        CHECK_EQ(e.amount, Gold(50000, roll, 1.0, 0.01));
        CHECK_EQ(e.market, (u8)4);
        CHECK(e.messaged);
    }
    // 2) scaledB 101 -> PersonFirst, roll%4, base2, scale0.01, + extra roll%6.
    {
        Effect e; i32 r = RunConsoleLine("scaledB 101", e);
        int roll = o.mod(4);
        i32 gold = Gold(50000, roll, 2.0, 0.01);
        (void)o.mod(6);                       // secondary roll (message arg)
        CHECK_EQ(r, (i32)kDbgHandled);
        CHECK_EQ(e.a1, 101);
        CHECK_EQ(e.amount, gold);
    }
    // 3) scaledH 102 -> MinusOneFirst, roll%3, base2.
    {
        Effect e; i32 r = RunConsoleLine("scaledH 102", e);
        int roll = o.mod(3);
        CHECK_EQ(r, (i32)kDbgHandled);
        CHECK_EQ(e.a1, -1);
        CHECK_EQ(e.a2, 102);
        CHECK_EQ(e.amount, Gold(50000, roll, 2.0, 0.01));
    }
    // 4) checktype 103 -> MinusOneFirst, roll%4, gate passes (buildType 3).
    {
        Effect e; i32 r = RunConsoleLine("checktype 103", e);
        int roll = o.mod(4);
        CHECK_EQ(r, (i32)kDbgHandled);
        CHECK_EQ(e.a2, 103);
        CHECK_EQ(e.amount, Gold(50000, roll, 1.0, 0.01));
    }
    // 5) scaledM 104 -> MinusOneFirst, roll%3 + extra roll%15.
    {
        Effect e; i32 r = RunConsoleLine("scaledM 104", e);
        int roll = o.mod(3);
        i32 gold = Gold(50000, roll, 1.0, 0.01);
        (void)o.mod(15);
        CHECK_EQ(r, (i32)kDbgHandled);
        CHECK_EQ(e.amount, gold);
    }
    // The whole sequence must have advanced the live LCG in lockstep.
    CHECK_EQ((u32)*crt::RandStatePtr(), o.s);
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// Gate-blocked line: emits nothing and returns 1024; the RNG is untouched.
// --------------------------------------------------------------------------
TEST(DebugCmd2E2E, GatedLineBlocksWithNoEmitNoRng) {
    g_h = MakeHooks(); SetDebugCmdHooks(&g_h);
    g_found = true;
    g_person = DebugCmdPerson{};
    g_person.wealth = 10000;
    g_person.officeRank = 14;             // ifnot14a gate -> 1024

    crt::Srand(777);
    u32 before = (u32)*crt::RandStatePtr();

    Effect e; i32 r = RunConsoleLine("ifnot14a 9", e);
    CHECK_EQ(r, (i32)kDbgIneligible);
    CHECK(!e.emitted);
    CHECK(!e.messaged);
    CHECK_EQ((u32)*crt::RandStatePtr(), before);
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// Unknown person -> handler returns 1, the console flow surfaces it.
// --------------------------------------------------------------------------
TEST(DebugCmd2E2E, MissingPersonReturnsOne) {
    g_h = MakeHooks(); SetDebugCmdHooks(&g_h);
    g_found = false;
    Effect e;
    CHECK_EQ(RunConsoleLine("scaledC 5", e), (i32)kDbgNoPerson);
    CHECK(!e.emitted);
    g_found = true;
    SetDebugCmdHooks(nullptr);
}

// --------------------------------------------------------------------------
// Unknown command name does not dispatch.
// --------------------------------------------------------------------------
TEST(DebugCmd2E2E, UnknownCommandIgnored) {
    g_h = MakeHooks(); SetDebugCmdHooks(&g_h);
    g_found = true; g_person = DebugCmdPerson{};
    Effect e;
    CHECK_EQ(RunConsoleLine("bogus 5", e), (i32)kDbgNoPerson);
    CHECK(!e.emitted);
    SetDebugCmdHooks(nullptr);
}
