#pragma once
#include "guild/common/types.h"
#include "sim/script_vm.h"      // ScriptHost (the InvokeCommand ABI)
#include "sim/script_run.h"     // LoadedScript / LoadScriptFrom* / RunMain
#include <array>
#include <string>
#include <vector>
#include <cstdint>

// =============================================================================
// guild::sim — the EFFECT-SCRIPT (.esc) command set + effect VM driver.
//
// The .esc files (e.g. effekte\Schornstein_dunkel.esc, the chimney smoke) are
// C-like SOURCE TEXT compiled and run by the engine's tree-walking script VM
// (already reconstructed 1:1 in sim/script_{lexer,compiler,symbols,run,vm}). The
// VM dispatches each registered COMMAND by name through ScriptHost::invokeCommand.
//
// This module reconstructs, 1:1, the EFFECT/EMITTER COMMAND SET those scripts
// invoke — the part the VM core deliberately left as a host hook (rule 8). The
// commands and their handlers are registered by VIBE_Script_RegisterObjectCommands
// @0x440618 / VIBE_Script_RegisterCommands @0x43c850; each handler validates the
// emitter handle, scales its integer args by a recovered fixed constant, and
// writes them into the emitter parameter block at exact byte offsets.
//
// WHAT IS RECOVERED BYTE-FOR-BYTE (gilde.exe, imagebase 0x400000)
// ---------------------------------------------------------------------------
//   0x440618  VIBE_Script_RegisterObjectCommands — the full effect command table
//             (name -> handler -> (resultClass, argc, argType[argc])).
//   0x43fd24  VIBE_Particle_CreateEmitter        — CreateEmitter(kind, amplitude,
//             texSlot, owner, "tex", userType, trigger): fill the default emitter
//             template, allocate a system, return its handle.
//   0x4405c4  VIBE_Particle_SetPos      (SetParticlePos): pos = (arg1,arg2,arg3)
//                                        (NO reorder — disasm register trace; the
//                                        Hex-Rays x,z,y read was a mislabel).
//   0x43fe9c  VIBE_Emitter_SetAmplitude : f[16..19]=arg*0.01, f[44]=arg4*0.01
//   0x43ff30  VIBE_Emitter_SetPhasespeed: f[20..23]=arg*0.01, f[45]=arg4*0.01
//   0x43ffc4  VIBE_Emitter_SetDirection : f[27..29]=arg*0.01
//   0x44002c  VIBE_Emitter_SetSize      : f[24..26]=arg (NO scale)
//   0x4400d4  VIBE_Emitter_SetAcceleration: f[36..38]=arg*0.001
//   0x440234  VIBE_Emitter_SetTimeAndAlpha: u32[+188/192/196]=arg1/2/3, f[+184]=arg4
//   0x44028c  VIBE_Emitter_SetColor     : b[+202]=r, b[+201]=g, b[+200]=b, b[+203]=a
//                                        (note: BGR write order — see below).
//   0x43fe68  VIBE_Particle_KillEmitter : free the emitter (handle invalidated).
//   0x43c708  VIBE_Script_CmdSleep      : Sleep(ms) — yield the script context.
//
// SCALE CONSTANTS (get_bytes, bit-exact)
// ---------------------------------------------------------------------------
//   flt_617748 / flt_617774 / flt_6177A0 = 0x3c23d70a = 0.0099999998  (~0.01)
//   flt_617820                           = 0x3a83126f = 0.00099999997 (~0.001)
//
// THE BY-POINTER ARG ABI (recovered from the handler register signatures)
// ---------------------------------------------------------------------------
// VIBE_Script_InvokeCommand passes the evaluated args by POINTER in this fixed
// order: a1@eax = arg0 (the emitter handle), a2@edx = arg1, a3@ebx = arg2,
// ecx = arg3, then the remaining args on the stack (a4, a5, ...). The handlers
// therefore read args[0]=handle, args[1..] = the parameters in source order. We
// reproduce the exact per-command arg->field mapping below.
//
// EMITTER PARAMETER BLOCK (the fields the handlers touch)
// ---------------------------------------------------------------------------
// The handlers write into the emitter object the CreateEmitter template/system
// represents. The float-indexed fields (v6[N] = N*4 bytes) and the byte/dword
// offsets are reproduced verbatim. We model the block as EffectEmitter with the
// touched fields at their true offsets; untouched bytes are zero (memset by
// CreateEmitter). This is the OBSERVABLE state a smoke/effect script produces.
// =============================================================================
namespace guild::sim {

// Recovered fixed-point scale constants (bit-exact, see header note).
constexpr float kEmitterAmpScale   = 0.0099999998f;  // 0x3c23d70a  flt_617748
constexpr float kEmitterPhaseScale = 0.0099999998f;  // 0x3c23d70a  flt_617774
constexpr float kEmitterDirScale   = 0.0099999998f;  // 0x3c23d70a  flt_6177A0
constexpr float kEmitterAccelScale = 0.00099999997f; // 0x3a83126f  flt_617820

// ---------------------------------------------------------------------------
// The emitter parameter block. Fields placed at their TRUE byte offsets so a
// reinterpret over the raw bytes matches the original writes exactly. Only the
// fields the effect command handlers touch are named; the rest are zero padding
// (CreateEmitter memset's 0xA4 bytes of the template before SpawnSystemByType).
//
// Float index N in a handler (v6[N]) == byte offset 4*N.
//   [16..19] amplitude xyzw  (+64..+76)   SetEmitterAmplitude args1..4
//   [44]     amplitude.spread (+176)      SetEmitterAmplitude arg5
//   [20..23] phasespeed xyzw (+80..+92)   SetEmitterPhasespeed args1..4
//   [45]     phasespeed.spread(+180)      SetEmitterPhasespeed arg5
//   [24..26] size xyz        (+96..+104)  SetEmitterSize args1..3
//   [27..29] direction xyz   (+108..+116) SetEmitterDirection args1..3
//   [36..38] acceleration xyz(+144..+152) SetEmitterAcceleration args1..3
//   +188/192/196(u32) time = args1/2/3, +184(f) life-base = arg4  SetEmitterTimeAndAlpha
//   +200/201/202/203(u8) colour b,g,r,a               SetEmitterColor
// ---------------------------------------------------------------------------
struct EffectEmitter {
    // --- CreateEmitter inputs (kept for observability / the spawn handoff) ---
    bool        alive   = false;  // a valid (allocated, not killed) emitter
    int         kind    = 0;      // arg0 of CreateEmitter (system kind)
    int         amplitudeArg = 0; // arg1 (life amplitude)
    int         texSlot = 0;      // arg2
    int         owner   = 0;      // arg3
    std::string texName;          // arg4 (the particle texture, e.g. "rauch")
    int         userType = 0;     // arg5
    int         trigger  = 0;     // arg6

    // --- position (SetParticlePos) ------------------------------------------
    float       posX = 0.0f, posY = 0.0f, posZ = 0.0f;

    // --- the raw float/byte/u32 parameter image (offsets above) -------------
    // We carry the touched fields explicitly with their semantic names; the byte
    // image (rawImage) below is the authoritative 0xA4-byte block tests pin.
    std::array<float, 0x29> f{};   // f[0..40] (covers indices 16..38)
    float ampSpread = 0.0f;        // f[44]  (+176)
    float phaseSpread = 0.0f;      // f[45]  (+180)
    float lifeBase = 0.0f;         // +184
    uint32_t time0 = 0, time1 = 0, time2 = 0; // +188,+192,+196
    uint8_t  colB = 0, colG = 0, colR = 0, colA = 0; // +200,+201,+202,+203
};

// ---------------------------------------------------------------------------
// The effect VM context: the emitter table the commands operate on, plus the
// observable command log (for golden tests + the smoke handoff). One context per
// running effect script (the smoke script allocates emitter[0] and emitter[1]).
// ---------------------------------------------------------------------------
struct EffectVm {
    std::vector<EffectEmitter> emitters;   // handle == 1-based index into this
    // The (x,y,z) the script's main(x,y,z) was entered with — SetParticlePos
    // writes these; the smoke path passes the chimney dummy world position.
    int argX = 0, argY = 0, argZ = 0;
    // Sleep request: SetParticlePos / Sleep(-1) leaves the script resident; the
    // smoke path stores the handle and lets the per-frame stepper own it.
    bool sleeping = false;
    int  sleepMs  = 0;
    int  killCount = 0;   // KillEmitter invocations (the exit() cleanup)

    // gilde.exe 0x43fd24 — CreateEmitter: allocate a new emitter, return its
    // 1-based handle. Never returns 0 (a real allocation always succeeds here;
    // the original's failure path is the engine's out-of-memory case).
    i32 CreateEmitter(int kind, int amplitude, int texSlot, int owner,
                      const std::string& texName, int userType, int trigger);

    // Resolve a handle to an emitter, or nullptr (the VIBE_Memory_IsValidPointer
    // guard the handlers run before writing).
    EffectEmitter* Resolve(i32 handle);

    // The command dispatcher: invoke the named effect command with its evaluated
    // args (args[0] == the emitter handle for the SetEmitter*/SetParticlePos/
    // KillEmitter family). Returns the command result (CreateEmitter -> handle;
    // others -> 0, matching the handlers). Unknown commands return 0 (the VM's
    // host-hook default).
    i32 Invoke(const std::string& name, std::vector<i32>& args);
};

// ---------------------------------------------------------------------------
// The registered EFFECT command name set (the order the engine registers them,
// from VIBE_Script_RegisterCommands @0x43c850 then RegisterObjectCommands
// @0x440618). Passed to the .esc loader so the lexer classifies these tokens as
// commands (kTokFuncCall) rather than unknowns.
// ---------------------------------------------------------------------------
const std::vector<std::string>& EffectCommandNames();

// ---------------------------------------------------------------------------
// Build a ScriptHost whose invokeCommand routes into `vm`. callUserFunction is
// inert (effect scripts call only registered commands). This is the bridge from
// the generic VM core to the reconstructed effect command set.
// ---------------------------------------------------------------------------
ScriptHost MakeEffectHost(EffectVm& vm);

// ---------------------------------------------------------------------------
// Run a loaded effect script's `main(x,y,z)` over a fresh EffectVm, seeding the
// entry args (the smoke path passes the chimney world position). Returns the VM
// with its emitter table populated by the script body. `ls` must be a compiled
// script (LoadScriptFromSource / LoadScriptFromVfs with EffectCommandNames()).
// ---------------------------------------------------------------------------
EffectVm RunEffectScript(LoadedScript& ls, int x, int y, int z,
                         int stepBudget = 100000);

// Convenience: load `source` as an effect script and run its main(x,y,z).
// `ok` (out, may be null) reports load+compile success. Returns the EffectVm.
EffectVm RunEffectScriptSource(const std::string& name, const std::string& source,
                               int x, int y, int z, bool* ok = nullptr);

// Convenience: load `relName` from the VFS (effect command set, optional script-
// dir prefix) and run its main(x,y,z). `ok` (out) reports success.
EffectVm RunEffectScriptFromVfs(const char* relName, int x, int y, int z,
                                bool addPrefix, bool* ok);

} // namespace guild::sim
