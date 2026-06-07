// Unit tests for gui::Menu_ChooseNetworkMode @0x529a64 and gui::Menu_RunFileSelector
// @0x569668 — the EXACT build recorded via the hooks: the 3-radio network column
// (host/search/profile ids + RadioGroup_Create(3,host) + byte_63C8F4=-2 seed), and the
// file-selector row build (one AddTextLabel per enumerated file at y=24*i) for both the
// direct and edit modes (edit mode also builds the OK button + edit field).
#include "test.h"

#include "gui/netfile_run.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// Recording hooks for the network mode chooser: hand out deterministic widget ids and
// drive zero frames (RunFrameLoop default returns 0).
struct NetBuildHooks : NetModeHooks {
    int nextId = 100;
    int formHandle = 7;
    int FormLoad() override { return formHandle; }
    int RenderRichString(int textId) override { return textId; } // pass-through text handle
    int GetChildObjectId(int, int text) override {
        // map the rendered text id to a fresh widget id, recording the text->id pairing.
        int id = nextId++;
        lastText = text; lastId = id;
        return id;
    }
    int RadioGroupCreate(int count, int firstId) override {
        radioCount = count; radioFirst = firstId; return 555;
    }
    int lastText = 0, lastId = 0, radioCount = 0, radioFirst = 0;
};

struct FileBuildHooks : FileSelHooks {
    std::vector<std::string> listing;
    int nextId = 200;
    int FormLoad() override { return 9; }
    std::vector<std::string> DirectoryListing(const std::string&) override { return listing; }
    int AddTextLabel(int, const std::string&) override { return nextId++; }
    int GetChildObjectId(int, int) override { return nextId++; }
    int RenderRichString(int t) override { return t; }
};

} // namespace

TEST(NetfileRun, NetMode_BuildThreeRadioColumn) {
    NetBuildHooks hooks;
    NetModeHooks* prev = SetNetModeHooks(&hooks);

    NetModeState st;
    NetModeRecord rec;
    int r = Menu_ChooseNetworkMode(st, &rec, /*maxFrames*/ 0);

    SetNetModeHooks(prev);

    CHECK_EQ(r, 0);                       // no frames -> no pick -> 0
    CHECK_EQ(rec.radioCount, 3);          // RadioGroup_Create(3, host)
    CHECK_EQ(hooks.radioCount, 3);
    CHECK_EQ(hooks.radioFirst, rec.idHost);
    CHECK_EQ(rec.radioGroup, 555);
    CHECK_EQ(rec.byteState, 0xFE);        // byte_63C8F4 = -2
    // host/search/profile got distinct ascending ids (build order).
    CHECK(rec.idHost == 100);
    CHECK(rec.idSearch == 101);
    CHECK(rec.idProfile == 102);
    // build then destroy trace.
    CHECK_EQ(std::string(rec.trace[0]), std::string("FormLoad"));
    CHECK_EQ(std::string(rec.trace[1]), std::string("RadioGroupCreate"));
    CHECK_EQ(std::string(rec.trace[rec.traceCount - 1]), std::string("Destroy"));
}

TEST(NetfileRun, FileSel_DirectMode_RowsOnly) {
    FileBuildHooks hooks;
    hooks.listing = {"BERLIN.INI", "AUGSBURG.INI", "KOELN.INI"};
    FileSelHooks* prev = SetFileSelHooks(&hooks);

    std::string out;
    FileSelRecord rec;
    int r = Menu_RunFileSelector("\\project\\gfx", ".INI", /*direct*/ true,
                                 /*listText*/ 0x1234, out, &rec, /*maxFrames*/ 0);

    SetFileSelHooks(prev);

    CHECK_EQ(r, 0);                 // no frames -> no commit
    CHECK_EQ(rec.directMode, true);
    CHECK_EQ(rec.rowCount, 3);
    CHECK_EQ(rec.idOk, -1);         // no OK button in direct mode
    CHECK_EQ(rec.idEdit, -1);
    // rows at y = 24*i, names extension-stripped by SaveBrowser_EnumerateSaveFiles.
    CHECK_EQ(rec.rows.size(), (size_t)3);
    CHECK_EQ(rec.rows[0].y, 0);
    CHECK_EQ(rec.rows[1].y, 24);
    CHECK_EQ(rec.rows[2].y, 48);
    CHECK_EQ(rec.rows[0].name, std::string("BERLIN"));
    CHECK_EQ(rec.rows[1].name, std::string("AUGSBURG"));
}

TEST(NetfileRun, FileSel_EditMode_BuildsOkAndEditField) {
    FileBuildHooks hooks;
    hooks.listing = {"SAVE1.SAV", "SAVE2.SAV"};
    FileSelHooks* prev = SetFileSelHooks(&hooks);

    std::string out;
    FileSelRecord rec;
    int r = Menu_RunFileSelector("saves", ".SAV", /*direct*/ false,
                                 /*listText*/ 0x18D0, out, &rec, /*maxFrames*/ 0);

    SetFileSelHooks(prev);

    CHECK_EQ(r, 0);
    CHECK_EQ(rec.directMode, false);
    CHECK_EQ(rec.rowCount, 2);
    CHECK(rec.idOk != -1);   // OK button built (0x18D9)
    CHECK(rec.idEdit != -1); // edit field built
}
