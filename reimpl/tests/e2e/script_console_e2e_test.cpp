// End-to-end flow across the script_console slice: cold-init the engine, then drive a
// small "console session" — register an include, assign into its variables, step the
// context's statements to completion, and finally tear nothing down (the engine tables
// persist).  All cross-module leaves are supplied by one installed hook set so the flow
// is fully deterministic; this exercises the functions in composition, not in isolation.
#include "test.h"

#include "sim/script_console.h"
#include <cstring>
#include <vector>

using namespace guild;
using guild::sim::ScriptConsoleHooks;
using guild::sim::SetScriptConsoleHooks;
using guild::sim::ScriptEngine;
using guild::sim::ScriptTables;
using guild::sim::ScriptRun;

namespace {

// --- scripted lexer / evaluator state for the session ---
guild::u8 g_includedCtx[64];
int   g_compiled = 0;
int   g_statementsRun = 0;

// Lexer: first call returns a string-literal (class 7) for the include path.
guild::u8 LexInclude(const char*, guild::u8* out) { if (out) out[0] = 7; return 7; }

// Lexer for the assignment: ident (class 1) with sub-code '=' (2).
guild::u8 LexAssignEq(const char*, guild::u8* out) {
    if (out) { out[0] = 1; out[4] = 2; }
    return 1;
}

guild::i32 EvalSeven(int) { return 7; }

guild::u8* LoadInclude(char*) {
    std::strcpy(reinterpret_cast<char*>(g_includedCtx), "library.esc");
    return g_includedCtx;
}
int Compile(guild::u8*) { ++g_compiled; return 1; }

// ExecuteStatement: run a fixed number of statements then mark the context done by
// clearing stmtMode and the runnable bit.
guild::i32 ExecStmt(guild::u8* c) {
    ++g_statementsRun;
    if (c) {
        c[guild::sim::kScStmtMode] = (g_statementsRun < 3) ? 2 : 0;  // keep looping twice
        if (g_statementsRun >= 3) c[guild::sim::kScRunFlags] &= ~1;  // clear runnable
    }
    return static_cast<guild::i32>(g_statementsRun);
}

void* AllocZeroed(int size, const char*) {
    if (size <= 0) return nullptr;
    void* p = ::operator new[](static_cast<std::size_t>(size));
    std::memset(p, 0, static_cast<std::size_t>(size));
    return p;
}

} // namespace

TEST(ScriptConsoleE2E, ConsoleSessionFlow) {
    guild::sim::ResetScriptTables();
    guild::sim::ResetScriptTokens();
    guild::sim::ResetScriptRun();

    // 1) Cold-init the engine tables + token table.
    ScriptConsoleHooks initHooks{};
    initHooks.allocDebug = &AllocZeroed;
    SetScriptConsoleHooks(&initHooks);
    CHECK_EQ(guild::sim::ConsoleParseLine(), 1);
    CHECK(ScriptTables().ctxTable != nullptr);

    // Use the freshly allocated context table's first context as our session context.
    guild::u8* ctx = ScriptTables().ctxTable;
    CHECK(ctx != nullptr);
    if (!ctx) { SetScriptConsoleHooks(nullptr); return; }

    // 2) Register an include into the session context.
    g_compiled = 0;
    ScriptConsoleHooks incHooks{};
    incHooks.nextToken = &LexInclude;
    incHooks.loadFromDir = &LoadInclude;
    incHooks.compileBlock = &Compile;
    incHooks.utilStrCmp = [](const char* a, const char* b) { return std::strcmp(a, b); };
    SetScriptConsoleHooks(&incHooks);

    guild::i32 slot = guild::sim::ParseInclude(ctx);
    CHECK_EQ(slot, 0);
    CHECK_EQ(g_compiled, 1);
    // The include is now resolvable.
    guild::i32 found = guild::sim::LookupInclude(ctx, "library.esc");
    CHECK(found != 0);

    // 3) Assign a variable in the context (dword cell at varBase+0 = 7).
    guild::u8 store[64] = {};
    guild::u8 node[64] = {};
    node[0] = 0x01;  // low nibble 1 == dword storage
    *reinterpret_cast<guild::u8**>(node + guild::sim::kCallFnPtr) = store;
    ScriptEngine().currentCtx = ctx;
    ScriptConsoleHooks asgHooks{};
    asgHooks.nextToken = &LexAssignEq;
    asgHooks.evalExpression = &EvalSeven;
    SetScriptConsoleHooks(&asgHooks);
    guild::sim::AssignVariable(node);
    CHECK_EQ(*reinterpret_cast<guild::i32*>(store), 7);

    // 4) Make the context eligible and step it to completion.
    ScriptEngine().ownerId = 42;
    *reinterpret_cast<guild::i32*>(ctx + guild::sim::kScOwner) = 42; // owner == ownerId -> eligible
    *reinterpret_cast<guild::i32*>(ctx + guild::sim::kScCallCount) = 1;
    *reinterpret_cast<guild::u8**>(ctx + guild::sim::kScSceneCtx) = nullptr;
    *reinterpret_cast<guild::i32*>(ctx + guild::sim::kScSceneBlocked) = 0;
    *reinterpret_cast<guild::i32*>(ctx + guild::sim::kScCmdBlocked) = 0;
    ctx[guild::sim::kScRunFlags] = 1;     // runnable
    ctx[guild::sim::kScStmtMode] = 2;     // re-run-statement mode (drives the loop)

    g_statementsRun = 0;
    ScriptConsoleHooks runHooks{};
    runHooks.execStatement = &ExecStmt;
    SetScriptConsoleHooks(&runHooks);

    ScriptEngine().currentCtx = nullptr;
    guild::sim::Step(ctx);
    // The statement loop ran until ExecStmt cleared stmtMode/runnable.
    CHECK_EQ(g_statementsRun, 3);
    CHECK_EQ(ScriptRun().depth, 0);   // run stack balanced

    SetScriptConsoleHooks(nullptr);
    ScriptEngine().currentCtx = nullptr;
    guild::sim::ResetScriptRun();
    guild::sim::ResetScriptTables();
    guild::sim::ResetScriptTokens();
}
