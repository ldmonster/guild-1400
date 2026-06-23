// Unit: play::RunLoadGameScreen — the native load-game screen (the host of the 1:1
// gui::Menu_RunLoadGame @0x56a270) over a scripted platform + headless device +
// a listDir-capable in-memory filesystem seeded with real .SAV header fixtures.
//
// Covers: the @0x569d00 slot build through the REAL reconstructed browser leaves
// (enumerate @0x569530 / header @0x5a7af0 / placement @0x569c50: QUICKSAVE -> row 1,
// AUTOSAVE -> row 0, slot tag 2..15, duplicate/invalid rejection, the 16-file cap,
// the (hdr+4)&2 partial gate), mouse + keyboard selection, ESC cancel, the
// byte_63CC40 confirm gate, and the result protocol (loadPath == byte_122F530
// "Gamedata\Saves\%s.SAV", word_63C740 == 10).
#include "test.h"

#include "play/sdl_loadgame_screen.h"
#include "io/save.h"
#include "io/vfs.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/mem_filesystem.h"
#include "shim/IPlatform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;

namespace {

// --- a listDir-capable in-memory filesystem (the session_save test pattern) --
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
        dirs[d + "/" + n];
    }
    void addFile(const std::string& d, const std::string& n) {
        dirs[d].push_back({n, false});
    }
    shim::IDirListing* listDir(const char* path) override {
        auto it = dirs.find(path);
        if (it == dirs.end()) return nullptr;
        ListMemListing* l = new ListMemListing();
        for (auto& c : it->second) l->names.push_back(c.name);
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            shim::DirEntry e;
            e.name = l->names[i].c_str();
            e.isDir = it->second[i].isDir;
            l->entries.push_back(e);
        }
        return l;
    }
};

ListMemFS* MakeSaveDirFS() {
    ListMemFS* fs = new ListMemFS();
    fs->dirs["Resources"];
    fs->addSubdir("Resources", "gamedata");
    fs->addSubdir("Resources/gamedata", "Saves");
    return fs;
}

// Write a real .SAV header fixture (the writer stamps version 0x10045) through
// the same VFS write path the original uses.
void WriteSaveFixture(shim::IFileSystem& fs, const std::string& path,
                      const char* saveName, u8 slotTag, bool partial) {
    io::VfsInit(&fs, false);
    io::VfsHandle* h = io::VfsOpenFile(path.c_str(), "wb");
    CHECK(h != nullptr);
    if (!h) return;
    io::SaveHeader hdr;
    std::memset(&hdr, 0, sizeof hdr);
    hdr.flagByte = partial ? 2 : 1;     // (hdr+4): bit1 == partial/session format
    std::strncpy(hdr.name, saveName, sizeof hdr.name - 1);
    std::strncpy(hdr.name96, saveName, sizeof hdr.name96 - 1);
    hdr.byte649D50 = slotTag;           // the slot tag FindSaveSlot consumes
    hdr.field132 = 2;
    CHECK(io::SaveWriteScenarioBlock(h, hdr, nullptr));
    io::VfsCloseStream(h);
}

void AddSave(ListMemFS& fs, const char* fileName, const char* saveName, u8 slotTag,
             bool partial = true) {
    fs.addFile("Resources/gamedata/Saves", fileName);
    WriteSaveFixture(fs, std::string("Resources/gamedata/Saves/") + fileName,
                     saveName, slotTag, partial);
}

// --- scripted platform (the native_main_menu_itest pattern) ------------------
struct SeqPlatform : shim::IPlatform {
    struct Step { int x = 0, y = 0; bool left = false; int key = 0; };
    std::vector<Step> steps; int iter = 0, maxPumps = 100000;
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return iter < maxPumps; }
    std::uint32_t timeMs() override { return (std::uint32_t)iter * 16u; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override { const Step& s = at(); o.x = s.x; o.y = s.y; o.left = s.left; }
    bool keyDown(int vk) override { return at().key == vk; }
    const Step& at() const { static Step z; if (steps.empty()) return z;
        int i = iter < (int)steps.size() ? iter : (int)steps.size() - 1; return steps[i < 0 ? 0 : i]; }
};

SeqPlatform::Step OnRow(int row, bool left, int W = 800, int H = 600) {
    // Row centre at scroll=0 (the first rows are inside the viewport; the tests
    // only click visible rows — off-viewport rows are unhittable, like the engine).
    const play::LoadGameScreenLayout L = play::LoadGameComputeLayout(W, H);
    int rx, ry, rw, rh; L.RowRect(row, /*scroll=*/0, rx, ry, rw, rh);
    SeqPlatform::Step s; s.x = rx + rw / 2; s.y = ry + rh / 2; s.left = left; return s;
}
SeqPlatform::Step Key(int vk) { SeqPlatform::Step s; s.key = vk; return s; }

play::LoadGameScreenConfig Cfg(shim::IFileSystem* fs, int maxFrames = 64) {
    play::LoadGameScreenConfig c;
    // Native 800x600 (the real screen size); the scrollable 130px rows + viewport
    // only make sense at native scale (tiny windows would clip the whole list).
    c.fs = fs; c.fbW = 800; c.fbH = 600; c.frameCapMs = 0; c.maxFrames = maxFrames;
    return c;
}

} // namespace

// ===========================================================================
// Slot build — the @0x569d00 placement rules over the real leaves.
// ===========================================================================
TEST(SdlLoadGameScreen, SlotBuild_PlacementRules) {
    ListMemFS* fs = MakeSaveDirFS();
    AddSave(*fs, "Quicksave.SAV",        "QUICKSAVE",        1);
    AddSave(*fs, "Autosave.SAV",         "AUTOSAVE",         0);
    AddSave(*fs, "GILDE_SAVEGAME_2.SAV", "GILDE_SAVEGAME_2", 2);
    AddSave(*fs, "GILDE_SAVEGAME_5.SAV", "GILDE_SAVEGAME_5", 5);
    // The VFS tree enumerates NAME-SORTED (VIBE_Vfs_AddFileSorted @0x44ed58), so
    // "ZZ_DUP_TAG2" is seen after GILDE_SAVEGAME_2 and its row-2 claim is rejected.
    AddSave(*fs, "ZZ_DUP_TAG2.SAV",      "ZZ_DUP_TAG2",      2);  // duplicate row -> rejected
    AddSave(*fs, "BADTAG0.SAV",          "BADTAG0",          0);  // tag 0, not reserved -> rejected
    AddSave(*fs, "FAKEQUICK.SAV",        "NOTQUICK",         1);  // tag 1, name != QUICKSAVE -> rejected
    fs->addFile("Resources/gamedata/Saves", "notes.txt");         // .SAV filter drops it
    fs->addFile("Resources/gamedata/Saves", "Corrupt.SAV");       // header fails -> skipped
    fs->put("Resources/gamedata/Saves/Corrupt.SAV", {0x01, 0x02, 0x03});

    play::LoadGameSlotView views[play::kLoadGameSlotCount];
    const play::LoadGameScreenConfig cfg = Cfg(fs);
    const int occupied = play::LoadGameBuildSlotViews(*fs, cfg, views);

    CHECK_EQ(occupied, 4);
    // QUICKSAVE -> row 1, AUTOSAVE -> row 0 (VIBE_SaveBrowser_FindSaveSlot @0x569c50).
    CHECK(views[1].occupied);
    CHECK(views[1].saveName == "QUICKSAVE");
    CHECK(views[1].fileName == "Quicksave.SAV");
    CHECK(views[1].openPath == "Resources/gamedata/Saves/Quicksave.SAV");
    CHECK_EQ(views[1].slotTag, (u8)1);
    CHECK_EQ(views[1].version, (u32)0x10045);
    CHECK(views[1].partial);
    CHECK(views[0].occupied);
    CHECK(views[0].saveName == "AUTOSAVE");
    // Slot-tag rows.
    CHECK(views[2].occupied);
    CHECK(views[2].saveName == "GILDE_SAVEGAME_2");
    CHECK(views[5].occupied);
    CHECK(views[5].saveName == "GILDE_SAVEGAME_5");
    // Everything else stayed empty (rejections + filter + corrupt header).
    for (int i = 0; i < play::kLoadGameSlotCount; ++i)
        if (i != 0 && i != 1 && i != 2 && i != 5)
            CHECK(!views[i].occupied);
    delete fs;
}

TEST(SdlLoadGameScreen, SlotBuild_PartialGateAndFileCap) {
    // The strict original gate ((hdr+4) & 2 -> drop @0x569e1a): with
    // includePartialSaves=false a partial save is dropped, a full one kept.
    ListMemFS* fs = MakeSaveDirFS();
    AddSave(*fs, "Quicksave.SAV",        "QUICKSAVE",        1, /*partial=*/true);
    AddSave(*fs, "GILDE_SAVEGAME_2.SAV", "GILDE_SAVEGAME_2", 2, /*partial=*/false);

    play::LoadGameSlotView views[play::kLoadGameSlotCount];
    play::LoadGameScreenConfig cfg = Cfg(fs);
    cfg.includePartialSaves = false;
    int occupied = play::LoadGameBuildSlotViews(*fs, cfg, views);
    CHECK_EQ(occupied, 1);
    CHECK(!views[1].occupied);              // partial quicksave dropped (1:1 gate)
    CHECK(views[2].occupied);
    CHECK(!views[2].partial);

    // The bridge default admits the partial save.
    cfg.includePartialSaves = true;
    occupied = play::LoadGameBuildSlotViews(*fs, cfg, views);
    CHECK_EQ(occupied, 2);
    CHECK(views[1].occupied);
    delete fs;

    // The 16-file enumeration cap (0x569d64: if (v9 > 16) v9 = 16): seed 18 .SAV
    // files; only the first 16 ENUMERATED (name-sorted — the original tree is
    // sorted, VIBE_Vfs_AddFileSorted @0x44ed58) are even header-checked. The 16
    // real saves sort before the "ZZ_*" extras, fill all rows; the extras never
    // place.
    ListMemFS* fs2 = MakeSaveDirFS();
    AddSave(*fs2, "Quicksave.SAV", "QUICKSAVE", 1);
    AddSave(*fs2, "Autosave.SAV",  "AUTOSAVE",  0);
    for (int t = 2; t <= 15; ++t) {
        const std::string base = "GILDE_SAVEGAME_" + std::to_string(t);
        AddSave(*fs2, (base + ".SAV").c_str(), base.c_str(), (u8)t);
    }
    AddSave(*fs2, "ZZ_EXTRA_A.SAV", "ZZ_EXTRA_A", 9);   // beyond the 16-file cap
    AddSave(*fs2, "ZZ_EXTRA_B.SAV", "ZZ_EXTRA_B", 10);
    play::LoadGameSlotView v2[play::kLoadGameSlotCount];
    const int occ2 = play::LoadGameBuildSlotViews(*fs2, Cfg(fs2), v2);
    CHECK_EQ(occ2, 16);
    for (int i = 0; i < play::kLoadGameSlotCount; ++i) CHECK(v2[i].occupied);
    CHECK(v2[9].saveName == "GILDE_SAVEGAME_9");  // ZZ_EXTRA_A never displaced it
    delete fs2;
}

// ===========================================================================
// Screen: mouse pick -> the 1:1 result protocol.
// ===========================================================================
TEST(SdlLoadGameScreen, MousePickConfirms) {
    ListMemFS* fs = MakeSaveDirFS();
    AddSave(*fs, "Quicksave.SAV",        "QUICKSAVE",        1);
    AddSave(*fs, "GILDE_SAVEGAME_2.SAV", "GILDE_SAVEGAME_2", 2);

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.steps = { OnRow(1, false), OnRow(1, false), OnRow(1, true), OnRow(1, false) };
    plat.maxPumps = 40;

    play::LoadGameScreenResult r = play::RunLoadGameScreen(dev, plat, Cfg(fs));
    CHECK(r.confirmed);
    CHECK(!r.back);
    CHECK_EQ(r.slot, 1);
    CHECK_EQ(r.saveCount, 2);
    CHECK(r.saveName == "QUICKSAVE");
    // byte_122F530: "Gamedata\Saves\%s.SAV" of the slot name (fmt @0x624f40).
    CHECK(r.loadPath == "Gamedata\\Saves\\QUICKSAVE.SAV");
    // The real openable path for the session side.
    CHECK(r.savePath == "Resources/gamedata/Saves/Quicksave.SAV");
    CHECK_EQ(r.sessionFlags, 10);          // word_63C740 = 10 (0x56a426)
    CHECK(r.framesPresented > 0);
    CHECK(dev.presentCount() > 0);
    delete fs;
}

TEST(SdlLoadGameScreen, ClickOnEmptyRowDoesNothing) {
    ListMemFS* fs = MakeSaveDirFS();
    AddSave(*fs, "Quicksave.SAV", "QUICKSAVE", 1);

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    // Click row 7 (empty) repeatedly; the slot scan skips rows with row[12]==0.
    plat.steps = { OnRow(7, false), OnRow(7, false), OnRow(7, true), OnRow(7, false),
                   OnRow(7, true), OnRow(7, false) };
    plat.maxPumps = 16;   // window close ends the run

    play::LoadGameScreenResult r = play::RunLoadGameScreen(dev, plat, Cfg(fs, 200));
    CHECK(!r.confirmed);
    CHECK(r.quitByWindow);
    CHECK_EQ(r.sessionFlags, 0);
    CHECK(r.loadPath.empty());
    delete fs;
}

// ===========================================================================
// Screen: keyboard selection (down to the next occupied row, Enter confirms).
// ===========================================================================
TEST(SdlLoadGameScreen, KeyboardPickConfirms) {
    ListMemFS* fs = MakeSaveDirFS();
    AddSave(*fs, "Autosave.SAV",         "AUTOSAVE",         0);   // row 0 (first occupied)
    AddSave(*fs, "GILDE_SAVEGAME_3.SAV", "GILDE_SAVEGAME_3", 3);   // row 3

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    // Selection seeds at row 0; Down jumps over the empty rows 1..2 to row 3;
    // Enter (release between edges) confirms the selection.
    plat.steps = { {}, {}, Key(0x28), {}, Key(0x0D), {} };
    plat.maxPumps = 40;

    play::LoadGameScreenResult r = play::RunLoadGameScreen(dev, plat, Cfg(fs));
    CHECK(r.confirmed);
    CHECK_EQ(r.slot, 3);
    CHECK(r.saveName == "GILDE_SAVEGAME_3");
    CHECK(r.loadPath == "Gamedata\\Saves\\GILDE_SAVEGAME_3.SAV");
    CHECK_EQ(r.sessionFlags, 10);
    delete fs;
}

// ===========================================================================
// Screen: ESC cancels (dword_672230 -> dword_631614 = 1, return 0).
// ===========================================================================
TEST(SdlLoadGameScreen, EscCancels) {
    ListMemFS* fs = MakeSaveDirFS();
    AddSave(*fs, "Quicksave.SAV", "QUICKSAVE", 1);

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.steps = { {}, {}, Key(0x1B), {} };
    plat.maxPumps = 40;

    play::LoadGameScreenResult r = play::RunLoadGameScreen(dev, plat, Cfg(fs));
    CHECK(!r.confirmed);
    CHECK(r.back);
    CHECK(!r.quitByWindow);
    CHECK_EQ(r.sessionFlags, 0);    // word_63C740 untouched on cancel
    CHECK(r.loadPath.empty());
    CHECK_EQ(r.saveCount, 1);       // the list was built before the cancel
    delete fs;
}

TEST(SdlLoadGameScreen, EmptySaveDirListsNothing) {
    ListMemFS* fs = MakeSaveDirFS();
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.steps = { {}, {}, Key(0x1B), {} };
    plat.maxPumps = 40;
    play::LoadGameScreenResult r = play::RunLoadGameScreen(dev, plat, Cfg(fs));
    CHECK(!r.confirmed);
    CHECK(r.back);
    CHECK_EQ(r.saveCount, 0);
    delete fs;
}

// ===========================================================================
// Screen: the byte_63CC40 confirm gate (message box 257).
// ===========================================================================
TEST(SdlLoadGameScreen, ConfirmGateAcceptAndCancel) {
    ListMemFS* fs = MakeSaveDirFS();
    AddSave(*fs, "Quicksave.SAV", "QUICKSAVE", 1);

    // Accept: click row 1 -> overlay -> Enter.
    {
        shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
        SeqPlatform plat;
        plat.steps = { OnRow(1, false), OnRow(1, false), OnRow(1, true), OnRow(1, false),
                       Key(0x0D), OnRow(1, false) };
        plat.maxPumps = 40;
        play::LoadGameScreenConfig cfg = Cfg(fs);
        cfg.confirmGate = true;
        play::LoadGameScreenResult r = play::RunLoadGameScreen(dev, plat, cfg);
        CHECK(r.confirmed);
        CHECK_EQ(r.sessionFlags, 10);
    }
    // Cancel: click row 1 -> overlay -> ESC; the screen keeps running (LABEL_2
    // continue), then a second ESC cancels the whole screen.
    {
        shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
        SeqPlatform plat;
        plat.steps = { OnRow(1, false), OnRow(1, false), OnRow(1, true), OnRow(1, false),
                       Key(0x1B), OnRow(1, false), {}, Key(0x1B), {} };
        plat.maxPumps = 60;
        play::LoadGameScreenConfig cfg = Cfg(fs, 200);
        cfg.confirmGate = true;
        play::LoadGameScreenResult r = play::RunLoadGameScreen(dev, plat, cfg);
        CHECK(!r.confirmed);
        CHECK_EQ(r.sessionFlags, 0);
        CHECK(r.loadPath.empty());
    }
    delete fs;
}

// ===========================================================================
// HARDENING (wave-12): malformed / truncated / boundary save slots.
// ===========================================================================

// Truncated / corrupt headers must be skipped (SaveLoadHeaderAndThumbnail fails),
// never placed, and never read out of the row table. ASAN guards any stray access.
TEST(SdlLoadGameScreen, CorruptAndTruncatedHeadersSkipped) {
    ListMemFS* fs = MakeSaveDirFS();
    AddSave(*fs, "GILDE_SAVEGAME_4.SAV", "GILDE_SAVEGAME_4", 4);   // one good save
    // A handful of malformed .SAV files: empty, 1-byte, a few junk bytes, a
    // partial-but-truncated header. None must place a row or fault.
    fs->addFile("Resources/gamedata/Saves", "Empty.SAV");
    fs->put("Resources/gamedata/Saves/Empty.SAV", {});
    fs->addFile("Resources/gamedata/Saves", "OneByte.SAV");
    fs->put("Resources/gamedata/Saves/OneByte.SAV", {0x45});
    fs->addFile("Resources/gamedata/Saves", "Junk.SAV");
    fs->put("Resources/gamedata/Saves/Junk.SAV", {0,1,2,3,4,5,6,7,8,9});
    fs->addFile("Resources/gamedata/Saves", "ShortHdr.SAV");
    fs->put("Resources/gamedata/Saves/ShortHdr.SAV",
            std::vector<u8>(20, 0xAB));   // header read overruns the file -> fail

    play::LoadGameSlotView views[play::kLoadGameSlotCount];
    const int occ = play::LoadGameBuildSlotViews(*fs, Cfg(fs), views);
    CHECK_EQ(occ, 1);
    CHECK(views[4].occupied);
    CHECK(views[4].saveName == "GILDE_SAVEGAME_4");
    for (int i = 0; i < play::kLoadGameSlotCount; ++i)
        if (i != 4) CHECK(!views[i].occupied);
    delete fs;
}

// A save whose embedded name fills the full 32-byte field (no room for a NUL in
// the on-disk field) must still produce a NUL-terminated 32-char saveName — the
// build copies 32 bytes into a 33-byte scratch and sets nameZ[32]=0.
TEST(SdlLoadGameScreen, FullWidthNameTerminatesCleanly) {
    ListMemFS* fs = MakeSaveDirFS();
    // 40 chars requested; the header field is 32 wide so it is truncated to 31 by
    // strncpy in the fixture, but exercise the build's own 32-byte memcpy path.
    AddSave(*fs, "Long.SAV", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 6);
    play::LoadGameSlotView views[play::kLoadGameSlotCount];
    const int occ = play::LoadGameBuildSlotViews(*fs, Cfg(fs), views);
    CHECK_EQ(occ, 1);
    CHECK(views[6].occupied);
    CHECK(views[6].saveName.size() <= 32);    // no over-read of the 32-byte field
    delete fs;
}

// Render an occupied slot whose name is far wider than a tiny framebuffer: the
// row-label DrawText must not write past the scratch (right-edge band). ASAN
// turns any overrun into a failure; we just need the screen to run a few frames.
TEST(SdlLoadGameScreen, OverlongNameTinyBufferNoOob) {
    ListMemFS* fs = MakeSaveDirFS();
    AddSave(*fs, "GILDE_SAVEGAME_2.SAV",
            "A_VERY_LONG_SAVE_NAME_THAT_EXCEEDS_THE_PANEL", 2);
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(48, 40, 32, false));
    SeqPlatform plat;
    plat.steps = { {}, {} };
    plat.maxPumps = 8;
    play::LoadGameScreenConfig cfg = Cfg(fs, 6);
    cfg.fbW = 48; cfg.fbH = 40;
    play::LoadGameScreenResult r = play::RunLoadGameScreen(dev, plat, cfg);
    CHECK(r.framesPresented > 0);
    CHECK_EQ(r.saveCount, 1);
    delete fs;
}

// Out-of-range confirm slot: a confirmed pick must only index views[] when the
// matched slot is in [0,16). Drive an empty list + immediate window close; the
// result protocol must not dereference an unset slot.
TEST(SdlLoadGameScreen, NoSavesConfirmStaysInBounds) {
    ListMemFS* fs = MakeSaveDirFS();
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.steps = { {} };
    plat.maxPumps = 3;     // close immediately
    play::LoadGameScreenResult r = play::RunLoadGameScreen(dev, plat, Cfg(fs, 200));
    CHECK(!r.confirmed);
    CHECK(r.savePath.empty());
    CHECK(r.saveName.empty());
    CHECK_EQ(r.slot, -1);
    delete fs;
}

// ===========================================================================
// Real localized texts (guarded on GUILD_GAME_DIR): the `_OPTIONEN_*` entries
// resolve by NAME from the shipped textbin (title 0x1864 / empty-slot fmt
// dword_8C9A04 / confirm box 257 text dword_8C9A08).
// ===========================================================================
TEST(SdlLoadGameScreen, RealTextsLoadWhenAssetsPresent) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir || !*dir) { std::printf("    (skipped: GUILD_GAME_DIR not set)\n"); return; }
    play::LoadGameTexts t;
    const bool ok = play::LoadLoadGameTexts(dir, t);
    CHECK(ok);
    if (ok) {
        CHECK(!t.title.empty());      // _OPTIONEN_MENUE_LOAD+0
        CHECK(!t.emptyFmt.empty());   // _OPTIONEN_LOAD_GAME_SLOT_INFO_LEER+0
        CHECK(!t.confirm.empty());    // _OPTIONEN_LOAD_GAME_SICHERHEITS_ABFRAGE+0
        std::printf("    title='%s' empty='%s'\n", t.title.c_str(), t.emptyFmt.c_str());
    }
}

// ===========================================================================
// Layout sanity: 16 rows + the back row hit-test exactly what the renderer draws.
// ===========================================================================
TEST(SdlLoadGameScreen, LayoutHitTestMatchesRows) {
    const play::LoadGameScreenLayout L = play::LoadGameComputeLayout(800, 600);
    // At scroll=0, only rows whose body falls inside the viewport are clickable
    // (the engine only hits visible rows; off-viewport rows need the scrollbar).
    int visible = 0;
    for (int i = 0; i < play::kLoadGameSlotCount; ++i) {
        int rx, ry, rw, rh; L.RowRect(i, /*scroll=*/0, rx, ry, rw, rh);
        CHECK(rw > 0);
        CHECK(rh > 0);
        const int cy = ry + rh / 2;
        if (cy >= L.py && cy < L.py + L.viewH) {
            CHECK_EQ(L.HitRow(rx + rw / 2, cy, /*scroll=*/0), i);
            ++visible;
        }
    }
    CHECK(visible >= 1);   // at least the first rows are visible
    CHECK_EQ(L.HitRow(L.backX + L.backW / 2, L.backY + L.backH / 2, 0),
             play::kLoadGameSlotCount);    // the back row (design==fb at 800x600)
    CHECK_EQ(L.HitRow(0, 0, 0), -1);
    // After scrolling to the bottom, the last row becomes clickable.
    const int last = play::kLoadGameSlotCount - 1;
    int rx, ry, rw, rh; L.RowRect(last, L.maxScroll(), rx, ry, rw, rh);
    CHECK_EQ(L.HitRow(rx + rw / 2, ry + rh / 2, L.maxScroll()), last);
}

// ===========================================================================
// Native 1:1 layout: NATIVE 800x600 + CENTER-TRANSLATE only (no fbW/800 scale).
// The form `menu\loadgame_new`: WIN0 (128,136,449,575), body WIN1 (168,184,
// 390,493); rows are 16px-tall child windows stacked from the body top
// (AddChildWindow h=16 @0x569e88).
// ===========================================================================
TEST(SdlLoadGameScreen, NativeFormGeometry) {
    // At native size the center-translate offset is zero and the body window maps
    // straight onto WIN1's form rect.
    const play::LoadGameScreenLayout L = play::LoadGameComputeLayout(800, 600);
    CHECK_EQ(L.ox, 0);
    CHECK_EQ(L.oy, 0);
    CHECK_EQ(L.px, 168);   // WIN1 x
    CHECK_EQ(L.py, 184);   // WIN1 y
    CHECK_EQ(L.pw, 390);   // WIN1 w
    CHECK_EQ(L.ph, 493);   // WIN1 h
    CHECK_EQ(L.rowH, 130); // AddChildWindow 130*slot @0x569e92 (NOT a fbH/600 scale)
    CHECK_EQ(L.thumbW, 160);  // io::kThumbWidth  — save preview
    CHECK_EQ(L.thumbH, 120);  // io::kThumbHeight
    CHECK_EQ(L.nameDX, 168);  // Object_AddTextLabel(168,..) @0x569ebd
    // The rows are inside the body window horizontally; row 0 sits at the body top.
    int rx, ry, rw, rh; L.RowRect(0, /*scroll=*/0, rx, ry, rw, rh);
    CHECK_EQ(rx, L.px);
    CHECK(rx + rw <= L.px + L.pw);
    CHECK_EQ(ry, L.py);

    // A larger framebuffer must center-translate, never scale: widget sizes stay
    // native; only the canvas origin shifts by ((W-800)/2,(H-600)/2).
    const play::LoadGameScreenLayout B = play::LoadGameComputeLayout(1024, 768);
    CHECK_EQ(B.ox, (1024 - 800) / 2);
    CHECK_EQ(B.oy, (768 - 600) / 2);
    CHECK_EQ(B.pw, 390);   // body width unchanged (NOT 390*1024/800)
    CHECK_EQ(B.ph, 493);
    CHECK_EQ(B.rowH, 130); // native row stride unchanged
    CHECK_EQ(B.px, 168 + B.ox);
    CHECK_EQ(B.py, 184 + B.oy);
}

// The occupied-row info captions and empty-slot placeholder resolve by NAME from
// the shipped textbin (`_OPTIONEN_LOAD_GAME_SLOT_INFO+0/+1` and `..._LEER`).
TEST(SdlLoadGameScreen, RealSlotInfoCaptionsResolve) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir || !*dir) { std::printf("    (skipped: GUILD_GAME_DIR not set)\n"); return; }
    play::LoadGameTexts t;
    const bool ok = play::LoadLoadGameTexts(dir, t);
    CHECK(ok);
    if (ok) {
        // The empty-slot format carries the %i row-index hole.
        CHECK(t.emptyFmt.find("%i") != std::string::npos);
        // At least one of the occupied-row info captions resolved.
        CHECK(!t.slotInfo0.empty() || !t.slotInfo1.empty());
    }
}
