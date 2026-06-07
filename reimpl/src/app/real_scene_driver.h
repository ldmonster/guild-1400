#pragma once
// gilde.exe — REAL scene/script driver (guild::app).
//
// INTEGRATION GLUE, not a translation. This module wires the already-reconstructed
// .esc script LOAD+RUN chain (sim/script_run: StripCommentsAndWhitespace ->
// CompileScript -> LookupFunction("main") -> ScriptExecutor) and the per-frame
// context-table STEPPER (sim/script_vm: StepAllActive over the 128-slot
// ScriptContext table) over the REAL shipped PKZIP archives
// (Resources/Scripts.BIN, Resources/scenes.BIN) served through shim::IFileSystem /
// the bound VFS in app::RealGameAssets.
//
// What it does, end to end:
//   1. Open Scripts.BIN through the reconstructed io::ZipArchive (or reuse the
//      mounted ArchiveMount in a RealGameAssets), walk the central directory and
//      pick up every .esc member.
//   2. For each .esc: read its bytes, REGISTER the host command set, and load it
//      through sim::LoadScriptFromSource (the real strip+compile pass). Count the
//      scripts that parse and the commands registered across them.
//   3. STEP the parsed scripts a few ticks: each loaded script seeds one runnable
//      ScriptSlot; StepAllActive scans the slot table and drives each runnable
//      context through sim::RunMain over a deterministic ScriptHost. Count the
//      step invocations the stepper made.
//   4. (Optional) exercise the .esc BINARY-token parser (sim/script_import:
//      EscReadToken / EscParseBlock) over a member's bytes through an installable
//      stream-reader hook — proving the binary-token reader runs over real bytes.
//
// Unreconstructed callees (the actual registered command bodies, the live
// 2584-byte context-table slot management, scene swaps) are routed through the
// RealSceneDriverHooks struct below, which carries INERT DEFAULTS in the .cpp so
// every TU links cleanly and the run advances without error.
#include "app/real_boot.h"
#include "shim/IFileSystem.h"
#include "guild/common/types.h"

#include <string>
#include <vector>

namespace guild::app {

// The small result struct the driver returns (the "counts" the brief asks for).
struct RealSceneDriverResult {
    bool assetsPresent   = false;  // the Scripts.BIN archive opened
    int  membersSeen     = 0;      // total central-dir members walked
    int  escMembers      = 0;      // members with a .esc/.ESC extension
    int  scriptsParsed   = 0;      // .esc members that compiled (ls.compiled.ok)
    int  scriptsWithMain = 0;      // parsed scripts exposing a main() entry
    int  commandsRegistered = 0;   // distinct host commands the run invoked
    int  stepsExecuted   = 0;      // per-context StepAllActive invocations made
    int  parseErrors     = 0;      // .esc members that failed to compile
    // .esc binary-token parser pass (the script_import reader) — number of tokens
    // the EscReadToken/EscParseBlock walk consumed over the probed member.
    int  binaryTokensRead = 0;
    std::string firstEsc;          // the first .esc member name seen (diagnostic)
};

// The cross-module leaves the driver routes through inert hooks. The registered
// command bodies and the per-script byte stream the binary-token parser reads are
// the only callees with no directly-callable reconstructed target here; defaults
// in the .cpp make them deterministic no-ops so the run never aborts.
struct RealSceneDriverHooks {
    // Registered-command body: invoked by the script VM with the command name and
    // its (already-evaluated) integer args. Default returns 1 (a "handle"/true)
    // so dependent expressions stay well-defined. Mutate args for out-params.
    i32 (*invokeCommand)(const std::string& name, std::vector<i32>& args) = nullptr;
};

void SetRealSceneDriverHooks(const RealSceneDriverHooks* hooks);
const RealSceneDriverHooks& GetRealSceneDriverHooks();

// The default registered-command set the driver classifies identifiers against
// during the compile pass (so script identifiers lex as commands, not unknowns).
const std::vector<std::string>& DefaultScriptCommands();

// Drive the .esc scripts in an already-opened PKZIP archive served by `fs` at the
// relative path `archivePath` (e.g. "Resources/Scripts.BIN"). Loads up to
// `maxScripts` .esc members (0 == all), registering the command set and stepping
// each parsed script for `stepTicks` ticks through StepAllActive. Returns the
// driver counts. `out.assetsPresent` is false if the archive can't be opened.
RealSceneDriverResult DriveScriptsFromArchive(
    shim::IFileSystem* fs,
    const std::string& archivePath,
    int maxScripts = 0,
    int stepTicks = 3,
    const std::vector<std::string>& commandNames = {});

// Drive the .esc scripts from a mounted RealGameAssets (reuses the bound VFS +
// the mounted ArchiveMount for `archivePath`). Same counts. If the archive isn't
// mounted, falls back to opening it through `fs` directly.
RealSceneDriverResult DriveScriptsFromAssets(
    RealGameAssets& assets,
    shim::IFileSystem* fs,
    const std::string& archivePath = "Resources/Scripts.BIN",
    int maxScripts = 0,
    int stepTicks = 3,
    const std::vector<std::string>& commandNames = {});

// Load + step ONE .esc script from a raw source buffer (the synthetic-bytes entry
// the unit test drives). Registers `commandNames`, strip+compiles, then steps the
// resulting runnable context `stepTicks` ticks. Returns the counts for that one
// script (scriptsParsed 0/1, stepsExecuted, commandsRegistered).
RealSceneDriverResult DriveScriptSource(
    const std::string& name,
    const std::string& source,
    const std::vector<std::string>& commandNames,
    int stepTicks = 3);

// Run the .esc BINARY-token parser (sim/script_import EscReadToken / EscParseBlock)
// over `bytes` through the installed stream-reader hook, counting the tokens
// consumed until EOF/close/error. Used to prove the binary-token reader path runs
// over real (or synthetic) bytes. Returns the token count.
int DriveBinaryTokenParse(const std::vector<u8>& bytes);

} // namespace guild::app
