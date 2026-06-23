#pragma once
#include "guild/common/types.h"
#include <string>

// =============================================================================
// guild::render — BUILDING CHIMNEY SMOKE attach (wave-8, W8-SMOKE).
//
// The rising smoke over a building's chimney/forge/kitchen. In gilde.exe this is
// a particle emitter SYSTEM created by running the effect script
// `effekte\Schornstein_dunkel.esc` at the world position of the building's
// `dummy_RAUCH_0` ("smoke") dummy node. The script's body calls the
// `CreateEmitter` script command (0x43fd24) which spawns the particle system the
// per-frame render walk draws.
//
// 1:1 RECONSTRUCTION (gilde.exe, imagebase 0x400000)
// ---------------------------------------------------------------------------
//   0x4b60a0  VIBE_Object_SpawnChimneySmoke(a1@eax = building/person record,
//                                            a2@edi = scratch slot arg)
//             Per-building smoke setup. Reconstructed here as SpawnChimneySmoke.
//   0x504910  VIBE_Scene_RefreshBuildingEffects(a1@edi, a2@esi = world/owner)
//             The CITY driver: iterates every owned building (Person query
//             1/6), flags it, and calls SpawnChimneySmoke for each. The smoke
//             portion is reconstructed here as RefreshBuildingSmoke.
//
// THE EXACT DATA FLOW (recovered from the disasm @0x4b60a0)
// ---------------------------------------------------------------------------
//   season = day % 4                                  (0x58339c, day=GameTime.day)
//   if ((*(typeTable + 589 * record[0]) byte) == 7)   return;   // type 7 = no smoke
//   if (record[+97] == 0)                             return;   // no building node
//   // scan the 768-slot, 536-stride active-record array for an EFFECT slot bound
//   // to THIS building (alive, !=0xFFFF, kind!=10, homeBld==record, slotKind==4):
//   //   found && slot[+200] != 0  -> smoke already live: if its script handle
//   //                                expired, finish it; return.
//   // else, time-of-day gate: only emit when hour in
//   //   [smokeStartHour[season], smokeEndHour[season])     (flt_6476FC/flt_64770C)
//   //   AND record[+149] (script handle) == -1 (none running)
//   SwitchActiveSlot(0, 1, "dummy_RAUCH_0", a2);      // 0x5b4a24, push slot 0
//   node = Object_FindByHandle(record[+97], 256, ..., a2);  // 0x5b7be4 find dummy
//   if (node) {
//     scr = Script_LoadFromScriptDir("effekte\\Schornstein_dunkel.esc"); // 0x4424e0
//     if (scr) {
//       PointThroughBoneChain(node, node+19f, world);  // 0x5c8b38 dummy world pos
//       ax = (int)world[2]; ay = (int)world[1]; az = (int)world[0]; // frndint trunc
//       if (Script_RunWithArgs(scr, 3, az, ay, ax))     // 0x443a90 run main(z,y,x)
//         record[+149] = scr[+128];                     // store running handle
//       scr[+132] = 0;
//     }
//   }
//   SwitchActiveSlot(prevSlot, 1, ...);               // restore slot
//
// season tables (get_bytes, bit-exact):
//   smokeStartHour[4] @0x6476FC = { 8.0, 7.0, 8.0, 9.0 }   (hour smoke begins)
//   smokeEndHour[4]   @0x64770C = { 20.0, 21.0, 20.0, 19.0 } (hour smoke ends)
//
// PORTABILITY (rules 3,4): the building/person records, the scene nodes and the
// script VM are engine memory the host owns. This module reproduces the control
// flow VERBATIM and reaches that memory only through the BuildingFxHooks vtable,
// so the headless library has no engine dependency and golden tests drive it over
// synthetic memory. The real smoke geometry is produced by the script body, whose
// `CreateEmitter` call maps onto W8-EMITTER's render::SpawnEmitterAtPosition — the
// default `runSmokeScript` hook spawns exactly that system at the computed world
// position, so the observable effect (a smoke system at the chimney) is faithful.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Engine-default season smoke-window tables (the only data this routine needs;
// everything else is record/node memory reached through hooks). Indexed by
// season = GameTime.day % 4.
//   smokeStartHour @0x6476FC, smokeEndHour @0x64770C
// ---------------------------------------------------------------------------
extern const float kSmokeStartHour[4]; // { 8, 7, 8, 9 }
extern const float kSmokeEndHour[4];   // { 20, 21, 20, 19 }

// Field byte offsets inside a building/person record reached by SpawnChimneySmoke
// (the 536-stride active-record; see sim/types.h Person). All raw, unaligned.
enum BuildingFxRecordField : int {
    kBfxType          = 0x00,  // record[0]: building TYPE index into the 589-table
    kBfxBuildingNode  = 0x61,  // +97 : the building's model scene-node handle
    kBfxSmokeScript   = 0x95,  // +149: running smoke-script handle (-1 == none)
};

// ---------------------------------------------------------------------------
// Host-supplied access to the engine subsystems SpawnChimneySmoke touches. The
// reconstruction calls these in the exact order/conditions of the binary. A
// fully inert default set (defined in building_fx.cpp) lets the headless build
// link and the golden tests run over synthetic memory.
// ---------------------------------------------------------------------------
struct BuildingFxHooks {
    // --- record / type-table reads -----------------------------------------
    // record[0] (the building TYPE byte).                             (0x4b60b4)
    u8  (*recordType)(void* record) = nullptr;
    // *(byte*)(dword_13CE294 + 589 * typeIndex): the building type-def state byte
    // (== 7 => "no chimney smoke").                                   (0x4b60ce)
    u8  (*buildingTypeStateByte)(u8 typeIndex) = nullptr;
    // record[+97] dword: the building model scene-node handle.        (0x4b60e2)
    void* (*recordBuildingNode)(void* record) = nullptr;
    // record[+149] dword: the running smoke-script handle (-1 == none).(0x4b6119)
    i32 (*recordSmokeScript)(void* record) = nullptr;
    void (*setRecordSmokeScript)(void* record, i32 handle) = nullptr;

    // --- clock --------------------------------------------------------------
    // GameTime.day (qword_13CE852 dword[0]); season = day % 4.        (0x58339c)
    i32 (*gameTimeDay)() = nullptr;
    // GameTime.hour (WORD2(qword_13CE852) == *(u16*)(base+4)).        (0x4b6128)
    i32 (*gameTimeHour)() = nullptr;

    // --- the active-record effect-slot scan (0x4b60ee..0x4b626a) ------------
    // Reproduces the 768x536 scan that finds the EFFECT slot already bound to
    // `record` (alive && handle!=0xFFFF && kind!=10 && homeBld==record &&
    // slotKind==4). Returns the slot pointer (dword_12CEA8C[...]) or null. The
    // host owns the active-record array; the scan predicate is fixed (documented
    // above). Out-param `matched` mirrors the original's `v6/ecx` "a slot row was
    // matched at all" flag (set even when the slot pointer is null).
    void* (*findEffectSlot)(void* record, bool* matched) = nullptr;
    // slot[+200] dword (0x4b6263): non-zero => the slot already carries a live
    // effect (smoke is up).
    i32 (*effectSlotPayload)(void* slot) = nullptr;

    // --- universe slot context (0x5b4a24) -----------------------------------
    // SwitchActiveSlot(slot, 1, name, arg). Returns the PREVIOUS active slot
    // (dword_649D60) so the caller can restore it. Push: SwitchActiveSlot(0,...);
    // restore: SwitchActiveSlot(prev,...).
    i32 (*switchActiveSlot)(i32 slot, const char* name, i32 arg) = nullptr;

    // --- dummy-node lookup (0x5b7be4) ---------------------------------------
    // Object_FindByHandle(buildingNode, 256, ...): locate the `dummy_RAUCH_0`
    // child node under the building. Returns the node (float*) or null. The name
    // ("dummy_RAUCH_0") is bound by the preceding switchActiveSlot call in the
    // original; passed here for the host's convenience.
    float* (*findDummyNode)(void* buildingNode, const char* dummyName) = nullptr;

    // --- the smoke script (0x4424e0 / 0x443a90 / 0x442174 / 0x443f38) -------
    // Load + run the effect script at the dummy world position. `worldX/Y/Z` are
    // the truncated integer coords (frndint of PointThroughBoneChain output). The
    // original loads "effekte\\Schornstein_dunkel.esc" and runs its main(z,y,x).
    // Returns the script's running handle to store at record[+149], or -1 on
    // failure (script missing or main() rejected the args). The DEFAULT hook
    // delegates to render::SpawnEmitterAtPosition (the CreateEmitter the script
    // body invokes) so a real smoke system is produced at the chimney.
    i32 (*runSmokeScript)(const char* scriptPath, void* dummyNode,
                          i32 worldX, i32 worldY, i32 worldZ) = nullptr;
    // Script_FindByHandle(handle) (0x442174): resolve a running script by handle,
    // null if it has finished/expired.
    void* (*findScriptByHandle)(i32 handle) = nullptr;
    // Script_Finish(script) (0x443f38): stop a running script.
    void (*finishScript)(void* script) = nullptr;

    // --- the city driver's per-building iterator (0x504910) -----------------
    // Person_QueryBegin(world, 1, 6) (0x586c20): first owned building, or null.
    void* (*queryBuildingsBegin)(void* world) = nullptr;
    // Person_IterNext (0x586a6c): next owned building, or null.
    void* (*queryBuildingsNext)() = nullptr;
    // The 0x504930 flag write: building model-node +530 byte = (b & 0xF3) | 4.
    void (*flagBuildingNode)(void* buildingNode) = nullptr;
};

// Install the host hook set (null => the inert defaults).
void SetBuildingFxHooks(BuildingFxHooks* hooks);
BuildingFxHooks* BuildingFxHooksPtr();

// ---------------------------------------------------------------------------
// 0x4b60a0 — VIBE_Object_SpawnChimneySmoke. Attach (or refresh/expire) the
// chimney smoke for ONE building record. `slotArg` is the engine scratch the
// original threads through SwitchActiveSlot/Object_FindByHandle (a2@edi). 1:1
// control flow; all engine memory access goes through the installed hooks.
// ---------------------------------------------------------------------------
void SpawnChimneySmoke(void* record, i32 slotArg);

// Result of attaching smoke to a city (observability for the integration + tests).
struct BuildingSmokeAttachResult {
    int buildings    = 0;  // owned buildings visited by the driver
    int smokeSpawned = 0;  // records whose smoke script handle was set this pass
};

// ---------------------------------------------------------------------------
// 0x504910 (smoke portion) — VIBE_Scene_RefreshBuildingEffects. The CITY driver:
// walk every owned building (Person query 1/6), flag its node (+530), and run
// SpawnChimneySmoke. This is the clean scene/city-load entry the session wires:
// after a city loads, call AttachCityBuildingSmoke(world, slotArg) to give every
// chimney its smoke system. (The full original also rebuilds lights/octree/
// terrain after this loop — those belong to other modules; this entry owns the
// smoke arm only, exactly as the binary sequences it first.)
// ---------------------------------------------------------------------------
BuildingSmokeAttachResult AttachCityBuildingSmoke(void* world, i32 slotArg);

// ---------------------------------------------------------------------------
// EFFECT-SCRIPT VM BRIDGE (wave-18, W18-ESC) — rule 13 wiring.
//
// The DEFAULT runSmokeScript spawns a single approximate emitter. The faithful
// path RUNS the real effect script (effekte\Schornstein_dunkel.esc) through the
// reconstructed .esc VM + effect command set (sim/effect_script), which executes
// the script body's CreateEmitter / SetEmitter* / SetParticlePos commands and
// yields the exact two-emitter smoke set. This bridge runs that VM over the given
// script SOURCE and spawns one live particle SYSTEM per emitter the script
// created, at the script-placed world position. Returns the number of emitters
// spawned (0 if the source did not compile / created none).
//
// `scriptSource` is the .esc text (the host reads it from the VFS — the same
// "effekte\Schornstein_dunkel.esc" the original loads). `worldX/Y/Z` are the
// truncated dummy world coords (SpawnChimneySmoke's frndint output); the script's
// main(x,y,z) receives them and SetParticlePos places the emitters. `owner` is
// the chimney scene node the smoke systems ride (recorded at system+0x2F0).
//
// This is ADDITIVE: it does not change the inert default. A host that wants the
// real script-driven smoke installs a runSmokeScript hook that reads the .esc and
// calls this (see RunSmokeScriptViaVm below).
int SmokeScriptToEmitters(const char* scriptSource, void* owner,
                          i32 worldX, i32 worldY, i32 worldZ);

// A runSmokeScript-compatible wrapper that resolves `scriptPath` through the
// host's script reader, runs it via SmokeScriptToEmitters, and returns a stable
// non-(-1) handle (or -1 on failure). The host supplies the file reader because
// asset access is engine/VFS state; if no reader is installed this returns -1
// (the caller then leaves record[+149] == -1, exactly as a missing script does).
// Install a reader with SetSmokeScriptReader before pointing a hook here.
using SmokeScriptReader = bool (*)(const char* path, std::string& outSource,
                                   void* user);
void SetSmokeScriptReader(SmokeScriptReader reader, void* user);
i32  RunSmokeScriptViaVm(const char* scriptPath, void* dummyNode,
                         i32 worldX, i32 worldY, i32 worldZ);

} // namespace guild::render
