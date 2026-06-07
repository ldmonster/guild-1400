// Unit tests for gui::Menu_RunChooseCity @0x52e6d8 — the EXACT city-file enumeration
// (gamedata/cities, ".CTY"/".NET") -> "stadt_<name>" markers, the recovered constants
// (tower object / cutscene / form names) and the reused city predicates.
#include "test.h"

#include "gui/choosecity_run.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A recording harness: a fixed list of city files; enumeration spawns one marker per file
// (marker handle == 1000+index); no frames run (RunFrameLoop default returns 0).
struct EnumHooks : ChooseCityHooks {
    std::vector<std::string> files;
    int  formSeq = 100;
    std::string lastExt;

    int EnumerateCityFiles(const char* dir, const char* ext,
                           std::vector<std::string>& out) override {
        lastExt = ext ? ext : "";
        (void)dir;
        out = files;
        return static_cast<int>(files.size());
    }
    bool ReadCityName(const std::string& file, std::string& outName) override {
        outName = file; // header name == the file base for the test
        return true;
    }
    int SpawnCityMarker(const std::string& cityName) override {
        // distinct nonzero handle keyed by name length+content sum (deterministic)
        int h = 1000;
        for (char c : cityName) h += static_cast<unsigned char>(c);
        return h;
    }
    int FindTextIndex(const std::string& upperKey) override {
        // the BERLIN city has an info text; others do not
        return upperKey.find("BERLIN") != std::string::npos ? 77 : -1;
    }
    int FormLoad(const char*) override { return formSeq++; }
    int RunFrameLoop(int) override { return 0; } // no frames
};

} // namespace

TEST(ChooseCityRun, RecoveredConstants) {
    // Form names reused from newgame_setup; tower/cutscene/key formats from this module.
    CHECK(std::string(kFormChooseCity) == "Menu\\CHOOSECITY");
    CHECK(std::string(kFormChooseCityHeader) == "Menu\\CHOOSECITY_HEADER");
    CHECK(std::string(kCityTowerObject) == "sp_STADTTURM");
    CHECK(std::string(kCityCutscene) == "Startmenu/A_Stadtwahl.esc");
    CHECK(std::string(kCityDir) == "gamedata/cities");
    CHECK(std::string(kCityNamePrefix) == "stadt_");
    CHECK_EQ(kCityConfirmId, 1210);
    CHECK_EQ(kKeyEnter, 28);
    CHECK_EQ(kCityPickRadius, 96);
    CHECK_EQ(kCancelRenderList, 1773);
}

TEST(ChooseCityRun, ReusedCityPredicates) {
    // ChooseCity_Extension / IsConfirm / IsCityObject come from gui/newgame_setup (REUSED).
    CHECK(std::string(ChooseCity_Extension(false)) == ".CTY");
    CHECK(std::string(ChooseCity_Extension(true)) == ".NET");
    CHECK(ChooseCity_IsConfirm(1210, 0));
    CHECK(ChooseCity_IsConfirm(0, 28));
    CHECK(!ChooseCity_IsConfirm(0, 0));
    CHECK(ChooseCity_IsCityObject("stadt_BERLIN"));
    CHECK(!ChooseCity_IsCityObject("sp_STADTTURM"));
}

TEST(ChooseCityRun, EnumeratesCitiesToMarkers_Local) {
    EnumHooks hooks;
    hooks.files = {"BERLIN", "HAMBURG", "KOELN"};
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    st.network = false;
    ChooseCityRecord rec;
    int r = Menu_RunChooseCity(st, &rec, /*maxFrames*/ 0);

    Menu_SetChooseCityHooks(prev);

    CHECK_EQ(r, 0);                       // no confirm -> v80 = 0
    CHECK(hooks.lastExt == ".CTY");       // local -> ".CTY"
    CHECK(rec.extension == ".CTY");
    CHECK_EQ((int)rec.cities.size(), 3);

    // Each city got a name + a nonzero marker + a status registration.
    for (const CityEntry& e : rec.cities) {
        CHECK(!e.name.empty());
        CHECK(e.marker != 0);
        CHECK(e.registered);
    }
    // BERLIN resolved its info text; the others fell back to -1.
    CHECK_EQ(rec.cities[0].name, std::string("BERLIN"));
    CHECK_EQ(rec.cities[0].infoText, 77);
    CHECK_EQ(rec.cities[1].infoText, -1);

    // Two forms loaded (header + main).
    CHECK(rec.headerForm != -1);
    CHECK(rec.mainForm != -1);
    CHECK(rec.headerForm != rec.mainForm);
}

TEST(ChooseCityRun, NetworkUsesNetExtension) {
    EnumHooks hooks;
    hooks.files = {"NETCITY"};
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    st.network = true;
    ChooseCityRecord rec;
    Menu_RunChooseCity(st, &rec, 0);

    Menu_SetChooseCityHooks(prev);

    CHECK(hooks.lastExt == ".NET");
    CHECK(rec.extension == ".NET");
    CHECK_EQ((int)rec.cities.size(), 1);
}

TEST(ChooseCityRun, FailedMarkerSpawnSkipsRegistration) {
    struct NoMarkerHooks : EnumHooks {
        int SpawnCityMarker(const std::string&) override { return 0; } // spawn fails
    } hooks;
    hooks.files = {"GHOSTTOWN"};
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    ChooseCityRecord rec;
    Menu_RunChooseCity(st, &rec, 0);

    Menu_SetChooseCityHooks(prev);

    CHECK_EQ((int)rec.cities.size(), 1);
    CHECK_EQ(rec.cities[0].marker, 0);
    CHECK(!rec.cities[0].registered); // no marker -> no status-text registration
}
