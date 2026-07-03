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
// WAVE-2 SESSION INTEGRATION (all opt-in via additive SdlSessionConfig fields;
// the defaults keep the wave-1 loop byte-identical — see sdl_session.h):
//   * cfg.city3d         -> the REAL 3D city view: play::CityView3D over the
//                           .cty's EMBEDDED scene stream (io::LoadWorldEx ->
//                           LoadCityFromWorld -> BindWorldObjects), camera fed
//                           from SessionCamera::pose() (eye + pitch/yaw euler,
//                           terrain-followed eye height via BindTerrain), and
//                           play::SessionInput as THE pick/selection driver
//                           (the reconstructed RunFrameLoop input chain
//                           0x40dab8 -> 0x414a38 -> 0x4b950c).
//   * cfg.continuousClock-> play::SessionTick (TimeBase fptc @0x44e130, clock
//                           proc @0x527778, opcode-30 world commits @0x498954,
//                           the day-end gate @0x4c1324 + the InitOrLoadSession
//                           fast-forward/rollover @0x53449c..0x534671) with the
//                           day transition run through play::RunGameDay.
//   * cfg.applyNewGame   -> play::ApplyNewGameParams right after the world load
//                           (the original's 0x533e03 call site).
//   * cfg.loadSavePath   -> play::LoadLiveWorld session entry (no new-game commit).
//   * cfg.realHudArt     -> play::SetHudSpriteBankFromGfx feeds a REAL converted
//                           gilde.gfx bank to the HUD sprite leaf.
//   * cfg.uiPanels       -> per-frame play::SessionPanelsInputs (clickless hover
//                           pick -> the REAL tooltip dispatch @0x4f7424 + info
//                           panel @0x4b84c0) composited by SessionHud.
//   * cfg.atmosphere     -> additionally play::ApplyAtmosLightingFrame (the
//                           BlendBandLighting @0x5b85e4 ambient store + the
//                           Light_RefreshAllObjects @0x5c886c rebuild) bracketed
//                           by LightAtmosBegin/EndUniverseFrame per frame.
//
// Every link is a CALL into an already-reconstructed sibling. This file defines no
// new world state; it composes the same setup tests/e2e/real_city_render_e2e_test
// uses (MountRealGameAssets -> io::LoadWorld -> RealCityRenderer::Init/InitTextures)
// and the same click->order bridge + determinism rig playable_slice.cpp uses
// (ZeroWorldGlobals on load, plant-pointer normalize, Srand before each hash).
#include "play/sdl_session.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "app/real_boot.h"
#include "gui/hud.h"                   // Clock_ComputeTimeOfDay constants (HUD clock)
#include "gui/input.h"                 // gui hover/click dual views (input reset)
#include "gui/input_state.h"           // gui::ResetMouseInput (input reset)
#include "gui/tooltip.h"               // gui::TooltipKind (panel hover kind)
#include "io/save_world_load.h"
#include "io/save_person.h"            // kPlantBytes / kKindPlant (plant normalization)
#include "io/vfs.h"
#include "play/city_view3d.h"          // the real-3D whole-city view (cfg.city3d)
#include "play/session_persons3d.h"    // live persons in the 3D view (wave 3)
#include "play/game_day.h"
#include "play/input_command.h"
#include "play/input_recon4_hotkey.h"  // g_lastHotkeyChar (input reset)
#include "play/newgame_apply.h"        // ApplyNewGameParams (cfg.applyNewGame)
#include "play/real_city_render.h"
#include "play/scene_pick.h"
#include "play/session_atmos.h"
#include "play/session_audio.h"
#include "play/session_camera.h"
#include "play/session_hud.h"
#include "play/session_input.h"        // the REAL pick/selection driver (city3d)
#include "play/session_npc_daily.h"    // day-start daily-routine destinations (wave 4)
#include "play/session_save.h"
#include "play/session_select.h"
#include "play/session_tick.h"         // the continuous game clock
#include "play/settings_io.h"
#include "play/wire_atmos_bridge.h"    // ApplyAtmosLightingFrame
#include "play/wire_hud_bridge.h"      // SetHudSpriteBankFromGfx (real HUD art)
#include "play/wire_npc_movement.h"    // the REAL path-follow bridge (wave 4)
#include "sim/npc_clip_select.h"        // SelectPersonClipFromMovement (W8-NPCCLIP)
#include "sim/animal.h"                 // Animal_AllocPool/Update (W8-ANIMALS lifecycle)
#include "sim/animal_wander.h"          // Animal_LoadModels/ResetModelHandles
#include "render/particle_emitter_create.h" // DestroyAllSystems (W8-EMITTER teardown)
#include "play/map_view.h"              // MapView_RenderOverview (W8-MAP overview panel)
#include "sim/map.h"                   // MapGrid walkable cells (kCellBlocked)
#include "play/world_digest.h"
#include "render/gfx_archive.h"        // GfxRecord (the HUD bank blob source)
#include "render/heightmap.h"          // city heightmap for BindTerrain
#include "render/scene_floor.h"        // the REAL "<scene>_height" terrain grid
#include "render/light_atmos.h"        // LightAtmosBegin/EndUniverseFrame
#include "render/scene_transform.h"    // MatrixFromEuler / WorldToView (pick boxes)
#include "render/surface.h"            // SurfaceGetPixelRgb (frame dump)
#include "shim/IAudioDevice.h"
#include "shim/IFileSystem.h"
#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "sim/command.h"
#include "sim/combat_packets.h"
#include "sim/entity.h"
#include "sim/person.h"                // PersonGetCashAmount @0x58bc9c (person hover)
#include "sim/person_create.h"         // ResetPersonCreate / g_personNextId
#include "sim/building_stock.h"        // Building_ComputeSalePrice @0x591480
#include "sim/types.h"
#include "world/data_load.h"           // WorldLoadBuildingAndObjectData @0x5835f8

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
constexpr int kVkF5     = 0x74;   // quicksave (SaveLiveWorld -> Quicksave.SAV)
constexpr int kVkF9     = 0x78;   // quickload (LoadLiveWorld <- Quicksave.SAV)
constexpr int kVkM      = 0x4D;   // 'M' — toggle the 2D overview map (W8-MAP)

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
WorldOrder IssuePickedOrder(sim::CommandQueue& q, int fbW, int fbH, i32 pickedId,
                            float sx, float sy, float pickRadius, CursorMode mode) {
    std::vector<ScenePickObject> roster;
    ScenePickObject o;
    o.id = pickedId;
    o.pos[0] = sx;     // unit-scale top-down camera: world XZ -> screen pixels 1:1
    o.pos[1] = 0.0f;
    o.pos[2] = sy;
    roster.push_back(o);

    float eye[3] = {0.0f, 0.0f, 0.0f};
    CityViewCamera cam = MakeCityViewCamera(eye, /*pixelsPerUnit=*/1.0f, fbW, fbH);

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

// ---------------------------------------------------------------------------
// Reset every module-global the SessionInput chain touches (the live stores
// are process globals — dword_672xxx / selection latches / key tables), so a
// 3D session is deterministic across reruns in one process. Mirrors the
// canonical reset in tests/unit/session_input_test.cpp.
// ---------------------------------------------------------------------------
void ResetSessionInputGlobals() {
    gui::ResetMouseInput();
    g_inputClick = InputClickState{};
    g_inputClock = 0;
    g_softCursorMode = 0;
    g_widgetCursorX = 0;
    g_widgetCursorY = 0;
    std::memset(g_mouseEventRing, 0, sizeof g_mouseEventRing);
    std::memset(g_keyDownTable, 0, sizeof g_keyDownTable);
    std::memset(g_keyReleasedTable, 0, sizeof g_keyReleasedTable);
    g_keyRepeatLatch = 0;
    g_keyRepeatDueTick = 0;
    g_lastHotkeyChar = 0;

    g_selectionAnchors = SelectionAnchors{};
    g_selectionOwnerA = 0;
    g_selectionOwnerB = nullptr;
    Selection_SetResetHooks(SelectionResetHooks{});
    SelectEntity_SetHooks(SelectEntityHooks{});
    Input_SetPollHooks(InputPollHooks{});

    g_selectionContact = SelectionContactLatch{};
    g_selectionAnchorRecords = SelectionAnchorRecords{};
    g_selectionCommit = SelectionCommitState{};
    g_selectionOwners = SelectionOwnerRecords{};
    g_quickJump = QuickJumpRequest{};
    g_selectGate = SelectGateState{};
    g_selectionStatus = SelectionStatusLatch{};
    Selection_SetCommitHooks(SelectCommitHooks{});
    StatusLatchHooks sh;
    sh.computeSelectionFlags = [](u16, u8*, u8*, u8*) -> u16 { return 0; };
    sh.statusTextReset = []() {};
    Selection_SetStatusLatchHooks(sh);

    gui::g_hoverObject = -1;
    gui::g_hoverWindow = -1;
    gui::g_lastClickedWindow = -1;
    gui::g_mouseClick = 0;
    gui::g_mouseDown = 0;
}

// One projected live-object screen seat of the current 3D frame — the data the
// SessionInput entity boxes and the order-issue screen point are built from.
struct ProjectedObject {
    i32   id = 0;
    float sx = 0.0f, sy = 0.0f, r = 0.0f;
};

// Dump a 16bpp render::Surface as a binary PPM (P6). Returns true on success.
bool DumpSurfacePpm(const char* path, render::Surface* s) {
    if (!path || !*path || !s)
        return false;
    std::FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    std::fprintf(f, "P6\n%d %d\n255\n", s->width, s->height);
    for (int y = 0; y < s->height; ++y) {
        for (int x = 0; x < s->width; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, y, px);
            std::fwrite(px, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

// Dump the device backbuffer (16bpp RGB565) as a binary PPM. Returns true on success.
bool DumpBackbufferPpm(const char* path, shim::Surface* bb) {
    if (!path || !*path || !bb || !bb->pixels || bb->bpp != 16)
        return false;
    std::FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    std::fprintf(f, "P6\n%d %d\n255\n", bb->width, bb->height);
    const u8* base = static_cast<const u8*>(bb->pixels);
    for (int y = 0; y < bb->height; ++y) {
        const u16* row = reinterpret_cast<const u16*>(base + (std::size_t)y * bb->pitch);
        for (int x = 0; x < bb->width; ++x) {
            const u16 c = row[x];
            u8 px[3];
            px[0] = (u8)(((c >> 11) & 0x1F) << 3);
            px[1] = (u8)(((c >> 5) & 0x3F) << 2);
            px[2] = (u8)((c & 0x1F) << 3);
            std::fwrite(px, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

// ===========================================================================
// WAVE-9 W9-FRAME-ENRICH — CityAnimalWorld: the live IAnimalWorld that routes the
// ambient-animal spawn (sim::Animal_Update -> SpawnAnimal) to the CityView3D
// character render path (creature-wave8.md handoff item 3, the "one genuine gap":
// the host character-actor injection API). On SpawnAnimal it resolves the species
// model name (kind byte -> the LoadModels table; the SpawnCat/SpawnDog model swap
// is REAL — Cat loads "hund_HUND", Dog loads "katze_KATZE") and seats a renderable
// instance via CityView3D::AddAnimalInstance at a spawn placement (a real bound
// building's world position — the host stand-in for FindDoorTarget/PickSpawnBuilding
// over the live scene this session carries). The animal then draws through the same
// character mesh pass persons use (Options::animals). DestroyAnimal removes it.
// ===========================================================================
class CityAnimalWorld : public sim::IAnimalWorld {
public:
    explicit CityAnimalWorld(CityView3D* view) : view_(view) {}

    i32 SpawnAnimal(guild::u8 kind, float* outX, float* outY, float* outZ) override {
        // kind -> species model (creature-wave8.md model table; the Cat/Dog swap is
        // faithful: kAnimalCat==0 -> "hund_HUND", kAnimalDog==1 -> "katze_KATZE").
        const char* model = nullptr;
        switch (kind) {
            case sim::kAnimalCat:   model = "hund_HUND";       break;  // swap (real)
            case sim::kAnimalDog:   model = "katze_KATZE";     break;  // swap (real)
            case sim::kAnimalCow:   model = "kuh_KUH";         break;
            case sim::kAnimalSheep: model = "schaf_SCHAF";     break;
            case sim::kAnimalPig:   model = "schwein_SCHWEIN"; break;
            case sim::kAnimalHorse: model = "pferd_PFERD";     break;
            default: model = "schaf_SCHAF"; break;             // livestock fallback
        }
        // Placement: a real bound building's world position (the host stand-in for
        // the FindDoorTarget/PickSpawnBuilding door anchor over the live scene). Pick
        // round-robin over the bound buildings so successive spawns scatter.
        CityPlacement place{};
        const auto& objs = view_->boundObjects();
        if (!objs.empty()) {
            const auto& b = objs[spawnCursor_ % objs.size()];
            place.pos[0] = b.place.pos[0];
            place.pos[1] = b.place.pos[1];
            place.pos[2] = b.place.pos[2];
            ++spawnCursor_;
        }
        if (outX) *outX = place.pos[0];
        if (outY) *outY = place.pos[1];
        if (outZ) *outZ = place.pos[2];
        i32 tok = view_->AddAnimalInstance(model, place);
        return tok;   // 0 if the species mesh is not shipped (rule 8: not drawn)
    }

    void DestroyAnimal(i32 actor) override {
        if (actor)
            view_->RemoveAnimalInstance(actor);
    }

private:
    CityView3D* view_ = nullptr;
    std::size_t spawnCursor_ = 0;
};

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
    std::vector<u8> sceneBlob;          // the .cty's EMBEDDED city scene (city3d)
    if (!cfg.loadSavePath.empty()) {
        // LOAD-GAME ENTRY: the menu's Load-Game pick (NativeMenuResult::kLoadGame)
        // — play::LoadLiveWorld over the partial .SAV (it blanks + seeds itself),
        // entering the session WITHOUT ApplyNewGameParams.
        SessionLoadInfo li = LoadLiveWorld(fs, cfg.loadSavePath.c_str(), cfg.seed);
        tr.loaded = li.ok;
        tr.loadedFromSave = li.ok;
        world.objectCount = li.objectCount;
        world.cityRecCount = li.personCount;
    } else {
        // The city load (the original's "%s/%s.cty" Save_LoadGameFile @0x533d4b).
        // LoadWorldEx additionally captures the embedded scene stream — the REAL
        // post-load source of the city node placements + owner-object ids
        // (VIBE_Save_PostLoadInitScene @0x5a7ef8 -> Scene_LoadFromStream @0x5e7e38).
        tr.loaded = io::LoadWorldEx(cfg.cityPath.c_str(), world,
                                    cfg.city3d ? &sceneBlob : nullptr);
    }
    if (!tr.loaded) {
        io::VfsShutdown();
        return tr;
    }
    NormalizePlantPointers(world.objectCount);
    tr.liveObjects = CountLiveObjects();
    tr.persons     = world.cityRecCount;

    // --- REAL TYPE CATALOGS (the original's session-start data load) ----------
    // VIBE_World_LoadBuildingAndObjectData @0x5835f8: data/A_Geb.dat (72 x 589
    // BuildingTypeDef -> dword_13CE294) + data/A_Obj.dat (731 x 65 SceneTypeDef
    // -> dword_13CE27C), then VIBE_City_InitParameterTable @0x577a9c seeds the
    // 28-good economy table — exactly the Game_InitWorldAndSounds block the app
    // spine runs (app/wiring.cpp). The tooltip/info-panel record feeds and the
    // economy leaves (ComputeSalePrice @0x591480) read these catalogs; the
    // wave-2 session left them blank (the zeroed-panel-content gap). Runs after
    // the world load only because LoadLiveWorld's determinism blank-slate wipes
    // them — the loaders are independent, so the order is behavior-neutral.
    auto loadTypeTables = [&]() {
        tr.typeTablesLoaded =
            world::WorldLoadBuildingAndObjectData(&fs, "data/") == 0;
        world::CityInitParameterTable(100.0f);
    };
    loadTypeTables();

    // --- the REAL command queue + order-apply handler -------------------------
    sim::CommandQueue q;
    q.Init();
    q.set_standalone(true);
    InstallOrderApplyHandler(q);
    SetInputCommandApplyHooks(nullptr);     // inert-default record mutation

    // --- NEW-GAME COMMIT (the original's 0x533e03 call site, right after the
    //     world load): the chosen identity + parents + relations + start gold
    //     land in the live sim::g_persons through the REAL packet pipeline.
    //     Exactly the sequence tests/e2e/newgame_apply_e2e_test.cpp proves. -----
    if (cfg.applyNewGame && cfg.loadSavePath.empty()) {
        sim::ResetApply5State();
        sim::ResetPersonCreate();
        sim::ResetApply6State();
        sim::RegisterApplyHandlers6(q);     // batch 6: the opcode-27 relations
        i32 maxId = 0;
        for (int i = 0; i < sim::kPersonCapacity; ++i) {
            if (sim::g_persons[i].marker == -1) continue;
            if (sim::g_persons[i].id > maxId) maxId = sim::g_persons[i].id;
        }
        sim::g_personNextId = maxId + 1;    // allocate after the city's population
        sim::g_personArrayLoaded = true;
        crt::Srand(cfg.seed);
        NewGameApplyInputs ngin;
        // byte_6477A1 at the 0x533e03 commit / 0x5339df purse time: the FRESH-CITY
        // rate is 100 — the InitOrLoadSession reconstruction's purse block
        // (app/session_init.cpp: cityRate = 100; gold = Money_MultiplyByRate(base,
        // cityRate) @0x58f19c). NOTE the .cty scalar block itself carries 0 in
        // this byte (verified against AUGSBURG.cty), so the scalar is NOT the
        // commit-time value; the loaded scalar's live home is
        // play::SessionLoadedRateByte() (the LoadLiveWorld capture) for the
        // load path, where ApplyNewGameParams never runs.
        ngin.rateByte = 100;                // byte_6477A1 (fresh-city rate)
        NewGameApplyResult ngr =
            ApplyNewGameParams(cfg.newGame, cfg.newGame.difficulty, q, ngin);
        tr.newGameApplied = ngr.applied && !ngr.createFailed;
        tr.playerId = tr.newGameApplied && ngr.playerId > 0 ? ngr.playerId : 0;
    }

    // --- HOST/TEST seam (wave 4; NOT an engine feature): stage live-record
    //     state before the 3D view binds (see SdlSessionConfig docs). ---------
    if (cfg.postWorldLoadHook)
        cfg.postWorldLoadHook();

    // --- the city renderer: the REAL 3D view (cfg.city3d) or the legacy
    //     top-down RealCityRenderer ---------------------------------------------
    CityView3D view;
    bool use3d = false;
    bool mapViewOpen = false;   // W8-MAP: the overview-map panel toggle ('M')
    bool mapKeyPrev = false;    // edge-detect latch for the toggle key
    if (cfg.city3d) {
        if (view.Init(&fs)) {
            bool sceneOk = !sceneBlob.empty() && view.LoadCityFromWorld(sceneBlob);
            if (!sceneOk) {
                // LOAD-GAME / fallback 3D scene. A partial .SAV embeds no scene
                // stream (the engine save's VIBE_WorldIo_SaveSceneObjects
                // @0x5e65b8 sidecar — read by VIBE_Save_PostLoadInitScene
                // @0x5a7ef8 -> Scene_LoadFromStream @0x5e7e38 — has no
                // reconstructed WRITE side yet, named gap). Derive the city the
                // way the original's own data carries it:
                //   (1) the caller's explicit cfg.cityName3d,
                //   (2) the loaded save's header city name (SaveHeader +0x05 —
                //       the field the shipped .cty seeds carry, e.g. "Augsburg";
                //       the same field app/wiring.cpp publishes as cityName_),
                //   (3) the INI [General] Stadt (the ReturnedString @0x122EE50
                //       seed the "%s/%s.cty" load @0x533d4b formats from).
                // The scene then loads from scenes.BIN Staedte/stadt_<city>.ed3
                // (VIBE_Scene_LoadStadtScene @0x500218; case-insensitive mount).
                const std::string tries[3] = {
                    cfg.cityName3d,
                    tr.loadedFromSave ? SessionLoadedCityName() : std::string(),
                    assets.stadt};
                for (const std::string& n : tries) {
                    if (n.empty())
                        continue;
                    if (view.LoadCity(n.c_str())) {
                        sceneOk = true;
                        tr.city3dCityName = n;
                        break;
                    }
                }
            }
            if (sceneOk) {
                view.SetHooks({});           // REAL defaults: owner-id node match +
                                             // the node's shipped mesh (0x5a8140)
                tr.view3dBoundObjects = view.BindWorldObjects();
                use3d = true;
            }
        }
    }
    tr.view3dActive = use3d;

    // --- PERSONS IN THE 3D VIEW (wave-3): live sim::g_persons as real posed
    //     character meshes in the same universe pass as the buildings —
    //     SpawnAtBuildingEntrance @0x57c8f0 placement (entrance dummy) ->
    //     ResolveStaffModel @0x57c1e8 -> CreateFromModel @0x402d10 ->
    //     UpdateSkeletonPose @0x5cd1d8. Bound AFTER the new-game commit above so
    //     the freshly created player/parents are in the roster. ----------------
    SessionPersons3DOptions persons3dOpt;
    if (use3d && cfg.persons) {
        persons3dOpt.animStepPerFrame = cfg.personAnimStep;
        WireSessionPersons3D(view, &fs, persons3dOpt);
    }

    RealCityRenderer rc;                     // the legacy view (and 3D fallback)
    RealCityRenderer::Options opt;
    opt.fbW = cfg.fbW;
    opt.fbH = cfg.fbH;
    CityView3D::Options opt3;
    if (use3d) {
        tr.mounted = view.mounted();
        // The in-city 3D viewport EXCLUDES the right HUD sidebar: the live
        // SetupViewTransform @0x5af5f8 globals (frida, gilde.exe in-city) are
        // width=694 height=600 origin(0,0) centre(347,300) at 800x600 — the
        // sidebar owns the right 106px. The engine projection scale
        // flt_13FCD0C reads 640 live (sx = x/z*640 + 347) — hFOV
        // 2*atan(347/640) ~ 57 deg. (0x13FCAFC == 5093.07 is only the LOD
        // fovScale divisor 1/flt_13FC774, NOT the projection.) Scale both
        // with the configured resolution.
        opt3.fbW = cfg.fbW;                       // the full DDraw surface
        opt3.viewportW = cfg.fbW * 694 / 800;     // the 3D projection viewport
        opt3.fbH = cfg.fbH;
        opt3.viewScale = 640.0f * (float)cfg.fbW / 800.0f;
        opt3.textured = cfg.textured && view.texturesMounted();
        // GROUND PASS (terrain-ground wave 4): the real parsed city floor
        // through the BeginUniverseFrame @0x5B3900 0x5b3a2f arm. No-op when
        // the loaded scene carried no floor block.
        opt3.terrain = view.hasGround();
        opt3.water   = view.hasGround();   // water draws inside the ground pass
        // WAVE-6 W6-INTEGRATE — the full world-entity scene. The session is where
        // the visible scene comes alive: turn ON every wave-6 render feature so
        // guild_run --play shows the complete frame (sky backdrop, day/night lit
        // objects, drop shadows, fog, weather). The unit/e2e frame pins keep the
        // Options DEFAULT-OFF so they stay byte-identical; only this live session
        // opts in. Each feature honours its own engine gate inside CityView3D.
        opt3.sky          = true;   // time-of-day sky backdrop
        opt3.dynamicLight = true;   // day/night object shading (ComputeSunState)
        // Per-object drop shadows: OFF by default. The doShadows pass splats
        // EVERY instance's flattened silhouette — an approximation whose huge
        // angular ground patches visibly diverge from the original (the engine
        // leaf RenderObjectShadow @0x5f3f38 exists, but its real caster set /
        // trigger is not yet established — named gap). GUILD_SHADOWS=1 enables
        // the experimental pass for A/B work.
        opt3.shadows      = std::getenv("GUILD_SHADOWS") != nullptr;
        opt3.fog          = true;   // distance fog per-pixel span blend
        opt3.lodSelect    = true;   // per-distance node LOD pick
        opt3.worldSprites = true;   // billboard depth-fade sprite arm
        opt3.particles    = true;   // world particle systems (when spawned)
        opt3.mirror       = true;   // reflection pass (when a mirror node exists)
        opt3.weather      = std::getenv("GUILD_NO_WEATHER") == nullptr;   // snow/rain overlay (season/rain gated)
        if (std::getenv("GUILD_NO_CKEY"))          // debug A/B: colour key off
            render::SetColourKeyEnabled(false);
        // WAVE-8 W8-INTEGRATE — turn ON the new world-entity features in the live
        // session (DEFAULT-OFF keeps every pinned frame byte-identical):
        opt3.gouraudLight = true;   // per-pixel RGB Gouraud texture modulate (the
                                    // hardware path's diffuse: warm lantern pools,
                                    // tinted night ambient)
        // 24-bit material stand-in: the engine palettizes 24-bit BMPs via the
        // NOT-YET-reconstructed quantizer @0x5da34c; the default renders those
        // materials as LEVEL-shaded white (craft signs etc. glow white). The
        // documented stand-in binds them as RGB-affine instead — the real texel
        // colours through the in-tree affine kernel.
        render::SetRgb24MaterialStandIn(true);
        opt3.sceneLights  = true;   // per-vertex SUN-LIT object shading (W8-NORMALS +
                                    // W8-SCENELIGHTS: real object-space normals + the
                                    // day-cycle sun NdotL — objects are no longer flat)
        // WAVE-9 W9-FRAME-ENRICH — fire the wave-8 reconstructions that were inert in
        // the live frame because the simplified instance pipeline did not carry their
        // per-object data. Each rides its own gate inside CityView3D (DEFAULT OFF keeps
        // every pinned frame byte-identical; the session opts in):
        const bool extras = std::getenv("GUILD_NO_EXTRAS") == nullptr;
        opt3.flagAnim   = extras; // RefreshFlagAnimation over building dummy_FAHNE children
        opt3.vegRelight = extras; // per-frame type-4 vg_/pfl_ scenery light-cache relight
        opt3.reflective = extras; // consult reflective_nodes on scene meshes -> mirror gate
        opt3.animals    = extras; // draw the ambient animals through the character path
        // Spawn the city's chimney smoke (W8-EMITTER + W8-SMOKE: a particle system at
        // every dummy_RAUCH_0 node, linked into render::LiveSystems() the doParticles
        // walk drives). No-op when the scene ships no smoke dummies.
        tr.view3dSmokeSystems = view.SpawnCityChimneySmoke();
        // WAVE-9 W9-FRAME-ENRICH — FLAGS: fire RefreshFlagAnimation @0x4b5ef8 over each
        // bound building's universe-node dummy_FAHNE children (the object-refresh path
        // trigger, == VIBE_Character_RefreshAllFlags @0x4b5fa4). The engine's own gate
        // (heraldry != 0xFFFF && build-type in {5,6,7}) shuts unless a heraldry table
        // is supplied; with no per-building heraldry column carried in this view the
        // gate stays shut (faithful — no flag fired where the engine itself would not).
        // A host that carries the heraldry supplies the provider; the attach/refresh
        // arm is now WIRED to the live scene-graph child list (cloth-anim-wave8.md).
        tr.view3dFlagObjects = view.RefreshObjectFlags();
        // WAVE-8 W8-ANIMALS — the ambient-animal subsystem LIFECYCLE (creature-
        // wave8.md handoff): at city load allocate the 32-slot pool + load the 7
        // animal meshes; per tick the frame loop runs the real spawn-decision /
        // lifetime core (Animal_Update(season)). The live IAnimalWorld that routes
        // SpawnAnimal -> sim::CreateFromModel + door/building placement + the
        // character render path is the documented session boundary (no host
        // character-actor injection API on CityView3D yet) — with the inert default
        // world the Update core runs faithfully and spawns nothing (rule 8: no faked
        // animals). Teardown after the frame loop.
        sim::Animal_AllocPool();
        sim::Animal_LoadModels();
        tr.view3dAnimalsActive = true;
    } else {
        if (!rc.Init(&fs, cfg.objectsArchive.c_str())) {
            io::VfsShutdown();
            return tr;
        }
        tr.mounted = rc.mounted();
        if (!cfg.texturesArchive.empty())
            rc.InitTextures(&fs, cfg.texturesArchive.c_str());
        opt.textured = cfg.textured && rc.texturesMounted();
        // Person pass: live sim::g_persons rows rendered as their REAL character
        // meshes through the 1:1 pose chain (ResolveStaffModel @0x57c1e8 ->
        // UpdateSkeletonPose @0x5cd1d8 -> CalculateAnimNormals @0x5d0020).
        if (cfg.persons) {
            opt.scanPersons    = true;
            opt.personAnimStep = cfg.personAnimStep;
            rc.InitPersonAnims(&fs);
        }
        // Day/night brightness application: relight each resolved mesh through the
        // reconstructed lighting-table rebuild (LightAtmosEnsureNodeLit ->
        // BuildObjectCache @0x5c8218); pixels only — identical at the 200 ambient.
        opt.atmosRelight = cfg.atmosphere;
    }

    // WAVE-9 W9-FRAME-ENRICH — install the live IAnimalWorld that draws the ambient
    // animals the per-tick Animal_Update spawns (the creature-wave8.md render-arm
    // gap). The world routes SpawnAnimal -> CityView3D::AddAnimalInstance (the
    // character render path). The spawn gate (Animal_Update) requires a non-clear
    // weather basis (g_weatherState != 0 -> a non-zero population cap) AND >=350
    // ticks between attempts — the engine's own throttle; the session seeds a light
    // weather state so ambient animals can appear (byte_1233514 == 1, cap 16). The
    // world OUTLIVES the loop; uninstalled at teardown.
    CityAnimalWorld cityAnimalWorld(&view);
    if (use3d && tr.view3dAnimalsActive) {
        sim::SetAnimalWorld(&cityAnimalWorld);
        if (sim::g_weatherState == 0)
            sim::g_weatherState = 1;   // light weather -> non-zero spawn cap (16)
    }

    const CursorMode clickMode = ToCursorMode(cfg.clickCursorMode);

    // --- hashStart: HashFullWorld after load (determinism witness) ------------
    crt::Srand(cfg.seed);
    tr.hashStart = HashFullWorld();

    // --- the camera: the REAL reconstructed camera cluster --------------------
    // SessionCamera drives VIBE_Camera_Update @0x4b4c68 -> UpdateMovement
    // @0x4b41a8 (drag pan / wheel zoom / rotate) + the edge/key pan core
    // @0x4b365c, and its eye/zoom feed the per-frame render options (closing
    // the prior dead link where pan never reached the renderer).
    SessionCamera cam;
    render::Heightmap terrainHm{};
    std::vector<u8> terrainHeights;
    std::vector<u8> moveEntries;        // walkable tile records (wave-4 movement)
    bool npcMoveActive = false;
    if (use3d) {
        // City terrain bind: the REAL elevation grid. The heights are STORED in
        // the scene stream's floor block ("<scene>_height" record — wave-3
        // finding, see render/scene_floor.h and progress/terrain-colorkey-wave3.md);
        // ParseSceneFloorHeights replays the 0x5e7e38/0x5e67c8 grammar to the
        // floor flag and BuildCityHeightmapFromFloor assembles the Heightmap
        // exactly as VIBE_Heightmap_BuildTerrainMesh @0x5c5610 (DeriveGridScaleXZ
        // + originY = minY + 1.0 + scaleY = (maxY-minY)/252.85), so the camera's
        // terrain follow (Camera_AnchorToTerrain @0x4b2900 / ClampToTerrainHeight
        // @0x4b2a0c) rides the genuine ground.
        float lo[3], hi[3];
        view.WorldBounds(lo, hi);
        render::SceneFloorHeights fl = render::ParseSceneFloorHeights(sceneBlob);
        if (fl.ok &&
            render::BuildCityHeightmapFromFloor(fl, lo, hi, terrainHm,
                                                terrainHeights)) {
            // real grid bound (also the NPC movement walk grid below)
        } else {
            // No floor block in the loaded scene (e.g. the stadt_*.ed3 load-game
            // path's standalone scene was unavailable): flat heightmap over the
            // real city world bounds, the wave-2 behavior.
            terrainHeights.assign(64 * 64, 0);
            std::memset(&terrainHm, 0, sizeof terrainHm);
            terrainHm.size = 64;
            terrainHm.heights = terrainHeights.data();
            render::DeriveGridScaleXZ(&terrainHm, lo[0], hi[0], hi[2], lo[2]);
            terrainHm.originY = lo[1] + 1.0f;  // the 0x5c5610 originY = minY + 1.0
            terrainHm.scaleY = 0.0f;
        }
        // Camera bind: prefer the view's REAL @0x5c47dc-filled city heightmap
        // (terrain-ground wave 4 — the same grid the ground pass draws from);
        // fall back to the raw-copy grid above. The movement walk grid below
        // stays on terrainHm either way (it owns the entries plane).
        if (const render::Heightmap* vhm = view.cityHeightmap())
            cam.BindTerrain(vhm);
        else
            cam.BindTerrain(&terrainHm);

        // --- LIVING CITY (wave 4): the real NPC path-follow over this terrain.
        // Walkable grid: the terrain-type/collision bytes live in the UNPARSED
        // remainder of the scene floor block (VIBE_WorldIo_LoadFloorRegions
        // @0x5e78a8 — named gap, see render/scene_floor.h), so the session
        // walks an OPEN grid over the real heightmap with a blocked border
        // (the A* boundary requirement) — the wire_npc_movement e2e precedent.
        // The tile<->world geometry (origins/scales/heights) is fully real.
        if (cfg.persons && cfg.npcMovement && terrainHm.size > 0) {
            const int n = terrainHm.size;
            moveEntries.assign((std::size_t)n * n * sim::kTileEntryStride, 0);
            for (int i = 0; i < n * n; ++i)
                moveEntries[(std::size_t)i * sim::kTileEntryStride] = 1;
            terrainHm.entries = moveEntries.data();
            sim::MapGrid mg{n, moveEntries.data()};
            for (int i = 0; i < n; ++i) {
                sim::MapSetCellAt(mg, i, 0, sim::kCellBlocked);
                sim::MapSetCellAt(mg, i, n - 1, sim::kCellBlocked);
                sim::MapSetCellAt(mg, 0, i, sim::kCellBlocked);
                sim::MapSetCellAt(mg, n - 1, i, sim::kCellBlocked);
            }
            SetNpcMovementGrid(mg);
            InstallNpcMovement();
            ResetNpcMovementTallies();
            // The documented placementOverride handoff: moving persons draw at
            // TileToWorld(curTile) over this heightmap; everyone else keeps the
            // captured entrance-dummy seat (session_persons3d.h, wave 4).
            InstallSessionPersonsMovePlacement(view, persons3dOpt, &terrainHm);
            npcMoveActive = true;
            tr.npcMovementActive = true;
        }

        // Initial seat: the eye behind the city centre so the REAL anchored
        // pitch (Camera_AnchorToTerrain @0x4b2900: baseAngle at zoom 0, forward
        // = (0, sin(pitch), cos(pitch)) for yaw 0) looks AT the centre — the
        // host-default seat, like CityView3D::OverviewCamera.
        const render::CameraState defcs{};
        const float sp = std::sin(defcs.baseAngle);
        const float cp = std::cos(defcs.baseAngle);
        const float dist = (sp < -1e-4f) ? defcs.baseHeight * (cp / -sp) : 800.0f;
        cam.Init(0.5f * (lo[0] + hi[0]), 0.5f * (lo[2] + hi[2]) - dist,
                 cfg.fbW, cfg.fbH);
    } else {
        cam.Init(opt.eyeX, opt.eyeZ, opt.fbW, opt.fbH);
    }
    const float basePixelsPerUnit = opt.pixelsPerUnit;

    // Debug/verification: pin the camera to an exact pose read from the live
    // original (frida ground truth) so frames are directly diffable.
    // GUILD_CAM_EYE="x,y,z" (world position) + optional GUILD_CAM_YAW=f.
    // GUILD_CAM_LOCK=1 freezes the camera (no pan/zoom input) for the run.
    bool camLock = std::getenv("GUILD_CAM_LOCK") != nullptr;
    if (const char* ce = std::getenv("GUILD_CAM_EYE")) {
        float ex, ey, ez;
        if (std::sscanf(ce, "%f,%f,%f", &ex, &ey, &ez) == 3) {
            cam.obj.posX = ex; cam.obj.posY = ey; cam.obj.posZ = ez;
            cam.obj.wposX = ex; cam.obj.wposY = ey; cam.obj.wposZ = ez;
            if (const char* cy = std::getenv("GUILD_CAM_YAW")) {
                const float yaw = (float)std::atof(cy);
                cam.obj.worldY = yaw; cam.obj.wrotY = yaw;
            }
            camLock = true;
        }
    }

    // --- HUD overlay (money/date caption, player bar, status, markers) --------
    SessionHud hud;
    tr.hudActive = cfg.hud && (hud.Init(&fs), true);

    // PANEL CHROME — the in-city screen furniture over the 694px 3D viewport:
    // the real _PANEL_STEIN (gold top banner + right stone sidebar, transparent
    // centre) + the city's _STADTWAPPEN crest, per the original's in-city frame.
    if (tr.hudActive && hud.gfxLoaded()) {
        std::string base;   // "Resources/gamedata/Cities/KOELN.CTY" -> "KOELN"
        {
            std::string p = cfg.cityPath;
            const std::size_t sl = p.find_last_of("/\\");
            if (sl != std::string::npos) p = p.substr(sl + 1);
            const std::size_t dot = p.rfind('.');
            if (dot != std::string::npos) p = p.substr(0, dot);
            for (char& c : p) c = (char)std::toupper((unsigned char)c);
            base = p;
        }
        const std::string crest = "_STADTWAPPEN_" + base;
        if (hud.DecodePanelChrome("_PANEL_STEIN", crest.c_str())) {
            // Anchor the HUD text into the chrome (design 800x600, scaled):
            // date/time in the gold top banner; money in the sidebar money
            // slot; status line under the banner; player bar above the bottom
            // edge of the 3D viewport; markers inside the viewport.
            const int W = cfg.fbW, H = cfg.fbH;
            hud.layout.captionX = 360 * W / 800;  // date line (banner centre)
            hud.layout.captionY = 18 * H / 600;
            hud.layout.moneyX   = 706 * W / 800;  // sidebar money slot
            hud.layout.moneyY   = 484 * H / 600;
            hud.layout.statusX  = 12 * W / 800;
            hud.layout.statusY  = 56 * H / 600;
            hud.layout.barX     = 4 * W / 800;
            hud.layout.barY     = (600 - 90) * H / 600;
            // Map markers: the original's in-city frame shows no floating
            // marker region (its map anchors live in the map PANEL, 'M') — park
            // the region off-frame so the placeholder icons don't stamp the
            // 3D view. The 'M' overview map renders its own markers.
            hud.layout.mapX     = W + 64;
            hud.layout.mapY     = H + 64;
            // The selected-entity info panel lives in the SIDEBAR CARD slot
            // (the original's "Patri / building / thumbnail" card at ~(698,
            // 255)-(790,400)): content only, the stone art is the background.
            hud.panels().layout.panelX  = 700 * W / 800;
            hud.panels().layout.panelY  = 258 * H / 600;
            hud.panels().layout.panelW  = 90 * W / 800;
            hud.panels().layout.panelH  = 140 * H / 600;
            hud.panels().layout.drawBox = false;
        }
    }

    // --- REAL HUD ARTWORK: feed the gfx-1403 icon bank's raw SHAPBANK blob
    //     through the reconstructed conversion chain (ShapeBankConvertNew
    //     @0x5d80a8 -> ShapeConvertRgbTo16 @0x5d7c0c — the lazy convert
    //     VIBE_State_Helper @0x40e014 runs after d2_LoadObj). ------------------
    const u8* hudBank = nullptr;
    if (tr.hudActive && cfg.realHudArt && hud.gfxLoaded()) {
        const render::GfxArchive& ar = hud.archive();
        int idx = -1;
        if (cfg.hudIconGfxId >= 0 &&
            (std::size_t)cfg.hudIconGfxId < ar.recordCount() &&
            ar.record((std::size_t)cfg.hudIconGfxId).dataSize > 0)
            idx = cfg.hudIconGfxId;
        // Prefer the GEB record — the 48x48 building-thumbnail bank the info
        // panel's icon-object ids index (building icon id = code + 1010, the
        // gui/infopanel_build kIconObjBias). It takes priority over the legacy
        // cfg.hudIconGfxId pick; falls back to the first non-empty record (the
        // legacy single-shape behaviour).
        bool gebBank = false;
        {
            const int geb = ar.FindByName("GEB");
            if (geb >= 0 && ar.record((std::size_t)geb).dataSize > 0) {
                idx = geb;
                gebBank = true;
            }
        }
        if (idx < 0) {
            for (std::size_t i = 0; i < ar.recordCount(); ++i)
                if (ar.record(i).dataSize > 0) { idx = (int)i; break; }
        }
        if (idx >= 0) {
            const render::GfxRecord& rec = ar.record((std::size_t)idx);
            std::vector<u8> blob(rec.dataSize);
            if (shim::IFile* f = fs.open("gfx/gilde.gfx", "rb")) {
                f->seek((std::int64_t)rec.dataOffset, 0);
                const bool ok = f->read(blob.data(), blob.size()) == blob.size();
                fs.close(f);
                if (ok)
                    hudBank = SetHudSpriteBankFromGfx(blob.data(), blob.size());
            }
        }
        // With the GEB bank live, map icon-object ids to bank shapes:
        // shape = gfxId - kIconObjBias (id base 1010).
        SetHudSpriteIdBase(hudBank && gebBank ? 1010 : -1);
        tr.hudRealArt = hudBank != nullptr;
    }

    // --- selection layer (the real commit @0x4b950c / reset @0x4b9444) --------
    SessionSelect sel;                       // legacy-path selection
    SessionInput input;                      // city3d: THE pick/selection driver
    if (use3d) {
        ResetSessionInputGlobals();          // deterministic across reruns
        input.SetViewport(0, 0, cfg.fbW, cfg.fbH);
        input.SetSoftwareCursor(true);       // dword_62D0D4 = 1 (city scene)
    }
    SessionInput::Result lastIn{};           // last 3D input-frame result
    int hoverId = -1;                        // clickless hover pick (tooltips)
    int hoverSlot = -1;                      // hovered g_objects slot (building feed)
    int hoverPersonSlot = -1;                // hovered g_persons slot (legacy person)
    int curSelId = 0;                        // live selection id (HUD/panels)
    int curSelKind = 0;                      // selection kind (1 object / 3 person)
    const char* curSelName = nullptr;
    std::string selTypeName;                 // selected building gb_<type> name
    CityView3D::Result lastVr{};             // last 3D frame result (debug trace)
    i32 markerToken = -1;                    // HAUSPFEIL marker instance token
    int markerSelId = 0;                     // selection the marker is seated on

    // --- day-cycle / weather driver (UpdateBrightness @0x4b2504 chain) --------
    SessionAtmos atmos;
    tr.atmosActive = cfg.atmosphere;
    int atmosCursor = 0;                     // ApplyAtmosLightingFrame rebuild cursor

    // --- CONTINUOUS GAME CLOCK (cfg.continuousClock): play::SessionTick -------
    SessionTick tick;
    if (cfg.continuousClock) {
        tick.SetGameSpeedLevel(cfg.gameSpeedLevel); // dword_1233558 = 40*lvl
        tick.SyncClocksToDayStart();         // the turn-start sysmsg-3 (06:00)
        tick.BeginDay();                     // clear latches, unpause the clock
        tr.clockActive = true;
    }
    std::uint32_t prevTickMs = plat.timeMs();

    // --- LIVING CITY day start (wave 4): the turn-start daily-routine pass —
    //     BeginPlayerRound @0x533188 steps the He handlers; the per-NPC
    //     director among them (NpcDaily_DailyRoutineStep @0x4e7e88) dispatches
    //     each live person's morning destination; dispatched persons get their
    //     movement destination bound (SessionNpcDailyAssign). Runs AFTER the
    //     clock sync so the director reads the real 06:00 world clock. --------
    auto runDailyAssign = [&]() {
        if (!npcMoveActive)
            return;
        SessionNpcDailyResult dr = SessionNpcDailyAssign(view, &terrainHm);
        tr.dailyDispatched += dr.dispatched;
        tr.dailyAssigned   += dr.assigned;
    };
    runDailyAssign();

    // --- real session audio (music + ambience + 3D tick @0x4c09a0) ------------
    SessionAudio sa;
    if (cfg.audioDev) {
        SettingsBundle settings;
        LoadSettings(fs, cfg.iniName, settings);
        SessionAudioInit ai;
        ai.gameDir = cfg.gameDir;
        tr.audioActive = sa.Init(cfg.audioDev, &fs, settings.sound, ai);
    }

    // --- the render / present / input loop ------------------------------------
    bool prevLeft = false;          // left-button edge detector
    bool prevRight = false;         // right-button edge detector (deselect)
    bool prevSpace = false;         // SPACE edge detector
    bool prevF5 = false, prevF9 = false; // quicksave/quickload edges
    std::uint32_t prevMs = plat.timeMs();
    shim::MouseState lastMs{};      // previous frame's cursor (panel hover anchor)
    std::vector<ProjectedObject> proj;   // this frame's projected object seats
    int frame = 0;
    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames)
            break;

        // (0) day-cycle / weather params for this frame (the original's
        //     Weather_UpdateSky + DayCycle_UpdateBrightness frame steps), then
        //     the lighting application (BlendBandLighting ambient store +
        //     Light_RefreshAllObjects @0x5c886c via ApplyAtmosLightingFrame).
        if (cfg.atmosphere) {
            atmos.Frame(sim::g_sysGameTime, cfg.seed, plat.timeMs());
            tr.lastBrightness = atmos.brightness;
            AtmosLightingApplyResult ar =
                ApplyAtmosLightingFrame(atmos, (int)plat.timeMs(), atmosCursor);
            tr.atmosLightRebuilds = atmosCursor;
            (void)ar;
        }

        // (0b) the CONTINUOUS game clock: TimeBase fptc ticks -> clock proc ->
        //      opcode-30 world commits; the day-end gate runs the SAME day
        //      transition SPACE triggers (play::RunGameDay), then rolls over.
        if (cfg.continuousClock) {
            std::uint32_t tnow = plat.timeMs();
            std::uint32_t telapsed = tnow - prevTickMs;
            prevTickMs = tnow;
            SessionTick::FrameResult fr = tick.OnFrame(telapsed);
            tr.clockFires += fr.clockFires;
            tr.timeSyncCommits += fr.timeSyncCommits;
            // LIVING CITY (wave 4): step the real path-follow once per
            // opcode-30 WORLD-clock commit — the committed tick that runs the
            // per-tick world cascade (ExAdvanceGameTick @0x498954). The
            // engine's own continuous walk integration (WalkUpdate @0x40a0b8,
            // scaled by the master-tick delta dword_62D008-dword_62D004 per
            // frame) is sub-tile; the bridge is tile-grain, so the tile
            // advance rides the world-clock commit (named cadence gap: the
            // exact world-units-per-tick walk speed needs the live walk-anim
            // +92/+96 runtime — see progress/living-city-wave4.md).
            if (npcMoveActive && fr.timeSyncCommits > 0) {
                for (int s = 0; s < fr.timeSyncCommits; ++s)
                    StepNpcMovement();
                tr.moveSteps += fr.timeSyncCommits;
            }
            if (fr.dayEnded) {
                tick.FastForwardToDayEnd();  // +30 min commits up to 23:00
                GameDayState st = SeedGameDay(cfg.seed + (std::uint32_t)tr.daysAdvanced);
                RunGameDay(cfg.seed + (std::uint32_t)tr.daysAdvanced, st);
                ++tr.daysAdvanced;
                tick.RollToNextDay();        // +24h, 06:00:00, clock resumes
                tick.SyncClocksToDayStart();
                tick.BeginDay();
                runDailyAssign();            // next morning's destinations
            }
        }

        // (1) render the city into the device framebuffer.
        if (cfg.atmosphere)
            render::LightAtmosBeginUniverseFrame();  // 0x5b3982 budget reset

        // Shared HUD input block (money / clock / selection / panels).
        SessionHud::Inputs hi{};
        // Banner display name (title prepends inside SessionHud).
        std::string bannerName;
        if (!cfg.newGame.firstName.empty()) {
            bannerName = cfg.newGame.firstName;
            if (!cfg.newGame.familyName.empty())
                bannerName += " " + cfg.newGame.familyName;
        }
        hi.playerName = bannerName.empty() ? nullptr : bannerName.c_str();
        play::SessionPanelsInputs pin;
        // Per-frame record-content feeds for the panel layer (wave-3): the live
        // record fields the reconstructed builders dereference, filled from the
        // hovered/selected entity's REAL world records (pin points at these).
        play::PanelBuildingHover bldHover;     // hovered-building content feed
        play::PanelPersonHover   perHover;     // hovered-person content feed
        gui::InfoBuildingRecord  selBld;       // selected-building panel record
        gui::InfoPersonRecord    selPer;       // selected-person panel record
        char selBldName[33] = {0};             // record +5 custom name (NUL-forced)

        // BUILDING HOVER FEED — the hovered city entity is a live record in the
        // shared object/building array (sim::g_objects == dword_13CE298, the
        // table the original widget's +736 sceneRef points into, so the REAL
        // classification (Tooltip_ClassifySubject @0x4f7424) yields kBuilding
        // with code = record byte +0 (capped at 72 inside the core)). The
        // VIBE_Tooltip_BuildBuilding @0x4f78e4 record fields come from the
        // loaded A_Geb catalog (g_buildingTypes == dword_13CE294 + 589*code):
        // colour selector @+583, extra field @+579; salePrice through the REAL
        // VIBE_Building_ComputeSalePrice @0x591480 with the building's
        // sale-record quality byte (+358 of the buildings-as-persons row found
        // by Person_FindRecordById @0x58bc6c over the same entity id).
        auto feedBuildingHover = [&](int slot) -> bool {
            if (slot < 0 || slot >= sim::kObjectCapacity)
                return false;
            const u8* rec = reinterpret_cast<const u8*>(&sim::g_objects[slot]);
            if (!rec[0])
                return false;
            pin.hoverKind = gui::TooltipKind::kBuilding;
            pin.hoverBuildingCode = rec[0];        // record +0 type byte
            bldHover = play::PanelBuildingHover{};
            if (sim::g_buildingTypesLoaded && rec[0] < world::kBuildingTypeLoadCount) {
                const u8* td =
                    reinterpret_cast<const u8*>(&sim::g_buildingTypes[rec[0]]);
                bldHover.colorSelector = td[583];             // record[+583]
                std::memcpy(&bldHover.extraField, td + 579, 4); // *(record+579)
                u8 quality = 0;                    // sale-record byte +358
                i32 oid = 0;
                std::memcpy(&oid, rec + 1, 4);
                if (sim::Person* sr = sim::PersonFindRecordById(oid))
                    quality = reinterpret_cast<const u8*>(sr)[358];
                bldHover.salePrice =
                    sim::Building_ComputeSalePrice(rec[0], quality);
            }
            pin.building = &bldHover;
            return true;
        };

        // PERSON HOVER FEED — the hovered person's live 536-byte record
        // (sim::g_persons == word_12CE910): the exact fields VIBE_Tooltip_-
        // BuildPerson @0x4f84ac dereferences (+0 id word, +9 female, +12 class,
        // +13 religion, +356 job, +358/+361 traits), cash through the REAL
        // Person_GetCashAmount @0x58bc9c, spouse/children name codes through
        // Person_FindRecordById @0x58bc6c over the live array. NOT fed (named
        // gaps — env edges whose original arguments ride unreconstructed
        // clusters): rank (@0x58a560 arg unrecovered), wealth (@0x591f7c needs
        // the owned-building worth join), statusCard (@0x553ce8), betrothed
        // (the He_FindFirstHandlerByFilter @0x4c63f8 walk).
        auto feedPersonHover = [&](int slot) -> bool {
            if (slot < 0 || slot >= sim::kPersonCapacity)
                return false;
            const u8* rec = reinterpret_cast<const u8*>(&sim::g_persons[slot]);
            pin.hoverKind = gui::TooltipKind::kPerson;
            u16 idw = 0;
            std::memcpy(&idw, rec, 2);
            pin.hoverPersonId = idw;
            perHover = play::PanelPersonHover{};
            gui::TooltipPersonView& v = perHover.view;
            v.id        = idw;        // +0
            v.female    = rec[9];     // +9
            v.classCode = rec[12];    // +12
            v.religion  = rec[13];    // +13
            v.jobCode   = rec[356];   // +356
            v.trait1    = rec[358];   // +358
            v.trait2    = rec[361];   // +361
            gui::TooltipPersonEnv& e = perHover.env;
            e.cash = (i32)sim::PersonGetCashAmount((u16)slot);  // @0x58bc9c
            i32 spouseId = 0;                    // +92 spouse person id
            std::memcpy(&spouseId, rec + 92, 4);
            if (spouseId > 0) {
                if (sim::Person* sp = sim::PersonFindRecordById(spouseId)) {
                    if (reinterpret_cast<const u8*>(sp)[8]) {  // byte +8 set
                        u16 nm = 0;
                        std::memcpy(&nm, sp, 2);
                        e.spouseNameId = nm;
                    }
                }
            }
            for (int k = 0; k < 5; ++k) {        // +104..+120 children ids
                i32 cid = 0;
                std::memcpy(&cid, rec + 104 + 4 * k, 4);
                if (cid > 0) {
                    if (sim::Person* cp = sim::PersonFindRecordById(cid)) {
                        u16 nm = 0;
                        std::memcpy(&nm, cp, 2);
                        e.childNameIds[k] = nm;
                    }
                }
            }
            pin.person = &perHover;
            return true;
        };

        // SELECTION FEED — the selected entity's record for the REAL info-panel
        // update (VIBE_InfoPanel_Update @0x4b84c0 -> the @0x4b64b0/0x4b7104
        // builders). A selected city entity is a live g_objects record (the
        // dword_11BC278 sceneObject handle -> the kBuilding builder); a person
        // selection (legacy person pick) is a live g_persons record. NOT fed
        // (named gaps): selSelectionFlags (ComputeSelectionFlags @0x588dec is
        // unreconstructed), selCategory (the @0x4b84c0 category-arg source is
        // unrecovered), satisfaction/outputRatioPct (relation-matrix / output
        // leaves' argument joins).
        auto feedSelection = [&](int selId, int selKind) {
            if (selId == 0)
                return;
            if (selKind == 3) {                  // person selection
                if (sim::Person* pr = sim::PersonFindRecordById(selId)) {
                    pin.selection.person = selId;        // dword_11BC270
                    const u8* rec = reinterpret_cast<const u8*>(pr);
                    selPer = gui::InfoPersonRecord{};
                    u16 nm = 0;
                    std::memcpy(&nm, rec, 2);
                    selPer.nameCode  = nm;               // *(u16*)record
                    selPer.classByte = rec[2];           // record[2]
                    selPer.job1      = rec[357];         // record[357]
                    std::memcpy(&selPer.portraitObj, rec + 396, 4); // +396
                    pin.selPerson = &selPer;
                }
                return;
            }
            // The selected city entity is a live BUILDING record (g_objects ==
            // dword_13CE298), so it rides the building handle dword_631748 —
            // the handle the @0x4b84c0 builder selection requires for the
            // kBuilding branch (sceneObject/dword_11BC278 alone selects the
            // standard panel; wave-2 fed only that, hence the standard panel).
            pin.selection.building = selId;      // dword_631748 handle
            if (sim::ObjectRec* orc = sim::BuildingFindById(selId)) {
                const u8* rec = reinterpret_cast<const u8*>(orc);
                selBld = gui::InfoBuildingRecord{};
                selBld.code = rec[0];                    // *record low byte
                std::memcpy(selBldName, rec + 5, 32);    // record+5 name[32]
                selBldName[32] = '\0';
                selBld.customName = selBldName;
                u16 item = 0;
                std::memcpy(&item, rec + 39, 2);
                selBld.item = item;                      // *(u16*)(record+39)
                i32 packed = 0;
                std::memcpy(&packed, rec + 89, 4);
                selBld.upgradeLevel =                    // @0x58fc84
                    sim::Building_GetUpgradeLevel(packed);
                selBld.noSlider = (rec[90] & 1) != 0;    // record[90]&1
                pin.selBuilding = &selBld;
            }
        };

        if (tr.hudActive) {
            // Player money: the new-game commit's kind-6 player purse (+0x0A).
            if (tr.playerId != 0) {
                if (sim::Person* pl = sim::PersonFindRecordById(tr.playerId))
                    hi.money = pl->cash;
            }
            if (cfg.continuousClock) {
                // The HUD date/time reads the WORLD clock qword_13CE852
                // (tick.worldTime()) — the corrected provenance note on
                // gui::Clock_ComputeTimeOfDay (src/gui/hud.h). The caption's
                // hh:mm:ss splitter consumes a tick accumulator, so feed the
                // EXACT inverse of its (tick*0.00625 + 0.5) * dayLength map.
                const sim::GameTime& wt = tick.worldTime();
                const double totalSec =
                    (double)wt.hour * 3600.0 + (double)wt.minute * 60.0 +
                    (double)wt.second;
                hi.clock = &tick.worldTime();
                hi.clockTick = (int)std::lround(
                    (totalSec / (double)gui::g_dayLengthSeconds -
                     (double)gui::kClockTickBias) / (double)gui::kClockTickScale);
            } else {
                hi.clock = &sim::g_sysGameTime;
                hi.clockTick = frame;
            }
            if (use3d) {
                hi.selectedId = curSelId;
                hi.selectedName = curSelName;
            } else {
                SessionSelect::Info si = sel.current();
                hi.selectedId = si.has ? si.id : 0;
                hi.selectedName = si.has ? si.name : nullptr;
                curSelId = si.has ? si.id : 0;       // panel selection feed
                curSelKind = si.has ? si.kind : 0;
            }
            // SELECTION MARKER + floating label (the original's golden
            // HAUSPFEIL arrow over the selected building + its name under it).
            // The marker is the real _DYNAMIC/X_STUFF/HAUSPFEIL.bgf drawn as a
            // per-frame extra instance; the label rides the HUD (small gold
            // face). Reseated only when the selection changes.
            if (use3d && hi.selectedId != markerSelId) {
                if (markerToken >= 0) {
                    view.RemoveAnimalInstance(markerToken);
                    markerToken = -1;
                }
                markerSelId = hi.selectedId;
                if (markerSelId != 0) {
                    for (const auto& b : view.boundObjects()) {
                        if (b.id != markerSelId) continue;
                        CityPlacement mp = b.place;
                        mp.pos[1] += 430.0f;   // hover above the roofline
                        markerToken = view.AddAnimalInstance("HAUSPFEIL", mp,
                                                             /*fullbright=*/true);
                        if (std::getenv("GUILD_SELECT_FIRST"))
                            std::printf("  marker: sel=%d token=%d pos(%.0f,%.0f,%.0f) bldRec=%d\n",
                                        markerSelId, markerToken,
                                        mp.pos[0], mp.pos[1], mp.pos[2],
                                        (int)(sim::BuildingFindById(markerSelId) != nullptr));
                        break;
                    }
                }
            }
            if (use3d && markerSelId != 0 && hi.selectedName) {
                // Project the selected building for the label anchor (the same
                // view transform the pick boxes use).
                for (const auto& b : view.boundObjects()) {
                    if (b.id != markerSelId) continue;
                    CameraPose p2 = cam.pose();
                    const float negr2[3] = {-p2.rotX, -p2.rotY, -p2.rotZ};
                    const render::Mat3 RM2 = render::MatrixFromEuler(negr2);
                    const float eye2[3] = {p2.eyeX, p2.eyeY, p2.eyeZ};
                    float vv[3];
                    render::WorldToView(RM2, eye2, b.place.pos, vv);
                    if (vv[2] > 1.0f) {
                        const int vpW = opt3.viewportW > 0 ? opt3.viewportW
                                                           : opt3.fbW;
                        const float sc2 = (opt3.viewScale > 0.0f)
                                              ? opt3.viewScale
                                              : 0.5f * (float)vpW;
                        hi.labelText = hi.selectedName;
                        hi.labelX = (int)(vv[0] * sc2 / vv[2] + 0.5f * (float)vpW);
                        hi.labelY = (int)(-vv[1] * sc2 / vv[2] + 0.5f * (float)opt3.fbH) + 26;
                    }
                    break;
                }
            }
            // TOOLTIPS + INFO PANEL: the per-frame SessionPanelsInputs (the
            // dword_75BF3C hover model; selection -> InfoPanel_Update @0x4b84c0).
            // Wave-3: REAL record-content feeds — the hovered/selected entity's
            // live world records ride the inputs, so the reconstructed builders
            // (@0x4f78e4/0x4f84ac/0x4b64b0/0x4b7104) render real content.
            if (cfg.uiPanels) {
                pin.scenePickActive = 1;
                pin.cursorX = lastMs.x;
                pin.cursorY = lastMs.y;
                pin.hoveredTooltipId = hoverId;
                if (hoverId >= 0 && use3d) {     // 3D: latched bound-object slot
                    if (feedBuildingHover(hoverSlot))
                        ++tr.hoverFeedFrames;
                }
                feedSelection(curSelId, curSelKind);
                pin.textDb = hud.textDb();   // real localized panel strings
                // Selected building's TYPE NAME (from the owner-matched node's
                // gb_<type> member) -> the keyed localized name resolve.
                selTypeName.clear();
                if (use3d && curSelId != 0 && curSelKind == 1) {
                    for (const auto& b : view.boundObjects()) {
                        if (b.id != curSelId) continue;
                        const std::size_t g = b.member.rfind("gb_");
                        if (g != std::string::npos) {
                            for (std::size_t k = g + 3; k < b.member.size(); ++k) {
                                const char c = b.member[k];
                                if (c == '.' || c == '/' || c == '\\') break;
                                selTypeName += (char)std::toupper((unsigned char)c);
                            }
                        }
                        break;
                    }
                }
                pin.selBuildingTypeName =
                    selTypeName.empty() ? nullptr : selTypeName.c_str();
                hi.panels = &pin;
            }
        }

        if (use3d) {
            // (1-3d) the REAL 3D city: the SessionCamera pose IS the engine
            // camera node state (+76 eye / +132 euler) CityView3D consumes.
            CameraPose p = cam.pose();
            CityCamera3D c3;
            c3.eye[0] = p.eyeX; c3.eye[1] = p.eyeY; c3.eye[2] = p.eyeZ;
            c3.rot[0] = p.rotX; c3.rot[1] = p.rotY; c3.rot[2] = p.rotZ;
            // LIVING CITY (wave 4): moved persons' draw positions follow their
            // live movement tile (TileToWorld @0x5c65d4 over the real ground);
            // cheap when nothing moved (roster scan only, no rebind).
            if (npcMoveActive)
                tr.movePosUpdates +=
                    UpdateSessionPersons3DPositions(view, persons3dOpt);
            // WAVE-8 W8-NPCCLIP — per-person GAIT selection. For each bound person,
            // its live movement state (wire_npc_movement +0x74 == moving) selects the
            // factory-preloaded clip (sim::SelectPersonClipFromMovement: moving ->
            // gait "bewegung/gehen", still -> idle "stehen/stehen_newnoise"), flipped
            // onto the person's pose via CityView3D::SetBoundPersonClip BEFORE the pose
            // advance/render. A walking NPC now plays the walk cycle, a standing one
            // the idle — closing the wave-3/4 clip-selection gap.
            if (cfg.persons) {
                for (const auto& bp : view.boundPersons()) {
                    const NpcMovePos mp = GetEntityMovePos(bp.id);
                    sim::ClipSelection sel =
                        sim::SelectPersonClipFromMovement(mp.active, /*sit=*/false);
                    if (sel.clip && *sel.clip &&
                        std::strcmp(view.boundPersonClip(bp.id), sel.clip) != 0 &&
                        view.SetBoundPersonClip(bp.id, sel.clip))
                        ++tr.view3dGaitFlips;   // only count a genuine clip change
                }
            }
            // Person pose advance through the REAL driver (@0x5cd1d8), before
            // the frame renders them (safe no-op when nothing is bound).
            if (cfg.persons)
                UpdateSessionPersons3D(view, persons3dOpt.animStepPerFrame);
            // WAVE-6 W6-INTEGRATE — feed the live world clock to the sun/sky/
            // light/shadow/weather passes (render::ComputeSunState reads day/hour/
            // minute). The continuous clock owns the real time-of-day; otherwise
            // the system game time.
            {
                const sim::GameTime& wt = cfg.continuousClock
                                              ? tick.worldTime()
                                              : sim::g_sysGameTime;
                opt3.worldDay    = (i32)wt.day;
                // Debug/verification: pin the world day (season = day % 4).
                static const char* dayEnv = std::getenv("GUILD_DAY");
                if (dayEnv)
                    opt3.worldDay = std::atoi(dayEnv);
                opt3.worldHour   = (int)wt.hour;
                opt3.worldMinute = (int)wt.minute;
                // Debug/verification: pin the time-of-day (GUILD_HOUR=h[:m]) so
                // day/night frames are directly diffable against the original.
                static const char* fh = std::getenv("GUILD_HOUR");
                if (fh) {
                    int hh = 12, mm = 0;
                    if (std::sscanf(fh, "%d:%d", &hh, &mm) >= 1) {
                        opt3.worldHour = hh;
                        opt3.worldMinute = mm;
                    }
                }
            }
            CityView3D::Result vr = view.RenderFrame(c3, opt3);
            lastVr = vr;                       // debug trace (GUILD_SELECT_FIRST)
            tr.view3dInstances = vr.instancesDrawn;
            tr.view3dNonClear  = vr.nonClearPixels;
            tr.personsRendered = vr.personInstances;
            tr.view3dSunLitVerts = vr.sunLitVerts;     // (W8) per-vertex sun shades
            tr.view3dSceneLights = vr.sceneLightCount; // (W8) scene lights collected
            // WAVE-9 W9-FRAME-ENRICH — the per-frame veg relight / reflective scan /
            // animal draw read-back. Veg + reflective are per-frame recomputes;
            // animalsDrawn tracks the peak ambient animals seated + drawn this session.
            tr.view3dVegRelit = vr.vegRelitMeshes;
            tr.view3dReflectiveMeshes = vr.reflectiveMeshes;
            if (vr.animalInstances > tr.view3dAnimalsDrawn)
                tr.view3dAnimalsDrawn = vr.animalInstances;

            // WAVE-8 W8-MAP — the 2D overview map panel. 'M' toggles
            // play::MapView_RenderOverview over the rendered 3D frame (the engine's
            // VIBE_MapView_PanelDispatcher @0x5441d0: a full-screen map opened over the
            // session). Markers come from the live world the session already carries:
            // every bound BUILDING object at its world (x,z), plus the camera/player
            // marker. Composited into the SAME framebuffer before the HUD/present.
            {
                const bool mk = plat.keyDown(kVkM);
                if (mk && !mapKeyPrev)
                    mapViewOpen = !mapViewOpen;     // toggle on the key edge
                mapKeyPrev = mk;
            }
            if (mapViewOpen) {
                if (render::Surface* s = view.surface()) {
                    std::vector<play::OverviewMarker> markers;
                    for (const auto& b : view.boundObjects()) {
                        if (b.id == 0) continue;
                        play::OverviewMarker m;
                        m.worldX = b.place.pos[0];   // world X
                        m.worldZ = b.place.pos[2];   // world Z
                        m.kind   = play::MarkerKind::Building;
                        m.entity = b.id;
                        markers.push_back(m);
                    }
                    // the camera/player marker (the focused object @0x544cc7).
                    play::OverviewMarker pm;
                    pm.worldX = c3.eye[0]; pm.worldZ = c3.eye[2];
                    pm.kind = play::MarkerKind::Player; pm.entity = tr.playerId;
                    markers.push_back(pm);
                    play::OverviewCamera mcam{};     // centred (no pan/scroll)
                    play::OverviewRenderResult mr = play::MapView_RenderOverview(
                        *s, std::move(markers), mcam, /*viewX=*/0, /*viewY=*/0,
                        /*viewW=*/s->width, /*viewH=*/s->height);
                    tr.view3dMapMarkers = mr.markersDrawn;
                    tr.view3dMapOpened = true;
                }
            }
            // (1b) HUD overlay onto the rendered 16bpp city surface, BEFORE the
            // present blit (the DrawUniverseAndStats "world, then stats" model).
            if (tr.hudActive) {
                if (render::Surface* s = view.surface()) {
                    hud.Render(s->pixels, s->width, s->height, s->pitch, hi);
                    tr.hudCaptionGlyphs = hud.lastResult().captionGlyphs;
                    if (hud.lastResult().tooltipVisible) ++tr.tooltipFrames;
                    tr.panelVisible = hud.lastResult().panelVisible;
                    tr.tooltipTextOps = hud.lastResult().tooltipTextOps;
                    tr.panelTextOps = hud.lastResult().panelTextOps;
                }
            }
            // (2) present — the blit + device.present() (Vulkan/Memory device).
            view.PresentToDevice(device);
            if (device.backbuffer() != nullptr)
                ++tr.framesPresented;
        } else {
            RealCityRenderer::Result rr = rc.Render(opt, device);
            tr.personsRendered = rr.personMeshes;
            // (1b) HUD overlay onto the rendered 16bpp frame, before present.
            if (tr.hudActive) {
                if (shim::Surface* s = device.backbuffer()) {
                    // Clickless hover pick at the cursor for the tooltip layer
                    // (the same hit-test boundary SessionSelect::OnPick uses);
                    // person hover through the SAME nearest-wins arbitration the
                    // click path runs (rc.PickPerson over the rendered roster).
                    if (cfg.uiPanels) {
                        ScenePickResult hp =
                            rc.Pick(opt, (float)lastMs.x, (float)lastMs.y, 24.0f);
                        ScenePickResult hpp;
                        if (cfg.persons)
                            hpp = rc.PickPerson(opt, (float)lastMs.x,
                                                (float)lastMs.y, 24.0f);
                        const bool personHover =
                            hpp.index >= 0 && hpp.id != 0 &&
                            (hp.index < 0 || hpp.screenDist < hp.screenDist);
                        hoverSlot = -1;
                        hoverPersonSlot = -1;
                        if (personHover) {
                            hoverId = hpp.id;
                            const auto& roster = rc.personRoster();
                            if (hpp.index < (int)roster.size())
                                hoverPersonSlot = roster[(size_t)hpp.index].slot;
                        } else if (hp.index >= 0 && hp.id != 0) {
                            hoverId = hp.id;
                            hoverSlot = hp.index;   // g_objects slot (rc.Pick)
                        } else {
                            hoverId = -1;
                        }
                        pin.hoveredTooltipId = hoverId;
                        pin.hoverKind = gui::TooltipKind::kNone;
                        pin.building = nullptr;
                        pin.person = nullptr;
                        if (hoverId >= 0) {
                            if (hoverPersonSlot >= 0) {
                                if (feedPersonHover(hoverPersonSlot)) {
                                    ++tr.hoverFeedFrames;
                                    ++tr.hoverPersonFrames;
                                }
                            } else if (feedBuildingHover(hoverSlot)) {
                                ++tr.hoverFeedFrames;
                            }
                        }
                    }
                    hud.Render(s->pixels, s->width, s->height, s->pitch, hi);
                    tr.hudCaptionGlyphs = hud.lastResult().captionGlyphs;
                    if (hud.lastResult().tooltipVisible) ++tr.tooltipFrames;
                    tr.panelVisible = hud.lastResult().panelVisible;
                    tr.tooltipTextOps = hud.lastResult().tooltipTextOps;
                    tr.panelTextOps = hud.lastResult().panelTextOps;
                }
            }
            // (2) present — count a success when the device has a live framebuffer.
            device.present();
            if (device.backbuffer() != nullptr)
                ++tr.framesPresented;
        }
        if (cfg.atmosphere)
            render::LightAtmosEndUniverseFrame();    // 0x5b3c19 frame-end clear

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

        if (use3d) {
            // (4b-3d) the REAL input/selection chain (SessionInput): register
            // this frame's projected object screen boxes (the renderer-owned
            // dword_69FFB4 record data), then run the reconstructed frame:
            // LatchMouseState @0x40dab8 -> MainLoop scan @0x414a38 ->
            // Selection_CommitContact @0x4b950c. Result.pickedId is the
            // hover/click pick; orders issue on the click edge.
            input.ClearEntities();
            proj.clear();
            CameraPose p = cam.pose();
            const float negr[3] = {-p.rotX, -p.rotY, -p.rotZ};
            const render::Mat3 RM = render::MatrixFromEuler(negr);
            const float eye3[3] = {p.eyeX, p.eyeY, p.eyeZ};
            const int pickVpW = opt3.viewportW > 0 ? opt3.viewportW : opt3.fbW;
            const float scale = (opt3.viewScale > 0.0f)
                                    ? opt3.viewScale
                                    : 0.5f * (float)pickVpW;  // flt_13FCD0C
            for (const auto& b : view.boundObjects()) {
                if (b.id == 0 || b.slot < 0 || b.slot >= sim::kObjectCapacity)
                    continue;
                float v[3];
                render::WorldToView(RM, eye3, b.place.pos, v);
                if (v[2] <= 1.0f)
                    continue;                       // behind the near plane
                const float sx = v[0] * scale / v[2] + 0.5f * (float)pickVpW;
                const float sy = -v[1] * scale / v[2] + 0.5f * (float)opt3.fbH;
                float r = 12.0f * scale / v[2];     // ~12 world units half-extent
                if (r < 6.0f) r = 6.0f;
                if (r > 48.0f) r = 48.0f;
                if (sx + r < 0.0f || sy + r < 0.0f ||
                    sx - r >= (float)cfg.fbW || sy - r >= (float)cfg.fbH)
                    continue;
                SessionInputEntity e;
                e.id = b.id;
                e.left = (i32)(sx - r);
                e.top = (i32)(sy - r);
                e.width = (i32)(2.0f * r);
                e.height = (i32)(2.0f * r);
                e.innerLeft = e.left;
                e.innerRight = (i32)(sx + r);
                e.yMin = e.top;
                e.yMax = (i32)(sy + r);
                e.typeByte = 0;                     // city-entity profile
                e.kind = 1;                         // object
                e.typeCode =
                    reinterpret_cast<const u8*>(&sim::g_objects[b.slot])[0];
                e.name = b.member.c_str();
                input.UpdateEntity(e);
                proj.push_back(ProjectedObject{b.id, sx, sy, r});
            }
            SessionInputFrame fin;
            fin.mouseX = ms.x;
            fin.mouseY = ms.y;
            fin.left = ms.left;
            fin.right = ms.right;
            fin.middle = ms.middle;
            fin.clock = plat.timeMs() / 16u;        // dword_62EB44 master tick
            lastIn = input.Frame(fin);
            hoverId = lastIn.pickedId;
            // Resolve the picked entity id back to its g_objects slot through
            // the bound-object table (the same id->record boundary the order
            // path uses) for next frame's building-hover record feed.
            hoverSlot = -1;
            if (hoverId > 0) {
                for (const auto& b : view.boundObjects()) {
                    if (b.id == hoverId) { hoverSlot = b.slot; break; }
                }
            }
            curSelId = lastIn.selected ? lastIn.selectedId : 0;
            curSelKind = lastIn.selected ? lastIn.selectedKind : 0;
            curSelName = lastIn.selected ? input.selectedName() : nullptr;
            // Debug/verification: force-select the first bound building so the
            // HAUSPFEIL marker + label render in a scripted dump.
            static const char* selDbg = std::getenv("GUILD_SELECT_FIRST");
            if (selDbg && curSelId == 0) {
                // Pick the bound gb_ building nearest the camera's look point.
                CameraPose sp2 = cam.pose();
                const float lx = sp2.eyeX, lz = sp2.eyeZ + 1200.0f;
                float best = 1e18f;
                for (const auto& b : view.boundObjects()) {
                    if (b.id == 0 || b.member.find("gb_") == std::string::npos)
                        continue;
                    const float dx = b.place.pos[0] - lx, dz = b.place.pos[2] - lz;
                    const float d2 = dx * dx + dz * dz;
                    if (d2 < best) {
                        best = d2;
                        curSelId = b.id;
                        curSelKind = 1;
                        curSelName = "\xD3\xEA\xF0\xFB\xF2\xE8\xE5 \xEA\xEE\xED\xF2\xF0"
                                     "\xEE\xE1\xE0\xED\xE4\xE8\xF1\xF2\xE0";  // CP1251 sample
                    }
                }
            }
            if (lastIn.leftClickEdge) {
                ++tr.clicksHandled;
                if (lastIn.pickedId > 0) {
                    ++tr.picksHit;
                    tr.lastPickedId = lastIn.pickedId;
                    float osx = (float)ms.x, osy = (float)ms.y;
                    for (const ProjectedObject& po : proj)
                        if (po.id == lastIn.pickedId) { osx = po.sx; osy = po.sy; break; }
                    WorldOrder ord = IssuePickedOrder(q, cfg.fbW, cfg.fbH,
                                                      lastIn.pickedId, osx, osy,
                                                      24.0f, clickMode);
                    if (ord.issued)
                        ++tr.ordersIssued;
                }
            }
        } else {
            // (4b) left-click EDGE -> pick -> select (the real selection commit) ->
            //      issue the order on the hit object.
            bool leftNow = ms.left;
            if (leftNow && !prevLeft) {
                ++tr.clicksHandled;
                ScenePickResult pick = rc.Pick(opt, (float)ms.x, (float)ms.y, /*pickRadius=*/24.0f);
                // Person pick: nearest rendered person within the radius (the person
                // pass projects the roster each frame).
                ScenePickResult ppick;
                if (cfg.persons)
                    ppick = rc.PickPerson(opt, (float)ms.x, (float)ms.y, 24.0f);
                if (cfg.selectFeedback) {
                    // Route the click through the real selection path: a hit fills
                    // the hover latch for Selection_CommitContact @0x4b950c; a miss
                    // takes the real empty-click branch -> VIBE_Selection_Reset.
                    const bool personHit = ppick.index >= 0 && ppick.id != 0 &&
                                           (pick.index < 0 || ppick.screenDist < pick.screenDist);
                    const ScenePickResult& chosen = personHit ? ppick : pick;
                    SessionSelectEntry e{};
                    e.id = chosen.id;
                    e.kind = personHit ? 3 : 1;
                    if (!personHit && chosen.index >= 0 &&
                        chosen.index < sim::kObjectCapacity) {
                        const u8* r = reinterpret_cast<const u8*>(&sim::g_objects[chosen.index]);
                        e.typeCode = r[0];
                    }
                    e.screenX = (float)ms.x;
                    e.screenY = (float)ms.y;
                    sel.OnPick(chosen, &e, 1, (float)ms.x, (float)ms.y, 24.0f);
                }
                if (pick.index >= 0 && pick.id != 0) {
                    ++tr.picksHit;
                    tr.lastPickedId = pick.id;
                    WorldOrder ord = IssuePickedOrder(q, opt.fbW, opt.fbH, pick.id,
                                                      (float)ms.x, (float)ms.y,
                                                      24.0f, clickMode);
                    if (ord.issued)
                        ++tr.ordersIssued;
                }
            }
            prevLeft = leftNow;

            // (4b2) right-click EDGE -> the real deselect (Selection_Reset @0x4b9444).
            bool rightNow = ms.right;
            if (rightNow && !prevRight && cfg.selectFeedback)
                sel.Clear();
            prevRight = rightNow;
        }

        // (4c) SPACE EDGE -> advance one real game-day.
        bool spaceNow = plat.keyDown(kVkSpace);
        if (spaceNow && !prevSpace && cfg.advanceDayOnSpace) {
            GameDayState st = SeedGameDay(cfg.seed + (std::uint32_t)tr.daysAdvanced);
            RunGameDay(cfg.seed + (std::uint32_t)tr.daysAdvanced, st);
            ++tr.daysAdvanced;
            // LIVING CITY (wave 4): without the continuous clock the SPACE
            // day IS the tick — one path-follow step per day (the
            // wire_npc_movement e2e cadence) + the next morning's dispatch.
            if (npcMoveActive && !cfg.continuousClock) {
                StepNpcMovement();
                ++tr.moveSteps;
                runDailyAssign();
            }
        }
        prevSpace = spaceNow;

        // (4d) F5/F9 EDGE -> quicksave / quickload in the original's save format
        //      (SaveLiveWorld / LoadLiveWorld over Resources/gamedata/Saves).
        if (cfg.quickSaveKeys) {
            bool f5Now = plat.keyDown(kVkF5);
            if (f5Now && !prevF5 &&
                SaveLiveWorld(fs, QuickSavePath().c_str(), "QUICKSAVE", 0))
                ++tr.quickSaves;
            prevF5 = f5Now;
            bool f9Now = plat.keyDown(kVkF9);
            if (f9Now && !prevF9) {
                SessionLoadInfo li = LoadLiveWorld(fs, QuickSavePath().c_str(), cfg.seed);
                if (li.ok) {
                    NormalizePlantPointers(li.objectCount);
                    tr.liveObjects = CountLiveObjects();
                    ++tr.quickLoads;
                    if (use3d) {
                        tr.view3dBoundObjects = view.BindWorldObjects();
                        // Re-seat persons against the reloaded sim state
                        // (wave 4: refresh+capture the default seats first so
                        // new persons keep their genuine entrance-dummy seat,
                        // then rebind with the movement placement).
                        if (cfg.persons) {
                            if (npcMoveActive)
                                RebindSessionPersonsAfterSimChange(view,
                                                                   persons3dOpt);
                            else
                                RebindSessionPersons3D(view, persons3dOpt);
                        }
                    }
                    // LoadLiveWorld's blank-slate wipes the A_Geb/A_Obj
                    // catalogs; restore them for the panel/economy feeds.
                    if (tr.typeTablesLoaded)
                        loadTypeTables();
                }
            }
            prevF9 = f9Now;
        }

        // (4e) the REAL camera frame: arrows/edge pan + drag pan/rotate + zoom
        //      through VIBE_Camera_Update @0x4b4c68; eye/zoom feed next frame's
        //      render options (the link the old loop dropped on the floor).
        std::uint32_t nowMs = plat.timeMs();
        float dt = (float)(nowMs - prevMs);
        prevMs = nowMs;
        // Mouse-wheel notches (shim::MouseState::wheel, wave-3) feed the REAL
        // wheel-zoom branch: SessionCamera stages them into the dword_672254
        // accumulator consumed by VIBE_Camera_UpdateMovement @0x4b41a8 (the
        // zoom step + Camera_AnchorToTerrain @0x4b2900 eye/pitch recompute).
        if (!camLock)
            cam.Frame(ms, plat.keyDown(kVkLeft), plat.keyDown(kVkRight),
                      plat.keyDown(kVkUp), plat.keyDown(kVkDown),
                      /*wheelDelta=*/(float)ms.wheel, dt);
        tr.wheelNotches += ms.wheel;
        opt.eyeX = cam.eyeX();
        opt.eyeZ = cam.eyeZ();
        opt.pixelsPerUnit = basePixelsPerUnit * cam.pixelsPerUnit();

        // (4f) the per-frame audio tick (the 0x4c09a0 audio block) against the
        //      camera listener pose.
        if (tr.audioActive) {
            const float lpos[3] = {cam.eyeX(), 0.0f, cam.eyeZ()};
            const float lfwd[3] = {0.0f, 0.0f, 1.0f};
            const int season = (int)(sim::g_sysGameTime.day % 4);
            sa.Frame(lpos, lfwd, /*locationId=*/0, season, (std::uint32_t)frame);
        }

        // WAVE-8 W8-ANIMALS — the per-tick ambient-animal update (VIBE_Animal_Update
        // @0x48364c, driven once per frame as the engine's HUD/sim step does at
        // 0x4bc40c). Runs the real round-robin spawn-decision + lifetime/despawn core
        // with the current season (3 == winter, spawns off). With the inert default
        // IAnimalWorld no scene actor is created (the live-render boundary), so this
        // exercises the genuine decision/lifetime logic without inventing geometry.
        if (use3d && tr.view3dAnimalsActive) {
            // Advance the engine game-tick clock (dword_62EB38) the spawn throttle +
            // lifetime decay read (the original advances it in the sim step). Without
            // it the >=350-tick spawn gate never opens (the wave-8 animalsAtExit=0
            // root cause). One sim tick per frame mirrors the per-frame sim step.
            sim::g_gameTick += 50;
            const int season = (int)(sim::g_sysGameTime.day % 4);
            sim::Animal_Update(season);
            tr.view3dAnimalTicks++;
        }

        // (5) frame-cap.
        if (cfg.frameCapMs > 0)
            plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        lastMs = ms;
        ++frame;
    }
    if (tr.audioActive)
        sa.Shutdown();
    // WAVE-8 W8-ANIMALS — teardown on city exit (Animal_FreePool @0x4835ec +
    // Animal_ResetModelHandles @0x484424).
    if (tr.view3dAnimalsActive) {
        tr.view3dAnimalCount = sim::g_animalCount;
        sim::Animal_ResetModelHandles();
        sim::Animal_FreePool();          // calls DestroyAnimal -> RemoveAnimalInstance
        sim::SetAnimalWorld(nullptr);    // restore the inert default before the view drops
        view.ClearAnimalInstances();
    }
    // WAVE-8 W8-EMITTER — free the spawned particle systems (smoke) on exit.
    render::DestroyAllSystems();
    if (use3d) {
        tr.selectedId = lastIn.selected ? lastIn.selectedId : 0;
    } else {
        SessionSelect::Info si = sel.current();
        tr.selectedId = si.has ? si.id : 0;
    }
    if (cfg.continuousClock) {
        tr.worldDay = tick.worldTime().day;
        tr.worldHour = tick.worldTime().hour;
    }
    tr.zoomEnd = cam.zoom();        // flt_6316DC at exit (wheel-zoom witness)

    // --- LIVING CITY (wave 4): movement outcome + teardown --------------------
    if (npcMoveActive) {
        const NpcMovementTallies& mt = GetNpcMovementTallies();
        tr.personsMoved = mt.entitiesMoved;
        tr.moveArrivals = mt.arrivals;
        for (int i = 0; i < sim::kPersonCapacity; ++i) {
            if (sim::g_persons[i].marker == -1)
                continue;
            NpcMovePos mp = GetEntityMovePos(sim::g_persons[i].id);
            if (!mp.found)
                continue;
            if (mp.active)
                ++tr.personsMoving;
            // Witness the FIRST person with a live movement tile (moving or
            // arrived): tile + the TileToWorld draw seat over the real ground.
            const bool hasTile = mp.active ||
                ((mp.destX != 0 || mp.destZ != 0) &&
                 mp.curX == mp.destX && mp.curZ == mp.destZ);
            if (tr.moverId == 0 && hasTile) {
                float w[3];
                if (render::TileToWorld(&terrainHm, mp.curX, mp.curZ, w)) {
                    tr.moverId = sim::g_persons[i].id;
                    tr.moverTileX = mp.curX;
                    tr.moverTileZ = mp.curZ;
                    tr.moverWorldX = w[0];
                    tr.moverWorldY = w[1];
                    tr.moverWorldZ = w[2];
                }
            }
        }
        UninstallNpcMovement();
        ClearSessionPersonsMovePlacement();
    }
    if (tr.playerId != 0) {
        if (sim::Person* pl = sim::PersonFindRecordById(tr.playerId))
            tr.playerCashEnd = pl->cash;
    }

    // Final-frame dump for visual verification (cfg.dumpFramePath).
    if (!cfg.dumpFramePath.empty()) {
        tr.frameDumped = use3d
            ? DumpSurfacePpm(cfg.dumpFramePath.c_str(), view.surface())
            : DumpBackbufferPpm(cfg.dumpFramePath.c_str(), device.backbuffer());
    }

    // Release the bridge-owned converted HUD bank (process-global state).
    if (std::getenv("GUILD_SELECT_FIRST")) {
        const SessionPanelsResult& pr = hud.panels().lastResult();
        std::printf("  panel: form=%s builder=%d iconOps=%d iconBlits=%d text=%d "
                    "realArt=%d idBase=%d\n",
                    pr.panelForm, (int)pr.panelBuilder, pr.panelIconOps,
                    pr.panelIconBlits, pr.panelTextOps,
                    (int)tr.hudRealArt, HudSpriteIdBase());
        std::printf("  panel: hook=%p installed=%d\n",
                    (void*)GetHudRenderHooks().drawSprite,
                    (int)RealHudBridgeInstalled());
        std::printf("  view3d: band=%d blend=%.2f amb=%.0f,%.0f,%.0f\n",
                    lastVr.sunBand, lastVr.sunBlend, lastVr.ambient[0],
                    lastVr.ambient[1], lastVr.ambient[2]);

    }
    if (hudBank)
        SetHudSpriteBankFromGfx(nullptr, 0);

    tr.cleanQuit = tr.quitByWindow || tr.quitByEsc;

    // --- hashEnd: HashFullWorld at session end (re-anchor RNG for determinism) -
    crt::Srand(cfg.seed);
    tr.hashEnd = HashFullWorld();

    io::VfsShutdown();
    return tr;
}

} // namespace guild::play
