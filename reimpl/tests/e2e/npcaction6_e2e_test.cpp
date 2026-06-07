// E2E for NpcAction6 — runs a small "AI command" evaluation flow across the
// loyalty/salary/move/message leaves against a shared mock Person table + command
// log, exercising the deterministic RNG-driven action sequence end to end.
#include "sim/npcaction6.h"
#include "sim/npcaction.h"
#include "crt/rand.h"
#include "tests/framework/test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct Cmd { int kind; i32 a; i32 b; i32 amount; u8 cur; };   // kind: 0=bop93, 1=q16
std::vector<Cmd> g_log;
std::vector<i32> g_messages;

u8 g_person[256];
i32 g_personId = 1001;
i32 g_wealth = 5000;

u8* e2eFind(i32 id) { return id == g_personId ? g_person : nullptr; }
i32 e2eWealth(u16, u8*) { return g_wealth; }
int e2eGate(u8*) { return 1; }
void e2eBop93(i32 id, int kind, i32 id2, u8 amt) { g_log.push_back({0, id, (i32)kind, (i32)amt, (u8)id2}); }
void e2eQ16(i32 a, i32 b, i32 amount, u8 cur) { g_log.push_back({1, a, b, amount, cur}); }
void e2eMsg(i32, i32, i32 t) { g_messages.push_back(t); }

NpcAction6Hooks MakeHooks() {
    NpcAction6Hooks h{};
    h.findRecordById = e2eFind;
    h.computeTotalWealth = e2eWealth;
    h.pickNeedAndClearGroupB = e2eGate;
    h.requestBuildOp93 = e2eBop93;
    h.queueRequest16 = e2eQ16;
    h.sendEntityMessage = e2eMsg;
    h.currencyByte = 9;
    return h;
}

void MakeCmd(HeRecord* h, i32 personId, u16 textBase) {
    std::memset(h, 0, sizeof(HeRecord));
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 0) = personId;
    *reinterpret_cast<u16*>(reinterpret_cast<u8*>(h) + 4) = textBase;
}

} // namespace

// A guild master runs three AI actions on an employee in one deterministic tick:
//   raise salary -> increase loyalty -> dialog. Seeded once so the whole roll
//   sequence is reproducible; we assert each leaf fired with the expected payload.
TEST(NpcAction6E2E, ManagerActionSequence) {
    NpcAction6Hooks hk = MakeHooks();
    SetNpcAction6Hooks(&hk);
    g_log.clear();
    g_messages.clear();

    std::memset(g_person, 0, sizeof(g_person));
    *reinterpret_cast<u16*>(g_person + 0) = 12;     // person index
    *reinterpret_cast<i32*>(g_person + 4) = 4242;   // entity id
    g_person[130] = 255;                            // raise-salary willingness max
    g_person[128 + 0] = 50;                         // some loyalty stat
    *reinterpret_cast<i32*>(g_person + 44) = 1;     // employed
    g_wealth = 5000;

    crt::Srand(2024);

    HeRecord rec;

    // 1) Raise salary. seed 2024: gate roll then mult.
    MakeCmd(&rec, g_personId, 500);
    int r1 = NpcAction6_RaiseSalaryCmd(&rec);
    CHECK_EQ(r1, kNpc6Ok);

    // 2) Increase loyalty (continues the same RNG stream).
    MakeCmd(&rec, g_personId, 600);
    int r2 = NpcAction6_IncreaseLoyaltyCmd(&rec);
    CHECK_EQ(r2, kNpc6Ok);

    // 3) Dialog (gate passes).
    MakeCmd(&rec, g_personId, 700);
    int r3 = NpcAction6_DialogCmdA(&rec);
    CHECK_EQ(r3, kNpc6Ok);

    // Two command emissions: a QueueRequest16 (salary) then a RequestBuildOp93
    // (loyalty). The dialog only sends a message.
    CHECK_EQ((int)g_log.size(), 2);
    CHECK_EQ(g_log[0].kind, 1);            // salary queue16
    CHECK_EQ(g_log[0].a, g_personId);
    CHECK_EQ(g_log[0].b, -1);
    CHECK((int)g_log[0].cur == 9);
    CHECK(g_log[0].amount >= 0);
    CHECK_EQ(g_log[1].kind, 0);            // loyalty bop93
    CHECK_EQ(g_log[1].a, 4242);

    // Three status messages, each at its base + 3881.
    CHECK_EQ((int)g_messages.size(), 3);
    CHECK_EQ(g_messages[0], 500 + 3881);
    CHECK_EQ(g_messages[1], 600 + 3881);
    CHECK_EQ(g_messages[2], 700 + 3881);
}

// Determinism: re-seeding reproduces the same salary amount bit-for-bit.
TEST(NpcAction6E2E, ReseedReproducesAmount) {
    NpcAction6Hooks hk = MakeHooks();
    SetNpcAction6Hooks(&hk);

    std::memset(g_person, 0, sizeof(g_person));
    *reinterpret_cast<i32*>(g_person + 4) = 1;
    g_person[130] = 255;
    g_wealth = 7777;

    HeRecord rec; MakeCmd(&rec, g_personId, 0);

    g_log.clear();
    crt::Srand(555);
    CHECK_EQ(NpcAction6_RaiseSalaryCmd(&rec), kNpc6Ok);
    i32 first = g_log.empty() ? -1 : g_log[0].amount;

    g_log.clear();
    crt::Srand(555);
    CHECK_EQ(NpcAction6_RaiseSalaryCmd(&rec), kNpc6Ok);
    i32 second = g_log.empty() ? -1 : g_log[0].amount;

    CHECK_EQ(first, second);
    CHECK(first >= 0);
}
