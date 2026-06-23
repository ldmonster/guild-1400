#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::sim — script load/run leaves of gilde.exe. Faithful 1:1 reconstructions:
//
//   0x43c690  VIBE_Script_LoadAndRunMain      (load from script dir, then RunMain)
//   0x43c6ac  VIBE_Script_LoadAndRunWithArg   (load, then RunWithArgs(s,1,arg))
//   0x43c6d4  VIBE_Script_LoadAndRunWithArgAlt (identical body to 0x43c6ac)
//   0x50588c  VIBE_Script_RunPurchaseLocationScript
//
// The pure control flow / index arithmetic / path-string formatting is reproduced
// exactly. Engine edges (the person table scan target, building-type test, VFS path
// resolution, the script loader/finish/run leaves — all ALREADY present elsewhere in
// the reimpl: src/sim/script_run, script_import, person_personnel2, command_apply4,
// src/io/file_ops3) are routed through an inert-default hooks vtable so this
// translation unit is self-contained and orphan-free for the headless build.
//
// NOTE: VIBE_Script_LoadFromScriptDir (0x4424e0), VIBE_Script_RunMain (0x44396c),
// VIBE_Script_RunWithArgs (0x443a90), VIBE_Script_LoadScript (0x4421f0),
// VIBE_Script_FindByHandle (0x442174), VIBE_Script_Finish (0x443f38),
// VIBE_Person_FindActiveByEntity (0x5920b0), VIBE_Building_IsProductionType
// (0x587f80) and VIBE_Vfs_ResolvePath (0x44fa5c) are NOT redefined here; this file
// only adds the not-yet-present wrappers/orchestrator and drives those through hooks.
// =============================================================================
namespace guild::sim {

// ---------------------------------------------------------------------------
// 0x43c690 / 0x43c6ac / 0x43c6d4 — the LoadAndRun thin wrappers.
//
// All three load a script by name from the script directory; on failure (null)
// they return 0. On success the Main variant calls RunMain(script, script, ?);
// the WithArg variants call RunWithArgs(script, 1, arg).  IMPORTANT: all three
// return edx == the LOADED SCRIPT pointer (`mov eax, edx`), NOT the RunMain /
// RunWithArgs result (that result is discarded).  The "Alt" entry at 0x43c6d4
// has a byte-identical body to 0x43c6ac (a duplicate stub in the binary).
// ---------------------------------------------------------------------------
struct ScriptLoadRunHooks {
    // VIBE_Script_LoadFromScriptDir(name) -> script handle (0 on failure).
    void* (*loadFromScriptDir)(const char* name) = nullptr;
    // VIBE_Script_RunMain(script, script, edx) -> eax.
    int (*runMain)(void* script) = nullptr;
    // VIBE_Script_RunWithArgs(script, mode, arg) -> eax.
    int (*runWithArgs)(void* script, int mode, int arg) = nullptr;
};
const ScriptLoadRunHooks* SetScriptLoadRunHooks(const ScriptLoadRunHooks* hooks);

// gilde.exe 0x43c690 — VIBE_Script_LoadAndRunMain@<eax>(const char** a1@<eax>).
//   v1 = LoadFromScriptDir(*a1); if (!v1) return 0; RunMain(v1); return (int)v1.
// Returns the LOADED SCRIPT handle (eax=edx @0x43c6a7), not RunMain's result.
int Script_LoadAndRunMain(const char* name);

// gilde.exe 0x43c6ac — VIBE_Script_LoadAndRunWithArg@<eax>(const char** a1@<eax>).
// gilde.exe 0x43c6d4 — VIBE_Script_LoadAndRunWithArgAlt (identical body).
//   v1 = LoadFromScriptDir(*a1); if (!v1) return 0; RunWithArgs(v1, 1, *ecx);
//   return (int)v1.  Returns the LOADED SCRIPT handle (eax=edx @0x43c6d0), not
//   RunWithArgs's result.  `arg` is the already-dereferenced *ecx operand.
int Script_LoadAndRunWithArg(const char* name, int arg);

// ---------------------------------------------------------------------------
// 0x50588c — VIBE_Script_RunPurchaseLocationScript.
//
// Resolves the trading partner the player (entity a1) is talking to, then loads
// and runs that location's "Einkauf_<key>.esc" purchase script.
//
// Person table dword_11BB6A0: 32 dword slots (scanned by byte offset 0..128 step
// 4). Each slot is a person record pointer; the matching record is the one equal
// to FindActiveByEntity(a1). For a valid partner (record+0x08 byte > 1):
//
//   * if a2 (the building/location record) is null, OR the location key field
//     *(u32*)(a2+2) (byte offset +2; the decompile renders it *(u32*)(a2_i16+1))
//     equals the partner-building's *(u32*)(buildingRec+0x30) (dword index 12)
//     (buildingRec = *((u32*)partner + 97), i.e. partner+0x184), proceed.
//   * production location  (IsProductionType(a1) true):  key index =
//       589 * a1[0] + dword_13CE294   (+1 used as the %s key string base)
//   * purchase location    (otherwise):                  key index =
//        65 * a2[0] + dword_13CE27C   (+1)
//   * key string = (purchaseIdx ? purchaseIdx+1 : productionIdx+1)
//   * path = "%slocations\\%s\\Einkauf_%s.esc" with prefix "x:\\engine\\gfx\\scripts\\"
//     and the key string substituted for BOTH %s.
//   * resolve via VFS; if found: load the script; if the PARTNER BUILDING record
//     (partner+0x184) already has a running script handle (*(u32*)(bld+0x28) != -1)
//     finish it; then RunWithArgs(script, 2, partnerBuilding); store the new
//     handle/tick (dword_634494 = script[0x80]).
//
// dword_634494 is set to the run tick on success, or -1 if the load failed.
// Returns: null when no valid partner / no path; the partner-building record
// (partner+0x184) on key-mismatch AND on the run-success path (the original
// reassigns result = *(v6+0x184) @0x5059f9, NOT the script handle).
//
// The integer math (table stride, 589/65 key strides, the +1 offsets, the
// production-vs-purchase selection) is reproduced exactly. The record-field reads,
// VFS resolve, sprintf and the script loader/finish/run are routed through hooks.
// ---------------------------------------------------------------------------
struct PurchaseScriptHooks {
    // VIBE_Person_FindActiveByEntity(a1) -> active person record pointer.
    void* (*findActiveByEntity)(void* entity) = nullptr;
    // Person table dword_11BB6A0[i] for i in 0..31 (slot pointer; null == empty).
    void* (*personSlot)(int index) = nullptr;
    // record+0x08 (byte) — partner "state"/kind; must be > 1 to be valid.
    u8 (*personStateByte)(void* personRec) = nullptr;
    // *((u32*)personRec + 97)  (partner building record pointer at +0x184).
    void* (*personBuilding)(void* personRec) = nullptr;
    // *(u32*)(a2 + 2)   (the location record's key field at byte offset +2).
    u32 (*locationKeyField)(void* locationRec) = nullptr;
    // *((u32*)buildingRec + 12)  (partner building's key field at +0x30).
    u32 (*buildingKeyField)(void* buildingRec) = nullptr;
    // VIBE_Building_IsProductionType(a1) -> nonzero if a production location.
    int (*isProductionType)(void* entity) = nullptr;
    // a1[0] (i8) — the production location's type byte (drives the 589 stride).
    i8 (*entityTypeByte)(void* entity) = nullptr;
    // a2[0] (i16) — the purchase location's type word (drives the 65 stride).
    i16 (*locationTypeWord)(void* locationRec) = nullptr;
    // dword_13CE294 — production key-table base.
    int productionKeyBase = 0;
    // dword_13CE27C — purchase key-table base.
    int purchaseKeyBase = 0;
    // The key string for index `keyIndex` (the value passed twice as %s). The
    // original treats the index as a char* into a string-pool base; the host maps
    // it to the actual key text. Default returns "".
    const char* (*keyString)(int keyIndex) = nullptr;
    // VFS resolve of the formatted path; nonzero handle on success (else 0).
    void* (*resolvePath)(const char* path) = nullptr;
    // VIBE_Script_LoadScript(path) -> script handle (0 on failure).
    void* (*loadScript)(const char* path) = nullptr;
    // VIBE_Script_FindByHandle(handle) -> running-script pointer (0 if none).
    void* (*findByHandle)(int handle) = nullptr;
    // VIBE_Script_Finish(running) — terminate a running script.
    void (*finish)(void* running) = nullptr;
    // VIBE_Script_RunWithArgs(script, 2, partnerBuilding) -> run tick
    // (the original passes the partner BUILDING record, not the location record).
    int (*runWithArgs2)(void* script, void* partnerBuilding) = nullptr;
    // The partner building's current script handle: *(u32*)(bld+0x28). -1 == none
    // (read from partner+0x184+0x28, NOT from the location record).
    int (*locationScriptHandle)(void* partnerBuilding) = nullptr;
};
const PurchaseScriptHooks* SetPurchaseScriptHooks(const PurchaseScriptHooks* hooks);

// dword_634494 — last purchase-script run tick (-1 on load failure). Module-owned.
int& PurchaseScriptRunTick();

// gilde.exe 0x50588c — see the block comment above. `entity` is a1, `locationRec`
// is a2. Returns the loaded script handle (or null when no valid partner / no path).
void* Script_RunPurchaseLocationScript(void* entity, void* locationRec);

// Number of person slots scanned (dword_11BB6A0: 128 bytes / 4).
inline constexpr int kPurchasePersonSlots = 32;

// Path prefix aXEngineGfxScri @0x62e7a4 and format aSlocationsSEin @0x620f5c.
inline constexpr const char* kScriptDirPrefix = "x:\\engine\\gfx\\scripts\\";
inline constexpr const char* kEinkaufPathFmt  = "%slocations\\%s\\Einkauf_%s.esc";

void ResetScriptReconPurchase();

} // namespace guild::sim
