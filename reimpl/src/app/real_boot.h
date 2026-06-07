#pragma once
// gilde.exe — REAL game-asset boot helper (guild::app).
//
// INTEGRATION GLUE, not a translation. The app spine (app_init.cpp / wiring.cpp)
// runs the init->frame->shutdown lifecycle against the reconstructed subsystems,
// but its file-layer hooks (vfsInit / configReadGfxAndSound / guiLoadGfxFile /
// worldLoad...) are wired to synthetic in-memory stand-ins so the headless run
// needs no on-disk assets. This helper closes that gap: given a shim::IFileSystem
// rooted at a REAL "Die Gilde" install, it performs the load-bearing front half of
// a real boot end to end:
//
//   1. read the real Gilde.INI through the reconstructed INI parser
//      (config::IniFile + config::ReadGfxAndSoundSettings), plus the [General]
//      section keys (GfxPath / Stadt / GamePath) the launcher reads directly,
//   2. bind the reconstructed VFS to the host filesystem (io::VfsInit), so files
//      can be opened by name and the .BIN/.gz/.cty transparent backings apply,
//   3. mount the real Resources/*.BIN PKZIP archives via io::ArchiveMount so their
//      members are indexed and openable by name.
//
// Everything reached here is real reconstructed code over real bytes — the only
// OS boundary is shim::IFileSystem. The gfx/city loads themselves are driven by
// the caller (demo / test / spine) through the now-bound VFS, since those loaders
// (gui::Form_LoadFromFile, io::LoadGameState) already take VFS paths.
#include "config/ini.h"
#include "io/archive_mount.h"
#include "shim/IFileSystem.h"

#include <memory>
#include <string>
#include <vector>

namespace guild::app {

// The result of MountRealGameAssets: the parsed config + the mounted archives.
// Holds (owns) the ArchiveMount objects so member opens stay valid for the
// caller's lifetime; the bound VFS (io::VfsInit) is process-global, so the caller
// is responsible for io::VfsShutdown() when done.
struct RealGameAssets {
    // ---- parsed Gilde.INI ----
    bool iniLoaded = false;                 // the .INI file was found + parsed
    config::IniFile           ini;          // the live provider (kept for re-reads)
    config::GfxSettings       gfx;          // [Gfx] (ReadGfxAndSoundSettings)
    config::SoundSettings     sound;        // [Sound]
    config::GameSettings      game;         // [Game]
    // [General] section keys the launcher reads directly (not in the Gfx/Sound/
    // Game structs): the Windows install paths + the start city.
    std::string gfxPath;                    // [General] GfxPath  (e.g. "...\\Resources\\")
    std::string gamePath;                   // [General] GamePath
    std::string stadt;                      // [General] Stadt    (start city, e.g. "Augsburg")
    bool        showIntro = false;          // [General] show_intro

    // ---- VFS ----
    bool vfsBound = false;                  // io::VfsInit succeeded

    // ---- mounted archives ----
    struct MountedArchive {
        std::string name;                   // relative path, e.g. "Resources/forms.BIN"
        bool        mounted = false;
        std::size_t memberCount = 0;
        std::unique_ptr<io::ArchiveMount> mount;  // owns the live ZipArchive + index
    };
    std::vector<MountedArchive> archives;

    // Total indexed members across all mounted archives.
    std::size_t totalMembers() const {
        std::size_t n = 0;
        for (const auto& a : archives) n += a.memberCount;
        return n;
    }
    // Find the first mounted archive that indexes `member` (normalized name);
    // returns nullptr if none. Used to open a member by name through the mounts.
    io::ArchiveMount* archiveForMember(const char* member);
};

// gilde.exe — the real-boot front half (VIBE_App_InitSubsystemsAndMovieDll +
// VIBE_Render_InitDisplayAndPaths file-layer portions). Reads `<gameDir>` config,
// binds the VFS to `fs`, and mounts the Resources/*.BIN archives.
//
//   * `fs` MUST be rooted at the real game directory (so "Gilde.INI",
//     "gfx/gilde.gfx", "Resources/forms.BIN", "Resources/gamedata/..." resolve).
//   * `iniName` is the profile filename next to the executable (default
//     "Gilde.INI"); if absent the config keeps all reconstructed defaults.
//   * `archiveNames`, when empty, defaults to the full real Resources/*.BIN set.
//   * `caseInsensitive` mirrors byte_62EB84 (false = loose paths upper-cased; the
//     DiskFileSystem on a case-sensitive host wants false to match stored names).
//
// Returns the populated RealGameAssets. `out.vfsBound` reflects io::VfsInit; a
// failed archive mount is recorded (mounted=false) but never aborts the others.
RealGameAssets MountRealGameAssets(
    shim::IFileSystem* fs,
    const std::string& gameDir,
    const std::string& iniName = "Gilde.INI",
    const std::vector<std::string>& archiveNames = {},
    bool caseInsensitive = false);

// The default real Resources/*.BIN archive set (the files shipped under
// "<gameDir>/Resources/"). Exposed so callers can introspect / extend the list.
const std::vector<std::string>& DefaultResourceArchives();

// gilde.exe — build the real on-disk city-seed path the spine loads for a chosen
// start city. The launcher reads [General] Stadt (e.g. "Augsburg") and the engine
// opens "Resources/gamedata/Cities/<UPPER>.cty" (the shipped files are upper-cased,
// the extension casing varies). Returns the relative VFS path; `city` is upper-
// cased verbatim (no transliteration). Empty `city` yields "" (no path).
std::string RealCityPath(const std::string& city);

} // namespace guild::app
