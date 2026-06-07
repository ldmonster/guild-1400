#include "test.h"
#include "crt/runtime.h"
#include "crt/signal.h"
#include "crt/env.h"
#include "crt/locale.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;

// A full CRT startup -> runtime -> shutdown lifecycle:
//   1. Build the locale ctype tables (startup).
//   2. Run priority-ordered init functions (the _initterm pass).
//   3. Register atexit handlers and mutate the environment during "runtime".
//   4. Deliver a signal.
//   5. Run exit -> atexit handlers fire LIFO.
// Then verify the observed order and env state against an independent reference.

namespace {
std::vector<std::string> g_log; // global trace of the lifecycle order

int initEarly() { g_log.push_back("init:early"); return 0; }
int initLate()  { g_log.push_back("init:late");  return 0; }

void atexitFirst()  { g_log.push_back("atexit:first"); }
void atexitSecond() { g_log.push_back("atexit:second"); }
void atexitThird()  { g_log.push_back("atexit:third"); }

int g_signalDelivered = 0;
void sigHandler(int s) { g_signalDelivered = s; g_log.push_back("signal"); }
} // namespace

TEST(CrtRuntimeE2E, FullStartupToShutdownLifecycle) {
    g_log.clear();
    g_signalDelivered = 0;

    // ---- Startup: locale ctype tables ----
    crt::BuildCTypeTablesC();
    CHECK_EQ((int)crt::Locale().caseFold['A'], 'a'); // sanity: locale built

    // ---- Startup: priority-ordered init functions ----
    crt::PriorityInitTable init;
    init.RegisterInit(8, initLate);   // higher priority -> runs later
    init.RegisterInit(2, initEarly);  // lower priority  -> runs first
    init.RunInitFuncs(0xFF);

    // ---- Runtime: register atexit handlers (LIFO at shutdown) ----
    crt::ExitRegistry reg;
    reg.Atexit(atexitFirst);
    reg.Atexit(atexitSecond);
    reg.Atexit(atexitThird);

    // ---- Runtime: environment mutation ----
    crt::Environment env({"PATH=/bin", "USER=guest"});
    env.PutEnv("USER=admin");   // replace
    env.PutEnv("LANG=en_US");   // append
    env.PutEnv("PATH=");        // remove

    // ---- Runtime: signal delivery ----
    crt::SignalTable sig;
    sig.Signal(crt::kSIGFPE, sigHandler);
    sig.Raise(crt::kSIGFPE);
    CHECK_EQ(g_signalDelivered, crt::kSIGFPE);

    // ---- Shutdown: exit runs atexit handlers LIFO ----
    reg.Exit(0);

    // ---- Verify the full lifecycle order against a reference ----
    std::vector<std::string> expected = {
        "init:early",   // priority 2 first
        "init:late",    // priority 8 second
        "signal",       // raised during runtime
        "atexit:third", // LIFO: last registered first
        "atexit:second",
        "atexit:first",
    };
    CHECK_EQ(g_log.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < g_log.size(); ++i)
        CHECK(g_log[i] == expected[i]);

    // ---- Verify env state against a reference ----
    CHECK(env.GetEnv("USER") != nullptr);
    CHECK(std::strcmp(env.GetEnv("USER"), "admin") == 0);
    CHECK(std::strcmp(env.GetEnv("LANG"), "en_US") == 0);
    CHECK(env.GetEnv("PATH") == nullptr); // removed
    CHECK_EQ(env.Count(), (size_t)2);     // USER + LANG

    // ---- Exit bookkeeping ----
    CHECK_EQ(reg.ExitCode(), 0);
    CHECK(reg.Terminating());
}

// A second flow: the priority-tagged exit table with gating, mirroring the
// DoExit high-band-first drop (RunExitFuncs ceiling/gate semantics).
namespace {
std::vector<int> g_exitOrder;
int exitP3() { g_exitOrder.push_back(3); return 0; }
int exitP6() { g_exitOrder.push_back(6); return 0; }
int exitP9() { g_exitOrder.push_back(9); return 0; }
} // namespace

TEST(CrtRuntimeE2E, PriorityExitTableGatedDrain) {
    g_exitOrder.clear();
    crt::PriorityInitTable t;
    t.RegisterExit(3, exitP3);
    t.RegisterExit(6, exitP6);
    t.RegisterExit(9, exitP9);

    // First pass: drop the high band (gate 7 suppresses priority 9's call).
    t.RunExitFuncs(0, 7);
    // priority 9 was marked done but not called; 6 then 3 called descending.
    CHECK_EQ(g_exitOrder.size(), (size_t)2);
    CHECK_EQ(g_exitOrder[0], 6);
    CHECK_EQ(g_exitOrder[1], 3);

    // A second drain pass calls nothing further (everything is state==2).
    t.RunExitFuncs(0, 0xFF);
    CHECK_EQ(g_exitOrder.size(), (size_t)2);
}
