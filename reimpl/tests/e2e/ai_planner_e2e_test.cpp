// End-to-end test for the Guild AI behavior planner (guild::ai).
//
// Flow exercised:
//   1. Build a synthetic AI agent with a packed need word and a method score
//      table (a small catalog of scored methods on the planner).
//   2. Run the score-table planner to select the best method for the agent.
//   3. "Execute" the selected method: it (a) runs a seeded random need-pick to
//      decide which need to satisfy and (b) decays a need field — both of which
//      route their state changes through a mock command hook (the AI->command
//      boundary) instead of mutating state directly.
//   4. Verify the mock hook received the expected commands in the expected order:
//      the planner's chosen method id, then the need-pick delta, then the decay
//      delta + stock adjustment.
//
// This mirrors how the real planner (SelectBestRecursive) hands off to
// ExecuteSelected, which emits BeginAiMethodPacket/AppendAiMethodEntry/delta
// commands rather than touching entity memory (lockstep determinism).
#include "tests/framework/test.h"

#include "ai/score.h"
#include "ai/needs.h"
#include "crt/rand.h"

#include <string>
#include <vector>

using namespace guild;

namespace {

// Mock command sink: records every AI-emitted command in order. Stands in for the
// real command codec (BeginDeltaPacket/AppendDeltaField/QueueRequestState22 +
// Building_AdjustStockAndNotify).
struct MockCommandHook : ai::NeedsCommandHook {
    std::vector<std::string> log;

    void EmitNeedDelta(i32 entityId, u32 newNeedWord) override {
        log.push_back("NeedDelta id=" + std::to_string(entityId) +
                      " word=0x" + std::to_string(newNeedWord));
    }
    void AdjustStock(i16 type, i32 stockDelta) override {
        log.push_back("AdjustStock type=" + std::to_string(type) +
                      " delta=" + std::to_string(stockDelta));
    }
    void RecordMethodChosen(u8 methodId) {
        log.push_back("MethodChosen id=" + std::to_string(methodId));
    }
};

// Score table the eval callbacks read (the binary's per-method score vectors).
float g_a[64] = {};
float g_b[64] = {};
u8    g_act[64] = {};

ai::EvalResult Eval(const ai::Method& m, int) {
    ai::EvalResult r;
    r.produced = true;
    r.scoreA = g_a[m.id];
    r.scoreB = g_b[m.id];
    r.frameA.bytes[0] = m.id;
    return r;
}
u8 Apply(const ai::Method& m, int, ai::EvalResult&) { return g_act[m.id]; }

} // namespace

TEST(AiPlannerE2E, SelectExecuteEmitsOrderedCommands) {
    // --- 1. synthetic agent + score table -----------------------------------
    ai::Planner planner;
    planner.set_current_class(0);
    for (int i = 0; i < 64; ++i) { g_a[i] = -1e30f; g_b[i] = -1e30f; g_act[i] = 0; }
    for (int i = 0; i < planner.method_count(); ++i) planner.method(i) = ai::Method{};

    // Catalog: "go drink" (id 7, class 3) has the best score and is chosen.
    struct { u8 id; u8 cls; float a; u8 act; } cat[] = {
        {5, 1, 10.0f, 51},
        {7, 3, 75.0f, 57},   // winner
        {9, 5, 40.0f, 59},
    };
    for (int i = 0; i < 3; ++i) {
        ai::Method& m = planner.method(i);
        m.id = cat[i].id; m.classId = cat[i].cls; m.enabled = true;
        m.eval = Eval; m.apply = Apply;
        g_a[cat[i].id] = cat[i].a;
        g_b[cat[i].id] = cat[i].a;
        g_act[cat[i].id] = cat[i].act;
    }

    MockCommandHook hook;

    // --- 2. plan ------------------------------------------------------------
    ai::ActionFrame fa, fb;
    u8 chosen = planner.SelectBest(/*personIndex*/0, &fa, &fb);
    CHECK_EQ((int)chosen, 57);            // method 7's action byte
    CHECK_EQ((int)fa.bytes[0], 7);        // winning frame belongs to method 7
    hook.RecordMethodChosen(chosen);

    // --- 3. execute: deterministic need handling through the hook -----------
    // Seed so the whole execution is reproducible. seed=7:
    //   need-pick FromFourA (all eligible): rand 19564 -> start0 -> slot0 -> id2
    //   then decay: next rand 9806 -> mag = 9806%16 = 14 -> amount = 560
    crt::Srand(7);

    ai::NeedAgent agent;
    agent.type = 7;                       // a tavern/food building type
    agent.id = 4242;
    agent.needWord = 0xF0 | 0xF00 | 0x3000 | 0x1C000; // all four need groups set

    u8 needId = ai::PickRandomFlagFromFourA(agent, &hook);
    CHECK_EQ((int)needId, 2);             // slot 0 -> need-id 2
    CHECK_EQ((unsigned)(agent.needWord & 0xF0), 0u); // its group bits cleared

    // The low nibble of needWord is still clear here, so decay runs.
    int decayed = ai::ApplyRandomDecayField(agent, &hook);
    CHECK_EQ(decayed, 560);               // mag 14 * 40

    // --- 4. verify the command log order ------------------------------------
    // Expected order: method chosen, need-pick delta, decay delta, decay stock.
    CHECK_EQ((int)hook.log.size(), 4);
    CHECK(hook.log[0] == "MethodChosen id=57");
    CHECK(hook.log[1].rfind("NeedDelta id=4242", 0) == 0); // need-pick emits first
    CHECK(hook.log[2].rfind("NeedDelta id=4242", 0) == 0); // decay emits its delta
    CHECK(hook.log[3].rfind("AdjustStock type=7 delta=-560", 0) == 0);
}

TEST(AiPlannerE2E, NoEligibleNeedEmitsNothing) {
    crt::Srand(1);
    MockCommandHook hook;
    ai::NeedAgent agent;
    agent.type = 3; agent.id = 1; agent.needWord = 0; // no need flags
    u8 needId = ai::PickRandomFlagFromFourA(agent, &hook);
    CHECK_EQ((int)needId, 0);
    CHECK_EQ((int)hook.log.size(), 0);    // nothing eligible -> no command
}
