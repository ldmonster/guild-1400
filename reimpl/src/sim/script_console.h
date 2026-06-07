#pragma once
// Script-VM long-tail, slice "console" — the remaining big *driver / parser* bodies
// of the .esc engine that the earlier slices (script_import / _import2 / _import3 /
// _import4 / script_vm / script_run / script_compiler) left as forward-declared
// raw-pointer leaves.  These all operate on the byte-exact 2584-byte script context
// reached through the process-global "current context" pointer (dword_62E8A8),
// reusing the kSc* field offsets from script_vm.h.
//
// Faithful 1:1 from gilde.exe pseudocode (32-bit x86, imagebase 0x400000):
//   0x4453a4 VIBE_Script_ConsoleParseLine  — engine cold-init: allocate the five
//                                            engine tables, fill the context table
//                                            with -1, copy the symbol/keyword token
//                                            table into the global token array.
//   0x444774 VIBE_Script_CallFunction      — evaluate a call's argument expressions,
//                                            dispatch the command fn by its argc, and
//                                            (if the command yields) snapshot the args
//                                            into the context's wait slot.
//   0x4450e0 VIBE_Script_Step              — per-context step driver: push the context
//                                            onto the run stack, decide whether it is
//                                            runnable, run pending command / statements,
//                                            then pop back to the previous context.
//   0x4445bc VIBE_Script_AssignVariable    — lex an lvalue token then apply the
//                                            assignment / ++ / -- / typed-store the VM
//                                            opcode encodes.
//   0x4429c4 VIBE_Script_ParseInclude      — `#include "file"`: load + compile the sub
//                                            script and register it in the include slot
//                                            table at ctx+2492.
//   0x4415bc VIBE_Script_LookupInclude     — find an already-loaded include by name in
//                                            the ctx+2492 slot table.
//
// Namespace: guild::sim (same cluster as the other script_*).
//
// SHARED STATE / REUSE.
//   * ScriptEngineState (currentCtx = dword_62E8A8, ownerId = dword_62E8D4) is the
//     library-owned struct from script_import2.cpp — reused via ScriptEngine().
//   * ScriptEngineTables (the five engine tables: ctxTable / cmdTable / scratchB0 /
//     scratchB4 / log ring / event-token table) is the struct from script_import4.cpp,
//     reused via ScriptTables() — ConsoleParseLine populates them; ShutdownEngine
//     (script_import4) frees them.
//   * The run-stack / depth globals Step manipulates (dword_62E8E4 depth,
//     dword_7653CC saved-ctx stack, dword_7653D0 suspend stack, dword_62E8DC last
//     handle, dword_62E8DC result, dword_649D60 active-slot snapshot) are owned here
//     in one library-defined ScriptRunStack struct so the push/pop is byte-exact and
//     self-contained.
//   * LookupInclude uses the real reconstructed guild::util::StrCmp-shaped byte compare
//     via the utilStrCmp hook (default = faithful strcmp); the integration test wires
//     guild::util::StrCmpNoCase as a REAL sibling forwarded into that hook.
//   * CallFunction's "is the blocked command CmdSleep?" identity test compares against
//     the kFnCmdSleep identity token from script_import2.h.
//
// CROSS-MODULE LEAVES.  The lex / eval / report / finish / alloc helpers these bodies
// call (NextToken, EvaluateExpression, ReportError, Finish, LoadFromScriptDir,
// CompileBlock, InvokeCommand, ExecuteStatement, SwitchActiveSlot, AllocFromFreeList,
// AllocDebug) are NOT reconstructed under this raw-pointer ABI, so they route through
// an installable ScriptConsoleHooks struct with INERT DEFAULTS defined in the library
// .cpp (the ScriptCmdHooks / ScriptImport4Hooks pattern).  Tests install their own.
#include "guild/common/types.h"
#include "sim/script_vm.h"
#include "sim/script_import2.h"
#include "sim/script_import4.h"
#include <cstddef>
#include <cstdint>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Extra context-field offsets used by this slice but not yet named in
// script_vm.h.  (kScCursor=152, kScOwner=132, kScRunFlags=164, kScPendingCmd=2524,
// kScCmdBlocked=2528, kScArgScratch=2532, kScStmtMode=2564, kScSceneBlocked=2572,
// kScSceneSlot=2576, kScSceneCtx=2580 already exist.)
// ---------------------------------------------------------------------------
constexpr int kScArgCountBlock = 2492;  // include-slot table base (ParseInclude/LookupInclude)
// The original include-slot table is 8 dword (4-byte) slots at ctx+2492 holding
// loaded sub-script context pointers.  On a 64-bit host a recovered pointer is wider
// than 4 bytes, so we widen the slot STORAGE stride to pointer width (the table still
// holds up to 8 slots) to keep the stored value a real, dereferenceable pointer.
// ParseInclude still RETURNS the original byte offset (index*4: 0,4,..28) so its
// observable result is unchanged; only the storage stride is widened.
constexpr int kIncludeSlotStride = static_cast<int>(sizeof(void*));
constexpr int kIncludeSlotCount  = 8;

// Step reads the scene-context field (ctx+2580, kScSceneCtx) and CallFunction reads
// the +2524 pending-node field as REAL pointers (the original 32-bit dwords widened
// for a 64-bit host).  These are at the tail of the 2584-byte record, so a modeled
// context buffer must carry pointer-width slack past the record end.  Allocate
// contexts as kScriptContextStride + kScContextSlack bytes.
constexpr int kScContextSlack = 16;
constexpr int kScCallCount     = 152;   // CallFunction bumps *(currentCtx+152) (== kScCursor as dword)
constexpr int kScStrScratch    = 2532;  // CallFunction's per-arg wait-slot window (== kScArgScratch)

// Call-node record layout (the parsed call the VM hands CallFunction).  The
// original reaches its fields by raw offset off the node base:
//   +32 argc        (number of argument slots)
//   +36 per-arg type byte (1 int / 2 byte / 6 string / 7 float-ish)
//   +44 command fn pointer (dispatched by argc)
//   +48 return type byte (7 => copy a float result word)
// Modeled as raw-offset access on a u8* so the arithmetic is byte-exact.
// On a 64-bit host the recovered command fn pointer at +44 is 8 bytes wide and
// therefore overlaps the original +48 return-type byte.  We keep +44 for the fn
// pointer (so dispatch can hold a real callable) and relocate the return-type byte
// past it, to +56, in the widened node layout.  argc/argType stay at their original
// offsets (they precede the pointer and do not overlap).
constexpr int kCallArgc    = 32;
constexpr int kCallArgType = 36;
constexpr int kCallFnPtr   = 44;   // 8-byte fn pointer (44..51) on a 64-bit host
constexpr int kCallRetType = 56;   // original +48, relocated past the widened ptr

// ===========================================================================
// Library-owned run-stack / engine scalars (one definition in script_console.cpp).
// The originals are process globals; we own them so Step's push/pop is byte-exact
// and tests can poke them.
// ===========================================================================
struct ScriptRunStack {
    int depth = 0;                 // dword_62E8E4 (run-stack depth, -1 == empty base)
    int lastHandle = 0;            // dword_62E8DC (Step writes -1 on entry)
    int activeSlotSnapshot = 0;    // dword_649D60 (saved active universe slot)
    u8* savedCtx[64] = {0};        // dword_7653CC[] (pushed context bases)
    int suspendSlot[64] = {0};     // dword_7653D0[] (per-depth suspend marker)
};
ScriptRunStack& ScriptRun();
void ResetScriptRun();   // test helper: zero everything.

// ===========================================================================
// The 39-entry symbol/keyword token table ConsoleParseLine copies into the
// global token array.  Each entry is { source ASCII text, destination index }.
// In the original each is an open-coded UTF-16 byte-pair copy from an asc_*/a*
// string into a fixed 16-byte-strided destination slot; we model the destination
// as a flat 16-byte-strided byte array owned by the engine (see ScriptTokenTable).
// ===========================================================================
constexpr int kScriptTokenCount  = 39;
constexpr int kScriptTokenStride = 16;   // bytes per destination slot

// The token strings, in copy order (matches the gilde.exe asc_*/a* sequence).
extern const char* const kScriptTokenText[kScriptTokenCount];

// Engine token table: the destinations the ConsoleParseLine copy loops write to.
// (In the image these are scattered unk_7679xx / byte_7674xx globals; we own one
// contiguous 39 * 16 byte array so the copy is self-contained and inspectable.)
struct ScriptTokenTable {
    char slot[kScriptTokenCount][kScriptTokenStride] = {};
};
ScriptTokenTable& ScriptTokens();
void ResetScriptTokens();

// ===========================================================================
// Cross-module leaves (inert defaults in script_console.cpp; tests install).
// ===========================================================================
//   allocDebug     : VIBE_Memory_AllocDebug @0x438f10 — tagged allocation; returns
//                    a zeroed block of `size` bytes (default: operator new[]).
//   nextToken      : VIBE_Script_NextToken @0x441974 — lex one token into out[].
//   evalExpression : VIBE_Script_EvaluateExpression @0x443ff0 — evaluate the next
//                    expression, returning its int value.
//   reportError    : VIBE_Script_ReportError @0x440f94 (ctx, cursor, msg).
//   finishCtx      : VIBE_Script_Finish @0x443f38 — tear a context down; returns 0.
//   loadFromDir    : VIBE_Script_LoadFromScriptDir @0x4424e0 (ParseInclude).
//   compileBlock   : VIBE_Script_CompileBlock @0x4435d0 (ParseInclude).
//   invokeCommand  : VIBE_Script_InvokeCommand @0x444f4c (Step: pending command).
//   execStatement  : VIBE_Script_ExecuteStatement @0x444bd0 (Step: run statements).
//   switchSlot     : VIBE_Universe_SwitchActiveSlot @0x5b4a24 (Step scene gate).
//   allocFreeList  : VIBE_Memory_AllocFromFreeList @0x5dbe70 (CallFunction string arg).
//   utilStrCmp     : VIBE_Util_StrCmp @0x5d3f10 — byte strcmp (LookupInclude).
// ===========================================================================
struct ScriptConsoleHooks {
    void* (*allocDebug)(int size, const char* tag) = nullptr;
    u8    (*nextToken)(const char* cursor, u8* out) = nullptr;
    i32   (*evalExpression)(int stopOp) = nullptr;
    void  (*reportError)(u8* ctx, u32 cursor, const char* msg) = nullptr;
    i32   (*finishCtx)(u8* ctx) = nullptr;
    u8*   (*loadFromDir)(char* scratch) = nullptr;
    int   (*compileBlock)(u8* ctx) = nullptr;
    i32   (*invokeCommand)(u8* ctx, int arg) = nullptr;
    i32   (*execStatement)(u8* ctx) = nullptr;
    int   (*switchSlot)(int slot, int a, u8* ctx, int sceneBlocked) = nullptr;
    void* (*allocFreeList)(unsigned size) = nullptr;
    int   (*utilStrCmp)(const char* a, const char* b) = nullptr;
};
void SetScriptConsoleHooks(const ScriptConsoleHooks* hooks);
const ScriptConsoleHooks& GetScriptConsoleHooks();

// ===========================================================================
// 0x4453a4 — VIBE_Script_ConsoleParseLine()
// Engine cold-init: AllocDebug the five engine tables into ScriptTables(), fill the
// 128-entry context table's per-context "owner" word (offset +132 == -2456 from the
// table end the original uses) with -1, then copy the 39-entry symbol/keyword token
// table into ScriptTokens(), and zero the event-token header. Returns 1.
// ===========================================================================
i32 ConsoleParseLine();

// ===========================================================================
// 0x444774 — VIBE_Script_CallFunction(callNode@<eax>)
// Evaluate each of the call's argc argument expressions (writing each into the typed
// local arg slot per its +36 type byte), require the trailing ')', dispatch the
// command fn (+44) by argc 0..7 binding the matching number of arg pointers, capture
// the (optionally float) result, and — if the command yielded (stmtMode==1 with a
// blocked slot, or the blocked command IS CmdSleep) — snapshot the arg dwords (and
// deep-copy any string arg) into the context's +2532 wait window and record the node
// at +2524. Returns the call's result word.
//
// Models the call node as a raw u8* (kCall* offsets), the typed arg locals as a small
// caller-supplied scratch, and the command result as the i32 the fn returns. The
// per-arg string scratch is a 6*16-byte-strided local window exactly as the original
// _OWORD v25[48] / 96-byte deep-copy do.
// ===========================================================================
i32 CallFunction(u8* callNode);

// ===========================================================================
// 0x4450e0 — VIBE_Script_Step(ctx@<eax>)
// Push the previous current-context onto the run stack (depth < 32), set ctx current,
// clear the last-handle word; if the context is runnable (owner == -1 OR owner ==
// engine.ownerId, AND no scene-block, AND not suspended, AND has a non-zero call
// counter) run it: optionally switch the active universe slot for the scene, run the
// pending command (InvokeCommand) if +2528 is armed else run statements
// (ExecuteStatement) while stmtMode==2 & runnable, restore the slot. Always pop the
// previous context back. Returns the last sub-call's byte result.
// ===========================================================================
i32 Step(u8* ctx);

// ===========================================================================
// 0x4445bc — VIBE_Script_AssignVariable(lvalueNode@<eax>)
// Lex the assignment operator token (NextToken into a scratch); the token's sub-code
// selects the operation, and the lvalue node's high-nibble type (16*node[0] >> 4)
// selects dword (1) vs byte (2) vs float (7) vs string (6) storage:
//   sub 27 ('=' preceded by an index) : evaluate index, re-lex.
//   sub 2  (plain '=')                : store EvaluateExpression result (typed).
//   sub 3  ('++')                     : increment the typed cell.
//   sub 5  ('--')                     : decrement the typed cell.
// `varBase` supplies *(node+44) — the variable storage base. Returns 0.
// ===========================================================================
i32 AssignVariable(u8* lvalueNode);

// ===========================================================================
// 0x4429c4 — VIBE_Script_ParseInclude(ctx@<eax>)
// Lex the include path token; if it is a string literal (class 7) LoadFromScriptDir +
// CompileBlock it and register the loaded context base in the first free include slot
// (ctx+2492, stride 4, up to 8 slots). On a load failure or a non-string token ->
// ReportError + Finish. Returns the slot offset written, or Finish's result on error.
// ===========================================================================
i32 ParseInclude(u8* ctx);

// ===========================================================================
// 0x4415bc — VIBE_Script_LookupInclude(ctx@<eax>, name@<edx>)
// Scan the ctx+2492 include-slot table (up to 8 slots, stride 4) for a slot whose
// stored context's name matches `name` (utilStrCmp == 0). Returns the matching
// context base, or 0 if none.
// ===========================================================================
i32 LookupInclude(u8* ctx, const char* name);

} // namespace guild::sim
