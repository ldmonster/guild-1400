// Integration: the main menu's Load-Game path end to end — play::RunNativeMainMenu
// (the host of the 1:1 gui::Menu_RunMainMenu @0x529d08) -> the Load button click
// dispatch (widget v85 @0x52a82d) -> play::RunLoadGameScreen (the host of the 1:1
// gui::Menu_RunLoadGame @0x56a270) -> a slot pick -> NativeMenuResult{kLoadGame,
// savePath, saveLoadPath, saveName}. Headless device + scripted platform +
// a listDir-capable in-memory fs seeded with real .SAV header fixtures.
#include "test.h"

#include "play/native_main_menu.h"
#include "play/sdl_loadgame_screen.h"
#include "gui/main_menu.h"
#include "io/save.h"
#include "io/vfs.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/mem_filesystem.h"
#include "shim/IPlatform.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;

namespace {

// --- listDir-capable in-memory fs with .SAV header fixtures ------------------
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

void AddSave(ListMemFS& fs, const char* fileName, const char* saveName, u8 slotTag) {
    fs.addFile("Resources/gamedata/Saves", fileName);
    io::VfsInit(&fs, false);
    io::VfsHandle* h =
        io::VfsOpenFile((std::string("Resources/gamedata/Saves/") + fileName).c_str(), "wb");
    CHECK(h != nullptr);
    if (!h) return;
    io::SaveHeader hdr;
    std::memset(&hdr, 0, sizeof hdr);
    hdr.flagByte = 2;   // the session writer's partial format (the bridge admits it)
    std::strncpy(hdr.name, saveName, sizeof hdr.name - 1);
    std::strncpy(hdr.name96, saveName, sizeof hdr.name96 - 1);
    hdr.byte649D50 = slotTag;
    hdr.field132 = 2;
    CHECK(io::SaveWriteScenarioBlock(h, hdr, nullptr));
    io::VfsCloseStream(h);
}

ListMemFS* MakeFsWithSaves() {
    ListMemFS* fs = new ListMemFS();
    fs->dirs["Resources"];
    fs->addSubdir("Resources", "gamedata");
    fs->addSubdir("Resources/gamedata", "Saves");
    AddSave(*fs, "Quicksave.SAV", "QUICKSAVE", 1);
    AddSave(*fs, "GILDE_SAVEGAME_2.SAV", "MEISTERJAHR", 2);
    return fs;
}

// --- scripted platform --------------------------------------------------------
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

SeqPlatform::Step OnMenuButton(int index, bool left) {
    const play::MenuButtonRect r =
        play::MenuButtonScreenRect(gui::MainMenu_ButtonY(index), 320, 240);
    SeqPlatform::Step s; s.x = r.x + r.w / 2; s.y = r.y + r.h / 2; s.left = left; return s;
}
SeqPlatform::Step OnLoadRow(int row, bool left) {
    const play::LoadGameScreenLayout L = play::LoadGameComputeLayout(320, 240);
    int rx, ry, rw, rh; L.RowRect(row, /*scroll=*/0, rx, ry, rw, rh);
    SeqPlatform::Step s; s.x = rx + rw / 2; s.y = ry + rh / 2; s.left = left; return s;
}
SeqPlatform::Step Key(int vk) { SeqPlatform::Step s; s.key = vk; return s; }

std::vector<std::pair<std::string, std::string>> Cities() {
    return { {"AUGSBURG", "Resources/gamedata/Cities/AUGSBURG.cty"} };
}

} // namespace

// ===========================================================================
// Menu -> Load Game -> pick a slot -> NativeMenuResult carries the save.
// ===========================================================================
TEST(MenuLoadGameFlow, PickSaveFromMenu) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(320, 240, 32, false));
    ListMemFS* fs = MakeFsWithSaves();

    const int loadIdx = (int)gui::MainMenuItem::kLoad;
    SeqPlatform plat;
    plat.steps = {
        OnMenuButton(loadIdx, false),   // [0] (unused index-0 slot)
        OnMenuButton(loadIdx, false),   // [1] menu frame: hover Load
        OnMenuButton(loadIdx, true),    // [2] menu frame: click edge -> RunLoadGame
        OnLoadRow(1, false),            // [3] load screen: hover slot 1 (released)
        OnLoadRow(1, true),             // [4] load screen: click edge -> pick QUICKSAVE
        OnLoadRow(1, false),            // [5] back in the menu: close armed
        OnMenuButton(loadIdx, false),   // [6] drain
    };
    plat.maxPumps = 80;

    play::NativeMenuResult r = play::RunNativeMainMenu(
        dev, plat, *fs, /*gameDir=*/"", Cities(), 320, 240, /*frameCapMs=*/0,
        /*maxFrames=*/-1);

    CHECK(r.action == play::NativeMenuResult::kLoadGame);
    CHECK(!r.quitByWindow);
    CHECK(r.saveName == "QUICKSAVE");
    // byte_122F530 — the 1:1 "Gamedata\Saves\%s.SAV" string (fmt @0x624f40).
    CHECK(r.saveLoadPath == "Gamedata\\Saves\\QUICKSAVE.SAV");
    // The session-side contract: a real path openable through the menu's fs.
    CHECK(r.savePath == "Resources/gamedata/Saves/Quicksave.SAV");
    CHECK(fs->exists(r.savePath.c_str()));
    CHECK(r.cityPath.empty());           // not a New-Game result
    CHECK(r.framesPresented > 0);
    delete fs;
}

// ===========================================================================
// Menu -> Load Game -> ESC cancels -> back at the menu (then Quit).
// ===========================================================================
TEST(MenuLoadGameFlow, CancelReturnsToMenu) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(320, 240, 32, false));
    ListMemFS* fs = MakeFsWithSaves();

    const int loadIdx = (int)gui::MainMenuItem::kLoad;
    const int quitIdx = (int)gui::MainMenuItem::kQuit;
    SeqPlatform plat;
    plat.steps = {
        OnMenuButton(loadIdx, false),   // [0]
        OnMenuButton(loadIdx, false),   // [1] hover Load
        OnMenuButton(loadIdx, true),    // [2] click -> load screen
        OnLoadRow(1, false),            // [3] load screen frame
        Key(0x1B),                      // [4] ESC -> close armed in the load screen
        OnMenuButton(quitIdx, false),   // [5] load screen final frame / back in menu
        OnMenuButton(quitIdx, false),   // [6] menu: hover Quit (released)
        OnMenuButton(quitIdx, true),    // [7] menu: click Quit -> menu exits
        OnMenuButton(quitIdx, false),   // [8] drain
    };
    plat.maxPumps = 80;

    play::NativeMenuResult r = play::RunNativeMainMenu(
        dev, plat, *fs, "", Cities(), 320, 240, 0, -1);

    // The cancel fell back to the menu (LABEL_55 re-show); Quit then ended it.
    CHECK(r.action == play::NativeMenuResult::kQuit);
    CHECK(!r.quitByWindow);
    CHECK(r.savePath.empty());
    CHECK(r.saveLoadPath.empty());
    delete fs;
}

// ===========================================================================
// No saves at all: the screen lists 16 empty rows; ESC returns to the menu.
// ===========================================================================
TEST(MenuLoadGameFlow, NoSavesCancelKeepsMenuAlive) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(320, 240, 32, false));
    ListMemFS* fs = new ListMemFS();
    fs->dirs["Resources"];
    fs->addSubdir("Resources", "gamedata");
    fs->addSubdir("Resources/gamedata", "Saves");

    const int loadIdx = (int)gui::MainMenuItem::kLoad;
    SeqPlatform plat;
    plat.steps = {
        OnMenuButton(loadIdx, false),
        OnMenuButton(loadIdx, false),
        OnMenuButton(loadIdx, true),    // -> load screen
        OnLoadRow(4, false),
        OnLoadRow(4, true),             // click an empty row: no match
        OnLoadRow(4, false),
        Key(0x1B),                      // ESC -> back to the menu
        {},
    };
    plat.maxPumps = 24;                 // then the window closes -> menu quits

    play::NativeMenuResult r = play::RunNativeMainMenu(
        dev, plat, *fs, "", Cities(), 320, 240, 0, -1);

    CHECK(r.action == play::NativeMenuResult::kQuit);  // ended by window close
    CHECK(r.quitByWindow);
    CHECK(r.savePath.empty());
    delete fs;
}
