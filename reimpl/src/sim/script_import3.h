#pragma once
// Script-VM long-tail, slice 3: the command-table *registration* layer and the
// last per-command opcode body the first two slices left untranslated.
//
// Where slice 2 (script_import2.{h,cpp}) holds the per-command "Cmd*" bodies a
// .esc program dispatches to, this slice holds the code that BUILDS the command
// table those bodies are looked up in:
//
//   gilde.exe (32-bit x86, imagebase 0x400000):
//     0x445bc8 VIBE_Script_ImportCommand          — append one command record
//     0x441140 VIBE_Script_AddEventToken          — append one global event token
//     0x43c850 VIBE_Script_RegisterCommands        — register the core commands
//     0x440df0 VIBE_Script_RegisterSoundCommands   — register the sound commands
//     0x440618 VIBE_Script_RegisterObjectCommands  — register the object commands
//     0x43ca0c VIBE_Script_CmdCreateCharacterAtDummy — last untranslated Cmd body
//
// Namespace: guild::sim (same cluster as script_vm / script_import / _import2).
//
// SHARED STATE / REUSE.  The command-table base (dword_62E8AC) and event-token
// table (dword_767944/767948/76794C) are process globals in the original; here
// they are owned by a single library-defined CommandTable / EventTokenTable
// struct so the byte-exact pointer arithmetic is reproduced without a fixed
// image layout.  ImportCommand reuses the already-reconstructed
// FindCommandByName (script_import.cpp) and the command-record offsets from
// script_vm.h; CmdCreateCharacterAtDummy reuses ScriptEngineState and the
// ReportError hook from script_import2.
//
// CROSS-MODULE LEAVES.  CmdCreateCharacterAtDummy calls the transform/character/
// object math helpers (VIBE_Transform_*, VIBE_Character_CreateFromModel,
// VIBE_Object_Set*), none reconstructed in this slice; they route through an
// installable ScriptImport3Hooks struct with INERT DEFAULTS defined in the
// library .cpp (the ScriptCmdHooks / ScriptImportHooks pattern).  The dozens of
// leaf Cmd function pointers the Register* tables install are stored by identity
// only (never called through here) and are exposed as opaque tokens.
#include "guild/common/types.h"
#include "sim/script_vm.h"
#include "sim/script_import2.h"
#include <cstddef>
#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Command table (gilde.exe dword_62E8AC @0x62E8AC: 256 slots * 52 bytes).
// ===========================================================================
// Library-owned storage standing in for the process-global table base, so the
// raw `base + 52*slot + field` arithmetic in ImportCommand/FindCommandByName
// is byte-exact and self-contained.  One definition in script_import3.cpp.
//
// 32-bit vs 64-bit note: the original record holds a 4-BYTE command fn pointer
// at +44 (with the 1-byte kind at +48 immediately after).  A real host pointer
// is 8 bytes on a 64-bit build and would overlap +48, so the fn pointer is kept
// in a parallel per-slot `fnSlots` array (recoverable by identity via
// CommandFn()).  The +44 dword still receives a nonzero occupancy marker so the
// byte-exact free-slot scan in ImportCommand is reproduced.
struct CommandTable {
    u8          bytes[kCommandStride * kCommandCapacity] = {};   // 13312 bytes
    ScriptCmdFn fnSlots[kCommandCapacity] = {};                  // parallel fn ptrs
};
CommandTable& Commands();
// The command fn pointer stored for a record (recovered by slot index from the
// byte record's base offset).  Test/host helper.
ScriptCmdFn CommandFn(const u8* record);
// Reset the table to empty (all bytes zero, all fn slots null). Test helper.
void ResetCommands();

// ===========================================================================
// Event-token (global variable) table — gilde.exe dword_767944/767948/76794C.
// ===========================================================================
// AddEventToken appends a 48-byte record into a growable array:
//   dword_767944 = base ptr   dword_767948 = count   dword_76794C = capacity bytes
// Record (48 bytes), recovered from the writes at 0x4411b3..0x4411d5:
//   +0    (byte) type nibble: (existing & 0xF0) | (typeArg & 0x0F)
//   +1..  (UTF-16) name written at base+1 (2-byte stride, NUL-terminated)
//   +36   (dword) = 1   (kind/"is event token" marker)
//   +40   (dword) = 0
//   +44   (dword) = handler/value (a2)
// The table grows by 16 records (768 bytes) at a time via Alloc/Free hooks.
constexpr int kEventTokenStride = 48;
constexpr int kEventTokenGrowRecords = 16;     // grow chunk (records)
constexpr int kEventTokenGrowBytes = kEventTokenStride * kEventTokenGrowRecords;  // 768

struct EventTokenTable {
    u8*  base     = nullptr;   // dword_767944
    u32  count    = 0;         // dword_767948 (record count)
    u32  capBytes = 0;         // dword_76794C (capacity, bytes)
};
EventTokenTable& EventTokens();
// Free the table and reset the counters. Test helper, not in image.
void ResetEventTokens();

// ===========================================================================
// Cross-module leaves for CmdCreateCharacterAtDummy (inert defaults in .cpp).
// ===========================================================================
// reportError  : reuses the slice-2 ScriptCmdHooks.reportError (see below).
// pointThroughBoneChain : VIBE_Transform_PointThroughBoneChain @0x5c8b38
//      (objBase, objBase+76) -> out world position vec3.
// createFromModel : VIBE_Character_CreateFromModel @0x402d10 -> handle (0 = fail).
// rotateByHierarchy : VIBE_Transform_RotateVectorByHierarchy @0x5c8990.
// angleBetween : VIBE_Math_VectorAngleBetween @0x5ca334 -> float angle.
// setWorldTranslation : VIBE_Object_SetWorldTranslation @0x5af50c (subObj, vec).
// setPosition : VIBE_Object_SetPosition @0x5af38c (subObj, vec3).
// allocDebug / freeDebug : VIBE_Memory_AllocDebug @0x438f10 / FreeDebug @0x43923c.
struct ScriptImport3Hooks {
    void (*pointThroughBoneChain)(const float* a, const float* b, float* out) = nullptr;
    ScriptHandle (*createFromModel)(const char* model, const char** outSlot) = nullptr;
    void (*rotateByHierarchy)(ScriptHandle obj, const float* axis, float* out) = nullptr;
    float (*angleBetween)(const float* a, const float* b) = nullptr;
    void (*setWorldTranslation)(ScriptHandle subObj, const float* vec) = nullptr;
    void (*setPosition)(ScriptHandle subObj, const float* vec) = nullptr;
    // Memory allocator (AddEventToken's table growth). Inert default uses malloc/
    // free so the table can grow without a host; tests may install their own.
    void* (*allocDebug)(std::size_t bytes, const char* tag) = nullptr;
    void  (*freeDebug)(void* p) = nullptr;
};
void SetScriptImport3Hooks(const ScriptImport3Hooks* hooks);
const ScriptImport3Hooks& GetScriptImport3Hooks();

// ===========================================================================
// 0x445bc8 — VIBE_Script_ImportCommand(name, fn, kind, argc, argTypes...)
// ===========================================================================
// Append one command record to the table:
//   * reject (return 0) if strlen(name) > 31 OR the name already exists.
//   * scan for the first free slot (record +44 fn-ptr == 0); reject if >= 256.
//   * write fn@+44, argc@+32, kind@+48, the argc per-arg type bytes @+36+i,
//     then copy `name` into +0 as a 2-byte-stride (UTF-16) NUL-terminated string.
// Returns 1 on success, 0 on rejection.  argTypes supplies `argc` type codes.
i32 ImportCommand(const char* name, ScriptCmdFn fn, u8 kind, int argc,
                  const u8* argTypes);
// Convenience overload taking the type codes inline (variadic in the original).
i32 ImportCommand(const char* name, ScriptCmdFn fn, u8 kind, int argc);

// ===========================================================================
// 0x441140 — VIBE_Script_AddEventToken(name, handler, typeNibble)
// ===========================================================================
// Append a 48-byte global event-token record (grows the table by 768 bytes when
// full).  Writes the name as UTF-16 at +1, type nibble at +0, marker 1 at +36,
// 0 at +40, handler at +44.  Returns the record base pointer (as a handle).
ScriptHandle AddEventToken(const char* name, i32 handler, u8 typeNibble);

// ===========================================================================
// 0x43c850 / 0x440df0 / 0x440618 — the three command-table registrations.
// ===========================================================================
// Each issues the exact ImportCommand / AddEventToken sequence from the image
// (same names, fn-identity tokens, kind, argc and arg-type lists).  Returns the
// result of the final ImportCommand/AddEventToken call (1 on success).
i32 RegisterCommands();
i32 RegisterSoundCommands();
i32 RegisterObjectCommands();

// ===========================================================================
// 0x43ca0c — VIBE_Script_CmdCreateCharacterAtDummy(dummyPtr@<edx>, name@<ebx>)
// ===========================================================================
// Spawn a character at a dummy object's world transform:
//   * if *dummyPtr == 0 -> ReportError("invalid dummy"), return 0.
//   * compute the dummy's world position (PointThroughBoneChain), create the
//     character from the model implied by the dummy; on failure ReportError and
//     return the (failed) handle.
//   * copy the name into the new character (+5) and its sub-object (+52) as
//     UTF-16, compute the spawn yaw (RotateVectorByHierarchy + VectorAngleBetween
//     into a vec3 {pos.x?, yaw, 0}), then SetWorldTranslation(angle) and
//     SetPosition(worldPos) on the sub-object.  Returns the character handle.
ScriptHandle CmdCreateCharacterAtDummy(ScriptHandle* dummyPtr, char** namePtr);

} // namespace guild::sim
