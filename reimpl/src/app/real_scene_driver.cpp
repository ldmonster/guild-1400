// gilde.exe — guild::app  (MODULE: real scene/script driver — integration glue)
// See real_scene_driver.h for the wired call chain.
#include "app/real_scene_driver.h"

#include "io/zip_archive.h"
#include "io/archive_mount.h"
#include "sim/script_run.h"
#include "sim/script_vm.h"
#include "sim/script_import.h"

#include <cctype>
#include <cstring>
#include <set>

namespace guild::app {

// ---------------------------------------------------------------------------
// Inert hooks (default registered-command body). Defined in the library so every
// TU links; tests install their own via SetRealSceneDriverHooks.
// ---------------------------------------------------------------------------
namespace {

i32 InertInvoke(const std::string& /*name*/, std::vector<i32>& /*args*/) {
    // Default: every registered command returns 1 (a non-null "handle" / true)
    // so argument-dependent expressions stay well-defined and the VM advances.
    return 1;
}

const RealSceneDriverHooks kInert = [] {
    RealSceneDriverHooks h;
    h.invokeCommand = &InertInvoke;
    return h;
}();

const RealSceneDriverHooks* g_hooks = &kInert;

// .esc extension test (case-insensitive on the last dot).
bool ExtIsEsc(const char* name) {
    const char* dot = std::strrchr(name, '.');
    if (!dot) return false;
    const char* ext = ".esc";
    for (std::size_t i = 0; dot[i] || ext[i]; ++i) {
        if (std::tolower((unsigned char)dot[i]) != std::tolower((unsigned char)ext[i]))
            return false;
    }
    return true;
}

} // namespace

void SetRealSceneDriverHooks(const RealSceneDriverHooks* hooks) {
    g_hooks = hooks ? hooks : &kInert;
}
const RealSceneDriverHooks& GetRealSceneDriverHooks() { return *g_hooks; }

const std::vector<std::string>& DefaultScriptCommands() {
    // A broad set so identifiers classify as commands (class 4) during the lex/
    // compile pass rather than unknown symbols. Mirrors the e2e test's set.
    static const std::vector<std::string> kCmds = {
        "FindScript", "RunScript", "KillCharacter", "GetCharacterHandle",
        "CreateCharacterAtDummy", "GetObjectHandle", "CreateObjectAtDummy",
        "ObjectFlight", "Sleep", "KillObject", "PlaySound", "Wait", "SetCamera"};
    return kCmds;
}

// ---------------------------------------------------------------------------
// Step one loaded script through the StepAllActive context-table stepper.
//   Seeds one runnable ScriptSlot for the script, then runs `stepTicks` ticks of
//   sim::StepAllActive. The per-context step hook drives sim::RunMain over the
//   driver's ScriptHost (which records command invocations). Returns the number
//   of per-context step invocations StepAllActive made.
// ---------------------------------------------------------------------------
static int StepLoadedScript(sim::LoadedScript& ls, int stepTicks,
                            std::set<std::string>& invokedCmds) {
    if (!ls.ok) return 0;

    // One runnable context slot per the StepAllActive scan gate (+164 bit0).
    std::vector<sim::ScriptSlot> slots(1);
    slots[0].inUse    = 1;
    slots[0].handle   = 0;
    slots[0].runFlags = sim::kRunFlagRunnable;

    // The per-context stepper: runs main once (the first tick), then the context
    // has finished — clear its runnable flag so subsequent ticks no-op (mirrors a
    // script that completes and yields the slot). The host records every command.
    sim::ScriptHost host;
    host.invokeCommand = [&](const std::string& name, std::vector<i32>& a) -> i32 {
        invokedCmds.insert(name);
        return GetRealSceneDriverHooks().invokeCommand
                   ? GetRealSceneDriverHooks().invokeCommand(name, a)
                   : 1;
    };

    int steps = 0;
    bool ran = false;
    for (int tick = 0; tick < stepTicks; ++tick) {
        sim::StepAllActive(slots, [&](sim::ScriptSlot& s) -> int {
            ++steps;
            if (!ran) {
                sim::RunMain(ls, host);   // real strip/compile/execute over the body
                ran = true;
                s.runFlags = 0;            // completed -> clear runnable
            }
            return 0;
        });
    }
    return steps;
}

// ---------------------------------------------------------------------------
// DriveScriptSource — one script from a raw buffer (unit-test entry).
// ---------------------------------------------------------------------------
RealSceneDriverResult DriveScriptSource(
    const std::string& name, const std::string& source,
    const std::vector<std::string>& commandNames, int stepTicks) {
    RealSceneDriverResult r;
    r.assetsPresent = true;
    r.membersSeen   = 1;
    r.escMembers    = 1;
    r.firstEsc      = name;

    sim::LoadedScript ls = sim::LoadScriptFromSource(name, source, commandNames);
    if (ls.compiled.ok) ++r.scriptsParsed; else ++r.parseErrors;
    if (ls.ok)          ++r.scriptsWithMain;

    std::set<std::string> invoked;
    r.stepsExecuted = StepLoadedScript(ls, stepTicks, invoked);
    r.commandsRegistered = (int)invoked.size();
    return r;
}

// ---------------------------------------------------------------------------
// DriveScriptsFromArchive — walk a real PKZIP archive's .esc members.
// ---------------------------------------------------------------------------
RealSceneDriverResult DriveScriptsFromArchive(
    shim::IFileSystem* fs, const std::string& archivePath, int maxScripts,
    int stepTicks, const std::vector<std::string>& commandNames) {
    RealSceneDriverResult r;
    if (!fs) return r;

    io::ZipArchive z;
    if (!z.Open(fs, archivePath.c_str()))
        return r;                       // assetsPresent stays false
    r.assetsPresent = true;

    const std::vector<std::string>& cmds =
        commandNames.empty() ? DefaultScriptCommands() : commandNames;

    std::set<std::string> invoked;
    char nm[260];
    for (int rc = z.GoToFirstFile(); rc == io::kZipOk; rc = z.GoToNextFile()) {
        z.GetCurrentFileInfo(nullptr, nm, sizeof nm);
        ++r.membersSeen;
        if (!ExtIsEsc(nm)) continue;
        ++r.escMembers;
        if (r.firstEsc.empty()) r.firstEsc = nm;
        if (maxScripts > 0 && r.escMembers > maxScripts) continue;

        std::vector<u8> bytes;
        if (!z.ExtractByName(nm, bytes, false)) { ++r.parseErrors; continue; }
        std::string src(reinterpret_cast<const char*>(bytes.data()), bytes.size());

        sim::LoadedScript ls = sim::LoadScriptFromSource(nm, src, cmds);
        if (ls.compiled.ok) ++r.scriptsParsed; else { ++r.parseErrors; continue; }
        if (ls.ok)          ++r.scriptsWithMain;

        r.stepsExecuted += StepLoadedScript(ls, stepTicks, invoked);
    }
    r.commandsRegistered = (int)invoked.size();
    return r;
}

// ---------------------------------------------------------------------------
// DriveScriptsFromAssets — reuse a mounted RealGameAssets archive when present.
// ---------------------------------------------------------------------------
RealSceneDriverResult DriveScriptsFromAssets(
    RealGameAssets& assets, shim::IFileSystem* fs, const std::string& archivePath,
    int maxScripts, int stepTicks, const std::vector<std::string>& commandNames) {

    // Prefer the already-mounted ArchiveMount (its central-dir index is built);
    // walk its members directly so we exercise the mounted path, not a re-open.
    io::ArchiveMount* mount = nullptr;
    for (auto& a : assets.archives) {
        if (a.mounted && a.mount && a.name == archivePath) { mount = a.mount.get(); break; }
    }
    if (!mount)
        return DriveScriptsFromArchive(fs, archivePath, maxScripts, stepTicks,
                                       commandNames);

    RealSceneDriverResult r;
    r.assetsPresent = true;
    const std::vector<std::string>& cmds =
        commandNames.empty() ? DefaultScriptCommands() : commandNames;

    std::set<std::string> invoked;
    for (const auto& m : mount->members()) {
        ++r.membersSeen;
        if (!ExtIsEsc(m.name.c_str())) continue;
        ++r.escMembers;
        if (r.firstEsc.empty()) r.firstEsc = m.name;
        if (maxScripts > 0 && r.escMembers > maxScripts) continue;

        std::vector<u8> bytes;
        if (!mount->OpenMember(m.name.c_str(), bytes)) { ++r.parseErrors; continue; }
        std::string src(reinterpret_cast<const char*>(bytes.data()), bytes.size());

        sim::LoadedScript ls = sim::LoadScriptFromSource(m.name, src, cmds);
        if (ls.compiled.ok) ++r.scriptsParsed; else { ++r.parseErrors; continue; }
        if (ls.ok)          ++r.scriptsWithMain;

        r.stepsExecuted += StepLoadedScript(ls, stepTicks, invoked);
    }
    r.commandsRegistered = (int)invoked.size();
    return r;
}

// ---------------------------------------------------------------------------
// DriveBinaryTokenParse — run the .esc binary-token reader over `bytes`.
//   Installs a driver-owned stream-reader hook (backed by a cursor over the
//   passed bytes) on sim::SetScriptImportHooks, then pulls tokens via
//   sim::EscReadToken until EOF (0x2B) / error (0x27). The cursor lives in a
//   library-local static (not a test symbol). Restores prior hooks on exit.
// ---------------------------------------------------------------------------
namespace {
// Library-owned probe stream the binary-token reader hook draws from.
struct ProbeStream { const u8* data = nullptr; std::size_t len = 0, pos = 0; };
ProbeStream* g_probe = nullptr;

u32 ProbeReadStream(u8* buf, u32 size, int /*stream*/, int count) {
    if (!g_probe) return 0;
    u32 total = size * (u32)count;
    if (g_probe->pos + total > g_probe->len) return 0;
    std::memcpy(buf, g_probe->data + g_probe->pos, total);
    g_probe->pos += total;
    return total;
}
u32 ProbeReadByte(int stream, u8* out) { return ProbeReadStream(out, 1, stream, 1); }
u32 ProbeReadString(int stream, u8* out) {
    u32 res = 0; u8 b;
    do {
        if (!ProbeReadStream(&b, 1, stream, 1)) { *out = 0; return res; }
        *out++ = b; res = b;
    } while (b);
    return res;
}
} // namespace

int DriveBinaryTokenParse(const std::vector<u8>& bytes) {
    ProbeStream ps{ bytes.data(), bytes.size(), 0 };
    g_probe = &ps;

    sim::ScriptImportHooks hk;
    hk.readStream = &ProbeReadStream;
    hk.readByte   = &ProbeReadByte;
    hk.readString = &ProbeReadString;
    sim::SetScriptImportHooks(&hk);

    int tokens = 0;
    for (;;) {
        u8 tok = sim::EscReadToken(1);
        if (tok == sim::kEscTokEof || tok == sim::kEscTokError)
            break;
        ++tokens;
        if (tokens > (int)bytes.size() + 4) break;   // safety bound
    }

    sim::SetScriptImportHooks(nullptr);
    g_probe = nullptr;
    return tokens;
}

} // namespace guild::app
