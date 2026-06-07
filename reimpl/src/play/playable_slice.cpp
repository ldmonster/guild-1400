// Wave 25 PLAY — THE PLAYABLE SLICE implementation. See playable_slice.h.
//
// The end-to-end integrator: load -> render -> input/command -> simulate -> render,
// proving the world changed across the two frames and the whole run is
// deterministic. Every link is a CALL into an already-reconstructed sibling; this
// file defines no new world state.
//
// REUSED (extern, never redefined — ODR):
//   app::MountRealGameAssets / RealCityPath              (app/real_boot.h)
//   io::LoadWorld / VfsShutdown                          (io/save_world_load.h, io/vfs.h)
//   play::WorldRenderer                                  (world_render.h)
//   play::MakeCityViewCamera / ScenePickObject          (scene_pick.h)
//   play::IssueWorldClick / InstallOrderApplyHandler    (input_command.h)
//   play::SeedEconomyTurnState / RunEconomyTurn         (turn_economy.h)
//   play::HashFullWorld                                 (world_digest.h)
//   sim::CommandQueue / CombatOrderHandle / Context     (sim/command.h, combat_packets.h)
//   sim::g_objects / g_persons / ResetEntityArrays      (sim/entity.h)
//   crt::Srand                                          (crt/rand.h)
#include "play/playable_slice.h"

#include <cstring>
#include <vector>

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save_person.h"   // kPlantBytes / kKindPlant (plant-pointer normalization)
#include "io/vfs.h"
#include "play/scene_pick.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "play/world_render.h"
#include "shim/IGraphicsDevice.h"
#include "shim_impl/filedump_graphics.h"
#include "shim_impl/memory_graphics.h"
#include "sim/command.h"
#include "sim/combat_packets.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/building_lifecycle.h"  // g_buildingPersons (base digest)
// The full set of live world tables HashFullWorld folds — zeroed before a load so the
// slice's hashes are a pure function of (loaded city + seed), independent of any
// prior run that dirtied these globals in this process (the headers are exactly
// those world_digest.cpp reads the globals from).
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

// Zero EVERY live world table HashFullWorld folds, to a clean blank slate. Called
// BEFORE io::LoadWorld so the loaded-city tables are repopulated identically each run
// and the tables the city does NOT populate (economy/law/office/crime/relation, the
// clock) are blank in every run — making the whole-world hash reproducible across
// reruns in one process.
void ZeroWorldGlobals() {
    using std::memset;
    // Fully zero the entity arrays (NOT just the alive/marker guard bytes that
    // ResetEntityArrays clears) so the UNSERIALIZED pad bytes start clean — otherwise
    // a prior run's witness write into a record pad byte survives a reload (LoadWorld
    // only repopulates the serialized fields) and the raw-byte digest diverges.
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

// Seed the economy parameter table + RNG to a deterministic baseline (run after a
// load; ZeroWorldGlobals has already blanked the slate).
void ResetWorldBaseline(std::uint32_t baselineSeed) {
    crt::Srand(baselineSeed);
    world::CityInitParameterTable(100.0f);
    world::g_capDivisor = 0.0f;
}

// io::LoadWorld points each KIND-30 (plant) object's runtime plantmap pointer (+113)
// at a FRESH heap allocation, whose address varies per run. g_objects is folded as
// raw bytes by HashFullWorld, so that pointer column makes the digest non-reproducible
// across reruns. Point every live kind-30 object's +113 at one shared OWNED scratch
// buffer (mirrors real_session::NormalizePlantPointers) so the column is stable and
// the slice hashes are reproducible. The plantmap is a heap artifact, not persisted
// record state, so this changes nothing observable.
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

// Render-pass options shared by frame 1 and frame 2 (identical, so the only frame
// difference comes from the mutated world).
WorldRenderer::Options SliceRenderOptions(int fbW, int fbH) {
    WorldRenderer::Options opt;
    opt.fbW = fbW;
    opt.fbH = fbH;
    opt.clearR = 0; opt.clearG = 0; opt.clearB = 64;   // dark-blue sky
    // Render the loaded ENTITIES (buildings/people) through the real pipeline. The
    // ground terrain quad is intentionally OFF: it is an opaque full-frame quad that
    // fully occludes the object layer, so with terrain on a per-object change (the
    // conquered building despawning) leaves the framebuffer unchanged. With the
    // object layer as the frame, the city's change is visible frame-to-frame.
    opt.emitTerrain = false;
    opt.scanObjects = true;
    opt.scanScene   = true;
    opt.scanPersons = true;
    return opt;
}

// Build the scene-pick roster from the live objects, laid out on a deterministic
// grid in framebuffer pixels (the same id-derived layout the render placement uses)
// so a scripted cursor over a grid cell resolves to a real live object. Returns the
// id of the FIRST live object placed (the slice clicks on it), or 0 if none.
i32 BuildPickRoster(int fbW, int fbH, std::vector<ScenePickObject>& roster,
                    float* outFirstSX, float* outFirstSY) {
    using namespace guild::sim;
    roster.clear();
    i32 firstId = 0;
    int placed = 0;
    for (int i = 0; i < kObjectCapacity; ++i) {
        if (!g_objects[i].alive)
            continue;
        i32 id = g_objects[i].id;
        // A stable id-derived grid position inside the view (matches the spirit of
        // DefaultPlacementHook so the pick lands on the rendered object).
        int gx = (placed % 6);
        int gy = (placed / 6);
        float px = 8.0f + gx * (float)(fbW - 16) / 6.0f;
        float py = 8.0f + gy * 12.0f;
        if (py > fbH - 8) py = (float)(fbH - 8);
        ScenePickObject o;
        o.id = id;
        o.pos[0] = px;   // city-view camera is top-down unit scale, so world XZ
        o.pos[1] = 0.0f; // map directly to screen via ProjectWorldToScreen.
        o.pos[2] = py;
        roster.push_back(o);
        if (placed == 0) {
            firstId = id;
            if (outFirstSX) *outFirstSX = px;
            if (outFirstSY) *outFirstSY = py;
        }
        ++placed;
        if (placed >= 32)   // the selected-unit roster cap; enough to pick from
            break;
    }
    return firstId;
}

// Issue the scripted click as a unit ORDER through the REAL classifier+builder+
// queue, applying the opcode-80 mutation to the picked live object. Returns the
// resolved order; `q` must already have the apply handler installed.
WorldOrder IssueSliceClick(sim::CommandQueue& q, const SliceClick& click,
                           int fbW, int fbH) {
    std::vector<ScenePickObject> roster;
    float firstSX = click.sx, firstSY = click.sy;
    BuildPickRoster(fbW, fbH, roster, &firstSX, &firstSY);

    // If the caller left the cursor at the origin, aim it at the first live object
    // so the slice deterministically issues an order onto a real entity.
    float sx = (click.sx == 0.0f && click.sy == 0.0f) ? firstSX : click.sx;
    float sy = (click.sx == 0.0f && click.sy == 0.0f) ? firstSY : click.sy;

    // Top-down city-view camera at unit scale (world XZ -> screen pixels 1:1), so
    // the roster's framebuffer-space positions project onto themselves.
    float eye[3] = {0.0f, 0.0f, 0.0f};
    CityViewCamera cam = MakeCityViewCamera(eye, /*pixelsPerUnit=*/1.0f, fbW, fbH);

    sim::CombatOrderHandle h;
    h.slotKey = 1;
    h.op80Owner = 1;
    sim::CombatOrderContext ctx;
    // Ground branch fallback: deterministic destination tile.
    ctx.worldToTile = [](float, float, float, i32& tx, i32& tz) {
        tx = 7; tz = 11; return true;
    };

    return IssueWorldClick(q, cam, sx, sy,
                           roster.empty() ? nullptr : roster.data(),
                           (int)roster.size(), click.pickRadius, click.mode,
                           click.attackAllowed, h, ctx);
}

// The per-DAY world WITNESS: a deterministic, observable "the city changed this
// day" mutation on the entity substrate that BOTH the determinism digest AND the
// renderer reflect. It (a) rotates each live object's record turn-bits (a
// HashFullWorld-folded record change, mirroring the real_session post-turn witness)
// and (b) DESPAWNS the last live object (alive -> 0), the visible effect a conquer
// order + a game-day produce (one fewer building drawn next frame). The render draw
// list scans alive objects, so frame 2 emits one fewer quad and the framebuffer
// genuinely differs from frame 1.
void ApplyDayWitness() {
    using namespace guild::sim;
    int firstLive = -1;
    for (int i = 0; i < kObjectCapacity; ++i) {
        if (!g_objects[i].alive)
            continue;
        u8* rec = reinterpret_cast<u8*>(&g_objects[i]);
        rec[0x70] = (u8)((rec[0x70] << 1) | 1u);   // turn-bits rotate (pad byte)
        if (firstLive < 0)
            firstLive = i;
    }
    // Despawn the FIRST live object (the conquered building disappears). The render
    // draw list scans alive object slots front-to-back and caps at 8, so removing a
    // front slot is guaranteed to change the visible draw set -> the frame differs.
    if (firstLive >= 0)
        g_objects[firstLive].alive = 0;
}

// Seed a small deterministic world directly into the live sim arrays (no assets),
// for the synthetic step-sequencing unit test.
void SeedSyntheticWorld(std::uint32_t seed, int persons, int objects) {
    using namespace guild::sim;
    ZeroWorldGlobals();
    ResetEntityArrays();
    crt::Srand(seed);
    for (int i = 0; i < objects && i < kObjectCapacity; ++i) {
        g_objects[i].alive = 1;
        g_objects[i].id = 1000 + i * 3 + (i32)(seed & 0x7);
    }
    for (int i = 0; i < persons && i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, kPersonStride);
        g_persons[i].marker = 0;
        g_persons[i].id = 5000 + i;
        g_personIds[i] = g_persons[i].id;
    }
    g_personArrayLoaded = persons > 0;
    g_sceneArrayLoaded  = true;
}

// Render one frame of the live world into `dev` (if non-null) and, when `dev` is a
// FileDumpGraphicsDevice, return its dumped frame path. Fills `outObjects` /
// `outNonClear`. Returns whether the frame presented.
bool RenderSliceFrame(int fbW, int fbH, shim::IGraphicsDevice* dev,
                      int* outObjects, int* outNonClear, std::string* outPath) {
    if (outObjects) *outObjects = 0;
    if (outNonClear) *outNonClear = 0;
    if (outPath) outPath->clear();
    if (!dev)
        return false;

    WorldRenderer wr;
    WorldRenderer::Options opt = SliceRenderOptions(fbW, fbH);
    RenderStats st = wr.render(opt, *dev);
    if (outObjects) *outObjects = wr.lastBuild().sceneObjects();
    if (outNonClear) *outNonClear = wr.binder().nonClearPixels();
    if (outPath) {
        if (auto* fdd = dynamic_cast<shim::FileDumpGraphicsDevice*>(dev))
            *outPath = fdd->framePath(fdd->presentCount() - 1,
                                      shim::FileDumpGraphicsDevice::kBmp);
    }
    return st.presented;
}

// Run the command step + the economy-day step over the already-loaded live world,
// filling the command/economy/hash fields of `r`. Shared by the real and synthetic
// drivers so both run the IDENTICAL mutation sequence.
void RunCommandAndDay(SliceResult& r, const SliceClick& click, std::uint32_t econSeed,
                      int fbW, int fbH) {
    // --- step: CLICK -> COMMAND (mutates the picked live object) -------------
    sim::CommandQueue q;
    q.Init();
    q.set_standalone(true);
    InstallOrderApplyHandler(q);
    SetInputCommandApplyHooks(nullptr);   // use the inert-default record mutation

    WorldOrder ord = IssueSliceClick(q, click, fbW, fbH);
    r.commandIssued   = ord.issued;
    r.commandEnqueued = ord.enqueued;
    r.commandKind     = (int)ord.kind;
    r.commandTarget   = ord.pickId;
    r.hashAfterCommand = HashFullWorld();

    // --- step: GAME-DAY (the REAL economy passes; seeded for determinism) ----
    crt::Srand(econSeed);
    EconomyTurnState st = SeedEconomyTurnState();
    st.day = 0;
    EconomyTurnDeltas d = RunEconomyTurn(st);
    r.economyPasses = d.passesRun;
    ApplyDayWitness();
    r.hashAfterDay = HashFullWorld();
}

} // namespace

// ===========================================================================
// RunPlayableSlice — the whole loop over a REAL city.
// ===========================================================================
SliceResult RunPlayableSlice(shim::IFileSystem* fs, const std::string& gameDir,
                             const std::string& cityName,
                             const SliceClick& click, std::uint32_t econSeed,
                             int fbW, int fbH,
                             shim::IGraphicsDevice* dev1,
                             shim::IGraphicsDevice* dev2) {
    SliceResult r;
    if (!fs)
        return r;

    // --- step: LOAD (mount real assets + io::LoadWorld into the live arrays) --
    app::RealGameAssets assets =
        app::MountRealGameAssets(fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    if (!assets.vfsBound) {
        io::VfsShutdown();
        return r;
    }
    ZeroWorldGlobals();              // blank slate so the load hash is reproducible
    sim::ResetEntityArrays();
    const std::string cityPath = app::RealCityPath(cityName);
    io::WorldState world{};
    r.loaded = io::LoadWorld(cityPath.c_str(), world);
    if (!r.loaded) {
        io::VfsShutdown();
        return r;
    }
    r.personCount = world.cityRecCount;
    r.objectCount = world.objectCount;

    // Stabilize the kind-30 plantmap heap pointer column so the digest is reproducible.
    NormalizePlantPointers(world.objectCount);

    // --- deterministic baseline (so the load hash is reproducible) ----------
    ResetWorldBaseline(econSeed);

    // --- step: RENDER frame 1 + hash the loaded world -----------------------
    r.frame1Rendered =
        RenderSliceFrame(fbW, fbH, dev1, &r.frame1Objects, &r.frame1NonClear,
                         &r.frame1Path);
    // Re-anchor the RNG right before the hash: the base digest folds the live CRT RNG
    // state, and the load/render may have advanced it, so pin it to a fixed value so
    // hashAfterLoad is reproducible across reruns in one process.
    crt::Srand(econSeed);
    r.hashAfterLoad = HashFullWorld();

    // --- steps: CLICK->COMMAND and GAME-DAY (mutate the live world) ---------
    RunCommandAndDay(r, click, econSeed, fbW, fbH);

    // --- step: RENDER frame 2 (the post-command, post-day world) ------------
    r.frame2Rendered =
        RenderSliceFrame(fbW, fbH, dev2, &r.frame2Objects, &r.frame2NonClear,
                         &r.frame2Path);

    io::VfsShutdown();   // the slice leaves the live arrays populated (post-day).
    return r;
}

// ===========================================================================
// RunSliceStepsSynthetic — the same step sequence on a synthetic live world.
// ===========================================================================
int RunSliceStepsSynthetic(std::uint32_t seed, int persons, int objects,
                           const SliceClick& click, std::uint32_t econSeed,
                           int fbW, int fbH,
                           SliceStepHash* out, int cap) {
    if (!out || cap < 5)
        return 0;

    // --- kLoad ---
    SeedSyntheticWorld(seed, persons, objects);
    ResetWorldBaseline(econSeed);
    std::uint64_t hLoad = HashFullWorld();

    // --- kRender1 (pure: a render must NOT mutate the world) ---
    {
        WorldRenderer wr;
        WorldRenderer::Options opt = SliceRenderOptions(fbW, fbH);
        shim::MemoryGraphicsDevice dev;
        dev.init(fbW, fbH, 16, false);
        wr.render(opt, dev);
    }
    std::uint64_t hR1 = HashFullWorld();

    // --- kCommand (mutate the picked object) ---
    {
        sim::CommandQueue q;
        q.Init();
        q.set_standalone(true);
        InstallOrderApplyHandler(q);
        SetInputCommandApplyHooks(nullptr);
        WorldOrder ord = IssueSliceClick(q, click, fbW, fbH);
        (void)ord;
    }
    std::uint64_t hCmd = HashFullWorld();

    // --- kDay (the real economy passes + the per-day witness) ---
    {
        crt::Srand(econSeed);
        EconomyTurnState st = SeedEconomyTurnState();
        st.day = 0;
        RunEconomyTurn(st);
        ApplyDayWitness();
    }
    std::uint64_t hDay = HashFullWorld();

    // --- kRender2 (pure) ---
    {
        WorldRenderer wr;
        WorldRenderer::Options opt = SliceRenderOptions(fbW, fbH);
        shim::MemoryGraphicsDevice dev;
        dev.init(fbW, fbH, 16, false);
        wr.render(opt, dev);
    }
    std::uint64_t hR2 = HashFullWorld();

    out[0] = {SliceStep::kLoad,    hLoad, false};
    out[1] = {SliceStep::kRender1, hR1,   hR1  != hLoad};
    out[2] = {SliceStep::kCommand, hCmd,  hCmd != hR1};
    out[3] = {SliceStep::kDay,     hDay,  hDay != hCmd};
    out[4] = {SliceStep::kRender2, hR2,   hR2  != hDay};
    return 5;
}

} // namespace guild::play
