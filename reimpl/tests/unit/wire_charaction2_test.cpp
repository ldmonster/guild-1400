// Verifies InstallRealCharAction2Wiring() binds the real RNG sibling
// (util::RandomModulo, VIBE_Math_RandomModulo @0x58b89c) into the `randomModulo`
// field of all four CharAction step bridges (CharActionStep5..8Hooks), which were
// fully INERT at runtime before (no live-tree installer). The remaining
// engine-handler-record leaf fields stay at their faithful inert defaults.
#include "tests/framework/test.h"

#include "sim/wire_charaction2.h"
#include "sim/charaction_steps5.h"
#include "sim/charaction_steps6.h"
#include "sim/charaction_steps7.h"
#include "sim/charaction_steps8.h"
#include "sim/npcaction.h"     // NpcLeafHooks / SetNpcLeafHooks (freeHandlerEntry leaf)
#include "util/math_random.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// After install, the randomModulo field on every one of the four bridges is bound
// to a NON-NULL real adapter (the inert default pins the draw to a constant 0).
TEST(WireCharAction2, BindsRandomModuloIntoAllFourBridges) {
    // Baseline: the bare inert default pins randomModulo to the constant-0 leaf, so
    // even a nonzero count draws 0.
    SetCharActionStep5Hooks(nullptr);
    SetCharActionStep6Hooks(nullptr);
    SetCharActionStep7Hooks(nullptr);
    SetCharActionStep8Hooks(nullptr);
    CHECK(GetCharActionStep5Hooks().randomModulo != nullptr); // inert leaf, but valid
    CHECK(GetCharActionStep5Hooks().randomModulo(100) == 0);  // inert == constant 0

    InstallRealCharAction2Wiring();

    // Every bridge's randomModulo is now bound and identical (the one real adapter).
    auto r5 = GetCharActionStep5Hooks().randomModulo;
    auto r6 = GetCharActionStep6Hooks().randomModulo;
    auto r7 = GetCharActionStep7Hooks().randomModulo;
    auto r8 = GetCharActionStep8Hooks().randomModulo;
    CHECK(r5 != nullptr);
    CHECK(r6 != nullptr);
    CHECK(r7 != nullptr);
    CHECK(r8 != nullptr);
    CHECK(r5 == r6);
    CHECK(r6 == r7);
    CHECK(r7 == r8);

    // The real adapter mirrors VIBE_Math_RandomModulo exactly: a fresh-seeded draw
    // through the bound hook equals a fresh-seeded direct call. n==0 -> 0; the draw
    // is always in [0, n).
    crt::Srand(0x1357u);
    int viaHook = r5(100);
    crt::Srand(0x1357u);
    int direct = util::RandomModulo(100);
    CHECK_EQ(viaHook, direct);
    CHECK(viaHook >= 0 && viaHook < 100);
    CHECK(r5(0) == 0);   // count 0 -> 0 (matches the original)

    // The non-RNG fields kept their faithful inert defaults (every resolve absent).
    CHECK(GetCharActionStep5Hooks().findPersonById(123) == nullptr);
    CHECK(GetCharActionStep6Hooks().findPersonById(123) == nullptr);
    CHECK(GetCharActionStep7Hooks().findObjectById(123) == nullptr);
    CHECK(GetCharActionStep8Hooks().personFindRecordById(123) == nullptr);

    SetCharActionStep5Hooks(nullptr);
    SetCharActionStep6Hooks(nullptr);
    SetCharActionStep7Hooks(nullptr);
    SetCharActionStep8Hooks(nullptr);
}

// A representative step coroutine (RestorePosFinishAlt, steps5 @0x4d1674) executes
// over a zeroed He record with the real RNG wired and the freeHandlerEntry leaf
// supplied: the real RandomModulo(2) draw drives the 50/50 pose-free branch. With
// the wiring in place the branch is data-driven (not the inert constant-0 stuck
// path), and the call runs to a defined result without crashing.
TEST(WireCharAction2, WiredRandomModuloDrivesRealStepBranch) {
    InstallRealCharAction2Wiring();

    // Supply the shared NpcLeafHooks freeHandlerEntry leaf so the free branch has a
    // defined sink (records the call; returns a sentinel result code).
    static int s_freed = 0;
    static NpcLeafHooks leaf{};
    leaf.freeHandlerEntry = [](HeRecord*) -> int { ++s_freed; return 7; };
    SetNpcLeafHooks(&leaf);

    u8 buf[512];

    // Sweep enough seeds that the real RandomModulo(2) draw lands on BOTH the
    // free (odd) and no-free (even) branches — proving the wired draw is live, not
    // the inert constant 0 (which would never free).
    int frees = 0, noFrees = 0;
    for (u32 seed = 1; seed <= 32; ++seed) {
        crt::Srand(seed);
        std::memset(buf, 0, sizeof(buf));
        HeRecord* h = reinterpret_cast<HeRecord*>(buf);
        i32 r = RestorePosFinishAlt(h);
        // Defined outcome: either the no-free draw (0) or the free-leaf result (7).
        CHECK(r == 0 || r == 7);
        if (r == 7) ++frees; else ++noFrees;
    }
    CHECK(frees > 0);     // the real draw reached the free branch (inert never would)
    CHECK(noFrees > 0);   // ...and the no-free branch
    CHECK(s_freed == frees);

    SetNpcLeafHooks(nullptr);
    SetCharActionStep5Hooks(nullptr);
    SetCharActionStep6Hooks(nullptr);
    SetCharActionStep7Hooks(nullptr);
    SetCharActionStep8Hooks(nullptr);
}
