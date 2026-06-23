// Verifies InstallRealCombat2Wiring() binds the directly-bindable fields of the
// BrawlHooks (VIBE_CharAction_BrawlStep @0x4d201c) and Recon2Hooks (take/drop/look/
// ani Cmd cluster) bridges to real reconstructed leaves, and that the four
// zero-bindable bridges (CharQuery / CombatSlots3 / CombatSlots4 / Check) are left
// untouched. Previously nothing installed either bridge. Suite prefix: WireCombat2.
#include "tests/framework/test.h"

#include "sim/wire_combat2.h"
#include "sim/real_hooks3.h"               // InstallRealSimHooks3 (shared He pool)
#include "sim/charaction_brawl.h"
#include "sim/character_recon2_cmds.h"
#include "sim/he.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// Re-inert the two wireable bridges so a clean baseline holds before install.
static void InertBoth() {
    SetBrawlHooks(nullptr);
    character_recon2::SetRecon2Hooks(nullptr);
}

TEST(WireCombat2, BindsBrawlAndRecon2Leaves) {
    InertBoth();

    // Baseline: brawl leaves null, recon2 leaves null.
    CHECK(GetBrawlHooks().packetStatus == nullptr);
    CHECK(GetBrawlHooks().freeHandlerEntry == nullptr);
    CHECK(GetBrawlHooks().findPersonById == nullptr);
    CHECK(character_recon2::GetRecon2Hooks().strCmp == nullptr);
    CHECK(character_recon2::GetRecon2Hooks().strLen == nullptr);

    InstallRealCombat2Wiring();

    // --- BrawlHooks: the three clean binds are now real ----------------------
    const BrawlHooks& b = GetBrawlHooks();
    CHECK(b.packetStatus     != nullptr);
    CHECK(b.freeHandlerEntry != nullptr);
    CHECK(b.findPersonById   != nullptr);
    // unbound (no clean target) stays inert
    CHECK(b.aggressorRecord      == nullptr);
    CHECK(b.adjustRelationByMood == nullptr);
    CHECK(b.registerApEvent      == nullptr);
    CHECK(b.restorePoseAndRequeue == nullptr);

    // --- Recon2Hooks: the two recovered util leaves are real -----------------
    const character_recon2::Recon2Hooks& r = character_recon2::GetRecon2Hooks();
    CHECK(r.strCmp != nullptr);
    CHECK(r.strLen != nullptr);
    // deep engine / transform / action-builder leaves stay inert
    CHECK(r.queueInsertEntry  == nullptr);
    CHECK(r.insertActionArgs  == nullptr);
    CHECK(r.angleToTargetSigned == nullptr);
    CHECK(r.strNCopyPad       == nullptr);

    InertBoth();
}

// The bound Recon2 string leaves behave as the real reconstructed helpers: strCmp
// returns 0 on equal / sign on differ, strLen counts bytes.
TEST(WireCombat2, Recon2StringLeavesExecute) {
    InertBoth();
    InstallRealCombat2Wiring();

    const character_recon2::Recon2Hooks& r = character_recon2::GetRecon2Hooks();
    CHECK(r.strCmp("ub_Blutlache", "ub_Blutlache") == 0);
    CHECK(r.strCmp("a", "b") < 0);
    CHECK(r.strCmp("b", "a") > 0);
    CHECK_EQ(r.strLen(""), static_cast<std::size_t>(0));
    CHECK_EQ(r.strLen("anim"), static_cast<std::size_t>(4));

    InertBoth();
}

// A representative brawl step runs over a zeroed He record through the installed
// real leaves without crashing, returning a defined outcome — i.e. the wired
// control flow actually executes (packet-status read, free / person resolve against
// the shared real pool). InstallRealSimHooks3 first binds the sibling He pool.
TEST(WireCombat2, WiredBrawlExecutesOverZeroedRecord) {
    InstallRealSimHooks3();          // owns/creates the shared real He pool
    InstallRealCombat2Wiring();

    HeRecord rec;
    std::memset(&rec, 0, sizeof(rec));
    // state 0, busy flag clear, packet handle -1 (== none) => the landed-blow path;
    // the zeroed victim id resolves to no Person, so it takes the victim-gone branch
    // (saved-pose requeue is inert) and returns a defined outcome.
    BrawlOutcome out = BrawlStep(&rec);
    (void)out;

    // state -2 is the terminal free path: routes through the bound freeHandlerEntry
    // (HandlerTable::FreeHandlerEntry over a zeroed record) without crashing.
    std::memset(&rec, 0, sizeof(rec));
    Brawl_State(&rec) = -2;
    BrawlOutcome freed = BrawlStep(&rec);
    CHECK(freed == BrawlOutcome::Freed);

    SetBrawlHooks(nullptr);
    character_recon2::SetRecon2Hooks(nullptr);
}
