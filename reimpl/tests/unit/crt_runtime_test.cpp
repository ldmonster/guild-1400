#include "test.h"
#include "crt/runtime.h"
#include "crt/signal.h"
#include "crt/exception.h"
#include "crt/locale.h"
#include "crt/env.h"
#include "crt/ctype.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

// ---------------------------------------------------------------------------
// Init-func priority table: ascending priority order on init.
// ---------------------------------------------------------------------------
namespace {
std::vector<int> g_order;
int initA() { g_order.push_back(1); return 0; }
int initB() { g_order.push_back(2); return 0; }
int initC() { g_order.push_back(3); return 0; }
} // namespace

TEST(CrtRuntime, InitFuncsRunByAscendingPriority) {
    g_order.clear();
    crt::PriorityInitTable t;
    // Register out of order; priorities 5, 1, 3.
    t.RegisterInit(5, initC);
    t.RegisterInit(1, initA);
    t.RegisterInit(3, initB);
    t.RunInitFuncs(0xFF); // floor high enough to admit all
    CHECK_EQ(g_order.size(), (size_t)3);
    CHECK_EQ(g_order[0], 1); // priority 1 first
    CHECK_EQ(g_order[1], 2); // priority 3
    CHECK_EQ(g_order[2], 3); // priority 5 last
    // All slots marked done.
    for (const auto& s : t.InitSlots())
        CHECK_EQ((int)s.state, 2);
}

TEST(CrtRuntime, ExitFuncsRunByDescendingPriorityWithGate) {
    g_order.clear();
    crt::PriorityInitTable t;
    t.RegisterExit(1, initA);
    t.RegisterExit(5, initC);
    t.RegisterExit(3, initB);
    // floor 0 admits all; gate >= priority for all.
    t.RunExitFuncs(0, 0xFF);
    CHECK_EQ(g_order.size(), (size_t)3);
    CHECK_EQ(g_order[0], 3); // priority 5 first (descending)
    CHECK_EQ(g_order[1], 2); // priority 3
    CHECK_EQ(g_order[2], 1); // priority 1 last
}

TEST(CrtRuntime, ExitFuncsGateSuppressesHighBand) {
    g_order.clear();
    crt::PriorityInitTable t;
    t.RegisterExit(1, initA);
    t.RegisterExit(9, initC); // priority above the gate
    t.RegisterExit(3, initB);
    // gate=4 suppresses the call for priority 9 but still marks it done.
    t.RunExitFuncs(0, 4);
    // initC (priority 9) did NOT run.
    CHECK_EQ(g_order.size(), (size_t)2);
    CHECK_EQ(g_order[0], 2); // priority 3 (highest admitted by gate)
    CHECK_EQ(g_order[1], 1); // priority 1
}

// ---------------------------------------------------------------------------
// RunInitializerTable: call non-null fns in array order.
// ---------------------------------------------------------------------------
namespace {
std::vector<int> g_seq;
void f10() { g_seq.push_back(10); }
void f20() { g_seq.push_back(20); }
} // namespace

TEST(CrtRuntime, RunInitializerTableInOrderSkipsNull) {
    g_seq.clear();
    crt::CrtFunc tbl[] = {f10, nullptr, f20};
    crt::RunInitializerTable(tbl, tbl + 3);
    CHECK_EQ(g_seq.size(), (size_t)2);
    CHECK_EQ(g_seq[0], 10);
    CHECK_EQ(g_seq[1], 20);
}

// ---------------------------------------------------------------------------
// atexit / exit: LIFO.
// ---------------------------------------------------------------------------
namespace {
std::vector<int> g_atexit;
void ax1() { g_atexit.push_back(1); }
void ax2() { g_atexit.push_back(2); }
void ax3() { g_atexit.push_back(3); }
} // namespace

TEST(CrtRuntime, AtexitRunsLifo) {
    g_atexit.clear();
    crt::ExitRegistry reg;
    reg.Atexit(ax1);
    reg.Atexit(ax2);
    reg.Atexit(ax3);
    reg.Exit(7);
    CHECK_EQ(reg.ExitCode(), 7);
    CHECK(reg.Terminating());
    CHECK_EQ(g_atexit.size(), (size_t)3);
    CHECK_EQ(g_atexit[0], 3); // last registered, first run
    CHECK_EQ(g_atexit[1], 2);
    CHECK_EQ(g_atexit[2], 1);
}

TEST(CrtRuntime, QuickExitSkipsAtexitHandlers) {
    g_atexit.clear();
    crt::ExitRegistry reg;
    reg.Atexit(ax1);
    reg.QuickExit(0);
    CHECK_EQ(g_atexit.size(), (size_t)0); // quick path skips the onexit walk
}

// ---------------------------------------------------------------------------
// Stack probe residual.
// ---------------------------------------------------------------------------
TEST(CrtRuntime, StackProbeResidual) {
    // For size in (0,4096] the loop runs once: result = size - 4096 (<= 0).
    CHECK_EQ(crt::StackProbe(4096), 0);
    CHECK_EQ(crt::StackProbe(1), 1 - 4096);
    // For 4097..8192 it runs twice.
    CHECK_EQ(crt::StackProbe(4097), 4097 - 8192);
    CHECK_EQ(crt::StackProbe(8192), 0);
}

// ---------------------------------------------------------------------------
// Signal: set / get / raise / default.
// ---------------------------------------------------------------------------
namespace {
int g_sigHits = 0;
int g_sigArg = 0;
void onSig(int s) { g_sigHits++; g_sigArg = s; }
} // namespace

TEST(CrtSignal, RegisterReturnsPreviousAndRaiseInvokes) {
    crt::SignalTable tab;
    g_sigHits = 0;
    crt::SignalHandler prev = tab.Signal(crt::kSIGFPE, onSig);
    CHECK(prev == reinterpret_cast<crt::SignalHandler>(crt::kSIG_DFL));
    int r = tab.Raise(crt::kSIGFPE); // sig 8 -> LABEL_5 path
    CHECK_EQ(r, 0);
    CHECK_EQ(g_sigHits, 1);
    CHECK_EQ(g_sigArg, crt::kSIGFPE);
    // After raise, the slot is reset to SIG_RUNNING (one-shot semantics).
    CHECK_EQ((int)tab.GetHandlerSlot(crt::kSIGFPE), crt::kSIG_RUNNING);
}

TEST(CrtSignal, RegisterRejectsOutOfRange) {
    crt::SignalTable tab;
    auto r = tab.Register(99, reinterpret_cast<std::uintptr_t>(&onSig));
    CHECK_EQ((int)r, crt::kSignalError); // returns 3 + sets errno
    CHECK_EQ(crt::Errno(), crt::kEINVAL);
}

TEST(CrtSignal, RaiseUnknownReturnsMinusOne) {
    crt::SignalTable tab;
    CHECK_EQ(tab.Raise(13), -1); // 13 is outside the 1..12 dispatch table
}

TEST(CrtSignal, IgnoredHandlerNotInvoked) {
    crt::SignalTable tab;
    g_sigHits = 0;
    tab.Register(crt::kSIGFPE, crt::kSIG_IGN);
    int r = tab.Raise(crt::kSIGFPE);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_sigHits, 0); // SIG_IGN suppresses the call
}

// ---------------------------------------------------------------------------
// FP exception classification.
// ---------------------------------------------------------------------------
TEST(CrtException, FpClassToType) {
    CHECK_EQ(crt::FpClassToType(0x20), 5); // underflow / NZ
    CHECK_EQ(crt::FpClassToType(0x08), 1); // SNAN -> domain
    CHECK_EQ(crt::FpClassToType(0x04), 2); // singularity
    CHECK_EQ(crt::FpClassToType(0x01), 3); // infinity -> overflow
    CHECK_EQ(crt::FpClassToType(0x02), 4); // residual 2*(class&2)
    CHECK_EQ(crt::FpClassToType(0x00), 0);
    // Priority: 0x20 wins over lower bits.
    CHECK_EQ(crt::FpClassToType(0x2D), 5);
}

TEST(CrtException, FpSetErrno) {
    crt::MathErrno() = 0;
    CHECK_EQ(crt::FpSetErrno(1), 1);
    CHECK_EQ(crt::MathErrno(), crt::kEDOM);
    crt::MathErrno() = 0;
    CHECK_EQ(crt::FpSetErrno(3), 3);
    CHECK_EQ(crt::MathErrno(), crt::kERANGE_FP);
    crt::MathErrno() = 0;
    CHECK_EQ(crt::FpSetErrno(5), 5); // 5 is outside (1,3], errno untouched
    CHECK_EQ(crt::MathErrno(), 0);
}

// ---------------------------------------------------------------------------
// SEH scope walk: nested-handler unwind runs __finally bodies.
// ---------------------------------------------------------------------------
namespace {
std::vector<int> g_finally;
void finally0() { g_finally.push_back(0); }
void finally1() { g_finally.push_back(1); }
} // namespace

TEST(CrtException, UnwindRunsFinallyBodies) {
    g_finally.clear();
    // Two nested __finally scopes: level 0 (outer), level 1 (inner). filter==0
    // marks a __finally. enclosingLevel chains 1 -> 0 -> -1.
    crt::ScopeTableEntry scope[2];
    scope[0] = {-1, nullptr, finally0};
    scope[1] = {0, nullptr, finally1};
    crt::ExceptionFrame frame{};
    frame.scopeTable = scope;
    frame.tryLevel = 1; // currently inside the inner try
    crt::EhUnwindNestedHandlers(&frame, -1); // full unwind
    // Inner finally (level 1) runs before the outer (level 0).
    CHECK_EQ(g_finally.size(), (size_t)2);
    CHECK_EQ(g_finally[0], 1);
    CHECK_EQ(g_finally[1], 0);
    CHECK_EQ(frame.tryLevel, -1); // fully unwound
}

TEST(CrtException, UnwindToTargetStopsEarly) {
    g_finally.clear();
    crt::ScopeTableEntry scope[2];
    scope[0] = {-1, nullptr, finally0};
    scope[1] = {0, nullptr, finally1};
    crt::ExceptionFrame frame{};
    frame.scopeTable = scope;
    frame.tryLevel = 1;
    crt::EhUnwindNestedHandlers(&frame, 0); // stop at level 0
    CHECK_EQ(g_finally.size(), (size_t)1);
    CHECK_EQ(g_finally[0], 1); // only the inner finally
    CHECK_EQ(frame.tryLevel, 0);
}

// ---------------------------------------------------------------------------
// Locale ctype setup.
// ---------------------------------------------------------------------------
TEST(CrtLocale, BuildCTypeTablesC) {
    crt::BuildCTypeTablesC();
    const crt::LocaleCType& L = crt::Locale();
    for (int c = 0; c < 256; ++c) {
        if (c >= 'A' && c <= 'Z') {
            CHECK((L.classBits[c] & crt::kLocaleUpper) != 0);
            CHECK_EQ((int)L.caseFold[c], c + 32);
        } else if (c >= 'a' && c <= 'z') {
            CHECK((L.classBits[c] & crt::kLocaleLower) != 0);
            CHECK_EQ((int)L.caseFold[c], c - 32);
        } else {
            CHECK_EQ((int)L.caseFold[c], 0);
        }
    }
}

TEST(CrtLocale, IsCTypeUsesAnsiAndLocaleMasks) {
    // localeMask 0x10 (upper) matches A-Z via the locale table.
    CHECK_EQ(crt::LocaleIsCType('A', 0, crt::kLocaleUpper), 1);
    CHECK_EQ(crt::LocaleIsCType('a', 0, crt::kLocaleUpper), 0);
    // ansiMask 0x04 (_DIGIT in kPctype) matches digits via the ANSI table.
    CHECK_EQ(crt::LocaleIsCType('5', 0x04, 0), 1);
    CHECK_EQ(crt::LocaleIsCType('x', 0x04, 0), 0);
    // IsSpace uses ansiMask 0 + locale bit 4 -> always 0 in the C locale.
    CHECK_EQ(crt::LocaleIsSpace(' '), 0);
}

// ---------------------------------------------------------------------------
// Env getenv / putenv roundtrip.
// ---------------------------------------------------------------------------
TEST(CrtEnv, GetEnvFindsValue) {
    crt::Environment e({"PATH=/usr/bin", "HOME=/root", "TERM=xterm"});
    const char* v = e.GetEnv("HOME");
    CHECK(v != nullptr);
    CHECK(std::strcmp(v, "/root") == 0);
    CHECK(e.GetEnv("MISSING") == nullptr);
    // Prefix must not match: "HOM" is not "HOME".
    CHECK(e.GetEnv("HOM") == nullptr);
}

TEST(CrtEnv, PutEnvReplaceRemoveAppend) {
    crt::Environment e({"PATH=/usr/bin", "HOME=/root"});
    // Replace.
    CHECK_EQ(e.PutEnv("HOME=/home/user"), 0);
    CHECK(std::strcmp(e.GetEnv("HOME"), "/home/user") == 0);
    CHECK_EQ(e.Count(), (size_t)2);
    // Append.
    CHECK_EQ(e.PutEnv("LANG=C"), 0);
    CHECK_EQ(e.Count(), (size_t)3);
    CHECK(std::strcmp(e.GetEnv("LANG"), "C") == 0);
    // Remove (empty value).
    CHECK_EQ(e.PutEnv("PATH="), 0);
    CHECK(e.GetEnv("PATH") == nullptr);
    CHECK_EQ(e.Count(), (size_t)2);
    // Malformed (no key) rejected.
    CHECK_EQ(e.PutEnv("=novalue"), -1);
}

TEST(CrtEnv, FindIndexReturnsSignedSlot) {
    crt::Environment e({"A=1", "B=2", "C=3"});
    CHECK_EQ(e.FindIndex("B", 1), 1);
    CHECK(e.FindIndex("Z", 1) < 0); // not found -> negative
}
