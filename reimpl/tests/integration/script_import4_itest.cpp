#include "test.h"
// Integration: the script_import4 runner/control-flow bodies wired against their
// REAL reconstructed siblings (NOT mocks):
//
//   * FindActiveByHandle / RunWaitLoop CALL the real VIBE_Script_FindByHandle
//     (script_import.cpp, gilde.exe 0x442174) directly — the live wiring. We
//     build the genuine 128-slot * 2584-byte context table the original scans
//     and assert the cross-module flow (table scan -> runnable check -> frame
//     pump) agrees end to end.
//   * HandleExitKeyword's name compare routes through the strCmp hook; here we
//     wire it to the REAL guild::util::StrCmpNoCase (string_ops.cpp, gilde.exe
//     0x5cb8f0) so the "exit" detection runs through the genuine string sibling.
#include "sim/script_import4.h"
#include "sim/script_import.h"   // real FindByHandle (0x442174)
#include "sim/script_vm.h"
#include "util/string_ops.h"     // real StrCmpNoCase (0x5cb8f0)
#include <vector>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
std::vector<u8> MakeTable() {
    std::vector<u8> t(kScriptContextStride * kScriptContextCount, 0);
    for (int i = 0; i < kScriptContextCount; ++i)
        *reinterpret_cast<i32*>(&t[i * kScriptContextStride + kScHandle]) = -1;
    return t;
}
} // namespace

// FindActiveByHandle drives the REAL FindByHandle scan; the frame loop pumps
// while the located slot is runnable, then returns it once idle.
TEST(ScriptImport4Itest, FindActiveByHandleUsesRealFindByHandle) {
    std::vector<u8> table = MakeTable();
    int slot = 17;
    u8* rec = &table[slot * kScriptContextStride];
    *reinterpret_cast<i32*>(rec + kScHandle) = 555;
    rec[kScRunFlags] = 1;   // runnable -> loop will pump

    // Sanity: the real sibling resolves the handle to this exact slot.
    CHECK(FindByHandle(table.data(), 555) == rec);

    static u8* s_rec; s_rec = rec;
    static int s_pumps; s_pumps = 0;
    ScriptImport4Hooks h{};
    h.runFrameLoop = [](int, int) {
        ++s_pumps;
        if (s_pumps >= 3) s_rec[kScRunFlags] = 0;  // becomes idle after 3 pumps
        return 1;
    };
    SetScriptImport4Hooks(&h);
    u8* found = FindActiveByHandle(table.data(), 555);
    SetScriptImport4Hooks(nullptr);

    CHECK(found == rec);            // returns once the real scan finds it idle
    CHECK_EQ(s_pumps, 3);
}

// RunWaitLoop pumps the frame loop, re-resolving the handle through the REAL
// FindByHandle each frame; when the loop ends it Finishes the surviving context.
TEST(ScriptImport4Itest, RunWaitLoopUsesRealFindByHandle) {
    std::vector<u8> table = MakeTable();
    int slot = 4;
    u8* rec = &table[slot * kScriptContextStride];
    *reinterpret_cast<i32*>(rec + kScHandle) = 7;

    static int s_finishes; s_finishes = 0;
    static int s_frames; s_frames = 0;
    static int s_resetMouse; s_resetMouse = 0;
    ScriptImport4Hooks h{};
    // Loop runs twice, then RunFrameLoop returns 0 to end the wait.
    h.runFrameLoop = [](int, int) { return (++s_frames < 3) ? 1 : 0; };
    h.finishCtx = [](u8*) { ++s_finishes; };
    SetScriptImport4Hooks(&h);

    WaitLoopGuards g;
    RunWaitLoop(table.data(), 7, g, []() { ++s_resetMouse; });
    SetScriptImport4Hooks(nullptr);

    CHECK_EQ(s_frames, 3);          // pumped until RunFrameLoop returned 0
    CHECK_EQ(s_finishes, 1);        // surviving context finished
    CHECK_EQ(s_resetMouse, 1);

    // With the no-loop guard set, RunWaitLoop does nothing.
    s_frames = s_finishes = s_resetMouse = 0;
    SetScriptImport4Hooks(&h);
    WaitLoopGuards g2; g2.noLoopGuard = 1;
    RunWaitLoop(table.data(), 7, g2, []() { ++s_resetMouse; });
    SetScriptImport4Hooks(nullptr);
    CHECK_EQ(s_frames, 0);
    CHECK_EQ(s_finishes, 0);
}

// HandleExitKeyword's "exit" detection wired to the REAL util::StrCmpNoCase.
TEST(ScriptImport4Itest, HandleExitKeywordRealStrCmp) {
    ScriptImport4Hooks h{};
    h.strCmp = [](const char* a, const char* b) {
        return guild::util::StrCmpNoCase(a, b);   // REAL sibling
    };
    SetScriptImport4Hooks(&h);

    // "EXIT" — the REAL StrCmpNoCase folds case so it equals "exit" -> the
    // recovered no-op path (returns 0, no frame pop).
    std::vector<u8> ctx(kScriptContextStride, 0);
    static const char* nameE = "EXIT";
    static const char* slotE = nameE;
    *reinterpret_cast<const char***>(&ctx[kScScopeBase]) = &slotE;
    CHECK_EQ(HandleExitKeyword(ctx.data()), 0);

    // A non-"exit" scope symbol -> the real compare is nonzero -> the pop path
    // runs (outermost frame -> returns 0, runnable bit cleared).
    std::vector<u8> ctx2(kScriptContextStride, 0);
    ctx2[kScRunFlags] = 1;
    u8* frame = ctx2.data() + 168;
    static const char* nameW = "while";
    *reinterpret_cast<const char**>(frame) = nameW;
    *reinterpret_cast<u8**>(&ctx2[kScScopeBase]) = frame;
    *reinterpret_cast<i32*>(&ctx2[2328]) = 0;
    i32 r = HandleExitKeyword(ctx2.data());
    CHECK_EQ(r, 0);   // outermost frame popped via the real compare
    CHECK_EQ((int)(ctx2[kScRunFlags] & 1), 0);
    SetScriptImport4Hooks(nullptr);
}
