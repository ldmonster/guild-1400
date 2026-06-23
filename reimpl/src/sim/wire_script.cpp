// See wire_script.h. Binds the five Script-VM long-tail bridges (ScriptCmdHooks,
// ScriptImport3Hooks, ScriptImport4Hooks, ScriptLoadRunHooks, PurchaseScriptHooks)
// to their real reconstructed cross-cluster leaves. Glue only — no module logic.
//
// SEED-FROM-DEFAULTS. Each bridge is seeded from its current installed table
// (Get*Hooks(), which falls back to the module inert defaults) and only the fields
// with a genuine reconstructed callable target are overridden. This matters most
// for ScriptImport4Hooks: GetScriptImport4Hooks() dereferences g_hooks with NO null
// fallback and several import4 bodies (e.g. HandleExitKeyword) call hooks WITHOUT a
// null check, so a zero-initialised table would crash — we must keep import4's
// NON-null kDefaults for every field we don't bind.
#include "sim/wire_script.h"

#include "sim/script_import2.h"          // ScriptCmdHooks / Set/GetScriptCmdHooks
#include "sim/script_import3.h"          // ScriptImport3Hooks / Set/GetScriptImport3Hooks
#include "sim/script_import4.h"          // ScriptImport4Hooks / Set/GetScriptImport4Hooks
#include "sim/script_recon_purchase.h"   // ScriptLoadRunHooks / PurchaseScriptHooks

#include "util/util_recon.h"             // guild::util::ReconStrCmp (VIBE_Util_StrCmp @0x5d3f10)

namespace guild::sim {

namespace {

// VIBE_Util_StrCmp @0x5d3f10 — the byte-faithful word-at-a-time strcmp (normalised
// -1/0/+1 return; ptr==ptr short-circuit). The reconstruction is guild::util::
// ReconStrCmp; the hook field's positional (const char*, const char*) signature is
// exactly the C form of the original's __usercall(eax,edx) register args.
int WsStrCmp(const char* a, const char* b) {
    return guild::util::ReconStrCmp(a, b);
}

// --- process-lifetime wired hook tables (the global hook ptrs reference these) ---
ScriptCmdHooks      g_cmd{};
ScriptImport3Hooks  g_import3{};
ScriptImport4Hooks  g_import4{};
ScriptLoadRunHooks  g_loadRun{};
PurchaseScriptHooks g_purchase{};

} // namespace

void InstallRealScriptWiring() {
    // --- ScriptCmdHooks (script_import2.h) -----------------------------------
    // Seed from the module inert defaults. NONE of the per-command character/
    // action/memory leaves (reportError @0x440f94, createChar @0x402d10,
    // isValidPtr @0x4391f0, queueWalk @0x40b6a8, cmdHandler @0x40b888,
    // dummyBlocked @0x43dab4, createSound @0x405670, insertAction @0x40c2f0) is a
    // clean reconstructed leaf under this opaque-ScriptHandle ABI (the recon
    // siblings take typed CharacterRecord*/SceneNode*/ActionEntry* and walk deep
    // object state). Left inert (documented in wire_script.h). StrLen/StrNCopyPad
    // already delegate to guild::util inside script_import2.cpp directly.
    g_cmd = GetScriptCmdHooks();
    SetScriptCmdHooks(&g_cmd);

    // --- ScriptImport3Hooks (script_import3.h) -------------------------------
    // CmdCreateCharacterAtDummy's transform/character/object leaves take typed
    // object pointers + vec3 buffers (incompatible ABI). The allocator pair
    // (allocDebug/freeDebug) is the MemoryTracker subsystem the app layer owns;
    // the inert default ALREADY uses malloc/free so AddEventToken grows faithfully.
    // Nothing cleanly bindable here. Seed-from-defaults, install unchanged.
    g_import3 = GetScriptImport3Hooks();
    SetScriptImport3Hooks(&g_import3);

    // --- ScriptImport4Hooks (script_import4.h) -------------------------------
    // Seed from kDefaults (NON-null — import4 bodies call hooks unguarded). The one
    // genuine bind: strCmp -> the real VIBE_Util_StrCmp @0x5d3f10 reconstruction.
    // The loader/runner/lex/compile/destroy leaves are reconstructed under a
    // different ABI (LoadedScript&/ScriptHost/ctxTable, see script_run/script_import)
    // and stay at their faithful defaults; freeDebug/strtok/parseInt/strNCopyPad
    // likewise keep their faithful defaults (documented in wire_script.h).
    g_import4 = GetScriptImport4Hooks();
    g_import4.strCmp = &WsStrCmp;
    SetScriptImport4Hooks(&g_import4);

    // --- ScriptLoadRunHooks (script_recon_purchase.h) ------------------------
    // loadFromScriptDir / runMain / runWithArgs are the script loader+runner under
    // the incompatible LoadedScript/ScriptHost ABI; no clean leaf bind. Install the
    // (inert) seeded table so the bridge is explicitly initialised. Set* takes a
    // copy and returns the previous table (we discard it).
    g_loadRun = ScriptLoadRunHooks{};
    SetScriptLoadRunHooks(&g_loadRun);

    // --- PurchaseScriptHooks (script_recon_purchase.h) -----------------------
    // RunPurchaseLocationScript's leaves are all person-table (dword_11BB6A0),
    // building-type, VFS-path-resolve and script-loader/finish/run edges — process-
    // global tables and the loader leaves under the incompatible ABI above. None is
    // a clean reconstructed leaf. Install the (inert) seeded table explicitly.
    g_purchase = PurchaseScriptHooks{};
    SetPurchaseScriptHooks(&g_purchase);
}

} // namespace guild::sim
