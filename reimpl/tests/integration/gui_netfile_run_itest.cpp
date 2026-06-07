// Integration tests for gui::Menu_ChooseNetworkMode @0x529a64 + gui::Menu_RunFileSelector
// @0x569668 — scripted edges against each widget id -> the EXACT global-flag mutations
// (word_63C740 / dword_631614), which child sub-screen fired, and the OK/cancel result.
#include "test.h"

#include "gui/netfile_run.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// Network mode: one click on a chosen radio id on frame 0; loop runs maxFrames frames.
struct NetClickHooks : NetModeHooks {
    int idHost = 100, idSearch = 101, idProfile = 102;
    int clickFrame = 0;
    int clickId = -1;
    bool cancel = false;
    bool hostOK = false, searchOK = false, profileOK = false;
    std::string fired;

    int FormLoad() override { return 7; }
    int RenderRichString(int t) override { return t; }
    int GetChildObjectId(int, int text) override {
        // deterministic ids tied to the rendered text id.
        if (text == kNetModeTextHost) return idHost;
        if (text == kNetModeTextSearch) return idSearch;
        if (text == kNetModeTextProfile) return idProfile;
        return -1;
    }
    int RadioGroupCreate(int, int) override { return 555; }
    int RunFrameLoop(int) override { return 1; }
    bool ClickReady(int frame) override { return frame == clickFrame; }
    int HoverId(int frame) override { return frame == clickFrame ? clickId : -1; }
    bool CancelEdge(int frame) override { return cancel && frame == clickFrame; }

    bool RunHostNetworkSetup() override { fired = "Host"; return hostOK; }
    bool SearchNetworkGames(int) override { fired = "Search"; return searchOK; }
    bool ChooseNetworkProfile() override { fired = "Profile"; return profileOK; }
};

struct FileClickHooks : FileSelHooks {
    std::vector<std::string> listing;
    int nextId = 200;
    std::vector<int> rowIds;
    int okId = -1, editId = -1;
    int clickFrame = 0;
    int clickRow = -1;       // index into rowIds to click
    bool clickOk = false;    // click the OK button instead
    bool cancel = false;
    std::string editText;    // last text pushed in (build init / Select copy)
    std::string typed;       // user-typed field contents GetDataPtr returns on OK

    int FormLoad() override { return 9; }
    std::vector<std::string> DirectoryListing(const std::string&) override { return listing; }
    int RenderRichString(int t) override { return t; }
    int GetChildObjectId(int, int text) override {
        int id = nextId++;
        if (text == kFileSelTextOk) okId = id;
        else editId = id;     // the (form,0) edit-field call
        return id;
    }
    int AddTextLabel(int, const std::string&) override {
        int id = nextId++; rowIds.push_back(id); return id;
    }
    void EditFieldSetText(int, const std::string& t) override { editText = t; }
    std::string EditFieldGetText(int) override { return typed.empty() ? editText : typed; }

    int RunFrameLoop(int) override { return 1; }
    bool CancelEdge(int frame) override { return cancel && frame == clickFrame; }
    bool ListClickEdge(int frame) override { return clickRow >= 0 && frame == clickFrame; }
    bool OkReady(int frame) override { return clickOk && frame == clickFrame; }
    int HoverId(int frame) override {
        if (frame != clickFrame) return -1;
        if (clickOk) return okId;
        if (clickRow >= 0 && clickRow < (int)rowIds.size()) return rowIds[clickRow];
        return -1;
    }
};

} // namespace

// ---- network mode dispatch ----

TEST(NetfileRunI, NetMode_HostSuccess) {
    NetClickHooks h; h.clickId = h.idHost; h.hostOK = true;
    NetModeHooks* prev = SetNetModeHooks(&h);
    NetModeState st; NetModeRecord rec;
    int r = Menu_ChooseNetworkMode(st, &rec, 1);
    SetNetModeHooks(prev);

    CHECK_EQ(r, 1);
    CHECK_EQ(st.session, kNetModeSessHost); // word_63C740 = 5
    CHECK_EQ(st.close, 1);                  // dword_631614 = 1
    CHECK_EQ(h.fired, std::string("Host"));
}

TEST(NetfileRunI, NetMode_HostCancelledChild) {
    NetClickHooks h; h.clickId = h.idHost; h.hostOK = false;
    NetModeHooks* prev = SetNetModeHooks(&h);
    NetModeState st; NetModeRecord rec;
    int r = Menu_ChooseNetworkMode(st, &rec, 1);
    SetNetModeHooks(prev);

    CHECK_EQ(r, 0);                         // child failed -> no commit
    CHECK_EQ(st.session, kNetModeSessHost); // session still set to 5 before the child
    CHECK_EQ(st.close, 0);                  // not armed (child returned 0)
}

TEST(NetfileRunI, NetMode_SearchSuccessSetsSession5) {
    NetClickHooks h; h.clickId = h.idSearch; h.searchOK = true;
    NetModeHooks* prev = SetNetModeHooks(&h);
    NetModeState st; NetModeRecord rec;
    int r = Menu_ChooseNetworkMode(st, &rec, 1);
    SetNetModeHooks(prev);

    CHECK_EQ(r, 1);
    CHECK_EQ(st.session, kNetModeSessHost); // search also uses word_63C740 = 5
    CHECK_EQ(st.close, 1);
    CHECK_EQ(h.fired, std::string("Search"));
}

TEST(NetfileRunI, NetMode_ProfileSuccessSetsSession4) {
    NetClickHooks h; h.clickId = h.idProfile; h.profileOK = true;
    NetModeHooks* prev = SetNetModeHooks(&h);
    NetModeState st; NetModeRecord rec;
    int r = Menu_ChooseNetworkMode(st, &rec, 1);
    SetNetModeHooks(prev);

    CHECK_EQ(r, 1);
    CHECK_EQ(st.session, kNetModeSessProfile); // word_63C740 = 4
    CHECK_EQ(st.close, 1);
    CHECK_EQ(h.fired, std::string("Profile"));
}

TEST(NetfileRunI, NetMode_CancelArmsCloseButReturnsZero) {
    NetClickHooks h; h.cancel = true; h.clickId = -1;
    NetModeHooks* prev = SetNetModeHooks(&h);
    NetModeState st; NetModeRecord rec;
    int r = Menu_ChooseNetworkMode(st, &rec, 1);
    SetNetModeHooks(prev);

    CHECK_EQ(r, 0);              // cancel -> false
    CHECK_EQ(st.close, 1);       // dword_631614 = 1
    CHECK_EQ(st.session, 0);     // no branch hit -> session untouched
    CHECK_EQ(h.fired, std::string()); // no child fired
}

// ---- file selector dispatch ----

TEST(NetfileRunI, FileSel_DirectClickCommits) {
    FileClickHooks h; h.listing = {"BERLIN.INI", "AUGSBURG.INI"}; h.clickRow = 1;
    FileSelHooks* prev = SetFileSelHooks(&h);
    std::string out; FileSelRecord rec;
    int r = Menu_RunFileSelector("\\project\\gfx", ".INI", true, 0x10, out, &rec, 4);
    SetFileSelHooks(prev);

    CHECK_EQ(r, 1);
    CHECK_EQ(out, std::string("\\project\\gfx\\AUGSBURG.INI"));
    CHECK_EQ(rec.close, 1);     // dword_631614 = 1
}

TEST(NetfileRunI, FileSel_DirectFirstRow) {
    FileClickHooks h; h.listing = {"BERLIN.INI", "AUGSBURG.INI"}; h.clickRow = 0;
    FileSelHooks* prev = SetFileSelHooks(&h);
    std::string out; FileSelRecord rec;
    int r = Menu_RunFileSelector("dir", ".INI", true, 0x10, out, &rec, 4);
    SetFileSelHooks(prev);

    CHECK_EQ(r, 1);
    CHECK_EQ(out, std::string("dir\\BERLIN.INI"));
}

TEST(NetfileRunI, FileSel_EditClickCopiesToFieldNoCommit) {
    FileClickHooks h; h.listing = {"SAVE1.SAV", "SAVE2.SAV"}; h.clickRow = 0;
    FileSelHooks* prev = SetFileSelHooks(&h);
    std::string out; FileSelRecord rec;
    int r = Menu_RunFileSelector("saves", ".SAV", false, 0x10, out, &rec, 4);
    SetFileSelHooks(prev);

    CHECK_EQ(r, 0);                       // list click in edit mode -> no commit
    CHECK_EQ(h.editText, std::string("SAVE1")); // copied (extension-stripped) to field
    CHECK_EQ(out, std::string());
}

TEST(NetfileRunI, FileSel_EditOkCommitsFieldText) {
    FileClickHooks h; h.listing = {"SAVE1.SAV"}; h.clickOk = true; h.typed = "MYGAME";
    FileSelHooks* prev = SetFileSelHooks(&h);
    std::string out; FileSelRecord rec;
    int r = Menu_RunFileSelector("saves", ".SAV", false, 0x10, out, &rec, 4);
    SetFileSelHooks(prev);

    CHECK_EQ(r, 1);
    CHECK_EQ(out, std::string("saves\\MYGAME.SAV")); // dir\\<editfield><ext>
    CHECK_EQ(rec.close, 1);
}

TEST(NetfileRunI, FileSel_CancelArmsCloseNoCommit) {
    FileClickHooks h; h.listing = {"BERLIN.INI"}; h.cancel = true;
    FileSelHooks* prev = SetFileSelHooks(&h);
    std::string out; FileSelRecord rec;
    int r = Menu_RunFileSelector("dir", ".INI", true, 0x10, out, &rec, 1);
    SetFileSelHooks(prev);

    CHECK_EQ(r, 0);            // cancel -> false
    CHECK_EQ(rec.close, 1);    // dword_631614 = 1
    CHECK_EQ(out, std::string());
}
