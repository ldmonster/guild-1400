// End-to-end: drive a small scripted cutscene through the .esc VM core +
// cutscene slot/RNG substrate, tick by tick, and verify the execution trace and
// final state against a hand-computed reference.
//
// Scenario ("mini duel cutscene"):
//   1. Allocate a cutscene slot (type=duel) and register two participants.
//   2. Run a tiny .esc program:
//        hp      = 100 - 30        // var-set + arithmetic
//        winner  = (hp > 50)       // branch condition
//        if (winner) PlayAnim(7)   // emit a mock command when the branch holds
//   3. Roll a duel-outcome tier from the cutscene RNG (seeded, golden).
//   4. Mark the slot finished and remove it.
#include "sim/script_vm.h"
#include "sim/cutscene.h"
#include "tests/framework/test.h"

#include <vector>
#include <string>
#include <cstring>

using namespace guild::sim;

namespace {
ScriptToken_t Lit(int v)          { return {kTokIntLit, 0, v, ""}; }
ScriptToken_t Op(unsigned char o) { return {kTokSymbol, o, 0, ""}; }
ScriptToken_t Var(int i)          { return {kTokVariable, 0, i, ""}; }
ScriptToken_t End()               { return {kTokBlockEnd, 0, 0, ""}; }
ScriptToken_t Kw(unsigned char k) { return {kTokKeyword, k, 0, ""}; }
} // namespace

TEST(SimScriptE2E, MiniDuelCutscene) {
    // --- trace collectors ---
    std::vector<std::string> trace;

    // --- cutscene slot setup ---
    CutsceneTable tbl;
    CutsceneSlot tmpl;
    std::memset(&tmpl, 0, sizeof(tmpl));
    tmpl.type = 1;                 // duel type
    tmpl.partCount = 1;            // alive gate, first participant seeded
    tmpl.partIds[0] = 501;        // challenger
    CutsceneSlot* slot = tbl.AllocSlot(tmpl, 7);
    CHECK(slot != nullptr);
    CHECK_EQ((int)slot->priority, 8);    // alloc default
    trace.push_back("alloc:7");

    CHECK_EQ(tbl.AddParticipant(7, 502), 1);   // opponent
    CHECK_EQ((int)slot->partCount, 2);
    trace.push_back("part:502");

    // --- host hooks: record command emission ---
    ScriptHost host;
    host.invokeCommand = [&](const std::string& name, std::vector<int>& args) -> int {
        std::string s = "cmd:" + name;
        for (int a : args) s += ":" + std::to_string(a);
        trace.push_back(s);
        return 0;
    };
    host.callUserFunction = [&](int id) -> int { return id; };

    // Variable indices: 0 = hp, 1 = winner.
    // Statement 1: hp = 100 - 30
    {
        std::vector<ScriptToken_t> prog = {
            Var(0), Lit(100), Op(kOpSub), Lit(30), End(),
        };
        ScriptVm vm(prog, host);
        CHECK(vm.ExecStatement());
        CHECK_EQ(vm.vars().Get(0), 70);
        trace.push_back("hp=" + std::to_string(vm.vars().Get(0)));

        // carry hp forward to the next statement's VM by seeding its store.
        // Statement 2: winner = (hp > 50)
        std::vector<ScriptToken_t> prog2 = {
            Var(1), Var(0), Op(kOpGt), Lit(50), End(),
        };
        ScriptVm vm2(prog2, host);
        vm2.vars().Set(0, vm.vars().Get(0));
        CHECK(vm2.ExecStatement());
        int winner = vm2.vars().Get(1);
        CHECK_EQ(winner, 1);                 // 70 > 50 -> true
        trace.push_back("winner=" + std::to_string(winner));

        // Statement 3: if (winner) PlayAnim(7)
        // Model the branch: evaluate condition, and on true emit the command.
        std::vector<ScriptToken_t> cond = { Var(1), End() };
        ScriptVm vmc(cond, host);
        vmc.vars().Set(1, winner);
        int c = vmc.EvalExpression(0);
        CHECK_EQ(c, 1);
        if (c) {
            std::vector<int> args = {7};
            vmc.InvokeCommand("PlayAnim", args);
        }
    }

    // --- cutscene RNG: roll a duel outcome tier (seeded golden) ---
    CutsceneRng rng;
    rng.SetSeed(12345);
    unsigned tier = rng.RandInt(100);     // golden: 69
    CHECK_EQ(tier, 69u);
    trace.push_back("tier=" + std::to_string(tier));

    // --- finish + remove the slot ---
    slot->finished = 1;
    slot->stateFlags |= kCsFlagReady;
    trace.push_back("finish:7");
    CHECK(tbl.RemoveById(7));
    CHECK(tbl.FindById(7) == nullptr);
    trace.push_back("remove:7");

    // --- verify the full trace against the hand-computed reference ---
    const std::vector<std::string> expected = {
        "alloc:7",
        "part:502",
        "hp=70",
        "winner=1",
        "cmd:PlayAnim:7",
        "tier=69",
        "finish:7",
        "remove:7",
    };
    CHECK_EQ((int)trace.size(), (int)expected.size());
    for (size_t i = 0; i < expected.size() && i < trace.size(); ++i) {
        CHECK(trace[i] == expected[i]);
    }
}

// A second flow: branch NOT taken (no command emitted), exercising the
// false-condition path and the slot's lowest-priority pick across two slots.
TEST(SimScriptE2E, BranchNotTakenAndPriority) {
    std::vector<std::string> trace;
    ScriptHost host;
    host.invokeCommand = [&](const std::string& name, std::vector<int>&) -> int {
        trace.push_back("cmd:" + name);
        return 0;
    };
    host.callUserFunction = [&](int id) -> int { return id; };

    // hp = 10 - 30  (= -20) ; winner = (hp > 50) -> false ; if(winner) skip cmd
    std::vector<ScriptToken_t> prog = {
        Var(0), Lit(10), Op(kOpSub), Lit(30), End(),
    };
    ScriptVm vm(prog, host);
    CHECK(vm.ExecStatement());
    CHECK_EQ(vm.vars().Get(0), -20);

    std::vector<ScriptToken_t> cond = { Var(0), Op(kOpGt), Lit(50), End() };
    ScriptVm vmc(cond, host);
    vmc.vars().Set(0, vm.vars().Get(0));
    int c = vmc.EvalExpression(0);
    CHECK_EQ(c, 0);                  // -20 > 50 -> false
    if (c) { std::vector<int> a; vmc.InvokeCommand("PlayAnim", a); }
    CHECK_EQ((int)trace.size(), 0);  // no command emitted

    // priority pick across two active slots
    CutsceneTable tbl;
    CutsceneSlot tmpl;
    std::memset(&tmpl, 0, sizeof(tmpl));
    tmpl.partCount = 1;
    tmpl.stateFlags = kCsFlagActive;
    tbl.AllocSlot(tmpl, 40);
    tbl.AllocSlot(tmpl, 15);
    CutsceneSlot* low = tbl.FindLowestPriority();
    CHECK(low != nullptr);
    CHECK_EQ(low->id, 15);
}

// Verify the if-branch keyword path in ExecStatement skips the guarded token
// when the condition is false (the recovered DispatchTokenBranch semantics).
TEST(SimScriptE2E, KeywordIfSkipsGuardedStatement) {
    std::vector<std::string> trace;
    ScriptHost host;
    host.invokeCommand = nullptr;
    host.callUserFunction = [&](int id) -> int {
        trace.push_back("call:" + std::to_string(id));
        return 0;
    };

    // if (0) ; call(9)  -- the guarded statement (a func-call token) is skipped.
    std::vector<ScriptToken_t> prog = {
        Kw(kKwIf), Lit(0), End(),            // condition evaluates to 0 (false)
        {kTokFuncCall, 0, 9, ""},            // guarded statement -> skipped
    };
    ScriptVm vm(prog, host);
    CHECK(vm.ExecStatement());               // runs the if, skips guarded token
    CHECK_EQ((int)trace.size(), 0);          // call(9) was skipped
}
