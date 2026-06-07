// Unit tests for the .esc script VM core + cutscene slot/RNG (gilde.exe).
// Suites are prefixed SimScript* / SimCutscene* to stay unique.
#include "sim/script_vm.h"
#include "sim/cutscene.h"
#include "tests/framework/test.h"

#include <vector>
#include <string>
#include <cstring>
#include <cstddef>

using namespace guild::sim;

namespace {

// Build a host whose command/function hooks record calls for assertions.
struct RecordingHost {
    std::vector<std::string> cmdCalls;
    std::vector<std::vector<int>> cmdArgs;
    std::vector<int> funcCalls;
    int cmdResult = 0;

    ScriptHost make() {
        ScriptHost h;
        h.invokeCommand = [this](const std::string& name, std::vector<int>& args) -> int {
            cmdCalls.push_back(name);
            cmdArgs.push_back(args);
            return cmdResult;
        };
        h.callUserFunction = [this](int id) -> int {
            funcCalls.push_back(id);
            return id * 10;   // deterministic stub result
        };
        return h;
    }
};

// Convenience token builders.
ScriptToken_t Lit(int v)        { return {kTokIntLit, 0, v, ""}; }
ScriptToken_t Op(unsigned char op){ return {kTokSymbol, op, 0, ""}; }
ScriptToken_t Var(int idx)      { return {kTokVariable, 0, idx, ""}; }
ScriptToken_t Call(int id)      { return {kTokFuncCall, 0, id, ""}; }
ScriptToken_t End()             { return {kTokBlockEnd, 0, 0, ""}; }

} // namespace

// --------------------------------------------------------------------------
// Opcode / token decode table — verify the recovered constants.
// --------------------------------------------------------------------------
TEST(SimScriptOpcodes, TokenClasses) {
    CHECK_EQ((int)kTokSymbol, 1);
    CHECK_EQ((int)kTokVariable, 2);
    CHECK_EQ((int)kTokFuncDef, 3);
    CHECK_EQ((int)kTokFuncCall, 4);
    CHECK_EQ((int)kTokIntLit, 5);
    CHECK_EQ((int)kTokStrLit, 7);
    CHECK_EQ((int)kTokDeclare, 8);
    CHECK_EQ((int)kTokKeyword, 10);
    CHECK_EQ((int)kTokLabel, 11);
    CHECK_EQ((int)kTokBlockEnd, 12);
}

TEST(SimScriptOpcodes, OperatorCodes) {
    CHECK_EQ((int)kOpAdd, 4);
    CHECK_EQ((int)kOpSub, 6);
    CHECK_EQ((int)kOpDiv, 20);
    CHECK_EQ((int)kOpMul, 21);
    CHECK_EQ((int)kOpOr, 24);
    CHECK_EQ((int)kOpAnd, 25);
    CHECK_EQ((int)kOpEq, 1);
    CHECK_EQ((int)kOpNe, 7);
    CHECK_EQ((int)kOpLe, 16);
    CHECK_EQ((int)kOpLt, 17);
    CHECK_EQ((int)kOpGe, 18);
    CHECK_EQ((int)kOpGt, 19);
    CHECK_EQ((int)kOpBitOr, 22);
    CHECK_EQ((int)kOpBitAnd, 23);
}

TEST(SimScriptOpcodes, KeywordCodes) {
    CHECK_EQ((int)kKwWhile, 1);
    CHECK_EQ((int)kKwFor, 2);
    CHECK_EQ((int)kKwReturn, 3);
    CHECK_EQ((int)kKwIf, 4);
    CHECK_EQ((int)kKwModeLoop, 8);
    CHECK_EQ((int)kKwExit, 9);
}

TEST(SimScriptOpcodes, ContextAndCommandStrides) {
    CHECK_EQ(kScriptContextStride, 2584);
    CHECK_EQ(kScriptContextCount, 128);
    CHECK_EQ(kCommandStride, 52);
    CHECK_EQ(kCommandCapacity, 256);
}

// --------------------------------------------------------------------------
// Expression evaluator — arithmetic fold.
// --------------------------------------------------------------------------
TEST(SimScriptEval, AdditionFold) {
    // 2 + 3 + 5  == 10
    RecordingHost host;
    std::vector<ScriptToken_t> prog = {
        Lit(2), Op(kOpAdd), Lit(3), Op(kOpAdd), Lit(5), End(),
    };
    ScriptVm vm(prog, host.make());
    CHECK_EQ(vm.EvalExpression(0), 10);
}

TEST(SimScriptEval, MixedArithmetic) {
    // 10 - 4 * 2  -> folds left-to-right (no precedence): ((10-4)*2) = 12
    RecordingHost host;
    std::vector<ScriptToken_t> prog = {
        Lit(10), Op(kOpSub), Lit(4), Op(kOpMul), Lit(2), End(),
    };
    ScriptVm vm(prog, host.make());
    CHECK_EQ(vm.EvalExpression(0), 12);
}

TEST(SimScriptEval, DivisionAndBitwise) {
    // 20 / 5 | 8  == 4 | 8 == 12
    RecordingHost host;
    std::vector<ScriptToken_t> prog = {
        Lit(20), Op(kOpDiv), Lit(5), Op(kOpOr), Lit(8), End(),
    };
    ScriptVm vm(prog, host.make());
    CHECK_EQ(vm.EvalExpression(0), 12);
}

TEST(SimScriptEval, ComparisonReturnsBool) {
    // 7 == 7  -> 1
    RecordingHost host;
    std::vector<ScriptToken_t> p1 = { Lit(7), Op(kOpEq), Lit(7), End() };
    ScriptVm vm1(p1, host.make());
    CHECK_EQ(vm1.EvalExpression(0), 1);

    // 3 < 9 -> 1 ; 9 < 3 -> 0
    std::vector<ScriptToken_t> p2 = { Lit(3), Op(kOpLt), Lit(9), End() };
    ScriptVm vm2(p2, host.make());
    CHECK_EQ(vm2.EvalExpression(0), 1);

    std::vector<ScriptToken_t> p3 = { Lit(9), Op(kOpLt), Lit(3), End() };
    ScriptVm vm3(p3, host.make());
    CHECK_EQ(vm3.EvalExpression(0), 0);
}

TEST(SimScriptEval, VariableRead) {
    // var[2] + 100, with var[2] = 5 -> 105
    RecordingHost host;
    std::vector<ScriptToken_t> prog = { Var(2), Op(kOpAdd), Lit(100), End() };
    ScriptVm vm(prog, host.make());
    vm.vars().Set(2, 5);
    CHECK_EQ(vm.EvalExpression(0), 105);
}

TEST(SimScriptEval, FunctionCallOperand) {
    // call(3) + 1 -> (3*10) + 1 == 31, and the call is recorded.
    RecordingHost host;
    std::vector<ScriptToken_t> prog = { Call(3), Op(kOpAdd), Lit(1), End() };
    ScriptVm vm(prog, host.make());
    CHECK_EQ(vm.EvalExpression(0), 31);
    CHECK_EQ((int)host.funcCalls.size(), 1);
    CHECK_EQ(host.funcCalls[0], 3);
}

// --------------------------------------------------------------------------
// Statement execution — var-set and call dispatch.
// --------------------------------------------------------------------------
TEST(SimScriptStmt, VariableAssign) {
    // x = 4 + 6  -> x == 10
    RecordingHost host;
    std::vector<ScriptToken_t> prog = {
        Var(0), Lit(4), Op(kOpAdd), Lit(6), End(),
    };
    ScriptVm vm(prog, host.make());
    CHECK(vm.ExecStatement());
    CHECK_EQ(vm.vars().Get(0), 10);
}

TEST(SimScriptStmt, ReturnKeyword) {
    // return 42 -> finished, returnValue == 42
    RecordingHost host;
    std::vector<ScriptToken_t> prog = {
        {kTokKeyword, kKwReturn, 0, ""}, Lit(42), End(),
    };
    ScriptVm vm(prog, host.make());
    bool keep = vm.ExecStatement();
    CHECK(!keep);
    CHECK(vm.finished());
    CHECK_EQ(vm.returnValue(), 42);
}

TEST(SimScriptStmt, InvokeCommand) {
    RecordingHost host;
    host.cmdResult = 7;
    std::vector<ScriptToken_t> prog;
    ScriptVm vm(prog, host.make());
    int r = vm.InvokeCommand("SetWeather", {2, 9});
    CHECK_EQ(r, 7);
    CHECK_EQ((int)host.cmdCalls.size(), 1);
    CHECK(host.cmdCalls[0] == "SetWeather");
    CHECK_EQ((int)host.cmdArgs[0].size(), 2);
    CHECK_EQ(host.cmdArgs[0][0], 2);
    CHECK_EQ(host.cmdArgs[0][1], 9);
}

// --------------------------------------------------------------------------
// Context-table scanners.
// --------------------------------------------------------------------------
TEST(SimScriptSlots, StepAndCount) {
    std::vector<ScriptSlot> slots(4);
    slots[0].runFlags = kRunFlagRunnable;
    slots[0].inUse = 1;
    slots[1].inUse = 1;                 // in use but not runnable
    slots[2].runFlags = kRunFlagRunnable;
    // slots[3] idle

    int stepped = 0;
    StepAllActive(slots, [&](ScriptSlot&) { return ++stepped; });
    CHECK_EQ(stepped, 2);              // only runnable slots stepped

    CHECK_EQ(CountActive(slots), 3);   // in-use(0,1) + runnable(2)
}

TEST(SimScriptSlots, FreeFinished) {
    std::vector<ScriptSlot> slots(3);
    slots[0].handle = 5;               // live handle
    slots[1].inUse = 1;                // in use
    slots[2].handle = -1;              // free
    int destroyed = 0;
    int n = FreeFinished(slots, [&](ScriptSlot&) { ++destroyed; });
    CHECK_EQ(n, 2);
    CHECK_EQ(destroyed, 2);
}

// --------------------------------------------------------------------------
// Cutscene slot management.
// --------------------------------------------------------------------------
TEST(SimCutsceneSlots, StructLayout) {
    CHECK_EQ((int)sizeof(CutsceneSlot), 276);
    CHECK_EQ(kCutsceneSlotCount, 96);
    CHECK_EQ((int)offsetof(CutsceneSlot, id), 0);
    CHECK_EQ((int)offsetof(CutsceneSlot, finished), 4);
    CHECK_EQ((int)offsetof(CutsceneSlot, type), 8);
    CHECK_EQ((int)offsetof(CutsceneSlot, master), 12);
    CHECK_EQ((int)offsetof(CutsceneSlot, secondId), 20);
    CHECK_EQ((int)offsetof(CutsceneSlot, readyTime), 24);
    CHECK_EQ((int)offsetof(CutsceneSlot, stateFlags), 38);
    CHECK_EQ((int)offsetof(CutsceneSlot, started), 40);
    CHECK_EQ((int)offsetof(CutsceneSlot, partCount), 48);
    CHECK_EQ((int)offsetof(CutsceneSlot, priority), 49);
    CHECK_EQ((int)offsetof(CutsceneSlot, partIds), 52);
}

TEST(SimCutsceneSlots, AllocFindRemove) {
    CutsceneTable tbl;
    CutsceneSlot tmpl;
    std::memset(&tmpl, 0, sizeof(tmpl));
    tmpl.type = 3;
    tmpl.partCount = 1;                 // make it "alive" so FindById sees it
    tmpl.secondId = 77;

    CutsceneSlot* s = tbl.AllocSlot(tmpl, 100);
    CHECK(s != nullptr);
    CHECK_EQ(s->id, 100);
    CHECK_EQ((int)s->priority, 8);     // defaulted from 0
    CHECK_EQ((int)s->type, 3);

    CHECK(tbl.FindById(100) == s);
    CHECK(tbl.FindByTypeAndId(3, 77) == s);
    CHECK(tbl.FindById(999) == nullptr);

    CHECK(tbl.RemoveById(100));
    CHECK(tbl.FindById(100) == nullptr);
}

TEST(SimCutsceneSlots, AllocFillsTableThenFails) {
    CutsceneTable tbl;
    CutsceneSlot tmpl;
    std::memset(&tmpl, 0, sizeof(tmpl));
    tmpl.partCount = 1;
    for (int i = 0; i < kCutsceneSlotCount; ++i) {
        CHECK(tbl.AllocSlot(tmpl, 1000 + i) != nullptr);
    }
    CHECK(tbl.AllocSlot(tmpl, 9999) == nullptr);  // full
}

TEST(SimCutsceneSlots, Participants) {
    // FindById requires the alive gate (partCount, +48) to be nonzero, so the
    // slot starts live with one participant already present (the engine seeds a
    // slot's gate when it goes live). We then append/remove through the API.
    CutsceneTable tbl;
    CutsceneSlot tmpl;
    std::memset(&tmpl, 0, sizeof(tmpl));
    tmpl.partCount = 1;
    tmpl.partIds[0] = 11;              // first participant
    CutsceneSlot* s = tbl.AllocSlot(tmpl, 50);
    CHECK(s != nullptr);

    CHECK_EQ(tbl.AddParticipant(50, 22), 1);
    CHECK_EQ((int)s->partCount, 2);
    CHECK_EQ(s->partIds[1], 22);
    CHECK_EQ(tbl.AddParticipant(50, 22), 0);   // duplicate rejected
    CHECK_EQ(tbl.RemoveParticipant(50, 11), 1);
    CHECK_EQ(s->partIds[0], -1);
}

TEST(SimCutsceneSlots, LowestPriority) {
    CutsceneTable tbl;
    CutsceneSlot tmpl;
    std::memset(&tmpl, 0, sizeof(tmpl));
    tmpl.partCount = 1;
    tmpl.stateFlags = kCsFlagActive;

    tbl.AllocSlot(tmpl, 30);
    tbl.AllocSlot(tmpl, 10);
    tbl.AllocSlot(tmpl, 20);
    CutsceneSlot* low = tbl.FindLowestPriority();
    CHECK(low != nullptr);
    CHECK_EQ(low->id, 10);   // smallest id among active slots
}

// --------------------------------------------------------------------------
// Cutscene RNG — golden vectors (python-computed from the recovered LCG).
// --------------------------------------------------------------------------
TEST(SimCutsceneRng, IntGoldenSequence) {
    // seed 12345, RandInt(100): python reference [69,89,18,98,28,45,42,23]
    CutsceneRng rng;
    rng.SetSeed(12345);
    const unsigned expected[] = {69, 89, 18, 98, 28, 45, 42, 23};
    for (unsigned i = 0; i < 8; ++i) {
        CHECK_EQ(rng.RandInt(100), expected[i]);
    }
}

TEST(SimCutsceneRng, RangeZeroReturnsZero) {
    CutsceneRng rng;
    rng.SetSeed(1);
    CHECK_EQ(rng.RandInt(0), 0u);
}

TEST(SimCutsceneRng, FloatGoldenSequence) {
    // seed 999, RandFloat(): python reference
    CutsceneRng rng;
    rng.SetSeed(999);
    const double expected[] = {
        0.35050508100539446, 0.7218237854540348, 0.13815729226917028,
        0.38865321781486273, 0.12537003681063652,
    };
    for (int i = 0; i < 5; ++i) {
        double v = rng.RandFloat();
        double d = v - expected[i];
        if (d < 0) d = -d;
        CHECK(d < 1e-12);
        CHECK(v >= 0.0 && v < 1.0);
    }
}

TEST(SimCutsceneRng, GetSeedRoundTrip) {
    CutsceneRng rng;
    rng.SetSeed(0x12345678);
    CHECK_EQ(rng.GetSeed(), 0x12345678);
}
