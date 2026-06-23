#pragma once
// ===========================================================================
// script_run.{h,cpp} — load + run a real .esc script (gilde.exe, guild::sim).
// ===========================================================================
//
// Faithful 1:1 reconstruction of the cutscene/event script LOAD+RUN chain that
// sits on top of the recovered lexer/compiler/executor (script_lexer / _compiler
// / _symbols / _vm):
//
//   VIBE_Cutscene_LoadAndRunScript (0x4aa01c) — the public entry: if the global
//       script-disable flag (dword_6315BC) is set, do nothing; else load the
//       named script from the script dir and run its `main`. Returns the new
//       context's source pointer (ctx+128) or null.
//   VIBE_Script_LoadFromScriptDir  (0x4424e0) — prepend the script root
//       "x:\engine\gfx\scripts\" to the relative name, then LoadScript.
//   VIBE_Script_LoadScript         (0x4421f0) — VFS-open the .esc, read up to
//       0xFA00 (64000) bytes, StripCommentsAndWhitespace into the source buffer,
//       claim a free context slot (stride 2584, 128 slots), store the source +
//       a per-line debug map, and return the context.
//   VIBE_Script_StripCommentsAndWhitespace (0x441f30) — the source preprocessor:
//       turn control chars (tab/CR/LF/0x10) into spaces, strip /* */ comments,
//       collapse runs of spaces, and build a line-number map terminated by -1.
//   VIBE_Script_RunMain            (0x44396c) — CompileBlock the source, look up
//       the `main` function, EnterFunction it, mark the context runnable, and
//       seed the statement-mode/scope fields. ("No entrypoint(main)..." on miss.)
//
// This module reuses the EXISTING cores (no re-definition):
//   * StripCommentsAndWhitespace -> a local, faithful preprocessor (the original
//     0x441f30 is not yet implemented elsewhere; defined here).
//   * CompileScript / ScriptExecutor (script_compiler.{h,cpp}) for compile+run.
//
// Real file access goes through the VFS (io/vfs) exactly as the original; the
// script root prefix and the 64000-byte read cap are reproduced.
//
// DEFERRED (listed in the report): the live 2584-byte ScriptContext table slot
// management (dword_62E8A4) + DestroyContext, the per-context scene-slot binding
// (Universe_SwitchActiveSlot), and the render/fade/present tail of
// VIBE_Cutscene_LoadScene (0x4aa234) — all engine-state coupled. We run the
// script through the in-memory CompiledScript/ScriptExecutor instead, which is
// behaviour-equivalent for the tokenize/compile/execute portion the brief asks.
#include "guild/common/types.h"
#include "sim/script_compiler.h"
#include "sim/script_vm.h"   // ScriptHost
#include "io/vfs.h"
#include <string>
#include <vector>

namespace guild::sim {

// gilde.exe 0x441f30 — VIBE_Script_StripCommentsAndWhitespace.
// Preprocess `source` in place-equivalent fashion:
//   * the control chars tab/LF/CR/0x10 map to a single space (bytes 0x0E/0x0F
//     and <9 are kept verbatim, exactly as 0x441f30 does), it replaces C-style
//     /* ... */ comments with a single separating space (the original blanks the
//     span to spaces; pass 2 then collapses it — behavior-identical), and
//   * collapses runs of two-or-more spaces down to one.
// Returns the cleaned source. (The original also produces a per-line offset map
// for debug strings; that map is not needed by the reimplemented compiler and is
// recovered as `lineOffsets` for fidelity when requested.)
std::string StripCommentsAndWhitespace(const std::string& source,
                                       std::vector<int>* lineOffsets = nullptr);

// The recovered script-directory prefix (aXEngineGfxScri @0x62e7a4) and the
// per-read source cap (0xFA00 = 64000 bytes) from VIBE_Script_LoadScript.
constexpr const char* kScriptDirPrefix = "x:\\engine\\gfx\\scripts\\";
constexpr u32         kScriptReadCap   = 0xFA00u;  // 64000

// A loaded, compiled script ready to run: the cleaned source, the compiled
// symbol table, and the resolved `main` entry cursor (or -1 if no main).
struct LoadedScript {
    std::string    name;          // the original relative name passed in
    CompiledScript compiled;      // symbols + cleaned source
    int            mainCursor = -1; // body cursor of `main` (RunMain entry)
    std::vector<std::string> commandNames; // registered-command set (for re-lex)
    bool           ok = false;    // loaded + compiled + has a main entry
    std::string    error;
};

// gilde.exe 0x4421f0 — VIBE_Script_LoadScript (over a raw source buffer).
// Strip + compile `source` (already read from disk) as script `name` with the
// given registered-command set. Returns a LoadedScript; `ok` is false if compile
// failed. (The 2584-byte context-slot bookkeeping is deferred — see header.)
LoadedScript LoadScriptFromSource(const std::string& name,
                                  const std::string& source,
                                  const std::vector<std::string>& commandNames);

// gilde.exe 0x4424e0 + 0x4421f0 — VIBE_Script_LoadFromScriptDir + LoadScript.
// VFS-open `relName` under the script-dir prefix, read up to kScriptReadCap
// bytes, then LoadScriptFromSource. Returns a LoadedScript with ok==false if the
// file can't be opened. `addPrefix` controls the "x:\engine\gfx\scripts\" prefix
// (the original always prepends it; real archives are mounted at their own root,
// so callers that already pass a full VFS path pass addPrefix=false).
LoadedScript LoadScriptFromVfs(const char* relName,
                               const std::vector<std::string>& commandNames,
                               bool addPrefix = true);

// gilde.exe 0x44396c — VIBE_Script_RunMain: resolve `main` and run from its body.
// Runs the loaded script's `main` through ScriptExecutor with `host`. Returns the
// script's return value (dword_62E8D0 stash). If the script has no `main`, does
// nothing and returns 0.
i32 RunMain(LoadedScript& ls, ScriptHost host, int stepBudget = 100000);

// gilde.exe 0x4aa01c — VIBE_Cutscene_LoadAndRunScript: the public entry.
// If `scriptsDisabled` (dword_6315BC) is true, returns false without loading.
// Else loads `relName` from the VFS and runs its `main`. Returns true if the
// script loaded + compiled + had a main entry (i.e. actually ran). `returnValue`
// (out, may be null) receives main's return value.
bool CutsceneLoadAndRunScript(const char* relName,
                              const std::vector<std::string>& commandNames,
                              ScriptHost host,
                              bool scriptsDisabled = false,
                              i32* returnValue = nullptr,
                              bool addPrefix = true);

} // namespace guild::sim
