// Golden-vector tests for the long-tail leaf reconstructions:
//   * src/config/errorlog.cpp  — VIBE_ErrorLog_Init (0x438a98)
//   * src/sim/lasttail_recon.* — thunks/stubs + CmdLine_SkipFirstArg
#include "tests/framework/test.h"

#include "config/errorlog.h"
#include "sim/lasttail_recon.h"

#include <cstring>
#include <string>

using namespace guild::config;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// ErrorLog_Init — header format, rotation decision, 12-record loop.
// ---------------------------------------------------------------------------

namespace {
// File-size hook: every probed path reports a fixed size, recording calls.
struct SizeProbe {
    int                       size = -1; // -1 => open fails
    std::vector<std::string>  probed;
};
SizeProbe* g_probe = nullptr;
int ProbeSize(const std::string& path, void* /*user*/) {
    g_probe->probed.push_back(path);
    return g_probe->size;
}
} // namespace

TEST(LastTailErrorLog, Header_And_TitleBit) {
    ErrorLogInitHooks h;
    h.windowTitle = "Die Gilde";
    h.exeDir = "C:\\GAMES\\GILDE";
    h.ctimeDate = "Mon Jun  9 12:00:00 2026\n";

    std::string header = ErrorLog_Init(0x10, h);

    CHECK(h.titleBit10 == true);                 // flags & 0x10
    CHECK(h.installedExceptionFilter == true);
    CHECK_EQ(header,
        std::string("* ------------------------------------------------------------ *\n"
                    "[Die Gilde], Date: Mon Jun  9 12:00:00 2026\n"));
    CHECK_EQ(h.header, header);
    // No file flag, no console flag -> no rotation, no console.
    CHECK_EQ((int)h.rotation.size(), 0);
    CHECK(h.allocatedConsole == false);
}

TEST(LastTailErrorLog, TitleBit_Clear) {
    ErrorLogInitHooks h;
    ErrorLog_Init(0x00, h);
    CHECK(h.titleBit10 == false);
    ErrorLogInitHooks h2;
    ErrorLog_Init(0x08, h2);                       // 0x08 has no 0x10 bit
    CHECK(h2.titleBit10 == false);
    CHECK(h2.allocatedConsole == true);            // flags & 8 -> console
}

TEST(LastTailErrorLog, Record_Loop_Is_12) {
    ErrorLogInitHooks h;
    std::string header = ErrorLog_Init(0x04, h);   // any flags -> still 12 records

    CHECK_EQ((int)h.records.size(), 12);           // the 0x30/4 == 12 loop
    static const guild::u32 kExpect[12] = {
        0x001,0x002,0x004,0x008,0x010,0x020,
        0x040,0x080,0x100,0x200,0x400,0x800,
    };
    for (int i = 0; i < 12; ++i) {
        CHECK_EQ(h.records[i].channel, kExpect[i]);    // dword_62D82C[i]
        CHECK_EQ(h.records[i].flags, (guild::u8)0x04); // dl = flags
        CHECK_EQ(h.records[i].text, header);           // eax = header buffer
    }
}

TEST(LastTailErrorLog, Rotation_Loop_12_Names_NoRotate) {
    SizeProbe probe;
    probe.size = 0x100;                            // small file -> no rotate
    g_probe = &probe;

    ErrorLogInitHooks h;
    h.exeDir = "D:\\G";
    h.fileSize = &ProbeSize;
    ErrorLog_Init(0x01, h);                        // flags & 1 -> file loop

    CHECK_EQ((int)h.rotation.size(), 12);          // 0x180/0x20 == 12 names
    CHECK_EQ((int)probe.probed.size(), 12);
    CHECK_EQ(h.rotation[0].path, std::string("D:\\G\\_error.log"));
    CHECK_EQ(h.rotation[10].path, std::string("D:\\G\\___tracker.log"));
    CHECK_EQ(h.rotation[11].path, std::string("D:\\G\\_meister2.log"));
    for (int i = 0; i < 12; ++i) {
        CHECK(h.rotation[i].opened == true);
        CHECK(h.rotation[i].rotated == false);     // size <= 0x10000
    }
    g_probe = nullptr;
}

TEST(LastTailErrorLog, Rotation_Triggers_Above_64K) {
    SizeProbe probe;
    probe.size = 0x10001;                          // > 0x10000 -> rotate
    g_probe = &probe;

    ErrorLogInitHooks h;
    h.fileSize = &ProbeSize;
    ErrorLog_Init(0x01, h);

    for (int i = 0; i < 12; ++i) {
        CHECK(h.rotation[i].opened == true);
        CHECK(h.rotation[i].rotated == true);      // truncate + "deleted" message
    }
    g_probe = nullptr;
}

TEST(LastTailErrorLog, Rotation_Boundary_Exactly_64K_NoRotate) {
    SizeProbe probe;
    probe.size = 0x10000;                          // == limit -> jle, no rotate
    g_probe = &probe;
    ErrorLogInitHooks h;
    h.fileSize = &ProbeSize;
    ErrorLog_Init(0x01, h);
    for (int i = 0; i < 12; ++i)
        CHECK(h.rotation[i].rotated == false);
    g_probe = nullptr;
}

TEST(LastTailErrorLog, Rotation_OpenFails_NoStep) {
    SizeProbe probe;
    probe.size = -1;                               // open fails (v7 == 0)
    g_probe = &probe;
    ErrorLogInitHooks h;
    h.fileSize = &ProbeSize;
    ErrorLog_Init(0x01, h);
    CHECK_EQ((int)h.rotation.size(), 12);
    for (int i = 0; i < 12; ++i) {
        CHECK(h.rotation[i].opened == false);
        CHECK(h.rotation[i].rotated == false);
    }
    g_probe = nullptr;
}

// ---------------------------------------------------------------------------
// Forwarder thunks.
// ---------------------------------------------------------------------------

TEST(LastTailThunks, StrCmpNoCase_Thunk) {
    CHECK_EQ(Util_StrCmpNoCase_Thunk("ABC", "abc"), 0);   // case-insensitive eq
    CHECK(Util_StrCmpNoCase_Thunk("abc", "abd") != 0);
}

TEST(LastTailThunks, StrCmp_Thunk_Deref_And_Swap) {
    // 0x44ec98: return StrCmp(*a2, a1).  Equal strings -> 0.
    const char* b = "hello";
    const char* const* a2 = &b;
    CHECK_EQ(Util_StrCmpThunk("hello", a2), 0);
    // ReconStrCmp normalises to -1/+1. *a2 ("aaa") vs a1 ("aab"): 'a'<'b' -> -1.
    const char* lo = "aaa";
    const char* const* a2lo = &lo;
    CHECK_EQ(Util_StrCmpThunk("aab", a2lo), -1);
}

TEST(LastTailThunks, StrCmpNoCase_Thunk_Deref) {
    const char* b = "Foo";
    const char* const* a2 = &b;
    CHECK_EQ(Util_StrCmpNoCaseThunk("FOO", a2), 0);

    const char* a = "Bar";
    const char* bb = "bar";
    const char* const* pa = &a;
    const char* const* pb = &bb;
    CHECK_EQ(Util_StrCmpNoCaseDerefThunk(pa, pb), 0);
}

namespace {
int g_finishHandle = -1;
int g_finishCount = 0;
void RecFinish(int handle, void* /*u*/) { g_finishHandle = handle; ++g_finishCount; }
int  RetFind(int a1, int a2, void* /*u*/) { return a1 * 100 + a2; }
} // namespace

TEST(LastTailThunks, Script_FinishThunk_Derefs_And_Returns0) {
    ScriptThunkHooks hk;
    hk.scriptFinish = &RecFinish;
    g_finishHandle = -1; g_finishCount = 0;
    int handle = 0x1234;
    int rv = Script_FinishThunk(&handle, hk);      // Script_Finish(*a1); return 0
    CHECK_EQ(rv, 0);
    CHECK_EQ(g_finishHandle, 0x1234);
    CHECK_EQ(g_finishCount, 1);
}

TEST(LastTailThunks, Script_FindByNameThunk_Passthrough) {
    ScriptThunkHooks hk;
    hk.scriptFindByName = &RetFind;
    CHECK_EQ(Script_FindByNameThunk(3, 7, hk), 307);
}

// ---------------------------------------------------------------------------
// Trivial stubs.
// ---------------------------------------------------------------------------

TEST(LastTailStubs, NullStubs_Are_NoOps) {
    Util_NullStub();
    Util_NullStub2();
    Util_NullSub();
    CHECK_EQ(Util_RetZero_27cc5(), 0);
    CHECK(true);
}

// ---------------------------------------------------------------------------
// CmdLine_SkipFirstArg (0x1426221).
// ---------------------------------------------------------------------------

TEST(LastTailCmdLine, Unquoted_SkipsProgramAndWhitespace) {
    // "prog.exe   arg1" -> points at "arg1".
    const char* cl = "prog.exe   arg1";
    const char* r = CmdLine_SkipFirstArg(cl);
    CHECK_EQ(std::string(r), std::string("arg1"));
}

TEST(LastTailCmdLine, Unquoted_NoArgs) {
    const char* cl = "prog.exe";
    const char* r = CmdLine_SkipFirstArg(cl);
    CHECK_EQ(*r, '\0');                             // landed on the NUL
}

TEST(LastTailCmdLine, Quoted_Program) {
    // "\"my prog.exe\"  next" -> after closing quote + whitespace -> "next".
    const char* cl = "\"my prog.exe\"  next";
    const char* r = CmdLine_SkipFirstArg(cl);
    CHECK_EQ(std::string(r), std::string("next"));
}

TEST(LastTailCmdLine, Quoted_Unterminated_FallsThrough) {
    // No closing quote: scan stops at NUL, falls into the trailing-ws skip.
    const char* cl = "\"unterminated";
    const char* r = CmdLine_SkipFirstArg(cl);
    CHECK_EQ(*r, '\0');
}

TEST(LastTailCmdLine, LeadingWhitespaceOnlyUnquoted) {
    // Begins with whitespace (not '"', not > 0x20): the unquoted token loop is
    // skipped, then trailing whitespace is consumed to the first real arg.
    const char* cl = "   tail";
    const char* r = CmdLine_SkipFirstArg(cl);
    CHECK_EQ(std::string(r), std::string("tail"));
}
