#pragma once
// Script-VM long-tail, slice 2: the per-command ("Cmd*") opcode bodies that the
// registered-command table dispatches to.  These are the C side of the script
// commands a `.esc` program invokes (CreateCharacter, WalkToDummy,
// PlayCharacterAni, Sleep, KillLocalScripts, CallUserFunction, ...).
//
// Faithful 1:1 from gilde.exe pseudocode (32-bit x86, imagebase 0x400000):
//   0x43c650 CmdReturnTrue              0x43c658 CmdReturnFalse
//   0x43c708 CmdSleep                   0x43c790 CmdKillLocalScripts
//   0x43c7ec CallUserFunction           0x43c81c CallUserFunctionExtended
//   0x43c9c0 CmdCreateCharacter         0x43cb28 CmdWalkToDummy
//   0x43cbc8 CmdWalkToDummyRotate       0x43cc68 CmdWalkToDummyRotateVerified
//   0x43cd18 CmdWalkToDummyVerified     0x43cdc8 CmdPlayCharacterAni
//   0x43ce98 CmdPlayCharacterAniSound   0x43cfb0 CmdPlayCharacterAniScript
//
// Namespace: guild::sim (same cluster as script_vm.{h,cpp} / script_import).
//
// SHARED ENGINE STATE.  Every command body reaches three process globals from
// the original image:
//   dword_62E8A8  "current" script context base pointer  (currentCtx)
//   dword_62E8CC  currently-executing command record ptr (execCmd); its +44 is
//                 the command's fn pointer — used for the YIELD/RESUME check:
//                 when a body is re-entered as the still-executing command it
//                 only re-arms the wait slot and returns 0 ("still blocked").
//   dword_62EB38  global game tick                        (gameTick)
//   dword_62E8D4  owning scene/caller id                  (ownerId)
// To reproduce the byte-exact pointer arithmetic without a fixed image layout
// these live in one library-owned ScriptEngineState struct (one definition, in
// script_import2.cpp); tests poke it directly.  The command bodies index into
// the 2584-byte context via the kSc* offsets already defined in script_vm.h.
//
// CROSS-MODULE LEAVES.  The character / action / memory helpers a command body
// calls are not reconstructed in this slice (and some that are live behind
// signatures we must not couple to), so they are routed through an installable
// ScriptCmdHooks struct with INERT DEFAULTS defined in this library .cpp — the
// CutsceneMiscHooks / ScriptImportHooks pattern.  StrLen / StrNCopyPad delegate
// to the real reconstructed guild::util primitives when present.
#include "guild/common/types.h"
#include "sim/script_vm.h"
#include <cstddef>
#include <cstdint>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Context offsets used by the command bodies but not yet named in script_vm.h.
//   +2528 kScCmdBlocked  (re-arm/wait slot)   [already in script_vm.h]
//   +2564 kScStmtMode    (statement mode byte) [already in script_vm.h]
//   +2568 sleep wake-tick accumulator
// ---------------------------------------------------------------------------
constexpr int kScSleepWake = 2568;   // CmdSleep wake-time accumulator

// Command record layout (from script_vm.h: stride 52). +44 is the fn ptr the
// resume check compares against. Modeled as a small struct so execCmd->fn is
// pointer-comparable to the command body addresses.
using ScriptCmdFn = void*;   // opaque fn-identity token for the resume compare
struct ScriptCmdRecord {
    u8   _pad[44];           // +0..+43  name / argc / arg types (see script_vm.h)
    ScriptCmdFn fn;          // +44      command function pointer (identity key)
};

// ---------------------------------------------------------------------------
// Library-owned shared engine state (one definition in script_import2.cpp).
// ---------------------------------------------------------------------------
struct ScriptEngineState {
    u8*              currentCtx = nullptr;  // dword_62E8A8
    ScriptCmdRecord* execCmd    = nullptr;  // dword_62E8CC
    u32              gameTick   = 0;        // dword_62EB38
    i32              ownerId    = 0;        // dword_62E8D4
    i32              sceneId    = 0;        // dword_649D60 (RunWithArgs writes it
                                            // into ctx+2576 / kScSceneSlot)
};
ScriptEngineState& ScriptEngine();

// Identity tokens for the command bodies (used as the +44 fn-ptr values the
// resume check compares; the originals compare the literal function address).
// Stable addresses; never called through.
extern const ScriptCmdFn kFnCmdSleep;
extern const ScriptCmdFn kFnCmdWalkToDummy;
extern const ScriptCmdFn kFnCmdWalkToDummyRotate;
extern const ScriptCmdFn kFnCmdWalkToDummyRotateVerified;
extern const ScriptCmdFn kFnCmdWalkToDummyVerified;
extern const ScriptCmdFn kFnCmdPlayCharacterAni;
extern const ScriptCmdFn kFnCmdPlayCharacterAniSound;
extern const ScriptCmdFn kFnCmdPlayCharacterAniScript;

// ---------------------------------------------------------------------------
// Cross-module leaves (inert defaults in script_import2.cpp; tests install).
//   reportError  : VIBE_Script_ReportError  @0x440f94 (ctx, cursor, msg)
//   createChar   : VIBE_Character_CreateFromModel @0x402d10 -> handle (0 = fail)
//   isValidPtr   : VIBE_Memory_IsValidPointer @0x4391f0 (nonzero = valid)
//   queueWalk    : VIBE_CharAction_QueueWalkToTarget @0x40b6a8
//   cmdHandler   : VIBE_Command_Handler @0x40b888 (rotate-walk handler)
//   dummyBlocked : VIBE_Character_CmdDummyBlocked @0x43dab4 (nonzero = blocked)
//   createSound  : VIBE_Character_CreateSoundAction @0x405670
//   insertAction : VIBE_CharAction_InsertActionArgs @0x40c2f0 -> record (0=fail)
//   The two animation callbacks (@0x43ce4c / @0x43cf84) are only passed by
//   identity to insertAction; we pass opaque tokens.
// StrLen / StrNCopyPad delegate to guild::util when available (see .cpp).
// ---------------------------------------------------------------------------
// Operand values that the original image treats as OBJECT POINTERS (the
// character / dummy the command body dereferences, e.g. `*(char + 296)`). In
// the 32-bit image these are dword pointers; modeled pointer-width here so the
// field accesses are portable on 64-bit. (Operands that are plain integers —
// sound ids, durations — stay i32.)
using ScriptHandle = std::intptr_t;

struct ScriptCmdHooks {
    void (*reportError)(u8* ctx, u32 cursor, const char* msg) = nullptr;
    ScriptHandle (*createChar)(const char* model, i32* outFields) = nullptr;
    int  (*isValidPtr)(ScriptHandle p) = nullptr;
    void (*queueWalk)(ScriptHandle character, ScriptHandle target, ScriptHandle extra) = nullptr;
    void (*cmdHandler)(ScriptHandle character, ScriptHandle target) = nullptr;
    int  (*dummyBlocked)(ScriptHandle* dummy, int extra) = nullptr;
    void (*createSound)(ScriptHandle character, ScriptCmdRecord* cmd, int sound) = nullptr;
    // Returns the action-record base (a pointer in the original image; modeled
    // here as a real pointer so the +144/+240/+380 field writes are portable on
    // 64-bit). nullptr on failure.
    void* (*insertAction)(ScriptHandle character, void* callback, i64 arg, int extra) = nullptr;
};
void SetScriptCmdHooks(const ScriptCmdHooks* hooks);
const ScriptCmdHooks& GetScriptCmdHooks();

// Opaque animation-callback identity tokens (passed to insertAction only).
extern void* const kCbPlayAnimationSound;    // VIBE_Character_PlayAnimationSoundCallback  @0x43ce4c
extern void* const kCbPlayAnimationScript;   // VIBE_Character_PlayAnimationScriptCallback @0x43cf84

// ===========================================================================
// Command bodies.  Each takes its operand pointers exactly as the __usercall
// register prototype did; the implicit "current context" is ScriptEngine().
// ===========================================================================

// (0x43c650 CmdReturnTrue / 0x43c658 CmdReturnFalse are already translated in
//  script_import.cpp — reused, not re-defined here.)

// 0x43c708 — VIBE_Script_CmdSleep(durationPtr@<eax>).
// Resume check on execCmd: if already the sleeping command, re-arm and return 0
// once the wake tick is reached. Otherwise arm the wait and snapshot the tick.
//   *durationPtr == -1 (sleep forever) OR wakeAccum + dur/14 > gameTick: re-arm.
i32 CmdSleep(const i32* durationPtr);

// 0x43c790 — VIBE_Script_CmdKillLocalScripts(ctxTableBase).
// Finishes every other runnable context owned by the same scene as currentCtx.
// (ctxTableBase = dword_62E8A4; finish callback routed through ScriptCmdHooks
//  via VIBE_Script_Finish — supplied by the caller as a function pointer.)
i32 CmdKillLocalScripts(u8* ctxTableBase, void (*finish)(u8* ctx));

// 0x43c7ec — VIBE_Script_CallUserFunction(fnSlot@<eax>, arg@<ecx>).
i32 CallUserFunction(void (**fnSlot)(int), int arg);
// 0x43c81c — VIBE_Script_CallUserFunctionExtended(fnSlot@<eax>).
i32 CallUserFunctionExtended(void (**fnSlot)(void));

// 0x43c9c0 — VIBE_Script_CmdCreateCharacter(modelPtr@<eax>, outFields@<ecx>).
ScriptHandle CmdCreateCharacter(const char** modelPtr, i32* outFields);

// 0x43cb28 — VIBE_Script_CmdWalkToDummy(charPtr@<eax>, dummyPtr@<edx>).
i32 CmdWalkToDummy(ScriptHandle* charPtr, ScriptHandle* dummyPtr);
// 0x43cbc8 — VIBE_Script_CmdWalkToDummyRotate(charPtr@<eax>, dummyPtr@<edx>).
i32 CmdWalkToDummyRotate(ScriptHandle* charPtr, ScriptHandle* dummyPtr);
// 0x43cc68 — VIBE_Script_CmdWalkToDummyRotateVerified(charPtr, dummyPtr, extra).
i32 CmdWalkToDummyRotateVerified(ScriptHandle* charPtr, ScriptHandle* dummyPtr, int extra);
// 0x43cd18 — VIBE_Script_CmdWalkToDummyVerified(charPtr, dummyPtr, extra).
i32 CmdWalkToDummyVerified(ScriptHandle* charPtr, ScriptHandle* dummyPtr, int extra);

// 0x43cdc8 — VIBE_Script_CmdPlayCharacterAni(charPtr@<eax>, soundPtr@<ebx>).
i32 CmdPlayCharacterAni(ScriptHandle* charPtr, i32* soundPtr);

// 0x43ce98 — VIBE_Script_CmdPlayCharacterAniSound(char, aniName, flags, sound, sndName).
// Copies two UTF-16 strings (aniName -> rec+240, sndName -> rec+304) verbatim.
i32 CmdPlayCharacterAniSound(ScriptHandle* charPtr, char** aniName, u8* flagsPtr,
                             i32* soundPtr, char** sndName);

// 0x43cfb0 — VIBE_Script_CmdPlayCharacterAniScript(char, flags, sound, scriptName, extraName).
i32 CmdPlayCharacterAniScript(ScriptHandle* charPtr, u8* flagsPtr, i32* soundPtr,
                              char** scriptName, char** extraName);

} // namespace guild::sim
