// gilde.exe — guild::sim  (MODULE: .esc script load + run chain)
// See script_run.h for the recovered call chain and addresses.
#include "sim/script_run.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// gilde.exe 0x441f30 — VIBE_Script_StripCommentsAndWhitespace.
//
// Faithful transcription of the two-pass preprocessor:
//   Pass 1 (the `while (v7 < strlen(a2))` loop): classify each byte:
//     * tab (9), LF (0x0A), CR (0x0D), 0x10  -> a single space; LF also pushes a
//       line offset into the debug map.
//     * '/' followed by '*' (and a matching '*''/') -> the comment span is blanked
//       (the original memsets it to spaces via Light_SetGrayColorThunk(32,...)).
//     * bytes 0x0E, 0x0F and other controls < 9 -> left as-is (advanced).
//   Pass 2 (the `while (v9 < strlen(a2))` loop): collapse every run of 2+ spaces
//     down to a single space (MemMove of the tail), fixing up the line map.
// The original walks UTF-16-stride source in the live build; on the cleaned ASCII
// source the logic is identical. We produce the cleaned string directly.
// ===========================================================================
std::string StripCommentsAndWhitespace(const std::string& source,
                                       std::vector<int>* lineOffsets) {
    std::string s;
    s.reserve(source.size());
    std::vector<int> lines;

    // --- pass 1: control-char normalisation + comment stripping --------------
    std::size_t i = 0;
    const std::size_t n = source.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(source[i]);

        // '/' '*' ... '*' '/'  comment span.  gilde.exe @0x442060: when '/' is
        // followed by '*' and the closing '*/' is found, the original BLANKS the
        // whole span to spaces (SetGrayColorThunk(32, v11-v8+2, v8) memsets it to
        // ' '), it does NOT delete it; pass 2 then collapses that run of spaces to
        // a single space.  We emit exactly one space so the comment still acts as a
        // token separator: `a/* x */b` -> `a b` (two tokens), NOT `ab`.  (Emitting
        // one space is behavior-identical to N-spaces-then-collapse.)
        if (c == '/' && i + 1 < n && static_cast<unsigned char>(source[i + 1]) == '*') {
            std::size_t end = source.find("*/", i + 2);
            if (end != std::string::npos) {
                s.push_back(' ');     // comment span -> single separating space
                i = end + 2;
                continue;
            }
            // No closing '*/': the original leaves it; fall through and emit.
        }

        if (c == 9 || c == 0x0A || c == 0x0D || c == 0x10) {
            if (c == 0x0A)
                lines.push_back(static_cast<int>(s.size()) + 1);  // LF -> line map
            s.push_back(' ');
            ++i;
            continue;
        }

        // bytes 0x0E, 0x0F and < 9 are kept verbatim (advanced); printable kept.
        s.push_back(static_cast<char>(c));
        ++i;
    }

    // --- pass 2: collapse runs of 2+ spaces to a single space ----------------
    std::string out;
    out.reserve(s.size());
    for (std::size_t k = 0; k < s.size(); ++k) {
        char ch = s[k];
        out.push_back(ch);
        if (ch == ' ') {
            // skip the rest of the run
            while (k + 1 < s.size() && s[k + 1] == ' ')
                ++k;
        }
    }

    if (lineOffsets) {
        *lineOffsets = std::move(lines);
        lineOffsets->push_back(-1);   // the original terminates the map with -1
    }
    return out;
}

// ===========================================================================
// gilde.exe 0x4421f0 — VIBE_Script_LoadScript (over an in-memory source buffer).
// The original VFS-reads, strips, allocs a context slot and stores the source.
// Here we strip + compile; the 2584-byte context slot is deferred (see header).
// ===========================================================================
LoadedScript LoadScriptFromSource(const std::string& name,
                                  const std::string& source,
                                  const std::vector<std::string>& commandNames) {
    LoadedScript ls;
    ls.name = name;
    ls.commandNames = commandNames;

    std::string cleaned = StripCommentsAndWhitespace(source);
    ls.compiled = CompileScript(cleaned, commandNames);
    if (!ls.compiled.ok) {
        ls.error = ls.compiled.error.empty() ? "compile failed" : ls.compiled.error;
        return ls;
    }

    // RunMain resolves `main`; record its body cursor here for the run step.
    int mainIdx = ls.compiled.symbols.LookupFunction("main");
    if (mainIdx >= 0)
        ls.mainCursor = ls.compiled.symbols.funcs()[static_cast<std::size_t>(mainIdx)].bodyCursor;

    ls.ok = (ls.mainCursor >= 0);
    if (!ls.ok)
        ls.error = "evt_RunScript:No entrypoint(main) found in script...";
    return ls;
}

// ===========================================================================
// gilde.exe 0x4424e0 + 0x4421f0 — LoadFromScriptDir + LoadScript over the VFS.
// Open under the script-dir prefix, read up to 64000 bytes, strip + compile.
// ===========================================================================
LoadedScript LoadScriptFromVfs(const char* relName,
                               const std::vector<std::string>& commandNames,
                               bool addPrefix) {
    LoadedScript ls;
    ls.name = relName ? relName : "";

    std::string path;
    if (addPrefix)
        path = std::string(kScriptDirPrefix) + ls.name;
    else
        path = ls.name;

    // VIBE_Vfs_OpenFile(path, "rb", ...). The "rb" mode mirrors aRb_9.
    guild::io::VfsHandle* h = guild::io::VfsOpenFile(path.c_str(), "rb");
    if (!h) {
        ls.error = "evt_LoadScript: Could not open t_vfs_file";
        return ls;
    }

    // Read up to kScriptReadCap bytes (the original's 0xFA00 fixed buffer).
    std::vector<guild::u8> buf(kScriptReadCap, 0);
    guild::u32 got = guild::io::VfsReadStream(buf.data(), kScriptReadCap, h, 1);
    guild::io::VfsCloseStream(h);
    if (got == 0xFFFFFFFFu)
        got = 0;
    // The stream read returns the byte count; trim to it.
    std::string source(reinterpret_cast<const char*>(buf.data()),
                       got < kScriptReadCap ? got : kScriptReadCap);
    // Strip any trailing NULs the fixed buffer would carry (the original relies
    // on a NUL terminator within the 64000-byte buffer).
    std::size_t nul = source.find('\0');
    if (nul != std::string::npos)
        source.resize(nul);

    return LoadScriptFromSource(ls.name, source, commandNames);
}

// ===========================================================================
// gilde.exe 0x44396c — VIBE_Script_RunMain: CompileBlock the source, resolve the
// `main` function (LookupFunction "main"), EnterFunction it, mark the context
// runnable (ctx+164 |= 1), point the scope stack at ctx+168 (ctx+2472), clear the
// statement mode (ctx+2564 = 0), log "Run script: %s", and seed the scene fields.
// The original returns 1 on success / 0 on no-main-or-compile-failure (the
// script's RESULT value is stashed separately in dword_62E8D0 by the executor's
// `return` statement, NOT returned here).  In this in-memory model — where the
// CompiledScript is already compiled and the live ScriptContext slot is deferred
// (see header) — RunMain instead drives ScriptExecutor from main's body cursor and
// surfaces the script's return value; callers (CutsceneLoadAndRunScript) ignore it
// exactly as the original ignores RunMain's return.  The "No entrypoint(main)..."
// diagnostic (aEvtRunscriptNo @0x618354) is raised in LoadScriptFromSource.
// ===========================================================================
i32 RunMain(LoadedScript& ls, ScriptHost host, int stepBudget) {
    if (!ls.ok || ls.mainCursor < 0)
        return 0;
    ScriptExecutor exec(ls.compiled, std::move(host), ls.commandNames);
    return exec.Run(ls.mainCursor, stepBudget);
}

// ===========================================================================
// gilde.exe 0x4aa01c — VIBE_Cutscene_LoadAndRunScript: the public entry.
// ===========================================================================
bool CutsceneLoadAndRunScript(const char* relName,
                              const std::vector<std::string>& commandNames,
                              ScriptHost host,
                              bool scriptsDisabled,
                              i32* returnValue,
                              bool addPrefix) {
    if (scriptsDisabled)              // if (dword_6315BC) return 0;
        return false;
    LoadedScript ls = LoadScriptFromVfs(relName, commandNames, addPrefix);
    if (!ls.ok)
        return false;
    i32 rv = RunMain(ls, std::move(host));
    if (returnValue)
        *returnValue = rv;
    return true;
}

} // namespace guild::sim
