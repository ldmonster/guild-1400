#include "test.h"
// Unit tests for script_import4: the .esc engine's runner / control-flow /
// teardown bodies.  Each function is checked against a hand-computed golden
// vector or a small <cstring> oracle.  Cross-module leaves are driven through
// installable ScriptImport4Hooks (inert by default); the engine state is poked
// directly via ScriptEngine() / ScriptTables().
#include "sim/script_import4.h"
#include "sim/script_import2.h"   // ScriptEngine()
#include "sim/script_vm.h"
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A single 2584-byte context buffer (matches the real record stride so the
// +2472 scope base / +168 frames / +2564 stmt-mode writes land in bounds).
struct Ctx {
    u8 buf[kScriptContextStride] = {};
    u8* base() { return buf; }
};

// Recorders for hook calls.
struct Spy {
    int reportErrors = 0;
    int finishes = 0;
    int destroys = 0;
    std::vector<void*> freed;
    int frameLoopCalls = 0;
    int frameLoopReturn = 0;   // value RunFrameLoop returns each call
    int declareCalls = 0;
    int declareInit = 0;
    std::string declareName;
    int compileReturn = 1;
    u8* lookupReturn = nullptr;
    int enterCalls = 0;
    int runMainCalls = 0;
};
Spy g_spy;

void Reset() { g_spy = Spy(); }

ScriptImport4Hooks MakeHooks() {
    ScriptImport4Hooks h{};
    h.strCmp = [](const char* a, const char* b) { return std::strcmp(a, b); };
    h.reportError = [](u8*, u32, const char*) { g_spy.reportErrors++; };
    h.finishCtx = [](u8*) { g_spy.finishes++; };
    h.destroyCtx = [](u8*) { g_spy.destroys++; };
    h.freeDebug = [](void* p) { g_spy.freed.push_back(p); };
    h.runFrameLoop = [](int, int) { g_spy.frameLoopCalls++; return g_spy.frameLoopReturn; };
    h.compileBlock = [](u8*) { return g_spy.compileReturn; };
    h.lookupFunction = [](u8*, const char*) { return g_spy.lookupReturn; };
    h.enterFunction = [](u8*, u8*) { g_spy.enterCalls++; };
    h.runMain = [](u8*) { g_spy.runMainCalls++; };
    h.declareLocal = [](u8*, u8, const char* name, int init) {
        g_spy.declareCalls++; g_spy.declareName = name ? name : "";
        g_spy.declareInit = init; return 77;
    };
    return h;
}

} // namespace

// ---- flag setters ---------------------------------------------------------
TEST(ScriptImport4, FlagSetters) {
    Ctx c;
    ScriptEngine().currentCtx = c.base();
    CHECK(SetBreakFlag() == c.base());
    CHECK_EQ(c.buf[kScStmtMode], (u8)1);
    CHECK(SetContinueFlag() == c.base());
    CHECK_EQ(c.buf[kScStmtMode], (u8)2);
    CHECK(ClearReturnFlag() == c.base());
    CHECK_EQ(c.buf[kScStmtMode], (u8)0);
    ScriptEngine().currentCtx = nullptr;  // null is tolerated (guarded)
    CHECK(SetBreakFlag() == nullptr);
}

// ---- ReadSkipValue --------------------------------------------------------
TEST(ScriptImport4, ReadSkipValue) {
    Reset();
    static int sawStream = 0;
    ScriptImport4Hooks h = MakeHooks();
    h.readDword = [](int stream, u8* out) { sawStream = stream; if (out) *out = 0; return stream == 42 ? 1 : 0; };
    SetScriptImport4Hooks(&h);
    CHECK_EQ(ReadSkipValue(42), 1);
    CHECK_EQ(sawStream, 42);
    CHECK_EQ(ReadSkipValue(7), 0);   // non-42 stream -> reader returns 0
    SetScriptImport4Hooks(nullptr);
    // Inert default reads nothing -> 0.
    CHECK_EQ(ReadSkipValue(1), 0);
}

// ---- RunByHandle / GetRunByHandlePtr --------------------------------------
TEST(ScriptImport4, RunByHandle) {
    Reset();
    static Ctx loaded;
    *reinterpret_cast<i32*>(loaded.buf + 128) = 0x1234;   // result word
    ScriptImport4Hooks h = MakeHooks();
    h.loadFromDir = [](const char*) { return loaded.base(); };
    SetScriptImport4Hooks(&h);
    CHECK_EQ(RunByHandle("foo.esc"), 0x1234);
    CHECK_EQ(g_spy.runMainCalls, 1);
    CHECK_EQ(ScriptTables().runResult, 0x1234);

    // Load failure -> -1.
    h.loadFromDir = [](const char*) -> u8* { return nullptr; };
    SetScriptImport4Hooks(&h);
    CHECK_EQ(RunByHandle("missing.esc"), -1);
    CHECK_EQ(ScriptTables().runResult, -1);
    SetScriptImport4Hooks(nullptr);

    CHECK(GetRunByHandlePtr() == &RunByHandle);
}

// ---- LoadRunAndStoreResult: load a sub-object script, run, store result ----
TEST(ScriptImport4, LoadRunAndStoreResult) {
    Reset();
    // subObj layout: name at +144, an int arg at +384, result ptr written to +132.
    std::vector<u8> subObj(512, 0);
    std::strcpy(reinterpret_cast<char*>(&subObj[144]), "loc.esc");
    *reinterpret_cast<i32*>(&subObj[384]) = 5;

    // The loaded script context: RunWithArgs needs main with argc 1; +380 holds
    // the result the function writes back to subObj+132.
    static std::vector<u8> loaded(512, 0);
    static u8 mainFn[64];
    std::memset(mainFn, 0, sizeof mainFn);
    mainFn[36] = 1; mainFn[37] = 1;     // 1 dword arg
    static int sentinel = 0xABCD;
    *reinterpret_cast<int**>(&loaded[380]) = &sentinel;   // result ptr

    ScriptImport4Hooks h = MakeHooks();
    h.loadFromDir = [](const char*) { return loaded.data(); };
    g_spy.compileReturn = 1;
    g_spy.lookupReturn = mainFn;
    SetScriptImport4Hooks(&h);

    u8* r = LoadRunAndStoreResult(subObj.data());
    SetScriptImport4Hooks(nullptr);
    CHECK(r == reinterpret_cast<u8*>(&sentinel));
    CHECK(*reinterpret_cast<u8**>(&subObj[132]) == reinterpret_cast<u8*>(&sentinel));
    CHECK_EQ(g_spy.enterCalls, 1);   // ran main through RunWithArgs

    // Load failure -> null, nothing stored.
    Reset();
    h.loadFromDir = [](const char*) -> u8* { return nullptr; };
    SetScriptImport4Hooks(&h);
    CHECK(LoadRunAndStoreResult(subObj.data()) == nullptr);
    SetScriptImport4Hooks(nullptr);
}

// ---- FindActiveByHandle: pumps the frame loop while runnable ---------------
TEST(ScriptImport4, FindActiveByHandle) {
    Reset();
    // Build a 1-slot-equivalent context table (128 slots) so FindByHandle scans.
    std::vector<u8> table(kScriptContextStride * kScriptContextCount, 0);
    // mark every slot's handle = -1 (free), then set slot 3's handle.
    for (int i = 0; i < kScriptContextCount; ++i)
        *reinterpret_cast<i32*>(&table[i * kScriptContextStride + kScHandle]) = -1;
    int slot = 3;
    u8* rec = &table[slot * kScriptContextStride];
    *reinterpret_cast<i32*>(rec + kScHandle) = 99;
    rec[kScRunFlags] = 1;   // runnable

    ScriptImport4Hooks h = MakeHooks();
    // After 2 frame pumps, clear the runnable bit so the loop terminates.
    g_spy.frameLoopReturn = 1;
    h.runFrameLoop = [](int, int) {
        g_spy.frameLoopCalls++;
        return 1;
    };
    // We can't reach `rec` from the lambda cleanly; instead clear via a counter.
    static u8* s_rec; s_rec = rec;
    h.runFrameLoop = [](int, int) {
        g_spy.frameLoopCalls++;
        if (g_spy.frameLoopCalls >= 2) s_rec[kScRunFlags] = 0;
        return 1;
    };
    SetScriptImport4Hooks(&h);
    u8* found = FindActiveByHandle(table.data(), 99);
    CHECK(found == rec);
    CHECK_EQ(g_spy.frameLoopCalls, 2);

    // Unknown handle -> null, no pumping.
    Reset();
    SetScriptImport4Hooks(&h);
    CHECK(FindActiveByHandle(table.data(), 12345) == nullptr);
    CHECK_EQ(g_spy.frameLoopCalls, 0);
    SetScriptImport4Hooks(nullptr);
}

// ---- ShutdownEngine: frees every table in the recovered order --------------
TEST(ScriptImport4, ShutdownEngine) {
    Reset();
    ResetScriptTables();
    // Allocate real buffers so the in-use scan / clear loop run in bounds.
    std::vector<u8> ctxTable(kScriptContextStride * kScriptContextCount, 0);
    std::vector<u8> cmdTable(kCommandStride * kCommandCapacity, 7);  // nonzero
    int b0, b4, ev, ring;
    ctxTable[2 * kScriptContextStride + kScInUse] = 1;  // one in-use context
    ctxTable[5 * kScriptContextStride + kScInUse] = 1;  // another
    ScriptEngineTables& t = ScriptTables();
    t.ctxTable = ctxTable.data();
    t.cmdTable = cmdTable.data();
    t.scratchB0 = &b0; t.scratchB4 = &b4; t.eventToks = &ev; t.logRing = &ring;

    ScriptImport4Hooks h = MakeHooks();
    SetScriptImport4Hooks(&h);
    ShutdownEngine();
    SetScriptImport4Hooks(nullptr);

    CHECK_EQ(g_spy.destroys, 2);              // two in-use contexts destroyed
    CHECK_EQ(cmdTable[0], (u8)0);             // command record +0 cleared
    CHECK_EQ(cmdTable[kCommandStride], (u8)0);
    // freed in order: eventToks, cmdTable, scratchB0, scratchB4, logRing, ctxTable.
    CHECK_EQ((int)g_spy.freed.size(), 6);
    if (g_spy.freed.size() == 6) {
        CHECK(g_spy.freed[0] == &ev);
        CHECK(g_spy.freed[1] == cmdTable.data());
        CHECK(g_spy.freed[2] == &b0);
        CHECK(g_spy.freed[3] == &b4);
        CHECK(g_spy.freed[4] == &ring);
        CHECK(g_spy.freed[5] == ctxTable.data());
    }
    CHECK(t.ctxTable == nullptr);
    CHECK(t.cmdTable == nullptr);
    ResetScriptTables();
}

// ---- ProcessStringLiteral: oracle = manual scan ---------------------------
TEST(ScriptImport4, ProcessStringLiteral) {
    Reset();
    ScriptImport4Hooks h = MakeHooks();
    h.strNCopyPad = [](char* dst, const char* src, int n) {
        if (!dst) return;
        for (int i = 0; i < n; ++i) dst[i] = (src && src[i]) ? src[i] : 0;
    };
    SetScriptImport4Hooks(&h);
    Ctx c;

    // hello" ; -> closing quote at index 5, semicolon after. Copies "hello".
    char dst[64] = {};
    const char* src = "hello\";rest";
    const char* ret = ProcessStringLiteral(c.base(), src, dst);
    CHECK(ret != nullptr);
    CHECK_EQ(std::string(dst), std::string("hello"));
    if (ret) CHECK(ret == src + 6);     // past the closing quote
    CHECK_EQ(g_spy.reportErrors, 0);

    // Missing closing quote before ';' -> error, null.
    Reset();
    SetScriptImport4Hooks(&h);
    char dst2[64] = {};
    const char* ret2 = ProcessStringLiteral(c.base(), "noquote;\"late", dst2);
    CHECK(ret2 == nullptr);
    CHECK_EQ(g_spy.reportErrors, 1);
    SetScriptImport4Hooks(nullptr);
}

// ---- ParseDeclaration: type-name (+ optional = init) -> DeclareLocal -------
TEST(ScriptImport4, ParseDeclaration) {
    Reset();
    Ctx c;
    *reinterpret_cast<const char**>(c.buf + kScCursor) = "src";

    // Drive a token sequence via a scripted nextToken hook.
    // Tokens: (1) class=2(variable name) bound? no -> name "abc";
    //         (2) class=1 sub=27 ('='); (3) class=5 literal value=42.
    static int call;
    call = 0;
    ScriptImport4Hooks h = MakeHooks();
    h.nextToken = [](const char*, u8* out) -> u8 {
        std::memset(out, 0, 80);
        switch (call++) {
            case 0: // declared name token: class 2, name "abc" at out+5 (UTF-16 stride)
                out[0] = 2;
                out[8] = 0; // not bound
                // name source for cls!=0 is out+5 (contiguous byte string).
                out[5] = 'a'; out[6] = 'b'; out[7] = 'c'; out[8] = 0;
                break;
            case 1: // '=' : class 1, sub 27
                out[0] = 1; *reinterpret_cast<i32*>(out + 4) = 27; break;
            case 2: // literal 42 : class 5
                out[0] = 5; *reinterpret_cast<i32*>(out + 4) = 42; break;
        }
        return out[0];
    };
    SetScriptImport4Hooks(&h);
    i32 r = ParseDeclaration(c.base());
    SetScriptImport4Hooks(nullptr);
    CHECK_EQ(r, 77);                       // DeclareLocal's return
    CHECK_EQ(g_spy.declareCalls, 1);
    CHECK_EQ(g_spy.declareName, std::string("abc"));
    CHECK_EQ(g_spy.declareInit, 42);
    CHECK_EQ(g_spy.reportErrors, 0);
}

TEST(ScriptImport4, ParseDeclarationDuplicate) {
    Reset();
    Ctx c;
    *reinterpret_cast<const char**>(c.buf + kScCursor) = "src";
    ScriptImport4Hooks h = MakeHooks();
    h.nextToken = [](const char*, u8* out) -> u8 {
        std::memset(out, 0, 80);
        out[0] = 2;     // class 2 (variable)
        out[8] = 1;     // already-bound marker -> duplicate
        return 2;
    };
    SetScriptImport4Hooks(&h);
    CHECK_EQ(ParseDeclaration(c.base()), 0);
    CHECK_EQ(g_spy.reportErrors, 1);
    CHECK_EQ(g_spy.finishes, 1);
    CHECK_EQ(g_spy.declareCalls, 0);
    SetScriptImport4Hooks(nullptr);
}

// ---- HandleExitKeyword ----------------------------------------------------
// Recovered semantics: if the current scope symbol IS "exit" (StrCmp == 0) the
// call is a NO-OP returning 0.  Only when the symbol DIFFERS does it pop a frame.
TEST(ScriptImport4, HandleExitKeywordIsExitNoOp) {
    Reset();
    Ctx c;
    static const char* kExit = "exit";
    static const char* nameSlot = kExit;
    *reinterpret_cast<const char***>(c.buf + kScScopeBase) = &nameSlot;
    ScriptImport4Hooks h = MakeHooks();
    SetScriptImport4Hooks(&h);
    CHECK_EQ(HandleExitKeyword(c.base()), 0);   // "exit" -> no pop, returns 0
    SetScriptImport4Hooks(nullptr);
}

TEST(ScriptImport4, HandleExitKeywordPopOuter) {
    Reset();
    Ctx c;
    // scope symbol differs from "exit" -> pop path.
    static const char* kName = "while";
    static const char* nameSlot;
    nameSlot = kName;
    // scope base points at the frame at ctx+168 (so the pop clears the right one).
    u8* frame = c.buf + 168;
    *reinterpret_cast<u8**>(c.buf + kScScopeBase) = frame;
    // frame's first dword == the name pointer (compared to "exit").
    *reinterpret_cast<const char**>(frame) = nameSlot;
    // *(ctx+2328) == 0 enables the downward scan; the only active frame is at
    // index 0 (its +0 == the nonzero name pointer), so the scan stops at v3==0.
    *reinterpret_cast<i32*>(c.buf + 2328) = 0;
    ScriptImport4Hooks h = MakeHooks();
    SetScriptImport4Hooks(&h);
    i32 r = HandleExitKeyword(c.base());
    SetScriptImport4Hooks(nullptr);
    CHECK_EQ(r, 0);   // outermost frame popped
    // run-flag bit0 cleared.
    CHECK_EQ((int)(c.buf[kScRunFlags] & 1), 0);
    // scope base nulled.
    CHECK(*reinterpret_cast<u8**>(c.buf + kScScopeBase) == nullptr);
}

// ---- RunWithArgs: arg binding + dispatch ----------------------------------
TEST(ScriptImport4, RunWithArgs) {
    Reset();
    Ctx c;
    // function record: argc=2, arg types {1 (dword), 2 (byte)}.
    static u8 fnrec[64] = {};
    fnrec[36] = 2;            // arg count
    fnrec[37] = 1;            // arg0 type = dword
    fnrec[38] = 2;            // arg1 type = byte
    ScriptImport4Hooks h = MakeHooks();
    g_spy.compileReturn = 1;
    g_spy.lookupReturn = fnrec;
    SetScriptImport4Hooks(&h);

    i32 args[2] = {0xDEAD, 0x1FF};   // byte arg truncates to 0xFF
    i32 runRec[2] = {0, 0};
    CHECK_EQ(RunWithArgs(c.base(), 2, args, runRec), 1);
    CHECK_EQ(runRec[0], (i32)0xDEAD);
    CHECK_EQ(runRec[1], (i32)0xFF);
    CHECK_EQ(g_spy.enterCalls, 1);
    CHECK_EQ((int)(c.buf[kScRunFlags] & 1), 1);
    CHECK(*reinterpret_cast<u8**>(c.buf + kScScopeBase) == c.buf + 168);
    SetScriptImport4Hooks(nullptr);
}

TEST(ScriptImport4, RunWithArgsErrors) {
    Reset();
    Ctx c;
    ScriptImport4Hooks h = MakeHooks();

    // compile fail -> returns 0, no lookup.
    g_spy.compileReturn = 0;
    SetScriptImport4Hooks(&h);
    CHECK_EQ(RunWithArgs(c.base(), 0, nullptr, nullptr), 0);

    // no main -> ReportError, returns 0.
    Reset();
    g_spy.compileReturn = 1;
    g_spy.lookupReturn = nullptr;
    SetScriptImport4Hooks(&h);
    CHECK_EQ(RunWithArgs(c.base(), 1, nullptr, nullptr), 0);
    CHECK_EQ(g_spy.reportErrors, 1);

    // arg count mismatch -> ReportError + Finish.
    Reset();
    static u8 fn2[64] = {}; fn2[36] = 3;   // expects 3 args
    g_spy.compileReturn = 1;
    g_spy.lookupReturn = fn2;
    SetScriptImport4Hooks(&h);
    CHECK_EQ(RunWithArgs(c.base(), 1, nullptr, nullptr), 0);
    CHECK_EQ(g_spy.reportErrors, 1);
    CHECK_EQ(g_spy.finishes, 1);

    // null ctx -> 0.
    CHECK_EQ(RunWithArgs(nullptr, 0, nullptr, nullptr), 0);
    SetScriptImport4Hooks(nullptr);
}

// ---- ParseAndRunCall: tokenize + dispatch by count ------------------------
TEST(ScriptImport4, ParseAndRunCall) {
    Reset();
    Ctx c;
    // A strtok that yields a fixed token list (the engine passes UTF-16 fields;
    // our copy loop handles the byte-pair stride, so feed plain ASCII tokens).
    static const char* toks[] = {"12", "hello", "34"};
    static int idx;
    idx = 0;
    static char tokbuf[3][8];
    std::strcpy(tokbuf[0], "12");
    std::strcpy(tokbuf[1], "hello");
    std::strcpy(tokbuf[2], "34");

    ScriptImport4Hooks h = MakeHooks();
    h.strtok = [](char*, int) -> char* {
        if (idx >= 3) return nullptr;
        return tokbuf[idx++];
    };
    h.parseInt = [](const char* s) { return (int)std::strtol(s, nullptr, 10); };
    // RunWithArgs is invoked for count>0; stub via lookup/compile to succeed.
    static u8 fn3[64] = {};
    fn3[36] = 3; fn3[37] = 1; fn3[38] = 1; fn3[39] = 1;  // 3 dword args
    g_spy.compileReturn = 1;
    g_spy.lookupReturn = fn3;
    SetScriptImport4Hooks(&h);

    // argLine "12 hello 34" -> 3 tokens -> RunWithArgs(ctx, 3, ...).
    u8 r = ParseAndRunCall(c.base(), "12 hello 34", ' ');
    SetScriptImport4Hooks(nullptr);
    CHECK_EQ((int)r, 1);                  // RunWithArgs returns 1
    CHECK_EQ(g_spy.enterCalls, 1);
    (void)toks;
}

TEST(ScriptImport4, ParseAndRunCallZeroArgs) {
    Reset();
    Ctx c;
    static int idx2; idx2 = 0;
    ScriptImport4Hooks h = MakeHooks();
    h.strtok = [](char*, int) -> char* { return nullptr; };   // no tokens
    SetScriptImport4Hooks(&h);
    u8 r = ParseAndRunCall(c.base(), "", ' ');
    SetScriptImport4Hooks(nullptr);
    CHECK_EQ((int)r, 0);                   // RunMain path returns 0
    CHECK_EQ(g_spy.runMainCalls, 1);
    (void)idx2;
}
