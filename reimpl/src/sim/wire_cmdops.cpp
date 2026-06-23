// See wire_cmdops.h. Binds the one byte-faithful reconstructed leaf across the
// command-apply / action-op bridges this agent surveyed: OrderDriverHooks's
// `randomModulo` field, wired to the REAL guild::sim::Math_RandomModulo
// (gilde.exe 0x58b89c — RandNext() % n, over the CRT LCG crt::RandNext). This is
// exactly the wiring the combat_orders2 integration test performs by hand; here it
// is installed for the live call tree so the order driver's state-2 attack
// hit-chance roll consumes a real CRT draw instead of the inert 0.
//
// The other four/five surveyed bridges (Op85Hooks, Ops4Hooks/FileOps4Hooks,
// MiscActionHooks, KillHooks, Leaves9Hooks/RenderLeaves9Hooks) have ZERO
// byte-faithful bindable fields (see wire_cmdops.h for the per-bridge reasons:
// raw OS I/O = rule 6, distinct decoded view structs = not castable, no
// reconstructed callable leaf, or an incompatible typed signature). Per the
// agent constraint, no code is emitted for them; they remain on their module
// inert defaults.
#include "sim/wire_cmdops.h"

#include "sim/combat_orders2.h"   // OrderDriverHooks / Get/SetOrderDriverHooks
#include "sim/combat.h"           // Math_RandomModulo (the REAL reconstructed leaf)

namespace guild::sim {

namespace {

// OrderDriverHooks::randomModulo field shape: int (*)(u16 n). Math_RandomModulo
// has exactly that signature (returns int; RandNext()%n, or 0 when n==0), so the
// bind is direct — no adapter needed. Wrapped only to take its address through a
// stable function symbol.
int WcoRandomModulo(u16 n) { return Math_RandomModulo(n); }

// Process-lifetime wired hook table (the global hook ptr references this).
OrderDriverHooks g_orderDriver{};

} // namespace

void InstallRealCmdOpsWiring() {
    // --- OrderDriverHooks (combat_orders2.h) ---------------------------------
    // SEED-FROM-DEFAULTS: start from whatever is currently installed (the module
    // inert defaults at boot) so the unbound scene/path/command/sink fields keep
    // their safe inert stubs, then override only the byte-faithful reconstructed
    // leaf. Several order-driver leaf wrappers are inert-aware (HRoll falls back to
    // 0), so a partial table is safe; we still seed for parity with the other
    // wire_* installers.
    g_orderDriver = GetOrderDriverHooks();
    g_orderDriver.randomModulo = &WcoRandomModulo;
    // packetStatus / resolveUnit / onTargetTile / findFreeTile / unitBusy / emit:
    // scene/path/command/sink leaves with no standalone reconstructed callable
    // target -> left inert (documented in wire_cmdops.h).
    SetOrderDriverHooks(&g_orderDriver);
}

} // namespace guild::sim
