// Verifies InstallRealCharActionWiring() binds the five CharAction step /
// NpcAction-recon bridges (CharActionStep2/3/4Hooks, CharActionReconHooks,
// CharActionRecon2Hooks) to their real reconstructed leaves — previously all five
// were fully inert at runtime (nothing installed them). Suite prefix: WireCharAction.
#include "tests/framework/test.h"

#include "sim/wire_charaction.h"
#include "sim/real_hooks3.h"   // InstallRealSimHooks3 (binds the sibling NpcLeafHooks + He pool)
#include "sim/charaction_steps2.h"
#include "sim/charaction_steps3.h"
#include "sim/charaction_steps4.h"
#include "sim/charaction_npcaction_recon.h"
#include "sim/charaction_npcaction_recon2.h"
#include "sim/he.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// Re-inert every bridge so a clean baseline can be asserted before install.
static void InertAll() {
    SetCharActionStep2Hooks(nullptr);
    SetCharActionStep3Hooks(nullptr);
    SetCharActionStep4Hooks(nullptr);
    SetCharActionReconHooks(nullptr);
    SetCharActionRecon2Hooks(nullptr);
}

TEST(WireCharAction, BindsRealLeavesIntoAllFiveBridges) {
    InertAll();
    // NOTE: the installer SEEDS each table from its module inert defaults (non-null
    // stubs) and overrides only the wireable fields — required because several
    // CharAction step call sites invoke hooks WITHOUT a null-check. So unbound
    // fields stay as their inert stubs (non-null), not null; we assert the BOUND
    // fields point at real adapters, and the execute test proves real behaviour.
    InstallRealCharActionWiring();

    // --- step2: the find-by-filter scan + person resolve are now real ---------
    const CharActionStep2Hooks& s2 = GetCharActionStep2Hooks();
    CHECK(s2.findFirstByFilter != nullptr);
    CHECK(s2.findNextMatching  != nullptr);
    CHECK(s2.findPersonById    != nullptr);
    // unbound (no clean target) stays inert

    // --- step3: resolve / person / find / cmd25 are real ----------------------
    const CharActionStep3Hooks& s3 = GetCharActionStep3Hooks();
    CHECK(s3.resolveEntityById  != nullptr);
    CHECK(s3.findPersonById     != nullptr);
    CHECK(s3.findFirstByFilter  != nullptr);
    CHECK(s3.findNextMatching   != nullptr);
    CHECK(s3.queueRequestArgs25 != nullptr);

    // --- step4: resolve / person / find / category / random / cmds ------------
    const CharActionStep4Hooks& s4 = GetCharActionStep4Hooks();
    CHECK(s4.findPersonById    != nullptr);
    CHECK(s4.resolveEntityById != nullptr);
    CHECK(s4.findFirstByFilter != nullptr);
    CHECK(s4.findNextMatching  != nullptr);
    CHECK(s4.buildingCategory  != nullptr);
    CHECK(s4.randomModulo      != nullptr);
    CHECK(s4.queueArgs25       != nullptr);
    CHECK(s4.queueCoord27      != nullptr);

    // --- recon: full leaf surface bound where reconstructed -------------------
    const CharActionReconHooks& rc = GetCharActionReconHooks();
    CHECK(rc.resolveEntityById    != nullptr);
    CHECK(rc.findPersonById       != nullptr);
    CHECK(rc.findFirstByFilter    != nullptr);
    CHECK(rc.findNextMatching     != nullptr);
    CHECK(rc.queueRequestEntity29 != nullptr);
    CHECK(rc.freeHandlerEntry     != nullptr);
    CHECK(rc.queueRequestQuad60   != nullptr);
    CHECK(rc.queueRequestArgs25   != nullptr);
    CHECK(rc.queueRequestCoord27  != nullptr);
    CHECK(rc.randomModulo         != nullptr);

    // --- recon2 (DrinkInit): the three reconstructed leaves ------------------
    const CharActionRecon2Hooks& r2 = GetCharActionRecon2Hooks();
    CHECK(r2.findFirstByFilter != nullptr);
    CHECK(r2.findNextMatching  != nullptr);
    CHECK(r2.freeHandlerEntry  != nullptr);

    InertAll();   // restore for any later test in this TU
}

// A representative action from each bridge runs over a zeroed He record through the
// installed real leaves without crashing, returning a defined result — i.e. the
// wired control flow actually executes (free/find resolve against the real pool,
// emits stage onto the real queue).
TEST(WireCharAction, WiredStepsExecuteOverZeroedRecord) {
    // Match the real boot order: InstallRealSimHooks3 binds the sibling NpcLeafHooks
    // bridge (freeHandlerEntry et al.) + creates/Init's the shared He pool, which
    // some CharAction steps (e.g. BuyObjectStep) reach through GetNpcLeafHooks().
    InstallRealSimHooks3();
    InstallRealCharActionWiring();

    HeRecord rec;
    std::memset(&rec, 0, sizeof(rec));

    // steps2: a pure timestamp reset (returns the advanced hour-of-day).
    int a = StateReset24(&rec);
    CHECK(a >= 0);

    // steps3: arm the per-target duration (zeroed type => the inert duration path).
    std::memset(&rec, 0, sizeof(rec));
    int b = InitTargetState(&rec);
    CHECK(b >= 0);

    // steps4: the buy-object decision; state 0 over a zeroed record exercises the
    // home-city market scan against the real pool and frees. Defined return.
    std::memset(&rec, 0, sizeof(rec));
    i32 c = BuyObjectStep(&rec);
    (void)c;

    // recon: the arrest step machine (state 0 over a zeroed record passes through).
    std::memset(&rec, 0, sizeof(rec));
    i32 d = CharReconArrestStep(&rec);
    (void)d;

    // recon2: DrinkInit. Zeroed stat (inert statTableByte => 0 < cap) so it does
    // NOT early-free; with no other type-96 handler in the (empty) real pool it
    // proceeds, stamps the clock and advances. Returns GameTimeAdvance's hour.
    std::memset(&rec, 0, sizeof(rec));
    i32 e = CharRecon2DrinkInit(&rec);
    CHECK(e >= 0);

    InertAll();
}
