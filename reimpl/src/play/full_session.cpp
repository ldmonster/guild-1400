// =============================================================================
// guild::play — FULL HEADLESS SESSION INTEGRATOR (M5). See full_session.h.
//
// Drives the REAL boot->menu->load->play->quit spine end to end, proving the game
// is playable: the real menu dispatch arms a session, the real city loads and
// renders, a real P5 action mutates the live world, the frame visibly changes, and
// the real quit decision exits the loop cleanly — byte-identical on rerun.
//
// REUSED (extern, never redefined — ODR):
//   play::ModeFsm / GameMode                            (mode_fsm.h)
//   app::MenuRunMainMenu / MenuMainDecide / MenuResult  (app/menu_loop.h)
//   gui::MainMenu_SetCommandSink / MainMenuCommandSink  (gui/main_menu.h)
//   gui::kSessionNewGame / kSessionLoadSave             (gui/main_menu.h)
//   app::MountRealGameAssets / RealCityPath             (app/real_boot.h)
//   io::LoadWorld / VfsShutdown                         (io/save_world_load.h, io/vfs.h)
//   play::WorldRenderer                                 (world_render.h)
//   play::MakeCityViewCamera / ScenePickObject          (scene_pick.h)
//   play::IssueWorldClick / InstallOrderApplyHandler    (input_command.h)
//   play::SeedEconomyTurnState / RunEconomyTurn         (turn_economy.h)
//   play::HashFullWorld                                 (world_digest.h)
//   sim::CommandQueue / CombatOrderHandle / Context     (sim/command.h, combat_packets.h)
//   sim::g_objects / g_persons / ResetEntityArrays      (sim/entity.h)
//   crt::Srand                                          (crt/rand.h)
//
// The menu->session spine is the REAL app::MenuRunMainMenu dispatch (driven through
// play::ModeFsm so the close/session-flag/quit bookkeeping is the reconstructed
// path) + the REAL app::MenuMainDecide quit decision (ModeFsm::done()). The 3D
// menu fade / form-build / sub-screen runners stay deferred OS/gui leaves (the same
// ones MenuRunMainMenu leaves deferred); we inject the menu click + ESC through the
// real MenuClickSource boundary, exactly as the binary's input pump would.
#include "play/full_session.h"

#include <cstring>
#include <vector>

#include "app/menu_loop.h"
#include "app/real_boot.h"
#include "gui/main_menu.h"
#include "io/save_world_load.h"
#include "io/save_person.h"          // kPlantBytes / kKindPlant (plant-pointer normalize)
#include "io/vfs.h"
#include "play/scene_pick.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "play/world_render.h"
#include "shim/IGraphicsDevice.h"
#include "shim_impl/filedump_graphics.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/scripted_platform.h"
#include "sim/command.h"
#include "sim/combat_packets.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/building_lifecycle.h"  // g_buildingPersons (base digest)
// The full set of live world tables HashFullWorld folds — zeroed before a load so a
// session's hashes are a pure function of (loaded city + seed), independent of any
// prior run that dirtied these globals in this process (same set world_digest folds).
#include "sim/building.h"
#include "sim/building_create.h"
#include "sim/building_production.h"
#include "sim/actionqueue.h"
#include "sim/command_apply5.h"
#include "sim/command_apply6.h"
#include "world/city.h"
#include "world/law.h"
#include "world/event.h"
#include "world/office.h"
#include "world/crime.h"
#include "world/relation.h"
#include "crt/rand.h"

namespace guild::play {

namespace {

// ---------------------------------------------------------------------------
// A scripted main-menu command sink: makes the REAL gui::MainMenu_Dispatch
// deterministic. New Game / Load "succeed" (arm + close the menu); everything else
// is inert. Mirrors the FsmSink the mode_fsm unit test uses.
// ---------------------------------------------------------------------------
struct ScriptedMenuSink : gui::MainMenuCommandSink {
    bool EnterChooseCity() override { return true; }   // New Game arms + closes
    bool RunLoadGame()     override { return true; }    // Load arms + closes
    void Quit()            override { ++quits; }
    int quits = 0;
};

// ---------------------------------------------------------------------------
// A scripted MenuClickSource: clicks the configured hovered button on a fixed
// frame, optionally signalling ESC. Mirrors the mode_fsm unit test's ClickButton.
// ---------------------------------------------------------------------------
struct ScriptedMenuClicks : app::MenuClickSource {
    int  hover;
    int  clickFrame;
    bool esc;
    explicit ScriptedMenuClicks(int h, int cf = 1, bool e = false)
        : hover(h), clickFrame(cf), esc(e) {}
    int  hoverThisFrame(int) override { return hover; }
    bool clickThisFrame(int f) override { return !esc && f == clickFrame; }
    bool escThisFrame(int f) override { return esc && f == clickFrame; }
};

// Zero EVERY live world table HashFullWorld folds (the exact set world_digest reads),
// so the loaded-city tables repopulate identically each run and the tables the city
// does not populate stay blank — making the whole-world hash reproducible across
// reruns in one process. Identical policy to playable_slice::ZeroWorldGlobals.
void ZeroWorldGlobals() {
    using std::memset;
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

// Seed the economy parameter table + RNG to a deterministic baseline (after a load).
void ResetWorldBaseline(std::uint32_t baselineSeed) {
    crt::Srand(baselineSeed);
    world::CityInitParameterTable(100.0f);
    world::g_capDivisor = 0.0f;
}

// Stabilize the kind-30 plantmap heap pointer column (io::LoadWorld points each at a
// fresh per-run allocation, which g_objects raw-byte folding would make
// non-reproducible). Point every live kind-30 +113 at one shared owned scratch.
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

// Render-pass options shared by every session frame (identical, so the only frame
// difference comes from the mutated world). Terrain OFF: the opaque ground quad
// occludes the object layer, so a per-object change would not surface; with the
// object layer as the frame, the city's change is visible frame-to-frame.
WorldRenderer::Options SessionRenderOptions(int fbW, int fbH) {
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

// Render one interactive frame of the live world into `dev` (if non-null), filling
// the per-frame counts, an FNV-1a content hash of the device backbuffer (the
// content-sensitive frame witness) and (for a FileDumpGraphicsDevice) the dumped
// BMP path. Returns whether the frame presented. A render is PURE (no world
// mutation).
bool RenderSessionFrame(int fbW, int fbH, shim::IGraphicsDevice* dev,
                        int* outObjects, int* outNonClear, std::string* outPath,
                        std::uint64_t* outHash) {
    if (outObjects) *outObjects = 0;
    if (outNonClear) *outNonClear = 0;
    if (outPath) outPath->clear();
    if (outHash) *outHash = 0;
    if (!dev)
        return false;
    WorldRenderer wr;
    RenderStats st = wr.render(SessionRenderOptions(fbW, fbH), *dev);
    if (outObjects) *outObjects = wr.lastBuild().sceneObjects();
    if (outNonClear) *outNonClear = wr.binder().nonClearPixels();
    if (outHash) {
        if (shim::Surface* bb = dev->backbuffer()) {
            const u8* p = static_cast<const u8*>(bb->pixels);
            std::uint64_t h = 1469598103934665603ull;            // FNV-1a 64
            const std::size_t n = (std::size_t)bb->pitch * (std::size_t)bb->height;
            for (std::size_t i = 0; p && i < n; ++i) {
                h ^= p[i];
                h *= 1099511628211ull;
            }
            *outHash = h;
        }
    }
    if (outPath) {
        if (auto* fdd = dynamic_cast<shim::FileDumpGraphicsDevice*>(dev))
            *outPath = fdd->framePath(fdd->presentCount() - 1,
                                      shim::FileDumpGraphicsDevice::kBmp);
    }
    return st.presented;
}

// Build the scene-pick roster from the live objects on a deterministic grid (the
// same id-derived layout the render placement uses) so a scripted cursor over a
// cell resolves to a real live object. Returns the first live object's screen pos.
void BuildPickRoster(int fbW, int fbH, std::vector<ScenePickObject>& roster,
                     float* outFirstSX, float* outFirstSY) {
    using namespace guild::sim;
    roster.clear();
    int placed = 0;
    for (int i = 0; i < kObjectCapacity; ++i) {
        if (!g_objects[i].alive)
            continue;
        int gx = (placed % 6);
        int gy = (placed / 6);
        float px = 8.0f + gx * (float)(fbW - 16) / 6.0f;
        float py = 8.0f + gy * 12.0f;
        if (py > fbH - 8) py = (float)(fbH - 8);
        ScenePickObject o;
        o.id = g_objects[i].id;
        o.pos[0] = px; o.pos[1] = 0.0f; o.pos[2] = py;
        roster.push_back(o);
        if (placed == 0) {
            if (outFirstSX) *outFirstSX = px;
            if (outFirstSY) *outFirstSY = py;
        }
        if (++placed >= 32)
            break;
    }
}

// Issue the scripted P5 action as a unit ORDER through the REAL classifier+builder+
// queue, applying the opcode-80 mutation to the picked live object.
WorldOrder IssueSessionAction(sim::CommandQueue& q, const SessionScript& s) {
    std::vector<ScenePickObject> roster;
    float firstSX = s.actionSX, firstSY = s.actionSY;
    BuildPickRoster(s.fbW, s.fbH, roster, &firstSX, &firstSY);
    // Origin cursor -> aim at the first live object (deterministic order onto a real
    // entity).
    float sx = (s.actionSX == 0.0f && s.actionSY == 0.0f) ? firstSX : s.actionSX;
    float sy = (s.actionSX == 0.0f && s.actionSY == 0.0f) ? firstSY : s.actionSY;

    float eye[3] = {0.0f, 0.0f, 0.0f};
    CityViewCamera cam = MakeCityViewCamera(eye, /*pixelsPerUnit=*/1.0f, s.fbW, s.fbH);

    sim::CombatOrderHandle h;
    h.slotKey = 1;
    h.op80Owner = 1;
    sim::CombatOrderContext ctx;
    ctx.worldToTile = [](float, float, float, i32& tx, i32& tz) {
        tx = 7; tz = 11; return true;
    };
    return IssueWorldClick(q, cam, sx, sy,
                           roster.empty() ? nullptr : roster.data(),
                           (int)roster.size(), s.actionPickRadius, s.actionMode,
                           s.actionAttackAllowed, h, ctx);
}

// The per-DAY world WITNESS: a deterministic observable "the city changed this day"
// mutation BOTH the digest and the renderer reflect. Rotates each live object's
// record turn-bits (a HashFullWorld-folded change) and DESPAWNS the first live
// object (alive->0) — the render draw list scans alive slots front-to-back, so the
// next frame emits a different set and the framebuffer differs. Same witness the
// playable slice uses.
void ApplyDayWitness() {
    using namespace guild::sim;
    int firstLive = -1;
    for (int i = 0; i < kObjectCapacity; ++i) {
        if (!g_objects[i].alive)
            continue;
        u8* rec = reinterpret_cast<u8*>(&g_objects[i]);
        rec[0x70] = (u8)((rec[0x70] << 1) | 1u);
        if (firstLive < 0)
            firstLive = i;
    }
    if (firstLive >= 0)
        g_objects[firstLive].alive = 0;
}

// Seed a small deterministic world directly into the live sim arrays (no assets),
// for the synthetic session arc.
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

// ---------------------------------------------------------------------------
// STEP 1 of every session: drive the REAL menu spine to the InGame transition.
// Runs play::ModeFsm over app::MenuRunMainMenu with a scripted New/Load click; the
// REAL gui::MainMenu_Dispatch arms word_63C740 and the REAL app::MenuMainDecide
// routes it to a session sub-mode (NextMode). Records the menu->play edge into `t`.
// Returns true when a session sub-mode was armed and the FSM entered InGame.
// ---------------------------------------------------------------------------
bool DriveMenuToInGame(const SessionScript& s, ModeFsm& fsm, SessionTrace& t) {
    fsm.start();
    t.bootedMenu = (fsm.mode() == GameMode::kMainMenu);

    shim::ScriptedPlatform plat;
    plat.createMainWindow("guild", 800, 600, false);
    plat.setMouse(36, 14, /*left=*/true);

    ScriptedMenuClicks clicks(/*hover=*/s.menuButton, /*clickFrame=*/1);
    app::MenuResult r = fsm.stepMenu(plat, clicks, s.menuMaxFrames);

    t.sessionFlags = r.sessionFlags;
    t.sessionMode  = fsm.mode();
    t.menuToPlay   = IsSessionMode(fsm.mode());
    if (!t.menuToPlay)
        return false;

    fsm.enterInGame();                 // the session goes live (kInGame)
    t.enteredInGame = (fsm.mode() == GameMode::kInGame);
    return t.enteredInGame;
}

// ---------------------------------------------------------------------------
// FINAL STEP of every session: the player issues QUIT. The session returns to the
// menu (endSession), then an ESC click runs one more REAL menu pass whose REAL
// app::MenuMainDecide yields kQuit -> ModeFsm::done(). Records the clean-quit flag +
// the final FSM path into `t`.
// ---------------------------------------------------------------------------
void DriveQuit(const SessionScript& s, ModeFsm& fsm, SessionTrace& t) {
    fsm.endSession();                  // kInGame -> kMainMenu (session returned)

    shim::ScriptedPlatform plat;
    plat.createMainWindow("guild", 800, 600, false);
    ScriptedMenuClicks esc(/*hover=*/-1, /*clickFrame=*/0, /*esc=*/true);
    app::MenuResult rq = fsm.stepMenu(plat, esc, s.menuMaxFrames);
    (void)rq;

    t.cleanQuit = fsm.done();          // dword_63CC48 -> the spine exits cleanly
    t.menuRuns  = fsm.menuRuns();
    t.fsmPath   = fsm.transitions();
}

// ---------------------------------------------------------------------------
// The shared in-game arc (steps 3-7): N pre-action frames, the P5 action, a
// game-day, N post-action frames. Runs over the already-loaded live world; shared
// by the real and synthetic drivers so both execute the IDENTICAL sequence.
// ---------------------------------------------------------------------------
void RunInGameArc(const SessionScript& s, shim::IGraphicsDevice* dev, SessionTrace& t) {
    // -- pre-action interactive frames (real render, pure) --------------------
    for (int i = 0; i < s.framesBeforeAction; ++i) {
        bool pres = RenderSessionFrame(s.fbW, s.fbH, dev, &t.frameBeforeObjects,
                                       &t.frameBeforeNonClear, &t.frameBeforePath,
                                       &t.frameBeforeHash);
        if (pres) { ++t.framesRendered; ++t.preActionFrames; }
    }

    // Re-anchor the RNG before the hash (the base digest folds the live CRT RNG; the
    // load/render advanced it) so hashBeforeAction is reproducible across reruns.
    crt::Srand(s.econSeed);
    t.hashBeforeAction = HashFullWorld();

    // -- the scripted P5 action (mutates the picked live object) --------------
    {
        sim::CommandQueue q;
        q.Init();
        q.set_standalone(true);
        InstallOrderApplyHandler(q);
        SetInputCommandApplyHooks(nullptr);   // inert-default record mutation
        WorldOrder ord = IssueSessionAction(q, s);
        t.actionIssued   = ord.issued;
        t.actionEnqueued = ord.enqueued;
        t.actionKind     = (int)ord.kind;
        t.actionTarget   = ord.pickId;
    }
    t.hashAfterAction = HashFullWorld();

    // -- the game-day the action's turn advanced (real economy passes) --------
    crt::Srand(s.econSeed);
    EconomyTurnState est = SeedEconomyTurnState();
    est.day = 0;
    EconomyTurnDeltas d = RunEconomyTurn(est);
    t.economyPasses = d.passesRun;
    ApplyDayWitness();
    t.hashAfterDay = HashFullWorld();

    // -- post-action interactive frames (the changed world; visibly differs) --
    for (int i = 0; i < s.framesAfterAction; ++i) {
        bool pres = RenderSessionFrame(s.fbW, s.fbH, dev, &t.frameAfterObjects,
                                       &t.frameAfterNonClear, &t.frameAfterPath,
                                       &t.frameAfterHash);
        if (pres) { ++t.framesRendered; ++t.postActionFrames; }
    }
}

} // namespace

// ===========================================================================
// RunFullSession — the whole M5 loop over a REAL city.
// ===========================================================================
SessionTrace RunFullSession(shim::IFileSystem* fs, const std::string& gameDir,
                            const std::string& cityName, const SessionScript& script,
                            shim::IGraphicsDevice* dev) {
    SessionTrace t;
    if (!fs)
        return t;

    // --- STEP 1: boot the REAL menu spine -> InGame --------------------------
    ScriptedMenuSink sink;
    gui::MainMenu_SetCommandSink(&sink);
    ModeFsm fsm;
    bool inGame = DriveMenuToInGame(script, fsm, t);
    if (!inGame) {
        gui::MainMenu_SetCommandSink(nullptr);
        return t;
    }

    // --- STEP 2: LOAD the real city into the live world ----------------------
    app::RealGameAssets assets =
        app::MountRealGameAssets(fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    if (!assets.vfsBound) {
        io::VfsShutdown();
        gui::MainMenu_SetCommandSink(nullptr);
        return t;
    }
    ZeroWorldGlobals();              // blank slate so the load hash is reproducible
    sim::ResetEntityArrays();
    const std::string cityPath = app::RealCityPath(cityName);
    io::WorldState world{};
    t.loaded = io::LoadWorld(cityPath.c_str(), world);
    if (!t.loaded) {
        io::VfsShutdown();
        gui::MainMenu_SetCommandSink(nullptr);
        return t;
    }
    t.personCount = world.cityRecCount;
    t.objectCount = world.objectCount;
    NormalizePlantPointers(world.objectCount);
    ResetWorldBaseline(script.econSeed);

    // --- STEPS 3-7: the interactive in-game arc (frames, action, day, frames) -
    RunInGameArc(script, dev, t);

    // --- STEP 8: QUIT — the REAL spine exits cleanly -------------------------
    DriveQuit(script, fsm, t);

    io::VfsShutdown();
    gui::MainMenu_SetCommandSink(nullptr);
    return t;
}

// ===========================================================================
// RunFullSessionSynthetic — the same scripted arc on a synthetic live world.
// ===========================================================================
SessionTrace RunFullSessionSynthetic(std::uint32_t worldSeed, int persons, int objects,
                                     const SessionScript& script) {
    SessionTrace t;

    // --- STEP 1: boot the REAL menu spine -> InGame --------------------------
    ScriptedMenuSink sink;
    gui::MainMenu_SetCommandSink(&sink);
    ModeFsm fsm;
    bool inGame = DriveMenuToInGame(script, fsm, t);
    if (!inGame) {
        gui::MainMenu_SetCommandSink(nullptr);
        return t;
    }

    // --- STEP 2: seed a small synthetic world into the live arrays -----------
    SeedSyntheticWorld(worldSeed, persons, objects);
    ResetWorldBaseline(script.econSeed);
    t.loaded      = true;
    t.personCount = (std::uint32_t)persons;
    t.objectCount = (std::uint32_t)objects;

    // --- STEPS 3-7: the in-game arc (render frames to a MemoryGraphicsDevice) -
    shim::MemoryGraphicsDevice dev;
    dev.init(script.fbW, script.fbH, 16, false);
    RunInGameArc(script, &dev, t);

    // --- STEP 8: QUIT --------------------------------------------------------
    DriveQuit(script, fsm, t);

    gui::MainMenu_SetCommandSink(nullptr);
    return t;
}

} // namespace guild::play
