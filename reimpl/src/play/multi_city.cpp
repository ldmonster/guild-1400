// Wave 29 PLAY — MULTI-CITY ROBUSTNESS implementation. See multi_city.h.
//
// Load -> render -> simulate each shipped city, proving the engine is general (not
// AUGSBURG-specific) and that loading multiple real cities in one process leaves NO
// global leakage between loads. Every link is a CALL into an already-reconstructed
// sibling; this file defines no new world state (only its own inert scratch).
//
// REUSED (extern, never redefined — ODR):
//   app::MountRealGameAssets                      (app/real_boot.h)
//   io::LoadWorld / VfsShutdown                   (io/save_world_load.h, io/vfs.h)
//   play::WorldRenderer                           (world_render.h)
//   play::RunGameDay / SeedGameDay                (game_day.h)
//   play::HashFullWorld                           (world_digest.h)
//   sim::g_objects / g_persons / ResetEntityArrays(sim/entity.h)
//   world::CityInitParameterTable                 (world/city.h)
//   crt::Srand                                    (crt/rand.h)
#include "play/multi_city.h"

#include <cstring>
#include <vector>

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save_person.h"   // kPlantBytes / kKindPlant (plant-pointer normalization)
#include "io/vfs.h"
#include "play/game_day.h"
#include "play/world_digest.h"
#include "play/world_render.h"
#include "shim/IGraphicsDevice.h"
#include "shim_impl/filedump_graphics.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/building_lifecycle.h"  // g_buildingPersons (base digest)
// The full set of live world tables HashFullWorld folds — zeroed before each load so
// a city's hashes are a pure function of (its bytes + seed), independent of any
// PRIOR city exercised in this process. These headers are exactly the ones
// world_digest.cpp reads the globals from.
#include "sim/building.h"            // g_buildingTypes / g_buildingTypesLoaded
#include "sim/building_create.h"     // g_buildingNextId
#include "sim/building_production.h" // g_sceneTypes/_Loaded, g_sceneTypeRemap, g_prod*
#include "sim/actionqueue.h"         // g_gameTick
#include "sim/command_apply5.h"      // g_sysGameTime, g_currentPlayer/g_sysActivePlayer
#include "sim/command_apply6.h"      // g_tickClock, g_tickSubCounter
#include "world/city.h"              // g_cities, g_goods, g_capDivisor, totals
#include "world/law.h"               // g_lawTable
#include "world/event.h"             // g_eventTable, g_eventTableCount, g_missionLcgState
#include "world/office.h"            // g_officeHolders
#include "world/crime.h"             // g_crimeTable
#include "world/relation.h"          // g_relationMatrix
#include "crt/rand.h"

namespace guild::play {

namespace {

// Zero EVERY live world table HashFullWorld folds, to a clean blank slate. Identical
// in shape to playable_slice.cpp::ZeroWorldGlobals — the single source of cross-city
// leakage is these globals surviving from a previous load, so they MUST all be
// blanked before io::LoadWorld repopulates the serialized subset for the new city.
void ZeroWorldGlobals() {
    using std::memset;
    // Fully zero the entity arrays (NOT just the alive/marker guard bytes that
    // ResetEntityArrays clears) so the UNSERIALIZED pad bytes start clean — otherwise
    // a prior city's pad bytes survive a reload (LoadWorld only repopulates the
    // serialized fields) and the raw-byte digest carries city N into city N+1.
    memset(sim::g_objects, 0, sizeof(sim::g_objects));
    memset(sim::g_persons, 0, sizeof(sim::g_persons));
    memset(sim::g_personIds, 0, sizeof(sim::g_personIds));
    memset(sim::g_sceneNodes, 0, sizeof(sim::g_sceneNodes));
    memset(sim::g_buildingPersons, 0, sizeof(sim::g_buildingPersons));
    memset(sim::g_buildingTypes, 0, sizeof(sim::g_buildingTypes));
    sim::g_buildingTypesLoaded = false;
    sim::g_buildingNextId = 0;
    memset(sim::g_sceneTypes, 0, sizeof(sim::g_sceneTypes));
    sim::g_sceneTypesLoaded = false;
    memset(sim::g_sceneTypeRemap, 0, sizeof(sim::g_sceneTypeRemap));
    memset(sim::g_prodStore, 0, sizeof(sim::g_prodStore));
    memset(sim::g_prodSchedules, 0, sizeof(sim::g_prodSchedules));
    memset(world::g_cities, 0, sizeof(world::g_cities));
    memset(world::g_goods, 0, sizeof(world::g_goods));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;
    memset(&sim::g_sysGameTime, 0, sizeof(sim::g_sysGameTime));
    memset(&sim::g_tickClock, 0, sizeof(sim::g_tickClock));
    sim::g_tickSubCounter = 0;
    sim::g_gameTick = 0;
    sim::g_currentPlayer = 0;
    sim::g_sysActivePlayer = 0;
    memset(world::g_lawTable, 0, sizeof(world::g_lawTable));
    memset(world::g_eventTable, 0, sizeof(world::g_eventTable));
    world::g_eventTableCount = 0;
    world::g_missionLcgState = 0;
    memset(world::g_officeHolders, 0, sizeof(world::g_officeHolders));
    memset(world::g_crimeTable, 0, sizeof(world::g_crimeTable));
    memset(world::g_relationMatrix, 0, sizeof(world::g_relationMatrix));
}

// io::LoadWorld points each KIND-30 (plant) object's runtime plantmap pointer (+113)
// at a FRESH heap allocation whose address varies per run/city; g_objects is folded
// as raw bytes, so that pointer column makes the digest non-reproducible. Point every
// live kind-30 object's +113 at one shared OWNED scratch so the column is stable.
// The plantmap is a heap artifact, not persisted record state, so this is observably
// inert (mirrors real_session / playable_slice).
void NormalizePlantPointers(std::uint32_t objectCount) {
    using namespace guild::sim;
    static std::vector<guild::u8> plantScratch(io::kPlantBytes, 0);
    guild::u8* scratch = plantScratch.data();
    for (std::uint32_t i = 0; i < objectCount && i < (std::uint32_t)kObjectCapacity; ++i) {
        guild::u8* r = reinterpret_cast<guild::u8*>(&g_objects[i]);
        if (r[0] != io::kKindPlant)
            continue;
        std::memcpy(r + 113, &scratch, sizeof scratch);
    }
}

// Render options shared by every city's frame. Terrain OFF: it is an opaque
// full-frame quad that would occlude the object layer, so the per-city object set is
// the frame (distinct cities -> distinct object footprints -> distinct nonClear px).
WorldRenderer::Options CityRenderOptions(int fbW, int fbH) {
    WorldRenderer::Options opt;
    opt.fbW = fbW;
    opt.fbH = fbH;
    opt.clearR = 0; opt.clearG = 0; opt.clearB = 64;   // dark-blue sky
    opt.emitTerrain = false;
    opt.scanObjects = true;
    opt.scanScene   = true;
    opt.scanPersons = true;
    return opt;
}

// Render one frame of the live world into `dev` (if non-null). Fills the witness's
// render fields. Returns whether the frame presented. With a null device nothing
// renders (rendered stays false) — the sim half still runs.
void RenderCityFrame(CityWitness& w, int fbW, int fbH, shim::IGraphicsDevice* dev) {
    if (!dev)
        return;
    WorldRenderer wr;
    RenderStats st = wr.render(CityRenderOptions(fbW, fbH), *dev);
    w.rendered     = st.presented;
    w.sceneObjects = wr.lastBuild().sceneObjects();
    w.nonClearPx   = wr.binder().nonClearPixels();
}

// Advance ONE full game-day over the live world (the real BeginPlayerRound cascade),
// seeded for determinism. RunGameDay re-roots the CRT RNG itself (crt::Srand) so the
// day is reproducible; SeedGameDay draws the sub-turn states from the seeded stream.
void RunOneDay(CityWitness& w, std::uint32_t seed) {
    crt::Srand(seed);
    GameDayState st = SeedGameDay(seed);
    st.day = 0;
    GameDayDeltas d = RunGameDay(seed, st);
    w.dayRan      = true;
    w.dayStepsRun = d.stepsRun;
}

} // namespace

// ===========================================================================
// ResetLiveWorldForCity — full blank slate (shared by real + synthetic paths).
// ===========================================================================
void ResetLiveWorldForCity(std::uint32_t seed) {
    ZeroWorldGlobals();
    sim::ResetEntityArrays();
    // Deterministic economy baseline (the tables LoadWorld does NOT populate). Seed
    // the RNG first so the baseline draw is reproducible.
    crt::Srand(seed);
    world::CityInitParameterTable(100.0f);
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;
}

// ===========================================================================
// ExerciseLiveWorld — render + one-day sim + before/after digest over the live world.
// ===========================================================================
CityWitness ExerciseLiveWorld(std::uint32_t seed,
                              std::uint32_t objectCount, std::uint32_t personCount,
                              std::uint32_t nodeCount,
                              int fbW, int fbH, shim::IGraphicsDevice* dev) {
    CityWitness w;
    w.loaded      = true;   // the caller asserts the world is populated
    w.objectCount = objectCount;
    w.personCount = personCount;
    w.nodeCount   = nodeCount;

    // --- RENDER one frame (handles the 0-object case gracefully: a clean clear) --
    RenderCityFrame(w, fbW, fbH, dev);

    // --- digest BEFORE the day (re-anchor the RNG: the base digest folds the live
    //     CRT RNG state, which the render may have advanced) ----------------------
    crt::Srand(seed);
    w.hashBefore = HashFullWorld();

    // --- one game-day ----------------------------------------------------------
    RunOneDay(w, seed);

    // --- digest AFTER the day (re-anchor again so the compare is RNG-stable) ----
    crt::Srand(seed);
    w.hashAfter = HashFullWorld();
    return w;
}

// ===========================================================================
// LoadAndExerciseCity — the whole load->render->sim loop over a REAL city.
// ===========================================================================
CityWitness LoadAndExerciseCity(shim::IFileSystem* fs, const std::string& gameDir,
                                const std::string& ctyPath, std::uint32_t seed,
                                int fbW, int fbH, shim::IGraphicsDevice* dev) {
    CityWitness w;
    w.cityPath = ctyPath;
    if (!fs)
        return w;

    // --- step: MOUNT real assets -----------------------------------------------
    app::RealGameAssets assets =
        app::MountRealGameAssets(fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    if (!assets.vfsBound) {
        io::VfsShutdown();
        return w;
    }

    // --- step: FULL RESET so this city cannot inherit a prior city's globals ----
    ResetLiveWorldForCity(seed);

    // --- step: LOAD the city into the live arrays ------------------------------
    io::WorldState world{};
    w.loaded = io::LoadWorld(ctyPath.c_str(), world);
    if (!w.loaded) {
        io::VfsShutdown();
        return w;
    }
    w.objectCount = world.objectCount;
    w.personCount = world.cityRecCount;
    w.nodeCount   = world.sceneTileCount;

    // Stabilize the kind-30 plantmap heap-pointer column (reproducible digest).
    NormalizePlantPointers(world.objectCount);

    // --- step: RENDER one frame -------------------------------------------------
    RenderCityFrame(w, fbW, fbH, dev);

    // --- step: digest BEFORE the day -------------------------------------------
    crt::Srand(seed);
    w.hashBefore = HashFullWorld();

    // --- step: one game-day -----------------------------------------------------
    RunOneDay(w, seed);

    // --- step: digest AFTER the day --------------------------------------------
    crt::Srand(seed);
    w.hashAfter = HashFullWorld();

    io::VfsShutdown();   // the live arrays are left populated (post-day world).
    return w;
}

} // namespace guild::play
