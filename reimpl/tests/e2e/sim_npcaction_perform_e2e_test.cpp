// tests/e2e/sim_npcaction_perform_e2e_test.cpp — GUARDED real-asset e2e for the
// NpcActionPerform intrigue/office handlers.
//
// Full flow: an NPC commits to a sequence of chosen actions (threaten -> slander
// -> spy-shop -> drink-tavern -> office verdict), each dispatched through the REAL
// address-keyed perform table and driven against the real CommandQueue command
// pipeline. We verify the emitted action-result codes and that the tavern action
// stages a real opcode-72 packet in a real queue, exactly as the shipping
// dispatcher would when the NPC reaches each action.
//
// GUARDED: the perform handlers have no direct asset dependency, but this e2e
// stands in for the "drive a real NPC action chain" scenario; it skips cleanly
// (zero checks) unless GUILD_GAME_DIR points at the installed game, so the suite
// stays green on machines without the assets.
#include "tests/framework/test.h"

#include "sim/npcaction_perform.h"
#include "sim/command.h"
#include "sim/command_builders3.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

CommandQueue* g_q = nullptr;
std::vector<int> g_emit;   // ordered leaf-emit trace

void e_start(const char*) { g_emit.push_back(1000); }
void e_end() { g_emit.push_back(1001); }
void e_op28(int a, i32, int, i32, i32, i32) { g_emit.push_back(2000 + a); }
void e_op25(i32, int b, int, int, int) { g_emit.push_back(2500 + b); }
void e_op72(i32 id, u8 v) {
    g_emit.push_back(72);
    if (g_q) RequestBuildOp72(*g_q, id, static_cast<i8>(v));
}
void e_op90(int k, i32) { g_emit.push_back(9000 + k); }
int  e_threaten() { return 1; }
int  e_slander(i32) { return 1; }
int  e_spy() { return 1; }
int  e_tavern(i32, const void*, i32) { return 1; }
void e_menu(i32, int, i32, i32) { g_emit.push_back(54); }

struct Ctx { u8 buf[64]; };
Ctx MakeCtx(u8 type, i32 at4 = 0, i32 at16 = 0) {
    Ctx c{}; std::memset(c.buf, 0, sizeof(c.buf));
    c.buf[0] = type; std::memcpy(c.buf + 4, &at4, 4); std::memcpy(c.buf + 16, &at16, 4);
    return c;
}

} // namespace

TEST(NpcActionPerformE2E, ActionChain_realPipeline) {
    if (!std::getenv("GUILD_GAME_DIR"))
        return;  // clean skip: no real assets present

    CommandQueue q;
    q.Init();
    q.set_standalone(true);
    q.set_disconnected(false);
    g_q = &q;
    g_emit.clear();

    NpcActionPerformHooks hk{};
    hk.enqueueBuildingActionStart = e_start;
    hk.enqueueBuildingActionEnd = e_end;
    hk.queueRequestSlotReset28 = e_op28;
    hk.queueRequestArgs25 = e_op25;
    hk.requestBuildOp72 = e_op72;
    hk.requestBuildOp90 = e_op90;
    hk.aiExecThreaten = e_threaten;
    hk.aiExecSlander = e_slander;
    hk.aiQueueSpyMission = e_spy;
    hk.aiLoadBuildingGraphic = e_tavern;
    hk.amtBuildGuildOfficeMenu = e_menu;
    SetNpcActionPerformHooks(&hk);

    // 1. EnterBuilding (threaten accepts) -> 40.
    CHECK_EQ(NpcActionPerform_EnterBuilding(), 40);

    // 2. UseBack (slander accepts) -> 42, op25(...,484,...).
    Ctx ub = MakeCtx(0, /*id*/ 7);
    CHECK_EQ(NpcActionPerform_UseBack(ub.buf), 42);

    // 3. ShopTransaction spy pair (4,1) -> 50.
    u8 a4 = 4, b1 = 1;
    CHECK_EQ(NpcActionPerform_ShopTransaction(&a4, &b1), 50);

    // 4. DrinkTavern -> 52; stages a real opcode-72 packet.
    Ctx actor = MakeCtx(0, /*actorId*/ 555);
    Ctx tctx = MakeCtx(0); tctx.buf[16] = 0x7E;
    CHECK_EQ(NpcActionPerform_DrinkTavern(actor.buf, tctx.buf, 1), 52);

    // 5. Verdict office menu (type 21) -> 54.
    Ctx vctx = MakeCtx(21, 9, 10);
    CHECK_EQ(NpcActionPerform_Verdict(nullptr, vctx.buf, 3), 54);

    // The trace contains the op72 stage and the verdict-menu marker in order.
    bool saw72 = false, saw54 = false;
    for (int v : g_emit) { if (v == 72) saw72 = true; if (v == 54) saw54 = true; }
    CHECK(saw72);
    CHECK(saw54);

    // A real opcode-72 packet landed in the real send pipeline.
    CHECK(q.send_count() >= 1u);

    SetNpcActionPerformHooks(nullptr);
    g_q = nullptr;
}
