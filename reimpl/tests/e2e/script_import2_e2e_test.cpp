// E2E flow across the Script-VM command bodies: a tiny "scene" that creates a
// character, issues a blocking WalkToDummy, then a Sleep, and drives the
// yield/resume state machine across several simulated ticks the way the VM
// stepper would (re-invoking the still-executing command each tick).
#include "sim/script_import2.h"
#include "sim/script_vm.h"
#include "test.h"
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// Backing model of a "character": +296 holds the busy/in-motion flag the
// command bodies poll on resume.
struct FakeChar { u8 pad[296]; i32 busy; i32 tail; };

struct World {
    FakeChar hero{};
    int walkIssued = 0;
    int reportCount = 0;
} g_w;

void hReport(u8*, u32, const char*) { g_w.reportCount++; }
ScriptHandle hCreate(const char*, i32*) { return reinterpret_cast<ScriptHandle>(&g_w.hero); }
int  hValid(ScriptHandle) { return 1; }
void hWalk(ScriptHandle, ScriptHandle, ScriptHandle) { g_w.walkIssued++; g_w.hero.busy = 1; }   // start moving

} // namespace

TEST(ScriptImport2E2E, CreateWalkSleepResumeFlow) {
    // --- engine setup -------------------------------------------------------
    std::vector<u8> ctxBuf(kScriptContextStride, 0);
    u8* ctx = ctxBuf.data();
    auto& E = ScriptEngine();
    E.currentCtx = ctx; E.execCmd = nullptr; E.gameTick = 1000; E.ownerId = 5;

    ScriptCmdHooks h{};
    h.reportError = hReport; h.createChar = hCreate; h.isValidPtr = hValid;
    h.queueWalk = hWalk;
    SetScriptCmdHooks(&h);
    g_w = World();

    auto& stmtMode = *(ctx + kScStmtMode);
    auto cmdBlocked = [&]() -> ScriptCmdFn {
        return reinterpret_cast<ScriptCmdFn>(*reinterpret_cast<u8**>(ctx + kScCmdBlocked));
    };
    auto clearBlocked = [&]() { *reinterpret_cast<u8**>(ctx + kScCmdBlocked) = nullptr; };

    // --- step 1: CreateCharacter -------------------------------------------
    const char* model = "guard.mdl"; const char** mp = &model;
    ScriptHandle charHandle = CmdCreateCharacter(mp, nullptr);
    CHECK(charHandle != 0);
    CHECK_EQ(charHandle, reinterpret_cast<ScriptHandle>(&g_w.hero));

    // --- step 2: WalkToDummy in BLOCKING mode (stmtMode==1) -----------------
    ScriptHandle chr = charHandle; ScriptHandle dummy = 0x9000;
    ScriptHandle* cp = &chr; ScriptHandle* dp = &dummy;
    stmtMode = 1;
    clearBlocked();
    CHECK_EQ(CmdWalkToDummy(cp, dp), 1);
    CHECK_EQ(g_w.walkIssued, 1);
    // blocking -> the command armed itself as the wait command.
    CHECK_EQ(cmdBlocked(), kFnCmdWalkToDummy);
    CHECK_EQ(g_w.hero.busy, 1);

    // --- ticks: stepper re-invokes the still-executing walk command --------
    // Simulate the VM treating the armed command as the executing command.
    ScriptCmdRecord walkRec{}; walkRec.fn = kFnCmdWalkToDummy;
    E.execCmd = &walkRec;
    // tick while still moving: re-arm, returns 0.
    clearBlocked();
    CHECK_EQ(CmdWalkToDummy(cp, dp), 0);
    CHECK_EQ(cmdBlocked(), kFnCmdWalkToDummy);   // still blocked
    // movement finishes -> busy cleared; next resume does NOT re-arm.
    g_w.hero.busy = 0;
    clearBlocked();
    CHECK_EQ(CmdWalkToDummy(cp, dp), 0);
    CHECK(cmdBlocked() == nullptr);              // walk complete, unblocked
    E.execCmd = nullptr;

    // --- step 3: Sleep for 140 ticks (140/14 = 10) --------------------------
    i32 dur = 140;
    E.gameTick = 1000;
    clearBlocked();
    CHECK_EQ(CmdSleep(&dur), 1);                 // first entry arms + snapshots
    CHECK_EQ(cmdBlocked(), kFnCmdSleep);
    CHECK_EQ(*reinterpret_cast<i32*>(ctx + kScSleepWake), 1000);

    // resume mid-sleep (tick 1005 < wake 1010): still sleeping.
    ScriptCmdRecord sleepRec{}; sleepRec.fn = kFnCmdSleep;
    E.execCmd = &sleepRec;
    E.gameTick = 1005; clearBlocked();
    CHECK_EQ(CmdSleep(&dur), 0);
    CHECK_EQ(cmdBlocked(), kFnCmdSleep);
    // resume after wake (tick 1011 > 1010): wakes, no re-arm.
    E.gameTick = 1011; clearBlocked();
    CHECK_EQ(CmdSleep(&dur), 0);
    CHECK(cmdBlocked() == nullptr);
    E.execCmd = nullptr;

    CHECK_EQ(g_w.reportCount, 0);                // happy path, no errors

    SetScriptCmdHooks(nullptr);
}

TEST(ScriptImport2E2E, InertDefaultsNoCrash) {
    // With no hooks installed, the inert defaults must keep bodies link/run-safe.
    std::vector<u8> ctxBuf(kScriptContextStride, 0);
    auto& E = ScriptEngine();
    E.currentCtx = ctxBuf.data(); E.execCmd = nullptr; E.gameTick = 0;
    SetScriptCmdHooks(nullptr);
    const char* model = "x"; const char** mp = &model;
    CHECK_EQ(CmdCreateCharacter(mp, nullptr), (ScriptHandle)0);  // default createChar fails
    ScriptHandle chr = 0; ScriptHandle dummy = 0; ScriptHandle* cp = &chr; ScriptHandle* dp = &dummy;
    CHECK_EQ(CmdWalkToDummy(cp, dp), 0);            // null operands -> report path
}
