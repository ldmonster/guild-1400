#pragma once
// wire_script — wires the five Script-VM long-tail bridge tables into their REAL
// reconstructed cross-cluster leaves (rule 13). Before this, all five bridges were
// fully inert at runtime: nothing in the live call tree ever called their
// Set*Hooks installers (only the per-module .cpp inert defaults and the unit/e2e
// tests did). The five bridges:
//
//   * ScriptCmdHooks        (script_import2.h) — the per-command Cmd* opcode bodies'
//        character/action/memory leaves (createChar, queueWalk, createSound,
//        insertAction, isValidPtr, reportError, cmdHandler, dummyBlocked).
//   * ScriptImport3Hooks    (script_import3.h) — CmdCreateCharacterAtDummy's
//        transform/character/object leaves + AddEventToken's table allocator.
//   * ScriptImport4Hooks    (script_import4.h) — the runner/control-flow/teardown
//        drivers' compile/lex/run/destroy/alloc/str leaves.
//   * ScriptLoadRunHooks    (script_recon_purchase.h) — the LoadAndRun* thin
//        wrappers' loadFromScriptDir / runMain / runWithArgs leaves.
//   * PurchaseScriptHooks   (script_recon_purchase.h) — RunPurchaseLocationScript's
//        person-table / building / VFS / loader leaves.
//
// WHAT BINDS (genuine reconstructed callable target, same address, same behaviour):
//
//   * VIBE_Util_StrCmp @0x5d3f10  ->  guild::util::ReconStrCmp  (util_recon.h)
//        ScriptImport4Hooks.strCmp. The reconstruction is the byte-for-byte
//        word-at-a-time strcmp shipped at 0x5d3f10 (normalised -1/0/+1 return,
//        ptr==ptr short-circuit). HandleExitKeyword (0x443084) calls it
//        unconditionally with no null guard, so this is both a real bind AND a
//        crash-safety requirement.
//
// WHY THE REST STAY INERT (documented per bridge in wire_script.cpp):
//
//   * The script LOADER / RUNNER / lexer / compiler leaves (loadFromScriptDir,
//     loadScript, runMain, runWithArgs, compileBlock, lookupFunction,
//     enterFunction, nextToken, declareLocal, destroyCtx, finishCtx) ARE
//     reconstructed elsewhere (sim/script_run.{h,cpp} RunMain/CutsceneLoadAndRun,
//     sim/script_import.{h,cpp} FindByHandle/FindCommandByName), but under a
//     DIFFERENT ABI: they take LoadedScript&, ScriptHost, and u8* ctxTableBase —
//     the live VM context model — whereas these bridge hooks pass a bare
//     const char* name / void* / u8* ctx. Synthesising a LoadedScript+ScriptHost
//     from a bare name (or fabricating a ctx-table layout) would be an abstracted
//     stand-in, not a faithful leaf call (rule 8). Left inert.
//   * The character / action / scene leaves (ScriptCmdHooks.createChar @0x402d10,
//     queueWalk @0x40b6a8, createSound @0x405670, insertAction @0x40c2f0,
//     ScriptImport3Hooks.createFromModel @0x402d10, the VIBE_Transform_* /
//     VIBE_Object_* math) ARE reconstructed (character_factory, charaction_misc,
//     character_recon2_cmds, util/transform, util/math, object_lifecycle3) but take
//     typed CharacterRecord*/SceneNode*/ActionEntry* and dereference deep object
//     fields; the hooks pass opaque ScriptHandle (intptr_t)/void*. Binding them
//     would require fabricating object state these bodies walk into — not a clean
//     leaf bind, and unsafe over the zeroed handles a headless run supplies. Inert.
//   * The memory allocator pair (ScriptImport3Hooks.allocDebug/freeDebug @0x438f10/
//     @0x43923c, ScriptImport4Hooks.freeDebug) is VIBE_Memory_AllocDebug/FreeDebug,
//     reconstructed as MemoryTracker methods bound to a Heap& process-global the
//     app layer owns (app/wiring.h RealSubsystems::tracker). It is not exposed as a
//     standalone leaf (cf. object_attach_wiring.cpp, which uses a plain heap
//     stand-in, not the tracker). The script bridges' inert default ALREADY uses
//     malloc/free (script_import3.cpp), so AddEventToken's table grows faithfully;
//     re-binding to a non-tracker malloc would add nothing. Left at default.
//   * reportError @0x440f94, dummyBlocked @0x43dab4, cmdHandler @0x40b888,
//     isValidPtr @0x4391f0, readDword @0x5dc8b0, parseInt @0x5dc070, strtok
//     @0x5e9cd0, strNCopyPad @0x5d9360: no standalone reconstruction under the
//     bridge ABI (reportError/cmdHandler/dummyBlocked are unreconstructed; the str
//     helpers' import4 kDefaults are already faithful atoi/strncpy/no-op). Inert.
//   * PurchaseScriptHooks / ScriptLoadRunHooks are entirely person-table
//     (dword_11BB6A0), building-type, VFS-path-resolve and script-loader edges —
//     process-global tables and the loader/finish/run leaves under the incompatible
//     ABI above; none is a clean reconstructed leaf. Left fully inert.
namespace guild::sim {

// Install the real bindings into the five Script-VM long-tail bridges
// (ScriptCmdHooks, ScriptImport3Hooks, ScriptImport4Hooks, ScriptLoadRunHooks,
// PurchaseScriptHooks). SEED-FROM-DEFAULTS: each table is seeded from its module
// inert defaults (ScriptImport4Hooks's kDefaults are NON-null and several import4
// bodies call hooks with no null guard) and only the wireable fields are overridden,
// so unbound fields keep their safe defaults. Idempotent; the tables are
// process-lifetime storage the global hook pointers reference.
void InstallRealScriptWiring();

} // namespace guild::sim
