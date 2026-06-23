// vegetation_test.cpp — golden-vector unit tests for sim/vegetation
// (VIBE_Plant_LoadVegetationModel @0x56ec14): the level computation, the asset
// path-select format, the load-trigger/place/error orchestration, plus a guarded
// e2e that builds a real vegetation path against the shipped data dir. Suite: VEG.
#include "test.h"

#include "sim/vegetation.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Veg_ComputeLevel — (stagePacked>>24)/divisor, floored at 1 (gilde.exe idiv).
// ---------------------------------------------------------------------------
TEST(VEG, ComputeLevel_Basic) {
    VegSpeciesRecord sp;
    sp.found = true;
    sp.levelDivisor = 1;

    VegPlantView pl{};
    pl.stagePacked = 4 << 24;   // stage hi byte == 4
    CHECK_EQ(Veg_ComputeLevel(pl, sp), 4);

    pl.stagePacked = 9 << 24;
    sp.levelDivisor = 3;        // 9 / 3 == 3
    CHECK_EQ(Veg_ComputeLevel(pl, sp), 3);

    pl.stagePacked = 7 << 24;
    sp.levelDivisor = 3;        // 7 / 3 == 2 (truncates toward zero)
    CHECK_EQ(Veg_ComputeLevel(pl, sp), 2);
}

TEST(VEG, ComputeLevel_FloorAtOne) {
    VegSpeciesRecord sp; sp.found = true; sp.levelDivisor = 1;
    VegPlantView pl{};
    pl.stagePacked = 0;                 // 0/1 == 0 -> floored to 1
    CHECK_EQ(Veg_ComputeLevel(pl, sp), 1);
    // negative high byte: 0xFF000000 >> 24 == -1 (ASR) -> -1/1 == -1 -> floored 1
    pl.stagePacked = (i32)0xFF000000;
    CHECK_EQ(Veg_ComputeLevel(pl, sp), 1);
    // large stage / big divisor rounds to 0 -> floored to 1.
    pl.stagePacked = 2 << 24; sp.levelDivisor = 5;  // 2/5 == 0
    CHECK_EQ(Veg_ComputeLevel(pl, sp), 1);
}

// ---------------------------------------------------------------------------
// Veg_BuildModelPath — exact sprintf format (gilde.exe 0x625334 / 0x625354).
// ---------------------------------------------------------------------------
TEST(VEG, BuildModelPath_Plain) {
    VegSpeciesRecord sp; sp.found = true; sp.name = "Baum";
    std::string p = Veg_BuildModelPath("data/", sp, 2, /*fall*/false);
    CHECK(p == "data/vegetation/*pfl_Baum_02.ogr");
}

TEST(VEG, BuildModelPath_Fall) {
    VegSpeciesRecord sp; sp.found = true; sp.name = "Eiche";
    std::string p = Veg_BuildModelPath("", sp, 1, /*fall*/true);
    CHECK(p == "vegetation/*pfl_Eiche_01_FALL.ogr");
}

TEST(VEG, BuildModelPath_LevelFormatNoZeroPadBeyond) {
    // The format is "_0%i" so level 12 yields "_012" (one literal 0 then %i).
    VegSpeciesRecord sp; sp.found = true; sp.name = "X";
    CHECK(Veg_BuildModelPath("", sp, 12, false) == "vegetation/*pfl_X_012.ogr");
}

// ---------------------------------------------------------------------------
// Plant_LoadVegetationModel — full orchestration with recording hooks.
// ---------------------------------------------------------------------------
namespace {
struct VegRec {
    std::string lastPath;
    std::string lastSuffix;
    bool        marked = false;
    bool        lightCached = false;
    std::string lastError;
    bool        setPos = false;
    float       pos[3] = {0, 0, 0};
    void*       nodeToReturn = nullptr;
};
VegRec g_v;

const char* h_dataDir() { return "europe/"; }
bool h_fall_false() { return false; }
bool h_fall_true() { return true; }
VegSpeciesRecord h_species(u16 typeId) {
    VegSpeciesRecord sp;
    sp.found = true;
    sp.name = (typeId == 7) ? "Tanne" : "Strauch";
    sp.levelDivisor = 2;
    return sp;
}
void* h_load(const char* path) { g_v.lastPath = path; return g_v.nodeToReturn; }
void h_suffix(void* /*n*/, const char* s) { g_v.lastSuffix = s; }
void h_mark(void* /*n*/) { g_v.marked = true; }
void h_light(void* /*n*/) { g_v.lightCached = true; }
void h_err(const char* m) { g_v.lastError = m; }
const void* h_ref() { return reinterpret_cast<const void*>(0xBEEF); }
float h_pflt(const void* /*r*/, int off) {
    // identity-ish basis: translate (1,2,3) + 0 contributions.
    if (off == 0x90) return 1.0f;
    if (off == 0x94) return 2.0f;
    if (off == 0x98) return 3.0f;
    return 0.0f;
}
u8 h_pmap(const void* /*r*/, int /*x*/, int /*y*/) { return 0; }
void h_setpos(void* /*n*/, const float p[3]) {
    g_v.setPos = true; g_v.pos[0] = p[0]; g_v.pos[1] = p[1]; g_v.pos[2] = p[2];
}

VegetationHooks MakeHooks(bool fall) {
    VegetationHooks h{};
    h.dataDir = h_dataDir;
    h.isFallSeason = fall ? h_fall_true : h_fall_false;
    h.findSpecies = h_species;
    h.loadObjectGroup = h_load;
    h.appendNameSuffix = h_suffix;
    h.markVegetationFlag = h_mark;
    h.placementRef = h_ref;
    h.placementFloat = h_pflt;
    h.placementMapByte = h_pmap;
    h.setPosition = h_setpos;
    h.lightRequestCache = h_light;
    h.reportError = h_err;
    return h;
}
}  // namespace

TEST(VEG, Load_Success) {
    g_v = VegRec{};
    int fakeNode = 123;
    g_v.nodeToReturn = &fakeNode;
    VegetationHooks h = MakeHooks(/*fall*/false);
    SetVegetationHooks(&h);

    VegPlantView pl{};
    pl.recId = 77;
    pl.typeId = 7;          // -> "Tanne"
    pl.stagePacked = 6 << 24;  // 6 / divisor 2 == 3
    pl.plotX = 0; pl.plotY = 0;

    void* node = Plant_LoadVegetationModel(pl);
    CHECK(node == &fakeNode);
    CHECK(g_v.lastPath == "europe/vegetation/*pfl_Tanne_03.ogr");
    CHECK(g_v.lastSuffix == "_ID77");
    CHECK(g_v.marked);
    CHECK(g_v.lightCached);
    CHECK(g_v.setPos);
    // pos == base translate (1,2,3) since plot coords are 0 and map byte 0.
    CHECK(g_v.pos[0] == 1.0f);
    CHECK(g_v.pos[1] == 2.0f);
    CHECK(g_v.pos[2] == 3.0f);
    CHECK(g_v.lastError.empty());
    ResetVegetationHooks();
}

TEST(VEG, Load_FallSuffix) {
    g_v = VegRec{};
    int fakeNode = 1;
    g_v.nodeToReturn = &fakeNode;
    VegetationHooks h = MakeHooks(/*fall*/true);
    SetVegetationHooks(&h);

    VegPlantView pl{};
    pl.recId = 1; pl.typeId = 99; pl.stagePacked = 4 << 24;  // 4/2==2
    Plant_LoadVegetationModel(pl);
    CHECK(g_v.lastPath == "europe/vegetation/*pfl_Strauch_02_FALL.ogr");
    ResetVegetationHooks();
}

TEST(VEG, Load_Failure_LogsError) {
    g_v = VegRec{};
    g_v.nodeToReturn = nullptr;   // load fails
    VegetationHooks h = MakeHooks(false);
    SetVegetationHooks(&h);

    VegPlantView pl{};
    pl.recId = 5; pl.typeId = 7; pl.stagePacked = 2 << 24;  // 2/2==1
    void* node = Plant_LoadVegetationModel(pl);
    CHECK(node == nullptr);
    CHECK(!g_v.marked);
    CHECK(!g_v.lightCached);
    CHECK(g_v.lastError ==
          "init_SetPflanzenMap(): Could not load 3D-Objekt-roup "
          "'europe/vegetation/*pfl_Tanne_01.ogr'...");
    ResetVegetationHooks();
}

TEST(VEG, Load_PlacementBasisApplied) {
    g_v = VegRec{};
    int fakeNode = 1;
    g_v.nodeToReturn = &fakeNode;
    VegetationHooks h = MakeHooks(false);
    // override the basis with a per-axis scale so plot coords contribute.
    static auto pflt = [](const void*, int off) -> float {
        switch (off) {
            case 0x90: return 0.0f;  // base x
            case 0xA0: return 10.0f; // plotX -> x
            case 0xB0: return 100.0f;// plotY -> x
            default: return 0.0f;
        }
    };
    h.placementFloat = pflt;
    SetVegetationHooks(&h);

    VegPlantView pl{};
    pl.recId = 1; pl.typeId = 7; pl.stagePacked = 2 << 24;
    pl.plotX = 2; pl.plotY = 3;
    Plant_LoadVegetationModel(pl);
    // x = 0 + 2*10 + 3*100 + 0 == 320
    CHECK(g_v.pos[0] == 320.0f);
    ResetVegetationHooks();
}

// ---------------------------------------------------------------------------
// Guarded e2e: build a real vegetation path against the shipped data dir. The
// FULL model load hands off to the archive-mounted Scene_LoadObjectGroup (out of
// this module's scope); here we prove the path-select produces the canonical
// in-archive path the loader consumes. Skips cleanly without GUILD_GAME_DIR.
// ---------------------------------------------------------------------------
TEST(VEG, E2E_RealDataDirPath) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir || !*dir) {
        std::printf("    [skip] GUILD_GAME_DIR not set\n");
        return;
    }
    // The original prefixes byte_122F098 (the data root, e.g. "...\\data\\" or "").
    // Whatever it is, the path-select must yield the wildcard archive path form.
    std::string base = dir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\')
        base += '/';
    VegSpeciesRecord sp; sp.found = true; sp.name = "Baum"; sp.levelDivisor = 1;
    VegPlantView pl{}; pl.stagePacked = 1 << 24;
    std::string p =
        Veg_BuildModelPath(base, sp, Veg_ComputeLevel(pl, sp), false);
    // Must contain the canonical archive selector and .ogr extension.
    CHECK(p.find("vegetation/*pfl_Baum_01.ogr") != std::string::npos);
    CHECK(p.rfind(".ogr") == p.size() - 4);
    std::printf("    [e2e] veg path: %s\n", p.c_str());
}
