// tests/unit/app_real_boot_edge_test.cpp — WAVE-11 hardening edge tests for the
// app spine asset mount (app::MountRealGameAssets, src/app/real_boot.cpp).
//
// These run WITHOUT real game assets: they drive MountRealGameAssets over an
// in-memory shim::MemFileSystem so the missing/partial/malformed-install paths
// are exercised under ASAN+UBSAN. They assert MountRealGameAssets FAILS SAFE
// (defaults, mounted=false) on a missing or partial directory and never reads
// off a truncated archive or an empty INI.
//
// The valid-asset path (a real install) is covered by app_real_boot_e2e_test;
// these tests deliberately stay on the degraded paths and do not assert any
// golden values that would change observable output on valid input.
#include "test.h"

#include "app/real_boot.h"

#include "io/vfs.h"

#include "shim_impl/mem_filesystem.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace guild;

namespace {

// Convenience: turn a C-string into a MemFileSystem blob (no trailing NUL).
shim::MemFileSystem::Blob Bytes(const char* s) {
    return shim::MemFileSystem::Blob(reinterpret_cast<const std::uint8_t*>(s),
                                     reinterpret_cast<const std::uint8_t*>(s) +
                                         std::char_traits<char>::length(s));
}

} // namespace

// A completely empty filesystem == a missing game directory. MountRealGameAssets
// must not crash: no INI -> defaults; the VFS binds to the (empty) fs; every
// archive mount fails (mounted=false) but the call still returns a full record.
TEST(AppRealBootEdge, MissingDirectoryAllDefaults) {
    shim::MemFileSystem fs;
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, "missing", "Gilde.INI", {}, /*ci=*/false);

    CHECK(!a.iniLoaded);                 // no Gilde.INI present
    CHECK(a.stadt == a.game.stadt);      // fell back to the [Game] default
    CHECK(a.gfxPath.empty());            // [General] keys absent -> ""
    CHECK(!a.showIntro);
    // The default archive set was attempted; every one failed to mount.
    CHECK_EQ(a.archives.size(), app::DefaultResourceArchives().size());
    std::size_t mounted = 0;
    for (const auto& m : a.archives) {
        if (m.mounted) ++mounted;
        CHECK_EQ(m.memberCount, static_cast<std::size_t>(0)); // none indexed
    }
    CHECK_EQ(mounted, static_cast<std::size_t>(0));
    CHECK_EQ(a.totalMembers(), static_cast<std::size_t>(0));
    CHECK(a.archiveForMember("anything") == nullptr);
    CHECK(a.archiveForMember(nullptr) == nullptr); // null member -> nullptr
    io::VfsShutdown();
}

// A partial install: Gilde.INI present and parsed, but the Resources/*.BIN
// archives are all absent. The config must come through; archive mounts fail
// gracefully. Exercises the SlurpText -> ini.parse path on a real (small) INI.
TEST(AppRealBootEdge, PartialInstallIniOnlyNoArchives) {
    shim::MemFileSystem fs;
    fs.put("Gilde.INI", Bytes(
        "[General]\n"
        "GfxPath=C:\\Gilde\\Resources\\\n"
        "Stadt=Cologne\n"
        "show_intro=1\n"
        "[Sound]\n"
        "master_vol=77\n"));

    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, "partial", "Gilde.INI", {}, /*ci=*/false);

    CHECK(a.iniLoaded);
    CHECK(a.stadt == "Cologne");
    CHECK(!a.gfxPath.empty());
    CHECK(a.showIntro);
    CHECK_EQ((int)a.sound.masterVol, 77);
    // Archives still all fail (no .BIN files).
    for (const auto& m : a.archives) CHECK(!m.mounted);
    io::VfsShutdown();
}

// An empty (0-byte) INI must parse cleanly to all defaults and not read off the
// empty buffer (SlurpText resizes to 0, reads 0). iniLoaded is true (the file
// existed and was read), but every key falls back to its default.
TEST(AppRealBootEdge, EmptyIniFile) {
    shim::MemFileSystem fs;
    fs.put("Gilde.INI", shim::MemFileSystem::Blob{}); // 0-byte file
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, "empty", "Gilde.INI", {}, /*ci=*/false);
    CHECK(a.iniLoaded);                  // file existed and was slurped
    CHECK(a.stadt == a.game.stadt);      // default start city
    CHECK(a.gfxPath.empty());
    io::VfsShutdown();
}

// A truncated / malformed "archive": a file present at an archive name but whose
// contents are NOT a valid PKZIP archive (garbage / header-only). Mount must
// fail safe (mounted=false, 0 members) and never run off the truncated bytes.
TEST(AppRealBootEdge, TruncatedAndMalformedArchives) {
    shim::MemFileSystem fs;
    // 0-byte archive.
    fs.put("Resources/animations.BIN", shim::MemFileSystem::Blob{});
    // 1-byte archive.
    fs.put("Resources/forms.BIN", shim::MemFileSystem::Blob{0x50});
    // Header-only: the PKZIP local-file signature "PK\x03\x04" with nothing else.
    fs.put("Resources/Objects.BIN",
           shim::MemFileSystem::Blob{'P', 'K', 0x03, 0x04});
    // Truncated end-of-central-directory signature, no actual records.
    fs.put("Resources/Textures.BIN",
           shim::MemFileSystem::Blob{'P', 'K', 0x05, 0x06, 0, 0, 0, 0});
    // Pure garbage.
    fs.put("Resources/scenes.BIN", Bytes("not a zip at all, just text bytes"));

    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, "malformed", "Gilde.INI", {}, /*ci=*/false);

    // Whatever the archive layer decides, the result must be self-consistent:
    // memberCount is 0 for any archive that did not mount, and OpenMember of a
    // bogus name never succeeds / never crashes.
    for (const auto& m : a.archives) {
        if (!m.mounted)
            CHECK_EQ(m.memberCount, static_cast<std::size_t>(0));
    }
    CHECK(a.archiveForMember("BAUEN/GEB_BAUEN.FORM") == nullptr);
    io::VfsShutdown();
}

// Explicit archive name list with a NUL-less / odd member query: archiveForMember
// must handle an arbitrary (non-existent) member string in-bounds.
TEST(AppRealBootEdge, ArchiveForMemberOddQueries) {
    shim::MemFileSystem fs;
    std::vector<std::string> names = {"Resources/forms.BIN"};
    fs.put("Resources/forms.BIN", shim::MemFileSystem::Blob{0x00, 0x01, 0x02});
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, "x", "Gilde.INI", names, /*ci=*/false);
    CHECK_EQ(a.archives.size(), static_cast<std::size_t>(1));
    CHECK(a.archiveForMember("") == nullptr);       // empty member
    CHECK(a.archiveForMember(nullptr) == nullptr);  // null member
    CHECK(a.archiveForMember("a/b/c/does_not_exist.xyz") == nullptr);
    io::VfsShutdown();
}

// RealCityPath upper-cases and prefixes; an empty city yields an empty path
// (no out-of-bounds on the empty string), a normal city yields the .cty path.
TEST(AppRealBootEdge, RealCityPathEdge) {
    CHECK(app::RealCityPath("").empty());
    CHECK(app::RealCityPath("augsburg") ==
          "Resources/gamedata/Cities/AUGSBURG.cty");
    CHECK(app::RealCityPath("Köln").size() > 0); // non-ASCII: no crash on toupper
}
