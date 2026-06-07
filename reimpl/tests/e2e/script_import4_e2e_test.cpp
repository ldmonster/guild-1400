#include "test.h"
// E2E: a script lifecycle flow across the script_import4 bodies.
//   1. ParseAndRunCall splits an argument line and dispatches to RunWithArgs,
//      which compiles, looks up `main`, binds the args and marks the context
//      runnable (entering the function).
//   2. The running context sets its statement-mode flags (break/continue/clear).
//   3. HandleExitKeyword pops the outermost scope frame, clearing the runnable
//      bit so the script is done.
//   4. ShutdownEngine tears every table/context down in the recovered order.
// All cross-module leaves are driven through one installed hooks struct.
#include "sim/script_import4.h"
#include "sim/script_import2.h"   // ScriptEngine()
#include "sim/script_vm.h"
#include <vector>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
struct E2E {
    int compiles = 0, enters = 0, destroys = 0, frees = 0;
    u8* mainFn = nullptr;
};
E2E g_e;

u8 g_fnrec[64];

ScriptImport4Hooks MakeHooks() {
    std::memset(g_fnrec, 0, sizeof g_fnrec);
    g_fnrec[36] = 2;        // main takes 2 args
    g_fnrec[37] = 1;        // dword
    g_fnrec[38] = 1;        // dword
    g_e = E2E();
    g_e.mainFn = g_fnrec;

    ScriptImport4Hooks h{};
    h.compileBlock   = [](u8*) { g_e.compiles++; return 1; };
    h.lookupFunction = [](u8*, const char*) { return g_e.mainFn; };
    h.enterFunction  = [](u8*, u8*) { g_e.enters++; };
    h.destroyCtx     = [](u8*) { g_e.destroys++; };
    h.freeDebug      = [](void*) { g_e.frees++; };
    h.strtok         = [](char* s, int) -> char* {
        // Two-token strtok over a comma-separated "10,20" line.
        static char* cursor;
        if (s) cursor = s;
        if (!cursor || !*cursor) return nullptr;
        char* start = cursor;
        while (*cursor && *cursor != ',') ++cursor;
        if (*cursor == ',') { *cursor = 0; ++cursor; }
        return start;
    };
    h.parseInt = [](const char* s) { return (int)std::strtol(s, nullptr, 10); };
    return h;
}
} // namespace

TEST(ScriptImport4E2E, FullScriptLifecycle) {
    std::vector<u8> ctx(kScriptContextStride, 0);
    u8* C = ctx.data();
    ScriptEngine().currentCtx = C;
    ScriptEngine().ownerId = 0x42;

    ScriptImport4Hooks h = MakeHooks();
    SetScriptImport4Hooks(&h);

    // Step 1: parse + run "10,20" (UTF-8 here; the engine's UTF-16 copy degrades
    // gracefully for the ascii fast path used by the byte-pair loop).
    char line[16]; std::strcpy(line, "10,20");
    u8 r = ParseAndRunCall(C, line, ',');
    CHECK_EQ((int)r, 1);                 // RunWithArgs succeeded
    CHECK_EQ(g_e.compiles, 1);
    CHECK_EQ(g_e.enters, 1);
    CHECK_EQ((int)(C[kScRunFlags] & 1), 1);   // marked runnable
    CHECK_EQ(*reinterpret_cast<i32*>(C + kScOwner), 0x42);  // owner stamped

    // Step 2: the running statement sets and clears the statement-mode flag.
    SetBreakFlag();
    CHECK_EQ(C[kScStmtMode], (u8)1);
    SetContinueFlag();
    CHECK_EQ(C[kScStmtMode], (u8)2);
    ClearReturnFlag();
    CHECK_EQ(C[kScStmtMode], (u8)0);

    // Step 3: exiting the outermost block pops the scope frame and clears the
    // runnable bit (RunWithArgs set the scope base to C+168). The frame's symbol
    // differs from "exit", which is the recovered "pop" trigger.
    static const char* kBlock = "main";
    u8* frame = *reinterpret_cast<u8**>(C + kScScopeBase);
    CHECK(frame == C + 168);
    if (frame) {
        *reinterpret_cast<const char**>(frame) = kBlock;
        *reinterpret_cast<i32*>(C + 2328) = 0;
        ScriptImport4Hooks h2 = h;
        h2.strCmp = [](const char* a, const char* b) { return std::strcmp(a, b); };
        SetScriptImport4Hooks(&h2);
        i32 exitR = HandleExitKeyword(C);
        CHECK_EQ(exitR, 0);                       // outermost frame popped
        CHECK_EQ((int)(C[kScRunFlags] & 1), 0);   // no longer runnable -> done
        SetScriptImport4Hooks(&h);
    }

    // Step 4: shut the engine down — free order is event/cmd/b0/b4/ring/ctx.
    ResetScriptTables();
    std::vector<u8> ctxTable(kScriptContextStride * kScriptContextCount, 0);
    std::vector<u8> cmdTable(kCommandStride * kCommandCapacity, 9);
    ctxTable[0] = 1;     // one in-use context
    int b0, b4, ev, ring;
    ScriptEngineTables& t = ScriptTables();
    t.ctxTable = ctxTable.data();
    t.cmdTable = cmdTable.data();
    t.scratchB0 = &b0; t.scratchB4 = &b4; t.eventToks = &ev; t.logRing = &ring;
    ShutdownEngine();
    CHECK_EQ(g_e.destroys, 1);
    CHECK_EQ(g_e.frees, 6);
    CHECK(t.ctxTable == nullptr);
    CHECK_EQ(cmdTable[0], (u8)0);

    SetScriptImport4Hooks(nullptr);
    ResetScriptTables();
    ScriptEngine().currentCtx = nullptr;
}
