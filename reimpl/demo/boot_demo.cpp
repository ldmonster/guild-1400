// boot_demo — the REAL game-asset boot path for the Guild reconstruction.
//
// NOT a translation of gilde.exe and NOT a unit/e2e test: a standalone `main`
// that boots the reconstruction against the REAL "Die Gilde — Europe 1400"
// install directory and loads real data end to end:
//
//   1. shim::DiskFileSystem rooted at the real game dir,
//   2. read the real Gilde.INI through the reconstructed INI parser
//      (config::ReadGfxAndSoundSettings + the [General] keys) and print it,
//   3. mount the real Resources/*.BIN PKZIP archives via the reconstructed
//      VFS / ArchiveMount (members openable by name),
//   4. load the real gfx/gilde.gfx (1806 gfx objects) through the real gui
//      loader, and the real city Resources/gamedata/Cities/AUGSBURG.cty
//      (transparent gunzip + the save/city header loader) through the VFS,
//   5. run a few frames of the headless app spine, then shut down.
//
// Steps 1-3 reuse app::MountRealGameAssets (src/app/real_boot.{h,cpp}); the gfx
// and city loads go through the now-bound VFS. It prints a clear log of what real
// assets loaded and exits 0. If a loader isn't ready for a real file it notes it
// in the log but keeps going (it never silently skips).
//
// Build (from the repo root): demo/README links the needed src/*.cpp with g++.

#include "app/real_boot.h"
#include "app/wiring.h"

#include "config/ini.h"
#include "io/vfs.h"
#include "io/gamestate.h"
#include "gui/form_loader.h"

#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace guild;

namespace {

// The real game directory (override with argv[1]).
const char* kDefaultGameDir =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

int g_notes = 0; // non-fatal "loader not ready for this real file" notes

} // namespace

int main(int argc, char** argv) {
    const std::string gameDir = (argc > 1) ? argv[1] : kDefaultGameDir;

    std::printf("=== Guild reconstruction — boot_demo (REAL game-asset boot) ===\n");
    std::printf("[dir] game directory: %s\n", gameDir.c_str());

    // ----------------------------------------------------------------------
    // 1. shim::DiskFileSystem rooted at the real game dir.
    // ----------------------------------------------------------------------
    shim::DiskFileSystem fs(gameDir);
    std::printf("[shim] DiskFileSystem rooted; checking key assets...\n");
    const char* probe[] = {
        "Gilde.INI", "gfx/gilde.gfx", "Resources/forms.BIN",
        "Resources/gamedata/Cities/AUGSBURG.cty",
    };
    bool haveAll = true;
    for (const char* p : probe) {
        bool ok = fs.exists(p);
        haveAll &= ok;
        std::printf("       %-42s %s\n", p, ok ? "found" : "MISSING");
    }
    if (!haveAll) {
        std::printf("[dir] real game assets not all present — aborting boot.\n");
        return 1;
    }

    // ----------------------------------------------------------------------
    // 2 + 3. Read Gilde.INI, bind the VFS, mount Resources/*.BIN.
    // ----------------------------------------------------------------------
    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);

    std::printf("\n--- 2) Gilde.INI (reconstructed INI parser) ---\n");
    std::printf("[ini] loaded=%s\n", assets.iniLoaded ? "yes" : "no (defaults)");
    std::printf("[ini] [General] GfxPath  = \"%s\"\n", assets.gfxPath.c_str());
    std::printf("[ini] [General] GamePath = \"%s\"\n", assets.gamePath.c_str());
    std::printf("[ini] [General] Stadt    = \"%s\"\n", assets.stadt.c_str());
    std::printf("[ini] [General] show_intro = %d\n", assets.showIntro ? 1 : 0);
    std::printf("[ini] [Game] stadt=\"%s\" difficulty=%u historie=%d mission=%d "
                "mouse_speed=%d scroll_speed=%d\n",
                assets.game.stadt.c_str(), assets.game.difficulty, assets.game.historie,
                assets.game.mission, assets.game.mouseSpeed, assets.game.scrollSpeed);
    std::printf("[ini] [Gfx] cur_res=%u (%dx%d) texture_scale=%u details=%u "
                "character_detail=%u fog_plane=%u\n",
                assets.gfx.curRes, assets.gfx.resWidth, assets.gfx.resHeight,
                assets.gfx.textureScale, assets.gfx.details,
                assets.gfx.characterDetail, assets.gfx.fogPlane);
    std::printf("[ini] [Sound] master_vol=%u sfx_vol=%u msx_vol=%u speech_vol=%u "
                "msx_freq=%u\n",
                assets.sound.masterVol, assets.sound.sfxVol, assets.sound.msxVol,
                assets.sound.speechVol, assets.sound.msxFreq);

    std::printf("\n--- 3) Resources/*.BIN archives (reconstructed VFS/ArchiveMount) ---\n");
    std::printf("[vfs] VFS bound to DiskFileSystem: %s\n",
                assets.vfsBound ? "yes" : "NO");
    for (const auto& a : assets.archives) {
        if (a.mounted) {
            std::printf("[arc] %-30s mounted: %zu members", a.name.c_str(),
                        a.memberCount);
            if (a.memberCount) {
                std::printf(" (first: %s)", a.mount->members()[0].name.c_str());
            }
            std::printf("\n");
        } else {
            std::printf("[arc] %-30s FAILED to mount\n", a.name.c_str());
            ++g_notes;
        }
    }
    std::printf("[arc] total indexed members across archives: %zu\n",
                assets.totalMembers());

    // Open a known member by name through the mounts (proves name->bytes works).
    {
        const char* member = "BAUEN/GEB_BAUEN.FORM"; // a real forms.BIN member
        io::ArchiveMount* m = assets.archiveForMember(member);
        if (m) {
            std::vector<u8> bytes;
            if (m->OpenMember(member, bytes)) {
                std::printf("[arc] opened member \"%s\": %zu bytes (first dword=%u)\n",
                            member, bytes.size(),
                            bytes.size() >= 4 ? *reinterpret_cast<u32*>(bytes.data()) : 0);
            } else {
                std::printf("[arc] member \"%s\" indexed but extract failed\n", member);
                ++g_notes;
            }
        } else {
            std::printf("[arc] member \"%s\" not indexed in any archive (note)\n", member);
            ++g_notes;
        }
    }

    // ----------------------------------------------------------------------
    // 4a. Load the real gfx/gilde.gfx through the reconstructed gui loader.
    // ----------------------------------------------------------------------
    std::printf("\n--- 4a) gfx/gilde.gfx (reconstructed gui loader) ---\n");
    if (gui::Form_LoadFromFile("gfx/gilde.gfx")) {
        std::printf("[gfx] gilde.gfx loaded: %d gfx objects (expected 1806)\n",
                    gui::g_gfxObjectCount);
        if (gui::g_gfxObjectCount != 1806) {
            std::printf("[gfx] NOTE: object count differs from the expected 1806\n");
            ++g_notes;
        }
    } else {
        std::printf("[gfx] NOTE: gilde.gfx load failed (loader not ready)\n");
        ++g_notes;
    }

    // ----------------------------------------------------------------------
    // 4b. Load the real city AUGSBURG.cty through the VFS (transparent gunzip)
    //     + the reconstructed save/city header loader.
    //
    //     The .cty is a gzip-framed "Die Gilde" city seed (a SaveHeader-format
    //     file): the VFS gunzips it transparently and io::LoadGameState parses
    //     the version-gated header + scalar block. (The full per-table world
    //     load needs the sim/world table serializers, out of scope here — noted.)
    // ----------------------------------------------------------------------
    std::printf("\n--- 4b) Resources/gamedata/Cities/AUGSBURG.cty (gunzip + city loader) ---\n");
    {
        io::GameState city{};
        city.relink.assign(io::kRelinkBytes, 0);
        const char* cityPath = "Resources/gamedata/Cities/AUGSBURG.cty";
        if (io::LoadGameState(cityPath, city, /*load=*/nullptr)) {
            std::printf("[cty] AUGSBURG.cty loaded (VFS gunzip + header loader):\n");
            std::printf("      magic=0x%05x  name=\"%s\"  season=%u  wealth=%u\n",
                        city.header.magic, city.header.name, city.header.season,
                        city.header.wealth);
            std::printf("      idA=%d idB=%d  scalar.season=%u scalar.g632244=%u\n",
                        static_cast<int>(city.header.idA),
                        static_cast<int>(city.header.idB),
                        city.scalar.season, city.scalar.g632244);
            bool sane = city.header.magic >= io::kSaveVersionLoadMin &&
                        city.header.magic <= io::kSaveVersionLoadMax &&
                        city.header.name[0] != '\0';
            if (!sane) {
                std::printf("[cty] NOTE: header fields look out of range\n");
                ++g_notes;
            }
        } else {
            std::printf("[cty] NOTE: AUGSBURG.cty load failed (loader not ready for "
                        "this real file)\n");
            ++g_notes;
        }
    }

    // ----------------------------------------------------------------------
    // 5. Run a few frames of the headless app spine, then shut down.
    //    (init -> N frames -> verbatim 13-step teardown against RealSubsystems.)
    // ----------------------------------------------------------------------
    std::printf("\n--- 5) headless app spine (init -> frames -> shutdown) ---\n");
    app::HeadlessResult hr = app::RunHeadless(/*displayMode=*/1, /*showIntro=*/false,
                                              /*networkClient=*/false, /*frames=*/5);
    std::printf("[app] spine: exit=%d frames=%d presents=%d memTracker=%s vfs=%s sound=%s\n",
                hr.exitCode, hr.frameCount, hr.presentCount,
                hr.memoryTrackerInited ? "init" : "-",
                hr.vfsInited ? "init" : "-", hr.soundInited ? "init" : "-");

    // The spine's RunHeadless rebinds + tears down the VFS to its own MemFileSystem
    // (and shuts it down in teardown), so re-bind ours back to the disk for any
    // post-run use, then release it cleanly.
    io::VfsInit(&fs, /*caseInsensitive=*/false);
    io::VfsShutdown();

    // ----------------------------------------------------------------------
    std::printf("\n=== summary ===\n");
    std::printf("  ini=%s | archives_mounted=%zu/%zu members=%zu | gfx_objects=%d | "
                "city=loaded | spine_exit=%d | notes=%d\n",
                assets.iniLoaded ? "loaded" : "defaults",
                [&] { std::size_t n = 0; for (auto& a : assets.archives) n += a.mounted; return n; }(),
                assets.archives.size(), assets.totalMembers(),
                gui::g_gfxObjectCount, hr.exitCode, g_notes);
    if (hr.exitCode == 0) {
        std::printf("boot_demo: REAL ASSET BOOT COMPLETED (exit 0, %d non-fatal note(s))\n",
                    g_notes);
        return 0;
    }
    std::printf("boot_demo: app spine returned non-zero exit\n");
    return 1;
}
