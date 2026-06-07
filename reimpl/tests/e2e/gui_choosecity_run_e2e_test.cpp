// e2e tests for gui::Menu_EnterChooseCity / Menu_RunChooseCity — a full scripted screen
// session asserting the build + dispatch + cleanup call ORDER (recorded via the hooks),
// deterministic; plus a GUARDED enumeration over the real gamedata/cities directory.
#include "test.h"

#include "gui/choosecity_run.h"

#include <cctype>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A full-session recording harness: enumerate 2 cities, hover the first, confirm via the
// click id 1210 on frame 1, chain succeeds. Records an ordered tag stream of every leaf.
struct OrderHooks : ChooseCityHooks {
    std::vector<std::string> tags;
    std::vector<int> destroyedForms;
    std::vector<int> destroyedMarkers;
    int destroyedTower = 0;

    void rec(const std::string& t) { tags.push_back(t); }

    void SceneSetup() override { rec("SceneSetup"); }
    int  EnumerateCityFiles(const char*, const char*,
                            std::vector<std::string>& out) override {
        rec("Enumerate"); out = {"BERLIN", "HAMBURG"}; return 2;
    }
    bool ReadCityName(const std::string& f, std::string& n) override {
        rec("ReadName:" + f); n = f; return true;
    }
    int  SpawnCityMarker(const std::string& n) override {
        rec("SpawnMarker:" + n); return n == "BERLIN" ? 10 : 20;
    }
    int  FindTextIndex(const std::string&) override { return 5; }
    void RegisterStatusText(const std::string& k, const std::string&) override {
        rec("Register:" + k);
    }
    int  SpawnCityTower() override { rec("SpawnTower"); return 99; }
    void RunIntroCutscene() override { rec("Cutscene"); }
    int  FormLoad(const char* name) override {
        rec(std::string("FormLoad:") + name);
        return std::strstr(name, "_HEADER") ? 1 : 2;
    }
    void FormPositionAndSelect(int f, int) override { rec("FormPos:" + std::to_string(f)); }
    void FormSetVisible(int f, int v) override {
        rec("FormVis:" + std::to_string(f) + ":" + std::to_string(v));
    }
    void FormDestroy(int f) override { rec("FormDestroy:" + std::to_string(f));
                                       destroyedForms.push_back(f); }
    void DestroyCityTower(int t) override { rec("DestroyTower"); destroyedTower = t; }
    void DestroyMarker(int m) override { rec("DestroyMarker:" + std::to_string(m));
                                         destroyedMarkers.push_back(m); }

    int  RunFrameLoop(int frame) override { return frame < 3 ? 1 : 0; }
    int  PickNearestObject(int) override { return 10; }   // hover BERLIN
    bool PickIsHover(int) override { return true; }
    std::string ObjectName(int o) override { return o == 10 ? "stadt_BERLIN" : ""; }
    void RenderCityInfo(const std::string&, int) override { rec("RenderInfo"); }
    int  ClickedWidgetId(int frame) override { return frame == 1 ? 1210 : 0; }
    bool ChooseCharacterIntroVariant() override { rec("Chain:Intro"); return true; }
    bool RunChooseHistory() override { rec("Chain:History"); return true; }
};

std::vector<std::string> RunSession() {
    OrderHooks hooks;
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);
    ChooseCityState st;
    ChooseCityRecord rec;
    Menu_EnterChooseCity(st, &rec, /*maxFrames*/ 10);
    Menu_SetChooseCityHooks(prev);
    return hooks.tags;
}

bool HasUpperExt(const std::string& name, const char* ext) {
    size_t n = std::strlen(ext);
    if (name.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::toupper((unsigned char)name[name.size()-n+i]) !=
            std::toupper((unsigned char)ext[i])) return false;
    return true;
}

} // namespace

TEST(ChooseCityRunE2E, FullSession_BuildDispatchCleanupOrder) {
    std::vector<std::string> got = RunSession();

    // The ordered prefix: scene -> enumerate -> per-city read/spawn/register -> tower ->
    // cutscene -> forms.
    const char* wantPrefix[] = {
        "SceneSetup",
        "Enumerate",
        "ReadName:BERLIN", "SpawnMarker:BERLIN", "Register:stadt_BERLIN",
        "ReadName:HAMBURG", "SpawnMarker:HAMBURG", "Register:stadt_HAMBURG",
        "SpawnTower",
        "Cutscene",
        "FormLoad:Menu\\CHOOSECITY_HEADER", "FormPos:1",
        "FormLoad:Menu\\CHOOSECITY", "FormPos:2",
    };
    const int prefixN = (int)(sizeof(wantPrefix)/sizeof(wantPrefix[0]));
    CHECK(got.size() >= (size_t)prefixN);
    for (int i = 0; i < prefixN && i < (int)got.size(); ++i)
        CHECK_EQ(got[i], std::string(wantPrefix[i]));

    // The confirm chain ran in order somewhere after the forms.
    auto idx = [&](const std::string& t) -> int {
        for (int i = 0; i < (int)got.size(); ++i) if (got[i] == t) return i;
        return -1;
    };
    int intro = idx("Chain:Intro");
    int hist  = idx("Chain:History");
    CHECK(intro != -1);
    CHECK(hist > intro);

    // Cleanup is LAST and in order: forms destroyed, then tower, then markers.
    int fd2 = idx("FormDestroy:2");   // main form first (0x52edc2)
    int fd1 = idx("FormDestroy:1");   // header form second (0x52edd5)
    int dt  = idx("DestroyTower");
    int dm1 = idx("DestroyMarker:10");
    int dm2 = idx("DestroyMarker:20");
    CHECK(fd2 != -1 && fd1 > fd2);
    CHECK(dt > fd1);
    CHECK(dm1 > dt);
    CHECK(dm2 > dm1);
}

TEST(ChooseCityRunE2E, Deterministic_TwoRunsIdentical) {
    std::vector<std::string> a = RunSession();
    std::vector<std::string> b = RunSession();
    CHECK_EQ(a.size(), b.size());
    bool same = (a == b);
    CHECK(same);
}

// GUARDED real-asset test: enumerate the original gamedata/cities directory and assert the
// reconstructed predicates classify the ".CTY"/".NET" files the screen would consume.
TEST(ChooseCityRunE2E, RealGamedataCities_Guarded) {
    const char* dirs[] = {
        "gamedata/cities",
        "europe_guild_1400_original/Resources/gamedata/Cities",
        "../europe_guild_1400_original/Resources/gamedata/Cities",
    };
    DIR* dp = nullptr;
    for (const char* d : dirs) { dp = opendir(d); if (dp) break; }
    if (!dp) {
        std::printf("    SKIP RealGamedataCities: gamedata/cities not present\n");
        return; // guard: absent assets -> clean skip
    }

    int cty = 0, net = 0;
    struct dirent* ent;
    while ((ent = readdir(dp)) != nullptr) {
        std::string name = ent->d_name;
        if (HasUpperExt(name, ".CTY")) ++cty;
        else if (HasUpperExt(name, ".NET")) ++net;
    }
    closedir(dp);

    // The original enumerates ".CTY" (local) or ".NET" (network); the real set has both.
    CHECK(cty > 0);
    CHECK(net > 0);
    // The extension the screen matches per mode (REUSED predicate).
    CHECK(std::string(ChooseCity_Extension(false)) == ".CTY");
    CHECK(std::string(ChooseCity_Extension(true)) == ".NET");
    std::printf("    RealGamedataCities: %d .CTY, %d .NET\n", cty, net);
}
