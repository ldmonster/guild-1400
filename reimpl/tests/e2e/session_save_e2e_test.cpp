// tests/e2e/session_save_e2e_test.cpp — REAL session save/load over the shipped
// game assets (GUARDED; honors GUILD_GAME_DIR).
//
// Three flows per the session-save plan:
//   1. ROUND TRIP, all 5 shipped cities:
//        LoadLiveWorld(<CITY>.cty) -> h1 = SessionWorldHash
//          -> SaveLiveWorld(tmp .SAV, original partial format @0x10045)
//          -> LoadLiveWorld(tmp) -> h2 ; assert h1 == h2
//          -> SaveLiveWorld again ; assert byte-identical (fixed point).
//   2. SOURCE FIDELITY: re-serialize the freshly-loaded city through the
//      version-gated reconstructed writers at the SOURCE version (0x1003B;
//      BERLIN 0x1003E) and byte-compare each section against the source .cty
//      stream. Two named expected skips (see session_save.h): the 4-byte
//      version word (the writer always stamps 0x10045) and the object table
//      (field set changed at 0x10032/0x10043). The trailing scene-object
//      sidecar (WorldIo dump @0x5e65b8) is reported by size, not reproduced.
//   3. ENUMERATE: the original browser scan over the real install's
//      Resources/gamedata/Saves, plus over a temp dir seeded with a Quicksave +
//      a slot save written by SaveLiveWorld (metadata read back).
#include "test.h"

#include "io/save.h"
#include "io/vfs.h"
#include "play/session_save.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

std::string ResolveCityPath(shim::IFileSystem& fs, const std::string& city) {
    std::string base = "Resources/gamedata/Cities/" + city;
    if (fs.exists((base + ".cty").c_str())) return base + ".cty";
    if (fs.exists((base + ".CTY").c_str())) return base + ".CTY";
    return std::string();
}

std::vector<u8> SlurpHostFile(const std::string& path) {
    std::vector<u8> out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return out;
    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len > 0) {
        out.resize((std::size_t)len);
        if (std::fread(out.data(), 1, (std::size_t)len, f) != (std::size_t)len)
            out.clear();
    }
    std::fclose(f);
    return out;
}

const char* kCities[] = {"AUGSBURG", "BERLIN", "DRESDEN", "HANNOVER", "KOELN"};

// The shipped seeds' on-disk versions (read from the leading magic): BERLIN was
// re-exported by a later dev build (0x1003E); the other four are 0x1003B.
u32 CityVersion(const std::string& city) {
    return city == "BERLIN" ? 0x1003Eu : 0x1003Bu;
}

} // namespace

// ===========================================================================
// 1. Round trip: .cty -> live world -> .SAV -> live world, hash-equal +
//    byte-stable, for every shipped city.
// ===========================================================================
TEST(SessionSaveE2E, AllShippedCities_SaveLoadRoundTrip) {
    if (!RealAssetsPresent()) {
        std::printf("[SKIP] SessionSaveE2E: real assets not present at %s\n",
                    GameDir().c_str());
        return;
    }
    const std::uint32_t kSeed = 0x5E551011;
    shim::DiskFileSystem fsGame(GameDir());
    shim::DiskFileSystem fsTmp(".");   // the build/test cwd for the temp saves

    std::printf("\n  %-10s %8s %8s %8s  %-7s %-7s %9s\n",
                "CITY", "OBJECTS", "PERSONS", "TILES", "h1==h2", "S1==S2", "BYTES");
    std::printf("  ----------------------------------------------------------------\n");

    int okCities = 0;
    for (const char* city : kCities) {
        std::string path = ResolveCityPath(fsGame, city);
        CHECK(!path.empty());
        if (path.empty())
            continue;

        // Load the city seed into the live world.
        SessionLoadInfo li = LoadLiveWorld(fsGame, path.c_str(), kSeed);
        CHECK(li.ok);
        CHECK(li.partial);                        // .cty == partial save
        CHECK_EQ(li.version, CityVersion(city)); // the shipped city version
        CHECK(!li.aemterLoaded);                  // Aemter only at >= 0x10045
        if (!li.ok)
            continue;
        const std::uint64_t h1 = SessionWorldHash(kSeed);

        // Save it in the original partial format (the writer's 0x10045).
        std::string tmp = std::string("session_e2e_") + city + ".SAV";
        CHECK(SaveLiveWorld(fsTmp, tmp.c_str(), city, 2));
        std::vector<u8> s1 = SlurpHostFile("./" + tmp);
        CHECK(s1.size() > 0);

        // Reload the save; the whole (folded) world must be identical.
        SessionLoadInfo ri = LoadLiveWorld(fsTmp, tmp.c_str(), kSeed);
        CHECK(ri.ok);
        CHECK(ri.partial);
        CHECK_EQ(ri.version, (u32)io::kSaveVersionCurrent);  // 0x10045
        CHECK(ri.aemterLoaded);                   // our saves carry Aemter
        CHECK_EQ(ri.objectCount, li.objectCount);
        CHECK_EQ(ri.personCount, li.personCount);
        CHECK_EQ(ri.tileCount, li.tileCount);
        const std::uint64_t h2 = SessionWorldHash(kSeed);
        CHECK_EQ(h1, h2);

        // Save the reloaded world again: the format is a fixed point.
        std::string tmp2 = std::string("session_e2e_") + city + "_2.SAV";
        CHECK(SaveLiveWorld(fsTmp, tmp2.c_str(), city, 2));
        std::vector<u8> s2 = SlurpHostFile("./" + tmp2);
        bool stable = !s2.empty() && s1.size() == s2.size()
                   && std::memcmp(s1.data(), s2.data(), s1.size()) == 0;
        CHECK(stable);

        std::printf("  %-10s %8u %8u %8u  %-7s %-7s %9zu\n",
                    city, li.objectCount, li.personCount, li.tileCount,
                    h1 == h2 ? "yes" : "NO", stable ? "yes" : "NO", s1.size());
        if (li.ok && ri.ok && h1 == h2 && stable)
            ++okCities;

        std::remove(("./" + tmp).c_str());
        std::remove(("./" + tmp2).c_str());
    }
    std::printf("  ----------------------------------------------------------------\n");
    std::printf("  %d / 5 cities round-tripped\n\n", okCities);
    CHECK_EQ(okCities, 5);
    io::VfsShutdown();
}

// ===========================================================================
// 2. Source fidelity: section-by-section byte compare against the shipped .cty.
// ===========================================================================
TEST(SessionSaveE2E, AllShippedCities_SourceSectionFidelity) {
    if (!RealAssetsPresent()) {
        std::printf("[SKIP] SessionSaveE2E: real assets not present at %s\n",
                    GameDir().c_str());
        return;
    }
    shim::DiskFileSystem fsGame(GameDir());

    std::printf("\n  %-10s %-4s %-7s %-5s %-7s %-7s %-7s %-5s %9s %9s\n",
                "CITY", "hdr", "scalar", "tiles", "objects", "counter",
                "persons", "slots", "SIDECAR", "BYTES");
    std::printf("  ---------------------------------------------------------------------------\n");

    int okCities = 0;
    for (const char* city : kCities) {
        std::string path = ResolveCityPath(fsGame, city);
        CHECK(!path.empty());
        if (path.empty())
            continue;

        SectionCompareResult r = CompareSaveSectionsAgainstCity(fsGame, path.c_str());
        CHECK(r.loaded);
        CHECK_EQ(r.version, CityVersion(city));
        std::printf("  %-10s %-4s %-7s %-5s %-7s %-7s %-7s %-5s %9zu %9zu\n",
                    city,
                    r.headerTail ? "yes" : "NO",
                    r.scalars ? "yes" : "NO",
                    r.tiles ? "yes" : "NO",
                    r.objectsCompared ? (r.objects ? "yes" : "NO")
                                      : "skip",   // named skip: field set < 0x10043
                    r.counters ? "yes" : "NO",
                    r.persons ? "yes" : "NO",
                    r.slots ? "yes" : "NO",
                    r.trailingBytes,
                    r.sourceBytes);

        CHECK(r.headerTail);          // header minus the 4-byte version word
        CHECK(r.scalars);
        CHECK(r.tiles);
        CHECK(!r.objectsCompared);    // < 0x10043: the documented skip
        CHECK(r.counters);
        CHECK(r.persons);
        // < 0x1003E: exactly one masked +400 dword per record (the loader's
        // value-destroying stamp); none at 0x1003E+.
        CHECK_EQ(r.personsMaskedDwords,
                 (std::size_t)(CityVersion(city) < 0x1003E ? 1 : 0));
        CHECK(r.slots);
        CHECK(r.trailingBytes > 0);   // the scene-object sidecar (named, unread
                                      // by the shipping loader's partial path)
        if (r.ok())
            ++okCities;
    }
    std::printf("  ---------------------------------------------------------------------------\n");
    std::printf("  %d / 5 cities byte-faithful on every compared section\n\n", okCities);
    CHECK_EQ(okCities, 5);
    io::VfsShutdown();
}

// ===========================================================================
// 3. EnumerateSaves: the real install dir + a seeded temp save dir.
// ===========================================================================
TEST(SessionSaveE2E, EnumerateSaves_RealAndSeededDirs) {
    if (!RealAssetsPresent()) {
        std::printf("[SKIP] SessionSaveE2E: real assets not present at %s\n",
                    GameDir().c_str());
        return;
    }
    shim::DiskFileSystem fsGame(GameDir());

    // The real install's Resources/gamedata/Saves (typically empty).
    std::vector<SaveListEntry> real;
    int nReal = EnumerateSaves(fsGame, real);
    std::printf("  real install: %d save(s) in Resources/gamedata/Saves\n", nReal);
    CHECK(nReal >= 0);
    for (const SaveListEntry& e : real)
        std::printf("    %-28s name='%s' slot=%u v=0x%X\n", e.fileName.c_str(),
                    e.saveName.c_str(), (unsigned)e.slotTag, e.version);

    // A seeded temp dir using the original layout: write a Quicksave + a slot
    // save through the REAL session writer, then enumerate + read metadata back.
    const char* root = "session_e2e_saveroot";
    shim::DiskFileSystem fsTmp(".");
    CHECK(fsTmp.makeDir("session_e2e_saveroot/gamedata/saves"));

    // First give the temp root a loaded world (smallest shipped city is fine).
    std::string path = ResolveCityPath(fsGame, "AUGSBURG");
    SessionLoadInfo li = LoadLiveWorld(fsGame, path.c_str(), 0x33);
    CHECK(li.ok);

    CHECK(SaveLiveWorld(fsTmp, "session_e2e_saveroot/gamedata/saves/Quicksave.SAV",
                        kQuickSaveName, 1));
    CHECK(SaveLiveWorld(fsTmp,
                        "session_e2e_saveroot/gamedata/saves/GILDE_SAVEGAME_2.SAV",
                        "E2E CAMPAIGN", 2));

    std::vector<SaveListEntry> out;
    int n = EnumerateSaves(fsTmp, out, root, "session_e2e_saveroot/gamedata/saves");
    CHECK_EQ(n, 2);

    const SaveListEntry* quick = nullptr;
    const SaveListEntry* slot2 = nullptr;
    for (const SaveListEntry& e : out) {
        if (e.fileName == "Quicksave.SAV") quick = &e;
        if (e.fileName == "GILDE_SAVEGAME_2.SAV") slot2 = &e;
    }
    CHECK(quick != nullptr);
    CHECK(slot2 != nullptr);
    if (quick) {
        CHECK(quick->headerOk);
        CHECK(quick->saveName == "QUICKSAVE");
        CHECK(quick->reservedSlot0);
        CHECK_EQ(quick->slotTag, (u8)1);
        CHECK_EQ(quick->version, (u32)io::kSaveVersionCurrent);
    }
    if (slot2) {
        CHECK(slot2->headerOk);
        CHECK(slot2->saveName == "E2E CAMPAIGN");
        CHECK(!slot2->reservedSlot0);
        CHECK_EQ(slot2->slotTag, (u8)2);
        // A SaveLiveWorld file is loadable by the session loader.
        SessionLoadInfo ri = LoadLiveWorld(fsTmp, slot2->openPath.c_str(), 0x33);
        CHECK(ri.ok);
        CHECK_EQ(ri.personCount, li.personCount);
    }

    std::error_code ec;
    std::filesystem::remove_all("session_e2e_saveroot", ec);
    io::VfsShutdown();
}
