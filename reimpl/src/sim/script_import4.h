#pragma once
// Script-VM long-tail, slice 4: the last untranslated runner / control-flow /
// teardown bodies of the .esc engine.  Slices 1-3 covered the .esc binary token
// parser, the per-command Cmd* opcode bodies and the command-table registration
// layer.  This slice closes out the per-context *driver* functions the VM core
// (script_vm / script_run / script_compiler) left as forward-declared leaves:
//
//   gilde.exe (32-bit x86, imagebase 0x400000):
//     0x442a98 VIBE_Script_SetBreakFlag           — ctx +2564 = 1
//     0x442aa8 VIBE_Script_SetContinueFlag        — ctx +2564 = 2
//     0x442ab8 VIBE_Script_ClearReturnFlag        — ctx +2564 = 0
//     0x5e3d2c VIBE_Script_ReadSkipValue          — read+discard one .esc dword
//     0x445cfc VIBE_Script_RunByHandle            — load .esc by name + run main
//     0x445d38 VIBE_Script_GetRunByHandlePtr      — &RunByHandle (fn-ptr getter)
//     0x4ba284 VIBE_Script_FindActiveByHandle     — pump frame-loop until idle
//     0x43d0bc VIBE_Script_LoadRunAndStoreResult  — load+run a sub-object script
//     0x487098 VIBE_Script_RunWaitLoop            — block the game loop on a script
//     0x445288 VIBE_Script_ShutdownEngine         — free all tables + contexts
//     0x44250c VIBE_Script_ProcessStringLiteral   — copy a "…" literal to scratch
//     0x4413d0 VIBE_Script_ParseDeclaration       — parse a `type name;` decl
//     0x443084 VIBE_Script_HandleExitKeyword      — `exit` / pop the scope frame
//     0x443c88 VIBE_Script_ParseAndRunCall        — split "name a b c" + dispatch
//     0x443a90 VIBE_Script_RunWithArgs            — bind argc args + enter `main`
//
// Namespace: guild::sim (same cluster as script_vm / script_import / _import2 /
// _import3).
//
// SHARED STATE / REUSE.
//   * ScriptEngineState (dword_62E8A8 currentCtx, dword_62E8D4 ownerId, …) is the
//     library-owned struct from script_import2.cpp — reused via ScriptEngine().
//   * FindByHandle (gilde.exe 0x442174) is the already-reconstructed raw-pointer
//     sibling in script_import.cpp; FindActiveByHandle / RunWaitLoop CALL IT
//     directly (no hook, no re-definition) — this is the live wiring.
//   * The case-sensitive name compare in HandleExitKeyword routes through a
//     `strCmp` hook so a test can wire the real guild::util sibling.
//
// CROSS-MODULE LEAVES.  The compile/lex/run/destroy/alloc helpers these drivers
// call (VIBE_Script_CompileBlock, LookupFunction, EnterFunction, NextToken,
// DeclareLocal, ReportError, RunMain, LoadFromScriptDir, DestroyContext,
// VIBE_Memory_FreeDebug, VIBE_GameLogic_RunFrameLoop, VIBE_Bio_ReadDwordSwapArgs,
// VIBE_Text_StrtokWhitespace, VIBE_Util_ParseInt, VIBE_Util_StrNCopyPad) are NOT
// reconstructed under this raw-pointer ABI, so they are routed through an
// installable ScriptImport4Hooks struct with INERT DEFAULTS defined in the
// library .cpp (the ScriptCmdHooks / ScriptImportHooks pattern).  Tests install
// their own.
#include "guild/common/types.h"
#include "sim/script_vm.h"
#include "sim/script_import2.h"
#include <cstddef>
#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Engine table bases.  The originals use process globals (dword_62E8A4 context
// table, dword_62E8AC command table, the three scratch buffers and the event
// token table).  ShutdownEngine frees them all; rather than reference a fixed
// image layout we own them in one library-defined struct so the iteration is
// byte-exact and self-contained.  The context-table base is also what the
// FindByHandle-driven runners scan.
// ===========================================================================
struct ScriptEngineTables {
    u8*  ctxTable   = nullptr;  // dword_62E8A4 (128 * 2584)
    u8*  cmdTable   = nullptr;  // dword_62E8AC (256 * 52)
    void* scratchB0 = nullptr;  // dword_62E8B0 (arg buffer)
    void* scratchB4 = nullptr;  // dword_62E8B4
    void* eventToks = nullptr;  // dword_767944 (event token table base)
    void* logRing   = nullptr;  // dword_767950 ("Run script: %s" ring)
    i32   runResult = 0;        // dword_62E8DC (RunByHandle's last result)
};
ScriptEngineTables& ScriptTables();
void ResetScriptTables();   // test helper: zero everything.

// ===========================================================================
// Cross-module leaves (inert defaults in script_import4.cpp; tests install).
// ===========================================================================
//   strCmp        : VIBE_Util_StrCmp @0x5d3f10 — faithful strcmp (0 == equal).
//                   Default is a faithful byte strcmp; a test wires the real
//                   guild::util sibling.  Used by HandleExitKeyword.
//   readDword     : VIBE_Bio_ReadDwordSwapArgs @0x5dc8b0 — read one dword from a
//                   stream into *out; returns nonzero on success. (ReadSkipValue)
//   loadFromDir   : VIBE_Script_LoadFromScriptDir @0x4424e0 — resolve+load a
//                   .esc by relative name; returns its context base (0 on fail).
//   loadScript    : VIBE_Script_LoadScript @0x4421f0 — load from an absolute path.
//   runMain       : VIBE_Script_RunMain @0x44396c — compile + enter `main`.
//   compileBlock  : VIBE_Script_CompileBlock @0x4435d0 (RunWithArgs).
//   lookupFunction: VIBE_Script_LookupFunction @0x4415f8 -> func record (0=none).
//   enterFunction : VIBE_Script_EnterFunction @0x4431dc.
//   nextToken     : VIBE_Script_NextToken @0x441974 — lex one token into out[].
//   reportError   : VIBE_Script_ReportError @0x440f94.
//   finishCtx     : VIBE_Script_Finish @0x443f38 — tear a context down.
//   declareLocal  : VIBE_Script_DeclareLocal @0x441280.
//   destroyCtx    : VIBE_Script_DestroyContext @0x445a28.
//   freeDebug     : VIBE_Memory_FreeDebug @0x43923c.
//   runFrameLoop  : VIBE_GameLogic_RunFrameLoop @0x4c09a0 — pump one frame;
//                   returns nonzero while the loop should keep running.
//   strNCopyPad   : VIBE_Util_StrNCopyPad @0x5d9360 — copy n bytes (NUL pad).
//   strtok        : VIBE_Text_StrtokWhitespace @0x5e9cd0 — strtok over " \t".
//   parseInt      : VIBE_Util_ParseInt @0x5dc070 — atoi-style signed parse.
struct ScriptImport4Hooks {
    int   (*strCmp)(const char* a, const char* b) = nullptr;
    int   (*readDword)(int stream, u8* out) = nullptr;
    u8*   (*loadFromDir)(const char* relName) = nullptr;
    u8*   (*loadScript)(const char* path) = nullptr;
    void  (*runMain)(u8* ctx) = nullptr;
    int   (*compileBlock)(u8* ctx) = nullptr;
    u8*   (*lookupFunction)(u8* ctx, const char* name) = nullptr;
    void  (*enterFunction)(u8* ctx, u8* funcRec) = nullptr;
    u8    (*nextToken)(const char* cursor, u8* out) = nullptr;
    void  (*reportError)(u8* ctx, u32 cursor, const char* msg) = nullptr;
    void  (*finishCtx)(u8* ctx) = nullptr;
    int   (*declareLocal)(u8* ctx, u8 type, const char* name, int init) = nullptr;
    void  (*destroyCtx)(u8* ctx) = nullptr;
    void  (*freeDebug)(void* p) = nullptr;
    int   (*runFrameLoop)(int a, int b) = nullptr;
    void  (*strNCopyPad)(char* dst, const char* src, int n) = nullptr;
    char* (*strtok)(char* s, int unused) = nullptr;
    int   (*parseInt)(const char* s) = nullptr;
};
void SetScriptImport4Hooks(const ScriptImport4Hooks* hooks);
const ScriptImport4Hooks& GetScriptImport4Hooks();

// ===========================================================================
// Trivial statement-mode flag setters (gilde.exe 0x442a98/0x442aa8/0x442ab8).
// Each writes ScriptEngine().currentCtx[+2564] and returns the context base.
// (Original returns dword_62E8A8 in eax; modeled as the same pointer.)
// ===========================================================================
u8* SetBreakFlag();      // ctx +2564 = 1
u8* SetContinueFlag();   // ctx +2564 = 2
u8* ClearReturnFlag();   // ctx +2564 = 0

// ===========================================================================
// 0x5e3d2c — VIBE_Script_ReadSkipValue(stream@<eax>)
// Read one big-endian dword from the stream into a throwaway local and return
// the reader's status (nonzero on success). Used to skip an unwanted field.
// ===========================================================================
i32 ReadSkipValue(int stream);

// ===========================================================================
// 0x445cfc — VIBE_Script_RunByHandle(name@<ebx>)
// Resolve+load `name` from the script dir; if loaded, RunMain it and stash its
// result word (record +128, i.e. *(ctx+32 dwords)) into dword_62E8DC; else -1.
// Returns the stashed value.
// ===========================================================================
i32 RunByHandle(const char* name);

// 0x445d38 — VIBE_Script_GetRunByHandlePtr: returns &RunByHandle.
i32 (*GetRunByHandlePtr())(const char*);

// ===========================================================================
// 0x4ba284 — VIBE_Script_FindActiveByHandle(handle@<eax>)
// Repeatedly FindByHandle(handle); while the found context is runnable
// (+164 bit0), pump the game frame loop. Returns the context base once it is no
// longer runnable (or null). REUSES the real FindByHandle sibling.
// ===========================================================================
u8* FindActiveByHandle(u8* ctxTableBase, i32 handle);

// ===========================================================================
// 0x43d0bc — VIBE_Script_LoadRunAndStoreResult(subObj@<eax>)
// Load the .esc named at subObj+144; if loaded, RunWithArgs(it, 1, subObj+384),
// then write its resulting context (+380 of the run record) back to subObj+132.
// Returns the loaded context base (null if the load failed).
// ===========================================================================
u8* LoadRunAndStoreResult(u8* subObj);

// ===========================================================================
// 0x487098 — VIBE_Script_RunWaitLoop(handle@<edi>)
// If the "no-loop" guard is clear, pump the frame loop while it keeps running
// AND no abort flag is set AND the script's context is still alive; when the
// loop ends, Finish() any surviving context and reset the mouse state.
// Modeled with explicit guard/abort flags (the originals are process globals).
// REUSES the real FindByHandle sibling.
// ===========================================================================
struct WaitLoopGuards {
    int  noLoopGuard = 0;   // dword_6315BC (nonzero -> do nothing)
    int  abortFlag   = 0;   // dword_672230
    u8   escFlag     = 0;   // byte_67225C
};
void RunWaitLoop(u8* ctxTableBase, i32 handle, WaitLoopGuards& g,
                 void (*resetMouse)());

// ===========================================================================
// 0x445288 — VIBE_Script_ShutdownEngine()
// DestroyContext every in-use context (+0 nonzero), clear every command record
// (+0 = 0), then FreeDebug the event-token table, command table, the two scratch
// buffers, the log ring and the context table; null the pointers. Returns the
// last FreeDebug result (modeled as 0). Operates on ScriptTables().
// ===========================================================================
i32 ShutdownEngine();

// ===========================================================================
// 0x44250c — VIBE_Script_ProcessStringLiteral(ctx@<eax>, src@<edx>, dst@<ebx>)
// Find the closing '"' and the next ';' in `src` (each skipping over its bytes,
// nulling the cursor if the terminator is hit first). If the '"' is missing OR
// appears after the ';' -> ReportError + return null. Else copy [src, dquote)
// into dst (NUL-pad), NUL-terminate, and return src past the closing '"' (+1).
// ===========================================================================
const char* ProcessStringLiteral(u8* ctx, const char* src, char* dst);

// ===========================================================================
// 0x4413d0 — VIBE_Script_ParseDeclaration(ctx@<eax>)
// Lex the declared symbol name token; reject (ReportError + Finish) a duplicate
// (class 2 with a bound slot) or unknown (non-class-2) symbol. Otherwise copy the
// name (UTF-16, 2-byte stride) to a scratch, optionally read a `= <int>` init
// (class-1 sub 27 then a class-5 literal), and DeclareLocal(ctx, …, name, init).
// Returns DeclareLocal's result, or 0 on a syntax error.
// ===========================================================================
i32 ParseDeclaration(u8* ctx);

// ===========================================================================
// 0x443084 — VIBE_Script_HandleExitKeyword(ctx@<eax>)
// If the current scope frame's first symbol is "exit" -> return 0 (no-op). Else
// walk up to 15 scope frames (144 bytes each) looking for an active one (+168);
// pop it: clear the 32-byte scope locals window, restore the source cursor from
// the parent frame, and clear the popped frame's +168/+172. Returns 1 if a
// parent frame remains (jumped to it), 0 if the outermost frame was popped.
// ===========================================================================
i32 HandleExitKeyword(u8* ctx);

// ===========================================================================
// 0x443c88 — VIBE_Script_ParseAndRunCall(ctx@<eax>, argLine@<edx>, sep@<ecx>)
// Copy the (UTF-16) argument line, strtok it into up-to-7 fields; for each field
// whose first char is a digit (ctype 0x20 bit on (c+1)) ParseInt it, else keep
// the pointer; then dispatch: 0 args -> RunMain, 1..7 -> RunWithArgs(ctx, n,…).
// Returns the runner's byte result.
// ===========================================================================
u8 ParseAndRunCall(u8* ctx, const char* argLine, int sep);

// ===========================================================================
// 0x443a90 — VIBE_Script_RunWithArgs(ctx, argc, firstArg)
// CompileBlock(ctx); LookupFunction("main"); if its arg count != argc ->
// ReportError + Finish (count mismatch) / ReportError (no main). Bind each of the
// argc args by its declared per-arg type (6 = copy UTF-16 string, 1 = dword, 2 =
// byte) into the arg buffer, EnterFunction, mark runnable, set the scope base and
// run-record, log "Run script: %s". Returns 1 on success, 0 on error.
//
// `firstArgs` supplies the argc raw argument dwords (the original walks &a3, the
// first by-value vararg).  `runRecord` is the per-arg dword buffer (dword_62E8B0,
// stride 4, for type-1/2 args).  `strScratch` is the UTF-16 string scratch ring
// (unk_7675E0, stride 96) the type-6 string args are copied into; pass null to
// skip (the binder then drops type-6 args).
// ===========================================================================
i32 RunWithArgs(u8* ctx, int argc, const i32* firstArgs, i32* runRecord,
                char* strScratch = nullptr);

} // namespace guild::sim
