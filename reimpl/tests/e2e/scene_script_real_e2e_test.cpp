// End-to-end validation of the .ed3 scene-file loader (render/scene_load) and the
// .esc script load+run chain (sim/script_run) against the REAL shipped archives
// europe_guild_1400_original/Resources/{scenes.BIN, Scripts.BIN}. GUARDED: if the
// asset folder is absent the tests pass trivially so the suite stays green.
//
// Coverage:
//   1) Open scenes.BIN / Scripts.BIN with the reconstructed ZipArchive; list
//      members and their extensions (.ed3/.bak ; .esc).
//   2) Extract a real .ed3 and parse its header byte-for-byte (version 0x3A6C00BB,
//      camera "MegaCam", 7 lights) + recover its object count.
//   3) Extract real .esc scripts, run them through StripCommentsAndWhitespace ->
//      CompileScript -> LookupFunction("main") -> ScriptExecutor (RunMain),
//      verifying they compile, expose a main entry, and invoke their registered
//      commands in the right nesting order.
//   4) Exercise the VFS open dispatch (VfsOpenFile) over a .BIN archive served by
//      an in-memory filesystem.
#include "test.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/mem_filesystem.h"
#include "io/zip_archive.h"
#include "io/vfs.h"
#include "render/scene_load.h"
#include "sim/script_run.h"

#include <cstring>
#include <cctype>
#include <string>
#include <vector>

using namespace guild;

static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool assetsPresent() {
    shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/scenes.BIN") && fs.exists("Resources/Scripts.BIN");
}

static bool extIs(const char* name, const char* ext) {
    const char* dot = std::strrchr(name, '.');
    if (!dot) return false;
    // case-insensitive compare (avoids the non-standard strcasecmp).
    for (std::size_t i = 0; dot[i] || ext[i]; ++i) {
        if (std::tolower((unsigned char)dot[i]) != std::tolower((unsigned char)ext[i]))
            return false;
    }
    return true;
}

// --- 1) member listing + extension breakdown -------------------------------
TEST(SceneScriptRealE2E, ScenesBinMembersAndExtensions) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/scenes.BIN"));
    CHECK_EQ(z.numberEntry(), 128u);    // real PKZIP central-dir count

    int total = 0, ed3 = 0, bak = 0;
    char nm[260]; io::ZipFileInfo fi;
    std::string firstEd3;
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        z.GetCurrentFileInfo(&fi, nm, sizeof nm);
        ++total;
        if (extIs(nm, ".ed3")) { ++ed3; if (firstEd3.empty()) firstEd3 = nm; }
        if (extIs(nm, ".bak")) ++bak;
    }
    CHECK_EQ(total, 128);
    CHECK_EQ(ed3, 97);                  // 97 scene files
    CHECK_EQ(bak, 6);                   // 6 .bak leftovers
    CHECK(!firstEd3.empty());
}

TEST(SceneScriptRealE2E, ScriptsBinMembersAndExtensions) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/Scripts.BIN"));
    CHECK_EQ(z.numberEntry(), 413u);

    int total = 0, esc = 0;
    char nm[260];
    std::string firstEsc;
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        z.GetCurrentFileInfo(nullptr, nm, sizeof nm);
        ++total;
        if (extIs(nm, ".esc")) { ++esc; if (firstEsc.empty()) firstEsc = nm; }
    }
    CHECK_EQ(total, 413);
    CHECK_EQ(esc, 349);  // all .esc + .ESC (case-insensitive)
    CHECK(!firstEsc.empty());
}

// --- 2) parse a real .ed3 scene header byte-for-byte -----------------------
TEST(SceneScriptRealE2E, ParseRealEd3Header) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/scenes.BIN"));

    std::vector<u8> scene;
    CHECK(z.ExtractByName("Cutscenes/Duell.ed3", scene, false));
    CHECK(scene.size() > 1000);

    render::SceneObjectHooks noHooks;    // null body hook -> header + objCount only
    render::ParsedScene ps = render::ParseScene(scene, noHooks);
    CHECK(ps.headerOk);
    CHECK_EQ(ps.header.tag, 0x3A6C00BBu);          // shipped scene format version
    CHECK(ps.header.camName == "MegaCam");         // the camera/scene name string
    CHECK(ps.header.hasCamTarget);                 // >= 0x3A6C00B5
    CHECK(ps.header.hasFog);                       // >= 0x3A6C00B3
    CHECK_EQ((int)ps.header.lights.size(), 7);     // 7-light rig (>= 0x3A6C00BA)
    CHECK_EQ((int)ps.header.lights[0].keyframes.size(), 6);
    CHECK_EQ(ps.objectCount, 12u);                 // recovered top-level object count
}

// Parse the header of EVERY real .ed3 to prove the format gate holds across the
// whole archive (versions 0xAF/0xB9/0xBA/0xBB are all shipped).
TEST(SceneScriptRealE2E, ParseAllRealEd3Headers) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/scenes.BIN"));

    int parsed = 0, ed3 = 0;
    char nm[260];
    render::SceneObjectHooks noHooks;
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        z.GetCurrentFileInfo(nullptr, nm, sizeof nm);
        if (!extIs(nm, ".ed3")) continue;
        ++ed3;
        std::vector<u8> sc;
        if (!z.ExtractByName(nm, sc, false)) continue;
        render::ParsedScene ps = render::ParseScene(sc, noHooks);
        if (ps.headerOk && render::SceneTagValid(ps.header.tag) &&
            ps.header.camName.size() > 0)
            ++parsed;
    }
    CHECK_EQ(ed3, 97);
    CHECK_EQ(parsed, 97);   // every shipped .ed3 header parses cleanly
}

// --- 3) load + compile + run real .esc scripts -----------------------------
TEST(SceneScriptRealE2E, RunRealEscCreateScript) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/Scripts.BIN"));

    std::vector<u8> es;
    CHECK(z.ExtractByName("Cutscenes/Auktion/thb_create_auktionator.esc", es, false));
    std::string src(reinterpret_cast<const char*>(es.data()), es.size());

    // The script's registered commands (the host's command set).
    std::vector<std::string> cmds = {"CreateCharacterAtDummy", "GetObjectHandle"};
    sim::LoadedScript ls = sim::LoadScriptFromSource(
        "thb_create_auktionator.esc", src, cmds);
    CHECK(ls.compiled.ok);
    CHECK(ls.ok);                                   // has a main() entry
    CHECK(ls.compiled.symbols.LookupFunction("main") >= 0);

    // Run main; record the (nested) command invocations.
    std::vector<std::string> order;
    sim::ScriptHost host;
    host.invokeCommand = [&](const std::string& name, std::vector<i32>& a) -> i32 {
        (void)a; order.push_back(name); return 1;
    };
    sim::RunMain(ls, host);
    // Inner GetObjectHandle is evaluated as an argument before the outer
    // CreateCharacterAtDummy fires.
    CHECK_EQ((int)order.size(), 2);
    if (order.size() == 2) {
        CHECK(order[0] == "GetObjectHandle");
        CHECK(order[1] == "CreateCharacterAtDummy");
    }
}

TEST(SceneScriptRealE2E, RunRealEscKillScript) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/Scripts.BIN"));

    std::vector<u8> es;
    CHECK(z.ExtractByName("Cutscenes/Auktion/thb_kill_auktionator.esc", es, false));
    std::string src(reinterpret_cast<const char*>(es.data()), es.size());

    std::vector<std::string> cmds = {"KillCharacter", "GetCharacterHandle"};
    sim::LoadedScript ls = sim::LoadScriptFromSource(
        "thb_kill_auktionator.esc", src, cmds);
    CHECK(ls.ok);

    int calls = 0;
    sim::ScriptHost host;
    host.invokeCommand = [&](const std::string& name, std::vector<i32>& a) -> i32 {
        (void)name; (void)a; ++calls; return 0;
    };
    sim::RunMain(ls, host);
    CHECK_EQ(calls, 2);   // GetCharacterHandle, then KillCharacter
}

// Tokenize EVERY real .esc and prove they compile + expose a main entry. A few
// legacy scripts have no main (helper-only libraries); we only require that the
// vast majority compile and that the count of main-bearing scripts is high.
TEST(SceneScriptRealE2E, TokenizeAndCompileAllRealEsc) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/Scripts.BIN"));

    int esc = 0, compiled = 0, withMain = 0;
    char nm[260];
    // A broad command set so identifiers classify as commands (class 4) rather
    // than unknown; unknown identifiers are harmless to the compile pass anyway.
    std::vector<std::string> cmds = {
        "FindScript", "RunScript", "KillCharacter", "GetCharacterHandle",
        "CreateCharacterAtDummy", "GetObjectHandle", "CreateObjectAtDummy",
        "ObjectFlight", "Sleep", "KillObject", "PlaySound", "Wait", "SetCamera"};
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        z.GetCurrentFileInfo(nullptr, nm, sizeof nm);
        if (!extIs(nm, ".esc")) continue;
        ++esc;
        std::vector<u8> es;
        if (!z.ExtractByName(nm, es, false)) continue;
        std::string src(reinterpret_cast<const char*>(es.data()), es.size());
        sim::LoadedScript ls = sim::LoadScriptFromSource(nm, src, cmds);
        if (ls.compiled.ok) ++compiled;
        if (ls.compiled.symbols.LookupFunction("main") >= 0) ++withMain;
    }
    CHECK_EQ(esc, 349);  // all .esc + .ESC (case-insensitive)
    // Every real .esc tokenizes + compiles without aborting the lexer/compiler.
    CHECK_EQ(compiled, esc);
    // The overwhelming majority define a main() entrypoint.
    CHECK(withMain > esc * 9 / 10);
}

// --- 4) VFS open dispatch over a .BIN archive ------------------------------
TEST(SceneScriptRealE2E, VfsOpenArchiveMember) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);

    // Slurp the real Scripts.BIN bytes and serve them via an in-memory FS at a
    // ".BIN" path so VfsOpenFile takes its archive-member dispatch branch.
    shim::IFile* f = fs.open("Resources/Scripts.BIN", "rb");
    CHECK(f != nullptr);
    if (!f) return;
    std::vector<u8> bin((std::size_t)f->size());
    f->read(bin.data(), bin.size());
    fs.close(f);

    shim::MemFileSystem mem;
    mem.put("Scripts.BIN", bin);
    CHECK(io::VfsInit(&mem, false));
    io::VfsHandle* h = io::VfsOpenFile("Scripts.BIN", "rb");
    CHECK(h != nullptr);                 // archive opened, positioned on a member
    if (h) io::VfsCloseStream(h);
    io::VfsShutdown();
}
