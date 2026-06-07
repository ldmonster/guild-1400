// Unit tests for the script_console slice (script_console.cpp): the engine
// cold-init, the per-context Step driver, the variable-assignment parser, the
// include parser/lookup and the function-call dispatcher.  Each test installs its
// own ScriptConsoleHooks so the cross-module leaves are deterministic.
#include "test.h"

#include "sim/script_console.h"
#include <cstring>
#include <vector>

using namespace guild;
using guild::sim::ScriptConsoleHooks;
using guild::sim::SetScriptConsoleHooks;
using guild::sim::ScriptEngine;
using guild::sim::ScriptTables;
using guild::sim::ScriptTokens;
using guild::sim::ScriptRun;
using guild::sim::ResetScriptRun;
using guild::sim::ResetScriptTokens;

namespace {

// A zeroed script context.  Sized with pointer-width slack past the 2584-byte
// record so the +2580 sceneCtx field (widened to a real pointer on 64-bit) and the
// +2492 include slots (pointer stride) can be read without overrunning.
struct Ctx {
    std::vector<guild::u8> bytes;
    Ctx() : bytes(guild::sim::kScriptContextStride + 16, 0) {}
    guild::u8* p() { return bytes.data(); }
    guild::i32& i32at(int off) { return *reinterpret_cast<guild::i32*>(bytes.data() + off); }
    guild::u8&  u8at(int off)  { return bytes[off]; }
};

// Recording allocator: counts AllocDebug calls and hands back zeroed blocks.
struct AllocRec {
    int calls = 0;
    std::vector<void*> blocks;
};
AllocRec* g_alloc = nullptr;
void* RecAlloc(int size, const char*) {
    if (g_alloc) ++g_alloc->calls;
    if (size <= 0) return nullptr;
    void* p = ::operator new[](static_cast<std::size_t>(size));
    std::memset(p, 0, static_cast<std::size_t>(size));
    if (g_alloc) g_alloc->blocks.push_back(p);
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// ConsoleParseLine: allocates five tables, fills the context table +132 words with
// -1, and copies the 39-entry token table.
// ---------------------------------------------------------------------------
TEST(ScriptConsole, ConsoleParseLineInitsTables) {
    guild::sim::ResetScriptTables();
    ResetScriptTokens();
    AllocRec rec; g_alloc = &rec;

    ScriptConsoleHooks h{}; h.allocDebug = &RecAlloc;
    SetScriptConsoleHooks(&h);

    guild::i32 r = guild::sim::ConsoleParseLine();
    CHECK_EQ(r, 1);
    CHECK_EQ(rec.calls, 5);            // five tagged allocations

    auto& T = ScriptTables();
    CHECK(T.ctxTable != nullptr);
    CHECK(T.cmdTable != nullptr);
    if (T.ctxTable) {
        // Each of the 128 contexts: +128 handle word == -1 (the +128 == 2584-2456 fill).
        bool allFree = true;
        for (int c = 0; c < guild::sim::kScriptContextCount; ++c) {
            if (*reinterpret_cast<guild::i32*>(T.ctxTable + c * guild::sim::kScriptContextStride + 128) != -1) {
                allFree = false; break;
            }
        }
        CHECK(allFree);
    }
    // Token table copied verbatim (dense ASCII source -> byte-identical dest).
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(ScriptTokens().slot[0]), " "), 0);
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(ScriptTokens().slot[29]), "while"), 0);
    CHECK_EQ(std::strcmp(reinterpret_cast<const char*>(ScriptTokens().slot[34]), "#include"), 0);

    SetScriptConsoleHooks(nullptr);
    g_alloc = nullptr;
    guild::sim::ResetScriptTables();
    ResetScriptTokens();
}

TEST(ScriptConsole, TokenTableTextOrder) {
    // The recovered copy order: index 6 is "=", 9 is "{", 28 is "*", 34 is "#include".
    CHECK_EQ(std::strcmp(guild::sim::kScriptTokenText[6], "="), 0);
    CHECK_EQ(std::strcmp(guild::sim::kScriptTokenText[9], "{"), 0);
    CHECK_EQ(std::strcmp(guild::sim::kScriptTokenText[28], "*"), 0);
    CHECK_EQ(std::strcmp(guild::sim::kScriptTokenText[34], "#include"), 0);
}

// ---------------------------------------------------------------------------
// LookupInclude: scan ctx+2492 slot table for a matching stored-context name.
// ---------------------------------------------------------------------------
TEST(ScriptConsole, LookupIncludeFindsAndMisses) {
    Ctx ctx;
    // Two loaded sub-script "contexts": their bytes begin with the name string.  The
    // include slots store FULL-WIDTH pointers (the model widens the original dword),
    // so write a real pointer, not a truncated i32 (avoids ASLR flake).
    char nameA[] = "alpha.esc";
    char nameB[] = "beta.esc";
    const int S = guild::sim::kIncludeSlotStride;
    *reinterpret_cast<char**>(ctx.p() + 2492 + 0 * S) = nameA;
    *reinterpret_cast<char**>(ctx.p() + 2492 + 1 * S) = nameB;

    // Default hook (faithful strcmp).
    SetScriptConsoleHooks(nullptr);

    guild::i32 found = guild::sim::LookupInclude(ctx.p(), "beta.esc");
    CHECK(found != 0);
    guild::i32 miss  = guild::sim::LookupInclude(ctx.p(), "gamma.esc");
    CHECK_EQ(miss, 0);

    SetScriptConsoleHooks(nullptr);
}

// ---------------------------------------------------------------------------
// ParseInclude: string token -> load+compile+register in first free slot.
// ---------------------------------------------------------------------------
namespace {
int g_compileCount;
guild::u8 g_fakeLoaded[64];
guild::u8 DefTok7(const char*, guild::u8* out) { if (out) out[0] = 7; return 7; }
guild::u8 DefTok1(const char*, guild::u8* out) { if (out) out[0] = 1; return 1; }
guild::u8* LoadOk(char*)  { return g_fakeLoaded; }
guild::u8* LoadFail(char*) { return nullptr; }
int CompileCount(guild::u8*) { ++g_compileCount; return 1; }
int g_reportCount; int g_finishCount;
void RepInc(guild::u8*, guild::u32, const char*) { ++g_reportCount; }
guild::i32 FinInc(guild::u8*) { ++g_finishCount; return 0; }
}

TEST(ScriptConsole, ParseIncludeRegistersSlot) {
    Ctx ctx;
    g_compileCount = 0;
    ScriptConsoleHooks h{};
    h.nextToken = &DefTok7;       // class 7 == string literal
    h.loadFromDir = &LoadOk;
    h.compileBlock = &CompileCount;
    SetScriptConsoleHooks(&h);

    guild::i32 slot = guild::sim::ParseInclude(ctx.p());
    CHECK_EQ(slot, 0);            // first free slot byte-offset (index 0)
    CHECK_EQ(g_compileCount, 1);
    // loaded context registered in slot 0 (read as a full pointer).
    CHECK(*reinterpret_cast<guild::u8**>(ctx.p() + 2492) == g_fakeLoaded);

    // Second include lands in slot offset 4 (index 1).
    guild::i32 slot2 = guild::sim::ParseInclude(ctx.p());
    CHECK_EQ(slot2, 4);
    CHECK(*reinterpret_cast<guild::u8**>(ctx.p() + 2492 + guild::sim::kIncludeSlotStride) == g_fakeLoaded);

    SetScriptConsoleHooks(nullptr);
}

TEST(ScriptConsole, ParseIncludeLoadFailureFinishes) {
    Ctx ctx;
    g_reportCount = 0; g_finishCount = 0;
    ScriptConsoleHooks h{};
    h.nextToken = &DefTok7;
    h.loadFromDir = &LoadFail;    // load fails
    h.reportError = &RepInc;
    h.finishCtx = &FinInc;
    SetScriptConsoleHooks(&h);

    guild::i32 r = guild::sim::ParseInclude(ctx.p());
    CHECK_EQ(r, 0);
    CHECK_EQ(g_reportCount, 1);
    CHECK_EQ(g_finishCount, 1);

    SetScriptConsoleHooks(nullptr);
}

TEST(ScriptConsole, ParseIncludeWrongTokenFinishes) {
    Ctx ctx;
    g_reportCount = 0; g_finishCount = 0;
    ScriptConsoleHooks h{};
    h.nextToken = &DefTok1;       // class 1 != 7 -> wrong-parameter error
    h.reportError = &RepInc;
    h.finishCtx = &FinInc;
    SetScriptConsoleHooks(&h);

    guild::i32 r = guild::sim::ParseInclude(ctx.p());
    CHECK_EQ(r, 0);
    CHECK_EQ(g_reportCount, 1);
    CHECK_EQ(g_finishCount, 1);

    SetScriptConsoleHooks(nullptr);
}

// ---------------------------------------------------------------------------
// AssignVariable: typed store / increment / decrement driven by the lvalue node's
// high nibble and the lexed operator sub-code.
// ---------------------------------------------------------------------------
namespace {
guild::u8  g_assignSub;        // sub-code the fake lexer reports
guild::i32 g_evalValue;        // value EvaluateExpression returns
guild::u8 LexAssign(const char*, guild::u8* out) {
    if (out) { out[0] = 1; out[4] = g_assignSub; }
    return 1;
}
guild::i32 EvalConst(int) { return g_evalValue; }
}

TEST(ScriptConsole, AssignVariableDwordStore) {
    // Storage block + lvalue node: node[0] high nibble == 1 (dword), node+44 -> base.
    guild::u8 store[64] = {};
    guild::u8 node[64] = {};
    // Storage-type selector is the LOW nibble of node[0] (the original computes
    // (char)(16*node[0]) >> 4, i.e. the sign-extended low nibble): 1 == dword.
    node[0] = 0x01;
    *reinterpret_cast<guild::u8**>(node + 44) = store;

    Ctx ctx;
    ScriptEngine().currentCtx = ctx.p();
    g_assignSub = 2;          // plain '='
    g_evalValue = 0x1234;
    ScriptConsoleHooks h{};
    h.nextToken = &LexAssign;
    h.evalExpression = &EvalConst;
    SetScriptConsoleHooks(&h);

    guild::i32 r = guild::sim::AssignVariable(node);
    CHECK_EQ(r, 0);
    CHECK_EQ(*reinterpret_cast<guild::i32*>(store), 0x1234);

    // '++' on the same dword cell.
    g_assignSub = 3;
    guild::sim::AssignVariable(node);
    CHECK_EQ(*reinterpret_cast<guild::i32*>(store), 0x1235);

    // '--' on the same dword cell.
    g_assignSub = 5;
    guild::sim::AssignVariable(node);
    CHECK_EQ(*reinterpret_cast<guild::i32*>(store), 0x1234);

    ScriptEngine().currentCtx = nullptr;
    SetScriptConsoleHooks(nullptr);
}

TEST(ScriptConsole, AssignVariableByteStore) {
    guild::u8 store[64] = {};
    guild::u8 node[64] = {};
    node[0] = 0x02;  // low nibble 2 == byte storage
    *reinterpret_cast<guild::u8**>(node + 44) = store;

    Ctx ctx;
    ScriptEngine().currentCtx = ctx.p();
    g_assignSub = 2;
    g_evalValue = 0xAB;
    ScriptConsoleHooks h{};
    h.nextToken = &LexAssign;
    h.evalExpression = &EvalConst;
    SetScriptConsoleHooks(&h);

    guild::sim::AssignVariable(node);
    CHECK_EQ(store[0], 0xAB);

    ScriptEngine().currentCtx = nullptr;
    SetScriptConsoleHooks(nullptr);
}

TEST(ScriptConsole, AssignVariableRejectsNonIdent) {
    // token[0] != 1 -> immediate 0, no store.
    guild::u8 node[64] = {};
    Ctx ctx;
    ScriptEngine().currentCtx = ctx.p();
    ScriptConsoleHooks h{};
    h.nextToken = [](const char*, guild::u8* out) -> guild::u8 { if (out) out[0] = 5; return 5; };
    SetScriptConsoleHooks(&h);
    CHECK_EQ(guild::sim::AssignVariable(node), 0);
    ScriptEngine().currentCtx = nullptr;
    SetScriptConsoleHooks(nullptr);
}

// ---------------------------------------------------------------------------
// Step: not-runnable context (free, owner==-1) just pushes/pops without running.
// ---------------------------------------------------------------------------
namespace {
int g_execCount; int g_invokeCount;
guild::i32 ExecInc(guild::u8* c) { ++g_execCount; if (c) c[guild::sim::kScStmtMode] = 0; return 7; }
guild::i32 InvokeInc(guild::u8*, int) { ++g_invokeCount; return 3; }
}

TEST(ScriptConsole, StepNotEligibleJustPops) {
    ResetScriptRun();
    Ctx ctx;
    // NOT eligible to run: owner != -1, owner != ownerId, no scene block, not
    // suspended, callCount != 0.
    ctx.i32at(guild::sim::kScOwner) = 5;
    ScriptEngine().ownerId = 99;             // != owner
    ctx.i32at(guild::sim::kScSceneBlocked) = 0;
    ctx.u8at(guild::sim::kScRunFlags) = 0;
    ctx.i32at(guild::sim::kScCallCount) = 1; // callCount != 0
    g_execCount = 0; g_invokeCount = 0;
    ScriptConsoleHooks h{};
    h.execStatement = &ExecInc;
    h.invokeCommand = &InvokeInc;
    SetScriptConsoleHooks(&h);

    ScriptEngine().currentCtx = nullptr;
    guild::sim::Step(ctx.p());
    CHECK_EQ(g_execCount, 0);     // never ran statements
    CHECK_EQ(g_invokeCount, 0);
    CHECK_EQ(ScriptRun().depth, 0); // pushed then popped back to base

    ScriptEngine().currentCtx = nullptr;
    SetScriptConsoleHooks(nullptr);
    ResetScriptRun();
}

TEST(ScriptConsole, StepEligibleRunsStatements) {
    ResetScriptRun();
    Ctx ctx;
    // Eligible (callCount == 0 path runs the body); cmdBlocked==0 -> statements.
    ctx.i32at(guild::sim::kScOwner) = 5;
    ScriptEngine().ownerId = 99;
    ctx.i32at(guild::sim::kScCallCount) = 0;  // callCount == 0 -> eligible
    ctx.i32at(guild::sim::kScSceneCtx) = 0;   // no scene pre-empt
    ctx.i32at(guild::sim::kScSceneBlocked) = 0;
    ctx.u8at(guild::sim::kScRunFlags) = 0;
    ctx.i32at(guild::sim::kScCmdBlocked) = 0; // run statements
    ctx.u8at(guild::sim::kScStmtMode) = 0;
    g_execCount = 0; g_invokeCount = 0;
    ScriptConsoleHooks h{};
    h.execStatement = &ExecInc;
    h.invokeCommand = &InvokeInc;
    SetScriptConsoleHooks(&h);

    ScriptEngine().currentCtx = nullptr;
    guild::i32 r = guild::sim::Step(ctx.p());
    CHECK_EQ(g_execCount, 1);     // ran statements once (ExecInc clears stmtMode)
    CHECK_EQ(g_invokeCount, 0);
    CHECK_EQ(r & 0xFF, 7);        // ExecInc result byte

    ScriptEngine().currentCtx = nullptr;
    SetScriptConsoleHooks(nullptr);
    ResetScriptRun();
}

TEST(ScriptConsole, StepEligiblePendingCommand) {
    ResetScriptRun();
    Ctx ctx;
    ctx.i32at(guild::sim::kScOwner) = 5;
    ScriptEngine().ownerId = 5;               // owner == ownerId -> eligible
    ctx.i32at(guild::sim::kScCallCount) = 1;
    ctx.i32at(guild::sim::kScSceneCtx) = 0;
    ctx.i32at(guild::sim::kScSceneBlocked) = 0;
    ctx.u8at(guild::sim::kScRunFlags) = 0;
    ctx.i32at(guild::sim::kScCmdBlocked) = 1;  // pending command -> InvokeCommand
    g_execCount = 0; g_invokeCount = 0;
    ScriptConsoleHooks h{};
    h.execStatement = &ExecInc;
    h.invokeCommand = &InvokeInc;
    SetScriptConsoleHooks(&h);

    ScriptEngine().currentCtx = nullptr;
    guild::sim::Step(ctx.p());
    CHECK_EQ(g_invokeCount, 1);
    // cmdBlocked still 1 after InvokeCommand -> statements skipped.
    CHECK_EQ(g_execCount, 0);

    ScriptEngine().currentCtx = nullptr;
    SetScriptConsoleHooks(nullptr);
    ResetScriptRun();
}

// ---------------------------------------------------------------------------
// CallFunction: argc==0 path bumps the call counter and dispatches the fn.
// ---------------------------------------------------------------------------
namespace {
guild::i32 RetFortyTwo() { return 42; }
}

TEST(ScriptConsole, CallFunctionZeroArgDispatch) {
    Ctx ctx;
    ScriptEngine().currentCtx = ctx.p();
    SetScriptConsoleHooks(nullptr);

    guild::u8 node[64] = {};
    *reinterpret_cast<guild::i32*>(node + 32) = 0;   // argc 0
    using Fn0 = guild::i32 (*)();
    *reinterpret_cast<Fn0*>(node + guild::sim::kCallFnPtr) = &RetFortyTwo;
    node[guild::sim::kCallRetType] = 0;              // ret type != 7 (no overlap)

    guild::i32 before = ctx.i32at(guild::sim::kScCallCount);
    guild::i32 r = guild::sim::CallFunction(node);
    CHECK_EQ(r, 42);
    // argc==0 path bumps the call counter twice (entry + the zero-arg bump).
    CHECK_EQ(ctx.i32at(guild::sim::kScCallCount), before + 2);

    ScriptEngine().currentCtx = nullptr;
    SetScriptConsoleHooks(nullptr);
}

TEST(ScriptConsole, CallFunctionFloatReturnZeroed) {
    Ctx ctx;
    ScriptEngine().currentCtx = ctx.p();
    SetScriptConsoleHooks(nullptr);

    guild::u8 node[64] = {};
    *reinterpret_cast<guild::i32*>(node + 32) = 0;
    using Fn0 = guild::i32 (*)();
    *reinterpret_cast<Fn0*>(node + guild::sim::kCallFnPtr) = &RetFortyTwo;
    node[guild::sim::kCallRetType] = 7;   // ret type 7 -> float result word

    guild::i32 r = guild::sim::CallFunction(node);
    CHECK_EQ(r, 0);   // float-result word zeroed on the inert path

    ScriptEngine().currentCtx = nullptr;
    SetScriptConsoleHooks(nullptr);
}
