// Wave 30 PLAY — NATIVE INTERACTIVE CITY SESSION (RunSdlSession). See sdl_session.h.
//
// The device/platform-AGNOSTIC "play the city" loop. It renders the REAL loaded
// city through play::RealCityRenderer into the injected IGraphicsDevice's
// framebuffer, the device PRESENTS it (Vulkan swapchain -> SDL window on the real
// host; MemoryGraphicsDevice headless in tests), and it reads the platform's
// mouse/keys each frame to drive the REAL interaction layer:
//   * left-click EDGE  -> RealCityRenderer::Pick -> if it hits a live object, issue
//                         the cfg.clickCursorMode order on it through the REAL
//                         CommandQueue (play::IssueWorldClick / the order-apply
//                         handler) so the world mutates,
//   * SPACE EDGE       -> play::RunGameDay (advance one real game-day),
//   * arrows / edge    -> play::CameraControl pan,
//   * ESC              -> quit.
//
// Every link is a CALL into an already-reconstructed sibling. This file defines no
// new world state; it composes the same setup tests/e2e/real_city_render_e2e_test
// uses (MountRealGameAssets -> io::LoadWorld -> RealCityRenderer::Init/InitTextures)
// and the same click->order bridge + determinism rig playable_slice.cpp uses
// (ZeroWorldGlobals on load, plant-pointer normalize, Srand before each hash).
#include "play/sdl_session.h"

#include <cstring>
#include <vector>

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save_person.h"            // kPlantBytes / kKindPlant (plant normalization)
#include "io/vfs.h"
#include "play/camera_controls.h"
#include "play/game_day.h"
#include "play/input_command.h"
#include "play/real_city_render.h"
#include "play/scene_pick.h"
#include "play/world_digest.h"
#include "shim/IFileSystem.h"
#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "sim/command.h"
#include "sim/combat_packets.h"
#include "sim/entity.h"
#include "sim/types.h"

// The full set of live world tables HashFullWorld folds — zeroed before the load so
// the session's start/end hashes are a pure function of (loaded city + seed),
// independent of any prior run that dirtied these globals in this process. These are
// exactly the headers world_digest.cpp reads the globals from (mirrors
// playable_slice.cpp::ZeroWorldGlobals).
#include "sim/building.h"
#include "sim/building_create.h"
#include "sim/building_lifecycle.h"
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

// Win32 virtual-key codes the loop polls via IPlatform::keyDown (the SDL platform
// maps these to SDL scancodes in vkeyToScancode; the ScriptedPlatform keys on the
// raw vkey). Mouse buttons come from getMouse().
constexpr int kVkEscape = 0x1B;
constexpr int kVkSpace  = 0x20;
constexpr int kVkLeft   = 0x25;
constexpr int kVkUp     = 0x26;
constexpr int kVkRight  = 0x27;
constexpr int kVkDown   = 0x28;

// Zero EVERY live world table HashFullWorld folds, to a clean blank slate. Called
// BEFORE io::LoadWorld so the loaded-city tables repopulate identically each run and
// the tables the city does NOT populate are blank in every run — making the
// whole-world hash reproducible across reruns in one process. 1:1 with
// playable_slice.cpp::ZeroWorldGlobals (same fold set as world_digest.cpp).
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

// io::LoadWorld points each KIND-30 (plant) object's runtime plantmap pointer (+113)
// at a FRESH heap allocation whose address varies per run; g_objects is folded as raw
// bytes by HashFullWorld, so that pointer column would make the digest non-reproducible
// across reruns. Point every live kind-30 +113 at one shared OWNED scratch buffer
// (mirrors playable_slice / real_session) so the column is stable.
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

int CountLiveObjects() {
    int n = 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) ++n;
    return n;
}

// Map cfg.clickCursorMode (the small int the header carries, mirroring CursorMode)
// to the CursorMode enum the bridge consumes. 6 (the header's documented default) is
// the reliable record-mutating CONQUER order (CursorMode::kConquer).
CursorMode ToCursorMode(int m) {
    switch (m) {
        case 1: return CursorMode::kAttackMove;
        case 2: return CursorMode::kMove;
        case 3: return CursorMode::kLabeled;
        case 6: return CursorMode::kConquer;   // WARE conquer (kind 6)
        default: return CursorMode::kConquer;
    }
}

// Issue the picked object as a unit ORDER through the REAL classifier+builder+queue,
// applying the opcode-80 mutation to that live object. `q` must already have the apply
// handler installed. Mirrors playable_slice.cpp::IssueSliceClick: a single-entry
// roster holding the picked object at the click pixel, a unit-scale top-down camera so
// the roster position projects onto the cursor, the WARE/conquer path (no bonePoint).
WorldOrder IssuePickedOrder(sim::CommandQueue& q, const RealCityRenderer::Options& opt,
                            const ScenePickResult& pick, float sx, float sy,
                            float pickRadius, CursorMode mode) {
    std::vector<ScenePickObject> roster;
    ScenePickObject o;
    o.id = pick.id;
    o.pos[0] = sx;     // unit-scale top-down camera: world XZ -> screen pixels 1:1
    o.pos[1] = 0.0f;
    o.pos[2] = sy;
    roster.push_back(o);

    float eye[3] = {0.0f, 0.0f, 0.0f};
    CityViewCamera cam = MakeCityViewCamera(eye, /*pixelsPerUnit=*/1.0f, opt.fbW, opt.fbH);

    sim::CombatOrderHandle h;
    h.slotKey   = 1;
    h.op80Owner = 1;
    sim::CombatOrderContext ctx;
    ctx.worldToTile = [](float, float, float, i32& tx, i32& tz) {
        tx = 7; tz = 11; return true;
    };

    return IssueWorldClick(q, cam, sx, sy, roster.data(), (int)roster.size(),
                           pickRadius, mode, /*attackAllowed=*/false, h, ctx);
}

} // namespace

// ===========================================================================
// RunSdlSession — the interactive city session loop.
// ===========================================================================
SdlSessionTrace RunSdlSession(shim::IFileSystem& fs, shim::IGraphicsDevice& device,
                              shim::IPlatform& plat, const SdlSessionConfig& cfg) {
    SdlSessionTrace tr;

    // --- LOAD: mount real assets + io::LoadWorld into the live arrays ---------
    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, cfg.gameDir, cfg.iniName, {}, /*caseInsensitive=*/false);
    if (!assets.vfsBound) {
        io::VfsShutdown();
        return tr;
    }
    tr.mounted = true;

    ZeroWorldGlobals();                 // blank slate so the load hash is reproducible
    sim::ResetEntityArrays();
    io::WorldState world{};
    tr.loaded = io::LoadWorld(cfg.cityPath.c_str(), world);
    if (!tr.loaded) {
        io::VfsShutdown();
        return tr;
    }
    NormalizePlantPointers(world.objectCount);
    tr.liveObjects = CountLiveObjects();
    tr.persons     = world.cityRecCount;

    // --- the REAL city renderer over Objects.BIN (+ optional textures) --------
    RealCityRenderer rc;
    if (!rc.Init(&fs, cfg.objectsArchive.c_str())) {
        io::VfsShutdown();
        return tr;
    }
    tr.mounted = rc.mounted();
    if (!cfg.texturesArchive.empty())
        rc.InitTextures(&fs, cfg.texturesArchive.c_str());

    RealCityRenderer::Options opt;
    opt.fbW = cfg.fbW;
    opt.fbH = cfg.fbH;
    opt.textured = cfg.textured && rc.texturesMounted();

    // --- the REAL command queue + order-apply handler -------------------------
    sim::CommandQueue q;
    q.Init();
    q.set_standalone(true);
    InstallOrderApplyHandler(q);
    SetInputCommandApplyHooks(nullptr);     // inert-default record mutation

    const CursorMode clickMode = ToCursorMode(cfg.clickCursorMode);

    // --- hashStart: HashFullWorld after load (determinism witness) ------------
    crt::Srand(cfg.seed);
    tr.hashStart = HashFullWorld();

    // --- the camera (arrow / edge-scroll pan) ---------------------------------
    float camEye[3] = {opt.eyeX, 0.0f, opt.eyeZ};
    CameraControl cam = MakeCameraControl(camEye, /*yaw=*/0.0f, /*zoom=*/0.0f,
                                          opt.fbW, opt.fbH);

    // --- the render / present / input loop ------------------------------------
    bool prevLeft = false;          // left-button edge detector
    bool prevSpace = false;         // SPACE edge detector
    std::uint32_t prevMs = plat.timeMs();
    int frame = 0;
    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames)
            break;

        // (1) render the real city into the device framebuffer.
        rc.Render(opt, device);
        // (2) present — count a success when the device has a live framebuffer.
        device.present();
        if (device.backbuffer() != nullptr)
            ++tr.framesPresented;

        // (3) pump OS messages — false (window close) ends the session.
        if (!plat.pumpMessages()) {
            tr.quitByWindow = true;
            break;
        }

        // (4) input.
        shim::MouseState ms{};
        plat.getMouse(ms);

        // (4a) ESC -> quit.
        if (plat.keyDown(kVkEscape)) {
            tr.quitByEsc = true;
            break;
        }

        // (4b) left-click EDGE -> pick -> issue the order on the hit object.
        bool leftNow = ms.left;
        if (leftNow && !prevLeft) {
            ++tr.clicksHandled;
            ScenePickResult pick = rc.Pick(opt, (float)ms.x, (float)ms.y, /*pickRadius=*/24.0f);
            if (pick.index >= 0 && pick.id != 0) {
                ++tr.picksHit;
                tr.lastPickedId = pick.id;
                WorldOrder ord = IssuePickedOrder(q, opt, pick, (float)ms.x, (float)ms.y,
                                                  24.0f, clickMode);
                if (ord.issued)
                    ++tr.ordersIssued;
            }
        }
        prevLeft = leftNow;

        // (4c) SPACE EDGE -> advance one real game-day.
        bool spaceNow = plat.keyDown(kVkSpace);
        if (spaceNow && !prevSpace && cfg.advanceDayOnSpace) {
            GameDayState st = SeedGameDay(cfg.seed + (std::uint32_t)tr.daysAdvanced);
            RunGameDay(cfg.seed + (std::uint32_t)tr.daysAdvanced, st);
            ++tr.daysAdvanced;
        }
        prevSpace = spaceNow;

        // (4d) arrow keys / screen-edge -> pan the camera.
        PanInput pan;
        if (plat.keyDown(kVkLeft))  pan.dirX = -1;
        if (plat.keyDown(kVkRight)) pan.dirX = +1;
        if (plat.keyDown(kVkUp))    pan.dirZ = -1;
        if (plat.keyDown(kVkDown))  pan.dirZ = +1;
        if (pan.dirX == 0 && pan.dirZ == 0)
            pan = ResolveEdgeScroll(ms.x, ms.y, opt.fbW, opt.fbH, /*margin=*/8);
        std::uint32_t nowMs = plat.timeMs();
        float dt = (float)(nowMs - prevMs);
        prevMs = nowMs;
        CameraUpdatePan(cam, pan, dt);

        // (5) frame-cap.
        if (cfg.frameCapMs > 0)
            plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        ++frame;
    }

    tr.cleanQuit = tr.quitByWindow || tr.quitByEsc;

    // --- hashEnd: HashFullWorld at session end (re-anchor RNG for determinism) -
    crt::Srand(cfg.seed);
    tr.hashEnd = HashFullWorld();

    io::VfsShutdown();
    return tr;
}

} // namespace guild::play
