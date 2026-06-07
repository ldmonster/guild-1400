// gilde.exe — REAL game-asset boot helper (guild::app). Integration glue only.
// See real_boot.h for the contract. No subsystem logic lives here; this only
// sequences the reconstructed INI parse + VFS bind + archive mounts.
#include "app/real_boot.h"

#include "io/vfs.h"

#include <cctype>
#include <cstring>

namespace guild::app {

namespace {

// Read a whole file through the shim filesystem into a std::string. The original
// boot reads "Gilde.INI" next to the exe; here the only OS boundary is the shim,
// so the INI text comes through fs->open (rooted at the game dir) rather than a
// raw std::ifstream — keeping the AGENT_GUIDE "no OS calls in src/" boundary.
bool SlurpText(shim::IFileSystem* fs, const char* path, std::string& out) {
    if (!fs) return false;
    shim::IFile* f = fs->open(path, "rb");
    if (!f) return false;
    std::int64_t sz = f->size();
    if (sz < 0) sz = 0;
    out.resize(static_cast<std::size_t>(sz));
    std::size_t got = 0;
    if (sz > 0)
        got = f->read(&out[0], static_cast<std::size_t>(sz));
    out.resize(got);
    fs->close(f);
    return true;
}

} // namespace

const std::vector<std::string>& DefaultResourceArchives() {
    // The real Resources/*.BIN PKZIP archive set shipped with "Die Gilde —
    // Europe 1400". Order matches a directory listing of Resources/.
    static const std::vector<std::string> kArchives = {
        "Resources/animations.BIN",
        "Resources/forms.BIN",
        "Resources/Groups.BIN",
        "Resources/Objects.BIN",
        "Resources/scenes.BIN",
        "Resources/Scripts.BIN",
        "Resources/textbin.BIN",
        "Resources/textbin_deutsch.BIN",
        "Resources/Textures.BIN",
    };
    return kArchives;
}

std::string RealCityPath(const std::string& city) {
    if (city.empty())
        return std::string();
    std::string upper;
    upper.reserve(city.size());
    for (char c : city)
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return "Resources/gamedata/Cities/" + upper + ".cty";
}

io::ArchiveMount* RealGameAssets::archiveForMember(const char* member) {
    if (!member) return nullptr;
    for (auto& a : archives) {
        if (a.mounted && a.mount && a.mount->Find(member))
            return a.mount.get();
    }
    return nullptr;
}

RealGameAssets MountRealGameAssets(shim::IFileSystem* fs,
                                   const std::string& /*gameDir*/,
                                   const std::string& iniName,
                                   const std::vector<std::string>& archiveNames,
                                   bool caseInsensitive) {
    RealGameAssets out;

    // ----------------------------------------------------------------------
    // 1. Read the real Gilde.INI through the reconstructed INI parser.
    //    [Gfx]/[Sound]/[Game] go through VIBE_Config_ReadGfxAndSoundSettings;
    //    [General] keys (GfxPath/GamePath/Stadt/show_intro) are read directly,
    //    exactly as the spine reads them in MainEntryAndShutdown.
    // ----------------------------------------------------------------------
    std::string iniText;
    if (SlurpText(fs, iniName.c_str(), iniText)) {
        out.ini.parse(iniText);
        out.iniLoaded = true;
    }
    // ReadGfxAndSoundSettings is faithful for any provider (empty -> defaults).
    config::ReadGfxAndSoundSettings(out.ini, out.gfx, out.sound, out.game);
    out.gfxPath  = out.ini.getString("General", "GfxPath", "");
    out.gamePath = out.ini.getString("General", "GamePath", "");
    out.stadt    = out.ini.getString("General", "Stadt", out.game.stadt);
    out.showIntro = out.ini.getInt("General", "show_intro", 0) != 0;

    // ----------------------------------------------------------------------
    // 2. Bind the reconstructed VFS to the host filesystem so files open by
    //    name (loose / gzip-framed / .BIN-member backings all dispatch).
    // ----------------------------------------------------------------------
    out.vfsBound = io::VfsInit(fs, caseInsensitive);

    // ----------------------------------------------------------------------
    // 3. Mount the real Resources/*.BIN PKZIP archives (VIBE_Vfs_Enumerate-
    //    MatchingFiles per archive) so their members are indexed + openable.
    // ----------------------------------------------------------------------
    const std::vector<std::string>& names =
        archiveNames.empty() ? DefaultResourceArchives() : archiveNames;
    for (const std::string& name : names) {
        RealGameAssets::MountedArchive ma;
        ma.name = name;
        ma.mount = std::make_unique<io::ArchiveMount>();
        ma.mounted = ma.mount->Mount(fs, name.c_str(), caseInsensitive);
        ma.memberCount = ma.mounted ? ma.mount->memberCount() : 0;
        out.archives.push_back(std::move(ma));
    }

    return out;
}

} // namespace guild::app
