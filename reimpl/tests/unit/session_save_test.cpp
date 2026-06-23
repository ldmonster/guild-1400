// tests/unit/session_save_test.cpp — REAL session save/load, unit tier.
//
// Covers (no real assets needed):
//   * the Amt (Aemter) serializer pair (VIBE_Amt_SaveAemter @0x483198 /
//     VIBE_Amt_LoadAemter @0x4832d0): stream layout golden bytes, the fixed
//     count-word contract (count != 37 -> rejected), field round-trip.
//   * the full session save -> load round trip over an in-memory filesystem
//     (play::SaveLiveWorld / play::LoadLiveWorld through the real VFS "wb"/"rb"
//     paths): world-hash equality, field restoration, header metadata.
//   * the kind-30 (plant) object plantmap round trip through the session pool.
//   * EnumerateSaves over a listDir-capable in-memory filesystem (the original
//     browser scan @0x569530 + per-file header metadata).
//   * the recovered save-dir layout strings/paths.
#include "test.h"

#include "io/save.h"
#include "io/save_world_write.h"
#include "io/vfs.h"
#include "play/save_roundtrip.h"
#include "play/session_save.h"
#include "shim_impl/mem_filesystem.h"
#include "sim/command_apply5.h"
#include "sim/entity.h"
#include "world/office.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// --- helpers -----------------------------------------------------------------

u8* PersRec(int i) { return reinterpret_cast<u8*>(&sim::g_persons[i]); }
u8* ObjRec(int i)  { return reinterpret_cast<u8*>(&sim::g_objects[i]); }

void Put16(u8* p, int off, i16 v)  { std::memcpy(p + off, &v, 2); }
void Put32(u8* p, int off, i32 v)  { std::memcpy(p + off, &v, 4); }
i32  Get32(const u8* p, int off)   { i32 v; std::memcpy(&v, p + off, 4); return v; }

// Craft a live person/scene record touching ONLY serialized fields (the
// unserialized gap bytes stay zero so the raw-byte digest round-trips).
void CraftPerson(int slot, i16 marker, i32 id) {
    u8* r = PersRec(slot);
    Put16(r, 0, marker);              // marker word (== the record index on disk)
    Put32(r, 4, id);                  // id dword
    Put32(r, 16, 0x11223344);
    std::memcpy(r + 92, "UNIT PERSON", 12);     // name field (+92, 32 bytes)
    Put32(r, 84, 2000);               // counter A (biased -1342 on the wire)
    Put32(r, 396, 3000);              // counter B (biased -1468 on the wire)
    Put32(r, 364, -1);                // link ids (id-or-(-1) slots)
    Put32(r, 368, 7);
    Put32(r, 380, -1);
    Put32(r, 388, -1);
    sim::g_personIds[slot] = id;
}

// Craft a live object record (kind != 30) touching only serialized fields, with
// the loader's fixups pre-applied (+48 = 5000, +149 = -1) so a reload is
// byte-identical.
void CraftObject(int slot, u8 kind, i32 id) {
    u8* r = ObjRec(slot);
    r[0] = kind;
    Put32(r, 1, id);
    std::memcpy(r + 5, "UNIT_OBJECT", 12);      // +5 name (0x20)
    Put16(r, 37, 3);
    Put32(r, 43, 0x0BADF00D);
    Put32(r, 65, 2);                  // <= 4 (the loader clamps > 4 to 2)
    Put32(r, 48, 5000);               // loader stamps 5000
    Put32(r, 149, -1);                // loader stamps -1
    std::memcpy(r + 101, "MESHBLOCK", 10);      // +101 (0x30) inline block
    r[153] = 0x21;                    // +153 (0x10) lightmap block (v >= 0x10043)
}

void CraftHolders() {
    for (u32 i = 0; i < io::kAemterCount; ++i) {
        world::OfficeHolder& o = world::g_officeHolders[i];
        o.holder    = (u8)(i + 1);
        o.city      = (i32)(100 + i);
        o.type      = (u8)(i % 7);
        o.rank      = (i32)(i % 4);
        o.state     = (u8)(i % 3);
        o.secondary = (i32)(i * 11) - 1;
    }
}

// --- a listDir-capable in-memory filesystem for the browser scan -------------
struct ListMemListing : shim::IDirListing {
    std::vector<std::string> names;
    std::vector<shim::DirEntry> entries;
    std::size_t count() const override { return entries.size(); }
    const shim::DirEntry& at(std::size_t i) const override { return entries[i]; }
};

struct ListMemFS : shim::MemFileSystem {
    struct Child { std::string name; bool isDir; };
    std::map<std::string, std::vector<Child>> dirs;
    void addSubdir(const std::string& d, const std::string& n) {
        dirs[d].push_back({n, true});
        dirs[d + "/" + n];  // ensure the child dir exists
    }
    void addFile(const std::string& d, const std::string& n) {
        dirs[d].push_back({n, false});
    }
    shim::IDirListing* listDir(const char* path) override {
        auto it = dirs.find(path);
        if (it == dirs.end())
            return nullptr;
        ListMemListing* l = new ListMemListing();
        for (auto& c : it->second)
            l->names.push_back(c.name);
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            shim::DirEntry e;
            e.name = l->names[i].c_str();
            e.isDir = it->second[i].isDir;
            e.dosTime = 0;
            l->entries.push_back(e);
        }
        return l;
    }
};

} // namespace

// ===========================================================================
// Amt (Aemter) serializer pair.
// ===========================================================================
TEST(SessionSave, AmtAemter_StreamLayoutAndRoundTrip) {
    RoundTripZeroWorld(0x101);
    CraftHolders();

    // SAVE to a memory stream.
    std::vector<u8> buf(4096, 0xCD);
    io::VfsHandle* w = io::VfsOpenMemoryStream(buf.data(), (u32)buf.size(), "wb");
    CHECK(w != nullptr);
    CHECK(io::AmtSaveAemter(w, reinterpret_cast<const u8*>(world::g_officeHolders)));
    long len = io::VfsTell(w);
    io::VfsCloseStream(w);

    // Exact stream size: count word + 37 x 15 bytes.
    CHECK_EQ((u32)len, io::kAemterDiskBytes);   // 559
    // Golden leading bytes: count 37 LE, then record 0:
    //   holder=1(1) city=100(4) type=0(1) rank=0(4) state=0(1) secondary=-1(4).
    const u8 golden[19] = {37, 0, 0, 0,
                           1,
                           100, 0, 0, 0,
                           0,
                           0, 0, 0, 0,
                           0,
                           0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(std::memcmp(buf.data(), golden, sizeof golden) == 0);

    // LOAD back into a scratch table; serialized fields must round-trip.
    world::OfficeHolder loaded[37];
    std::memset(loaded, 0, sizeof loaded);
    io::VfsHandle* r = io::VfsOpenMemoryStream(buf.data(), (u32)len, "rb");
    CHECK(r != nullptr);
    CHECK(io::AmtLoadAemter(r, reinterpret_cast<u8*>(loaded)));
    io::VfsCloseStream(r);
    for (u32 i = 0; i < io::kAemterCount; ++i) {
        CHECK_EQ(loaded[i].holder, world::g_officeHolders[i].holder);
        CHECK_EQ(loaded[i].city, world::g_officeHolders[i].city);
        CHECK_EQ(loaded[i].type, world::g_officeHolders[i].type);
        CHECK_EQ(loaded[i].rank, world::g_officeHolders[i].rank);
        CHECK_EQ(loaded[i].state, world::g_officeHolders[i].state);
        CHECK_EQ(loaded[i].secondary, world::g_officeHolders[i].secondary);
    }

    // The fixed count contract: a stream whose count word != 37 is rejected
    // ("amt_fio_LoadAemter(): count read is %i, expected %i" -> return 0).
    buf[0] = 36;
    io::VfsHandle* bad = io::VfsOpenMemoryStream(buf.data(), (u32)len, "rb");
    CHECK(bad != nullptr);
    CHECK(!io::AmtLoadAemter(bad, reinterpret_cast<u8*>(loaded)));
    io::VfsCloseStream(bad);
}

// ===========================================================================
// Session save -> load round trip over an in-memory filesystem.
// ===========================================================================
TEST(SessionSave, SaveThenLoad_HashAndFieldsRoundTrip) {
    shim::MemFileSystem fs;
    const u32 seed = 0x5EED;

    RoundTripZeroWorld(seed);
    CraftPerson(2, 2, 4711);
    CraftPerson(10, 10, 4712);
    CraftObject(0, 7, 99);
    CraftHolders();
    sim::g_personArrayLoaded = true;   // a loaded world (the loader sets this;
                                       // the digest folds it)
    sim::g_sysGameTime.day = 42;       // the live clock (qword_13CE852) is saved
    sim::g_sysGameTime.hour = 13;

    const std::uint64_t h1 = SessionWorldHash(seed);

    // SAVE (real format, partial path, version 0x10045).
    CHECK(SaveLiveWorld(fs, "TEST.SAV", "UNITTEST", 3));
    CHECK(fs.exists("TEST.SAV"));
    std::vector<u8> s1 = fs.get("TEST.SAV");
    CHECK(s1.size() > 0);

    // LOAD it back (zeroes the world first).
    SessionLoadInfo info = LoadLiveWorld(fs, "TEST.SAV", seed);
    CHECK(info.ok);
    CHECK(info.partial);                         // header flag bit 1
    CHECK_EQ(info.version, (u32)io::kSaveVersionCurrent);  // 0x10045
    CHECK(info.aemterLoaded);                    // >= 0x10045 carries Aemter
    CHECK_EQ(info.personCount, 2u);
    CHECK_EQ(info.objectCount, 1u);

    // Hash equality: the save carried the whole (folded) world.
    const std::uint64_t h2 = SessionWorldHash(seed);
    CHECK_EQ(h1, h2);

    // Spot-check restored fields.
    CHECK_EQ(sim::g_persons[2].marker, (i16)2);
    CHECK_EQ(sim::g_persons[2].id, 4711);
    CHECK_EQ(Get32(PersRec(2), 84), 2000);       // bias A re-applied
    CHECK_EQ(Get32(PersRec(2), 396), 3000);      // bias B re-applied
    CHECK_EQ(Get32(PersRec(2), 368), 7);         // link id slot
    CHECK_EQ(ObjRec(0)[0], (u8)7);
    CHECK_EQ(Get32(ObjRec(0), 1), 99);
    CHECK_EQ(Get32(ObjRec(0), 48), 5000);        // loader fixup
    CHECK_EQ(world::g_officeHolders[5].city, 105);
    CHECK_EQ(sim::g_sysGameTime.day, 42);        // clock restored from the stream
    CHECK_EQ(sim::g_sysGameTime.hour, (u16)13);

    // SAVE again: the format is a fixed point (S1 == S2 byte-for-byte).
    CHECK(SaveLiveWorld(fs, "TEST2.SAV", "UNITTEST", 3));
    std::vector<u8> s2 = fs.get("TEST2.SAV");
    CHECK_EQ(s1.size(), s2.size());
    CHECK(s1 == s2);
}

// ===========================================================================
// kind-30 (plant) object: the plantmap rides the session pool.
// ===========================================================================
TEST(SessionSave, PlantObject_PlantmapRoundTrip) {
    shim::MemFileSystem fs;
    const u32 seed = 0x9;

    RoundTripZeroWorld(seed);
    CraftObject(0, 30, 1234);                    // kind 30 == plant
    // Bind a plantmap and fill ONLY its serialized sub-fields
    // (sub +0(4) +4(4) +8(1) +9(1) +10(2) +16(4) +12(1) +13(1); +20 dword is
    // zeroed by the loader).
    static u8 plant[io::kPlantBytes];
    std::memset(plant, 0, sizeof plant);
    for (int s = 0; s < io::kPlantBytes; s += io::kPlantStride) {
        Put32(plant + s, 0, s);
        Put32(plant + s, 4, s ^ 0x5A);
        plant[s + 8] = (u8)(s & 0xFF);
        plant[s + 12] = 0x77;
        Put32(plant + s, 16, -s);
    }
    u8* pp = plant;
    std::memcpy(ObjRec(0) + 113, &pp, sizeof pp);

    CHECK(SaveLiveWorld(fs, "PLANT.SAV"));
    SessionLoadInfo info = LoadLiveWorld(fs, "PLANT.SAV", seed);
    CHECK(info.ok);
    CHECK_EQ(info.objectCount, 1u);

    // The reloaded record's +113 points at the session pool; the plant bytes
    // must equal the crafted serialized fields.
    u8* lp = nullptr;
    std::memcpy(&lp, ObjRec(0) + 113, sizeof lp);
    CHECK(lp != nullptr);
    CHECK(lp != plant);                          // pool storage, not the source
    bool plantsEqual = true;
    for (int s = 0; s < io::kPlantBytes && plantsEqual; s += io::kPlantStride) {
        plantsEqual = Get32(lp + s, 0) == Get32(plant + s, 0)
                   && Get32(lp + s, 4) == Get32(plant + s, 4)
                   && lp[s + 8] == plant[s + 8]
                   && lp[s + 12] == plant[s + 12]
                   && Get32(lp + s, 16) == Get32(plant + s, 16)
                   && Get32(lp + s, 20) == 0;    // loader zeroes +20
    }
    CHECK(plantsEqual);
}

// ===========================================================================
// EnumerateSaves: the browser scan + header metadata.
// ===========================================================================
TEST(SessionSave, EnumerateSaves_BrowserScanAndMetadata) {
    ListMemFS fs;
    fs.dirs["Resources"];
    fs.addSubdir("Resources", "gamedata");
    fs.addSubdir("Resources/gamedata", "Saves");
    fs.addFile("Resources/gamedata/Saves", "Quicksave.SAV");
    fs.addFile("Resources/gamedata/Saves", "GILDE_SAVEGAME_2.SAV");
    fs.addFile("Resources/gamedata/Saves", "notes.txt");   // filtered out

    // Real save files (so the header metadata parses).
    RoundTripZeroWorld(1);
    CraftPerson(1, 1, 1);
    CHECK(SaveLiveWorld(fs, "Resources/gamedata/Saves/Quicksave.SAV",
                        "QUICKSAVE", 1));
    CHECK(SaveLiveWorld(fs, "Resources/gamedata/Saves/GILDE_SAVEGAME_2.SAV",
                        "MY GAME", 2));

    std::vector<SaveListEntry> out;
    int n = EnumerateSaves(fs, out, "Resources", "Resources/gamedata/Saves");
    CHECK_EQ(n, 2);                       // notes.txt excluded by the .SAV filter
    CHECK_EQ((int)out.size(), 2);

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
        CHECK(quick->reservedSlot0);             // QUICKSAVE -> slot 0 rule
        CHECK_EQ(quick->slotTag, (u8)1);
        CHECK_EQ(quick->version, (u32)0x10045);
        // browser record full path @+265 = Sprintf("%s/%s", basePath, name) with
        // the FULL name (extension kept) — the '.'-truncation at 0x5695ff applies
        // ONLY to the +9 display-name field, AFTER the Sprintf (decompile of
        // VIBE_SaveBrowser_EnumerateSaveFiles @0x569530 confirms).
        CHECK(quick->browsePath == "gamedata/saves/Quicksave.SAV");
    }
    if (slot2) {
        CHECK(slot2->headerOk);
        CHECK(slot2->saveName == "MY GAME");
        CHECK(!slot2->reservedSlot0);
        CHECK_EQ(slot2->slotTag, (u8)2);
    }
}

// ===========================================================================
// The recovered save-dir layout strings.
// ===========================================================================
TEST(SessionSave, SaveDirLayoutStrings) {
    CHECK(std::strcmp(kSaveBrowseDir, "gamedata/saves") == 0);          // @0x624f30
    CHECK(std::strcmp(kSaveSlotPathFmt,
                      "Gamedata/saves/GILDE_SAVEGAME_%i.SAV") == 0);    // @0x624f6c
    CHECK(std::strcmp(kQuickSaveName, "QUICKSAVE") == 0);               // @0x624ef0
    CHECK(std::strcmp(kAutoSaveName, "AUTOSAVE") == 0);                 // @0x624efc
    CHECK(SaveSlotPath(2) == "Resources/gamedata/Saves/GILDE_SAVEGAME_2.SAV");
    CHECK(QuickSavePath() == "Resources/gamedata/Saves/Quicksave.SAV");
    CHECK(AutoSavePath() == "Resources/gamedata/Saves/Autosave.SAV");
}
