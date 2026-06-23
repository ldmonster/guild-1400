// Verifies InstallRealScriptWiring() wires the five Script-VM long-tail bridges
// (ScriptCmdHooks, ScriptImport3Hooks, ScriptImport4Hooks, ScriptLoadRunHooks,
// PurchaseScriptHooks) — previously fully inert at runtime (nothing installed them).
// The single genuine reconstructed leaf is ScriptImport4Hooks.strCmp ->
// VIBE_Util_StrCmp @0x5d3f10; the rest stay at their (safe, non-null for import4)
// inert defaults because their leaves have no clean reconstructed target under the
// bridge ABI. Suite prefix: WireScript.
#include "tests/framework/test.h"

#include "sim/wire_script.h"
#include "sim/script_import2.h"
#include "sim/script_import3.h"
#include "sim/script_import4.h"
#include "sim/script_recon_purchase.h"

using namespace guild;
using namespace guild::sim;

// Re-inert the bridges so a clean baseline can be reasoned about before install.
// (import4 reverts to its NON-null kDefaults; the others to null/inert.)
static void InertAll() {
    SetScriptCmdHooks(nullptr);
    SetScriptImport3Hooks(nullptr);
    SetScriptImport4Hooks(nullptr);   // -> kDefaults (non-null)
    SetScriptLoadRunHooks(nullptr);
    SetPurchaseScriptHooks(nullptr);
}

TEST(WireScript, InstallBindsStrCmpAndSeedsAllFive) {
    InertAll();

    // Baseline: import4 falls back to its non-null kDefaults strCmp (the faithful
    // DefStrCmp), NOT the real VIBE_Util_StrCmp reconstruction.
    auto defaultStrCmp = GetScriptImport4Hooks().strCmp;
    CHECK(defaultStrCmp != nullptr);   // kDefaults is non-null

    InstallRealScriptWiring();

    // --- ScriptImport4Hooks: strCmp is now the real 0x5d3f10 reconstruction. ---
    const ScriptImport4Hooks& s4 = GetScriptImport4Hooks();
    CHECK(s4.strCmp != nullptr);
    CHECK(s4.strCmp != defaultStrCmp);   // overridden away from the default
    // Every other import4 field keeps its NON-null faithful default (several
    // import4 bodies call hooks unguarded, so seed-from-defaults must preserve them).
    CHECK(s4.parseInt   != nullptr);
    CHECK(s4.strNCopyPad != nullptr);
    CHECK(s4.runFrameLoop != nullptr);
    CHECK(s4.loadFromDir != nullptr);

    // --- The other four bridges are explicitly installed (non-crashing) ---------
    // ScriptCmdHooks / ScriptImport3Hooks have null-fallback accessors; after a
    // seed-from-defaults install their inert defaults are preserved (no clean leaf).
    const ScriptCmdHooks&     cmd = GetScriptCmdHooks();
    const ScriptImport3Hooks& s3  = GetScriptImport3Hooks();
    (void)cmd;
    (void)s3;   // no leaf bound — purely inert, documented in wire_script.h

    InertAll();   // restore for any later test in this TU
}

// The one bound leaf actually executes the real reconstructed VIBE_Util_StrCmp
// behaviour: normalised -1 / 0 / +1 return and the ptr==ptr short-circuit.
TEST(WireScript, BoundStrCmpExecutesRealReconBehaviour) {
    InertAll();
    InstallRealScriptWiring();

    auto cmp = GetScriptImport4Hooks().strCmp;
    CHECK(cmp != nullptr);

    const char* a = "main";
    const char* b = "main";
    const char* c = "exit";

    // Equal strings -> 0.
    CHECK_EQ(cmp(a, b), 0);
    // Identical pointer -> 0 (the original short-circuits a==b).
    CHECK_EQ(cmp(a, a), 0);
    // Inequality is normalised to exactly -1 / +1 (NOT the raw byte difference).
    int r1 = cmp("exit", "main");   // 'e'(0x65) < 'm'(0x6d) -> -1
    int r2 = cmp("main", "exit");   // 'm' > 'e'             -> +1
    CHECK_EQ(r1, -1);
    CHECK_EQ(r2, 1);
    (void)c;

    InertAll();
}
