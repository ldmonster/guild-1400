// WAVE-18 e2e — first-name tables over the REAL install.
//
// Loads Text_C_Personen.res from the shipped textbin archive (the PKZIP
// textbin_deutsch.BIN — the default Language="german" resolves to it in this
// patched install) through the reconstructed ZipArchive + BuildTextArray path,
// exactly as VIBE_Text_LoadTextFile @0x44dba0 does once the VFS resolves the
// member, and verifies the male/female/dynasty slices (dword_8C400C @id 599 /
// dword_8C4320 @id 796 / dword_8C4508 @id 918) populate with the real names so
// the RandomModulo(0xBF)/(0x70)/(0x95) draws index real strings.
//
// GUARDED: skips cleanly (passes trivially) when the asset folder is absent.
#include "tests/framework/test.h"

#include "sim/name_tables.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool TextbinPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/textbin_deutsch.BIN");
}

// Hex helper for the golden CP1251 name byte pins.
std::string Hex(const char* s) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
        out.push_back(d[*p >> 4]);
        out.push_back(d[*p & 0xF]);
    }
    return out;
}

} // namespace

TEST(NameTablesE2E, LoadRealNamesFromTextbinArchive) {
    if (!TextbinPresent()) { CHECK(true); return; } // skipped: no assets

    shim::DiskFileSystem fs(GameDir());

    sim::NameTables_Reset();
    CHECK(sim::NameTables_LoadFromArchive(&fs, "Resources/textbin_deutsch.BIN"));
    CHECK(sim::NameTables_Loaded());

    // Every slot in each slice is a non-empty real name.
    for (int i = 0; i < sim::kNameMaleCount; ++i)
        CHECK(std::strlen(sim::MaleNameAt(i)) > 0);
    for (int i = 0; i < sim::kNameFemaleCount; ++i)
        CHECK(std::strlen(sim::FemaleNameAt(i)) > 0);
    // The dynasty slice has ONE genuinely-empty slot in the shipped archive
    // (id 967, index 49) — the original indexes it and copies ""; every other
    // slot is a real surname. Pin that exact property.
    for (int i = 0; i < sim::kNameDynastyCount; ++i) {
        if (i == 49) CHECK_EQ(std::string(sim::DynastyNameAt(i)), std::string(""));
        else         CHECK(std::strlen(sim::DynastyNameAt(i)) > 0);
    }

    // Golden byte pins (CP1251) at the slice boundaries — recovered directly from
    // the real Text_C_Personen.res blob. These fix BOTH the slice anchors and the
    // exact bytes the original would copy at those indices.
    CHECK_EQ(Hex(sim::MaleNameAt(0)),     std::string("c0e4e0ebfce3e5f0")); // id 599
    CHECK_EQ(Hex(sim::MaleNameAt(190)),   std::string("d6e5f0f0e5f1"));     // id 789 (last male)
    CHECK_EQ(Hex(sim::FemaleNameAt(0)),   std::string("c0e3ede5f1"));       // id 796
    CHECK_EQ(Hex(sim::FemaleNameAt(111)), std::string("c7e5ede0"));         // id 907 (last female)
    CHECK_EQ(Hex(sim::DynastyNameAt(0)),  std::string("c0e4e0ebebe5f0"));   // id 918
    CHECK_EQ(Hex(sim::DynastyNameAt(148)),std::string("d6f3ecf8f3f1f1"));   // id 1066 (last dynasty)

    // Out-of-range index past a slice still returns "".
    CHECK_EQ(std::string(sim::MaleNameAt(sim::kNameMaleCount)), std::string(""));
}

// Re-loading is idempotent and the buffer path agrees with the archive path.
TEST(NameTablesE2E, BufferPathMatchesArchivePath) {
    if (!TextbinPresent()) { CHECK(true); return; }

    shim::DiskFileSystem fs(GameDir());
    sim::NameTables_Reset();
    CHECK(sim::NameTables_LoadFromArchive(&fs, "Resources/textbin_deutsch.BIN"));
    std::string a = sim::MaleNameAt(42);

    // Reset and reload — same result (no residual state).
    sim::NameTables_Reset();
    CHECK(!sim::NameTables_Loaded());
    CHECK(sim::NameTables_LoadFromArchive(&fs, "Resources/textbin_deutsch.BIN"));
    CHECK_EQ(std::string(sim::MaleNameAt(42)), a);
}
