// Unit tests for gui::Map_LoadCityFile / Menu_FormatMissionBuildingName /
// Menu_BuildChooseMissionDialog (build-time / single-call behavior).
#include "test.h"

#include "gui/mission_load_run.h"

#include <cstring>
#include <string>

using namespace guild::gui;

namespace {

// Records the exact path + flags Map_LoadCityFile builds.
struct RecHooks : MapLoadCityHooks {
    char path[256] = {0};
    const char* tag = nullptr;
    int mode = -99, a3 = -99;
    const char* enteredCity = nullptr;
    int sceneRet = 7;
    const char* loadedScene = nullptr;
    int writeCalls = 0, resetCalls = 0, universeCalls = 0;

    void SceneEnterCity(const char* c) override { enteredCity = c; }
    void SaveWriteGameFile(const char* p, const char* t, int aa3, int m) override {
        std::snprintf(path, sizeof(path), "%s", p); tag = t; a3 = aa3; mode = m; ++writeCalls;
    }
    void BuildingResetAll() override { ++resetCalls; }
    void UniverseSwitchAndReset() override { ++universeCalls; }
    int  SceneLoadFromStream(const char* p) override { loadedScene = p; return sceneRet; }
};

} // namespace

TEST(MissionLoad, LoadCity_NonNetwork_BuildsCtyPath) {
    RecHooks h;
    MapLoadCityHooks* prev = Map_SetLoadCityHooks(&h);

    MapLoadCityRecord rec;
    int r = Map_LoadCityFile(/*networkFlag*/ 0, "Augsburg", &rec);

    Map_SetLoadCityHooks(prev);

    CHECK_EQ(std::string(h.path), std::string("gamedata/cities/Augsburg.CTY"));
    CHECK_EQ(std::string(rec.path), std::string("gamedata/cities/Augsburg.CTY"));
    CHECK_EQ(std::string(h.tag), std::string("city"));   // aCity write key
    CHECK_EQ(h.mode, 2);                                  // Save_WriteGameFile mode 2
    CHECK_EQ(h.a3, 0);
    CHECK_EQ(std::string(h.enteredCity), std::string("Augsburg"));
    CHECK_EQ(std::string(h.loadedScene), std::string("scenes/*ChooseCity.ed3"));
    CHECK_EQ(r, 7);                                       // returns SceneLoadFromStream byte
    CHECK_EQ(h.writeCalls, 1);
    CHECK_EQ(h.resetCalls, 1);
    CHECK_EQ(h.universeCalls, 2);                         // reset pre + post
    CHECK_EQ(rec.networkFlag, 0);
    CHECK(rec.wroteFile);
    CHECK(rec.resetWorld);
}

TEST(MissionLoad, LoadCity_Network_BuildsNetPath) {
    RecHooks h;
    MapLoadCityHooks* prev = Map_SetLoadCityHooks(&h);

    int r = Map_LoadCityFile(/*networkFlag*/ 1, "Koeln", nullptr);

    Map_SetLoadCityHooks(prev);

    CHECK_EQ(std::string(h.path), std::string("gamedata/cities/Koeln.NET"));
    CHECK_EQ(r, 7);
    (void)r;
}

TEST(MissionLoad, FormatBuildingName_SeedsScratch_TwoByteStride) {
    // The original copy loop steps BOTH src and dst by 2 each iteration (dst[i]=src[i],
    // dst[i+1]=src[i+1]), so for byte strings it is a faithful copy terminating at the NUL.
    MissionNameScratch out;
    unsigned len = Menu_FormatMissionBuildingName("Hello", "World", out);

    // Scratch seed values (gilde.exe constants).
    CHECK_EQ(out.d122F4A0, 856692811u);
    CHECK_EQ(out.d122F4A4, 1342u);
    CHECK_EQ(out.d122F528, 1555u);
    CHECK_EQ((int)out.b122F4A8, 0);
    CHECK_EQ((int)out.b122F4A9, 0);

    // "Hello" copied verbatim into String; "World" into byte_122F4CA.
    CHECK_EQ(std::string(out.primary), std::string("Hello"));
    CHECK_EQ(std::string(out.secondary), std::string("World"));

    // No lookup hook installed -> recordLen 0.
    CHECK_EQ(len, 0u);
    CHECK_EQ(out.recordLen, 0u);
}

// A lookup hook so we can assert the SHIBYTE(dword_122F4A0) -> type byte and qmemcpy len.
namespace {
struct NameHooks : MissionNameHooks {
    int sawType = -1;
    unsigned LookupBuildingTypeRecord(int t, void* buf) override {
        sawType = t;
        static const char rec[4] = {1,2,3,4};
        std::memcpy(buf, rec, sizeof(rec));
        return sizeof(rec);
    }
};
}

TEST(MissionLoad, FormatBuildingName_TypeByteAndRecordCopy) {
    NameHooks nh;
    MissionNameHooks* prev = Menu_SetMissionNameHooks(&nh);

    MissionNameScratch out;
    unsigned len = Menu_FormatMissionBuildingName("x", "y", out);

    Menu_SetMissionNameHooks(prev);

    // SHIBYTE(856692811) = (856692811 >> 24) & 0xFF = 0x33 = 51.
    CHECK_EQ(nh.sawType, 51);
    CHECK_EQ(len, 4u);
    CHECK_EQ(out.recordLen, 4u);
    CHECK_EQ(out.record[0], (char)1);
    CHECK_EQ(out.record[3], (char)4);
}

// Build-only dialog hooks: loop runs zero frames (RunFrameLoop default 0).
namespace {
struct DlgBuildHooks : MissionDialogHooks {
    int nextSlot = 100;
    int titleCalls = 0, rowRenders = 0;
    int lastTitleId = -1;
    int rowIds[8]; int rowIdCount = 0;
    int enableSets = 0, clearFlags = 0;
    int formLoaded = 0, formDestroyed = 0;

    int  FormLoad() override { ++formLoaded; return 55; }
    void FormDestroy(int) override { ++formDestroyed; }
    void TextRenderRichString(const char* fmt, int arg, const char*) override {
        if (std::strcmp(fmt, "$Z%s") == 0) { ++titleCalls; lastTitleId = arg; }
        else { if (rowIdCount < 8) rowIds[rowIdCount++] = arg; ++rowRenders; }
    }
    int  FormGetChildObjectId(int) override { return nextSlot++; }
    void SetRowEnabled(int) override { ++enableSets; }
    void ClearRowFlag2(int) override { ++clearFlags; }
};
}

TEST(MissionLoad, MissionDialog_BuildsFiveRowsPlusOkAndEnable) {
    DlgBuildHooks h;
    MissionDialogHooks* prev = Menu_SetMissionDialogHooks(&h);

    int seed[5] = {0,0,0,0,0};
    MissionDialogResult res;
    int r = Menu_BuildChooseMissionDialog(seed, res, /*maxFrames*/ 0);

    Menu_SetMissionDialogHooks(prev);

    CHECK_EQ(r, 0);                       // no frame, no selection
    CHECK_EQ(h.formLoaded, 1);
    CHECK_EQ(h.formDestroyed, 1);
    CHECK_EQ(h.titleCalls, 1);
    CHECK_EQ(h.lastTitleId, 0x1CB2);      // title rich id 7346
    CHECK_EQ(h.enableSets, 5);            // +68=1 per row
    CHECK_EQ(h.clearFlags, 5);            // +444 &= ~2 per row

    // Row rich ids 7347..7351 then 7352 (enable) then 7353 (OK).
    const int want[7] = {7347,7348,7349,7350,7351,7352,7353};
    CHECK_EQ(h.rowIdCount, 7);
    for (int i = 0; i < 7; ++i) CHECK_EQ(h.rowIds[i], want[i]);

    // Built object slots are recorded; OK/enable are the last two issued.
    CHECK(res.rowObj[0] >= 0);
    CHECK_EQ(res.enableObj, res.rowObj[4] + 1);
    CHECK_EQ(res.okObj, res.enableObj + 1);
    CHECK(!res.impliesMissionFlags);     // no success -> no menu-side flags
}
