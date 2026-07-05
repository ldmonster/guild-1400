#include "test.h"

// =============================================================================
// PLAYABLE-FLOW e2e harness — drives the WHOLE native game flow headlessly with
// scripted input, proving the menu -> new game -> city session -> back-to-menu
// -> quit loop works end to end. Pure shim backends (MemoryGraphicsDevice +
// ScriptedPlatform), no SDL/Vulkan: the same code paths guild_run --play runs on
// the real window.
//
// Scripting model: every play-layer screen pumps IPlatform::pumpMessages()
// exactly once per frame, so ONE pump-indexed timeline on ScriptedPlatform
// (scriptAt) can deterministically steer a whole multi-screen flow: the input
// state a screen reads after its p-th pump is timeline state[p]. Clicks are a
// single left=true pump (one edge) so a held button never leaks a spurious edge
// into the next screen.
//
//   A FullNewGameFlow   (guarded): RunNativeMainMenu -> New Game -> 3D city pick
//       (AUGSBURG, Enter) -> difficulty (normal) -> history (personal) -> player
//       wizard ("Test"/"Player" via pollText) -> charcreate (profession+confirm)
//       -> kPlayCity with the AUGSBURG cityPath.
//   B SessionDeterministic (guarded): RunSdlSession bounded frames on AUGSBURG;
//       loaded + framesPresented + hashStart/hashEnd reproducible across 2 runs.
//   C MenuRoundTrip     (asset-free): menu -> game options -> ESC back ->
//       credits -> ESC back -> Quit; clean kQuit, deterministic frame count.
//   D EscFromSessionThenMenuAgain (guarded): ESC quits the session (quitByEsc);
//       a second RunNativeMainMenu pass still works in-process afterwards.
//   E IntegratedCity3DSession (guarded): the WAVE-2 wired session — new-game
//       params committed into the live world (player in sim::g_persons with the
//       chosen name), the continuous SessionTick clock fired, the CityView3D
//       real-3D frame is non-trivial, real HUD artwork fed, and the whole
//       integrated session stays hash-deterministic across reruns.
//
// NativeMenuResult now carries the full gui::NewGameParams block; TEST A
// asserts the chosen city / difficulty / history / identity / profession all
// survive the chain (see also tests/integration/newgame_result_itest.cpp for
// the asset-free variant).
// =============================================================================
#include "play/native_main_menu.h"
#include "play/real_city_render.h"       // Options (TEST H hover projection)
#include "play/scene_pick.h"             // MakeCityViewCamera / ProjectWorldToScreen
#include "play/sdl_charintro_screen.h"   // CharIntroComputeLayout (row hit rects)
#include "play/sdl_session.h"
#include "app/real_boot.h"               // MountRealGameAssets (TEST H projection)
#include "gui/main_menu.h"               // MainMenu_ButtonY / MainMenuItem
#include "gui/newgame_setup.h"           // Profession_ButtonX/Y (charcreate grid)
#include "shim_impl/memory_graphics.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/scripted_platform.h"
#include "io/save_world_load.h"          // io::LoadWorld (TEST H projection)
#include "io/vfs.h"
#include "play/city_view3d.h"            // TEST I: independent ground-height check
#include "render/heightmap.h"            // TileToWorld @0x5c65d4 (TEST I)
#include "render/scene_floor.h"          // ParseSceneFloorHeights (TEST I)
#include "sim/building_type.h"           // Building_IsProductionKind (TEST I seed)
#include "sim/entity.h"
#include "sim/person.h"                  // PersonIsValidActiveRecord / SetDword

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace guild;

namespace {

constexpr int kVkEsc = 0x1B, kVkReturn = 0x0D, kVkBackspace = 0x08;
constexpr int kW = 800, kH = 600;   // the design resolution: sub-screen layout == design coords

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/scenes.BIN") && fs.exists("Resources/Objects.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

const std::string kAugsburgPath = "Resources/gamedata/Cities/AUGSBURG.cty";

std::vector<std::pair<std::string, std::string>> AugsburgFirstCities() {
    // AUGSBURG first => it is the 3D city screen's default-selected tower, so a
    // plain Enter confirms it (the 1:1 ChooseCity_IsConfirm key-28 path).
    return { {"AUGSBURG", kAugsburgPath},
             {"BERLIN",   "Resources/gamedata/Cities/BERLIN.cty"} };
}

// Centre of a main-menu button by its build index (the same centred+scaled
// layout the menu hit-tests against).
void MenuButtonCentre(gui::MainMenuItem item, int& cx, int& cy) {
    const play::MenuButtonRect r =
        play::MenuButtonScreenRect(gui::MainMenu_ButtonY((int)item), kW, kH);
    cx = r.x + r.w / 2; cy = r.y + r.h / 2;
}

// Centre of row `i` of a radio-of-N panel (difficulty / history screens).
void RadioRowCentre(int rowCount, int i, int& cx, int& cy) {
    const play::CharIntroLayout L = play::CharIntroComputeLayout(kW, kH, rowCount);
    int rx, ry, rw, rh; L.RowRect(i, rx, ry, rw, rh);
    cx = rx + rw / 2; cy = ry + rh / 2;
}

} // namespace

// ---------------------------------------------------------------------------
// TEST A — the full scripted New-Game flow through RunNativeMainMenu.
// ---------------------------------------------------------------------------
TEST(PlayableFlowE2E, FullNewGameFlow) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlayableFlowE2E.FullNewGameFlow: real game dir absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }

    shim::MemoryGraphicsDevice dev; CHECK(dev.init(kW, kH, 32, false));
    shim::MemFileSystem fs;          // RunNativeMainMenu's own fs (menu art via gameDir)
    shim::ScriptedPlatform plat;
    plat.quitAfterPumps(900);        // watchdog: a desynced script ends in a window close

    int ngX, ngY; MenuButtonCentre(gui::MainMenuItem::kNewGame, ngX, ngY);
    // Difficulty panel: 5 levels + "back" = 6 rows; row 2 = "normal".
    int diffX, diffY; RadioRowCentre(6, 2, diffX, diffY);
    // History panel: buttons centred at x=W/2, row i top y=324+40*i (height 33). Pick
    // row 1 = PERSONAL (flag 2): the FACTUAL row (0) branches into the tasks screen;
    // personal/none go straight to the wizard, keeping this script linear.
    const int histX = kW / 2;
    const int histY = (324 + 40) * kH / 600 + (33 * kH / 600) / 2;
    // Player-wizard layout (sdl_chooseplayer_screen.cpp, 800x600 design): parchment
    // (px=(800-490)/2=155, py=72, pw=490); radio rows y0=py+170, rowH=46, rh=38, centred.
    const int rowX = kW / 2;                          // gender/faith rows centre on screen
    const int row0Y = 72 + 170 + 38 / 2;              // 261 — row 0 centre
    // Wappen grid: gx=px+30=185, gy=py+130=202, cw=(490-60)/4=107 (cell w = cw-8), ch=70.
    const int wapX = 185 + (107 - 8) / 2, wapY = 202 + 70 / 2;      // (234,237) cell 0 centre
    // Charcreate profession cell 0 (real geometry helpers; cell 84x68).
    const int profX = gui::Profession_ButtonX(0) + 84 / 2;          // 142
    const int profY = gui::Profession_ButtonY(0) + 68 / 2;          // 134
    // Charcreate confirm button (sdl_charcreate_screen.cpp: 520,520,220,44).
    const int cfmX = 520 + 220 / 2, cfmY = 520 + 44 / 2;            // (630,542)

    // ---- the timeline (one pump == one frame of whichever screen is live) ----
    // Menu: hover New Game (pumps 0-2), click on pump 3 -> EnterChooseCity.
    plat.scriptAt(0, ngX, ngY, false);
    plat.scriptAt(3, ngX, ngY, true);
    // 3D city screen (pumps 4..5): Enter skips the A_Stadtwahl intro flight on its
    // first frame, then confirms the default-selected first city (AUGSBURG).
    plat.scriptAt(4, 10, 10, false, {kVkReturn});
    // Difficulty screen (pumps 6..7): hover row 2 ("normal"), click on pump 7.
    plat.scriptAt(6, diffX, diffY, false);
    plat.scriptAt(7, diffX, diffY, true);
    // History screen (pumps 8..9): hover row 1 (personal history), click on pump 9.
    plat.scriptAt(8, histX, histY, false);
    plat.scriptAt(9, histX, histY, true);
    // Player wizard (first pump 10). Page 0 (first name): the field is seeded
    // "Spieler" — 7 Backspace EDGES (down on odd pumps 11..23, up between) clear
    // it, then the pollText stream types "Test", then Enter advances.
    plat.scriptAt(10, 10, 10, false);
    for (int k = 0; k < 7; ++k) {
        plat.scriptAt(11 + 2 * k, 10, 10, false, {kVkBackspace});
        plat.scriptAt(12 + 2 * k, 10, 10, false);
    }
    plat.scriptAt(25, 10, 10, false, {}, "Test");
    plat.scriptAt(26, 10, 10, false, {kVkReturn});
    plat.scriptAt(27, 10, 10, false);
    // Page 1 (family name, seeded empty): type "Player", Enter advances.
    plat.scriptAt(28, 10, 10, false, {}, "Player");
    plat.scriptAt(29, 10, 10, false, {kVkReturn});
    // Page 2 (gender): click row 0 (male) on pump 31.
    plat.scriptAt(30, rowX, row0Y, false);
    plat.scriptAt(31, rowX, row0Y, true);
    // Page 3 (faith): click row 0 (catholic) on pump 33.
    plat.scriptAt(32, rowX, row0Y, false);
    plat.scriptAt(33, rowX, row0Y, true);
    // Page 4 (wappen): click cell 0 on pump 35 -> page 5 auto-commits (pump 36).
    plat.scriptAt(34, wapX, wapY, false);
    plat.scriptAt(35, wapX, wapY, true);
    // Charcreate (first pump 37): click profession 0 on pump 38 -> wappen phase,
    // then click CONFIRM on pump 40 -> the new-game chain commits.
    plat.scriptAt(36, profX, profY, false);
    plat.scriptAt(38, profX, profY, true);
    plat.scriptAt(39, cfmX, cfmY, false);
    plat.scriptAt(40, cfmX, cfmY, true);
    plat.scriptAt(41, 10, 10, false);

    play::NativeMenuResult r = play::RunNativeMainMenu(
        dev, plat, fs, GameDir(), AugsburgFirstCities(),
        kW, kH, /*frameCapMs=*/0, /*maxFrames=*/-1);

    std::printf("[flow-e2e] action=%d cityPath='%s' menuFrames=%d pumps=%d "
                "devPresents=%d quitByWindow=%d\n",
                (int)r.action, r.cityPath.c_str(), r.framesPresented, plat.pumps(),
                dev.presentCount(), (int)r.quitByWindow);

    CHECK(r.action == play::NativeMenuResult::kPlayCity);
    CHECK_EQ(r.cityPath, kAugsburgPath);
    CHECK(r.framesPresented > 0);
    CHECK(!r.quitByWindow);                 // the chain committed; no watchdog close
    // The FULL chosen new-game state survives the chain (the 0x122F4A0.. block):
    CHECK_EQ(r.params.cityName, "stadt_AUGSBURG");
    CHECK_EQ(r.params.cityFile, "AUGSBURG");      // ReturnedString base name
    CHECK_EQ(r.params.difficulty, 2);             // row 2 = "normal" (byte_12335BA)
    CHECK_EQ(r.params.historyFlag, 2);            // personal history (skips tasks screen)
    CHECK_EQ(r.params.firstName, "Test");
    CHECK_EQ(r.params.familyName, "Player");
    CHECK_EQ(r.params.gender, 0);
    CHECK_EQ(r.params.faith, 0);
    CHECK_EQ(r.params.wappen, 0);
    CHECK_EQ(r.params.profession, 1);             // grid cell 0 -> beruf byte 1
    CHECK_EQ(r.params.professionVariant, 1);
    CHECK(r.params.started);                      // armed for ApplyNewGameParams
    // Menu frames: iterations on pumps 0..3 (the whole sub-chain runs nested in
    // iteration 3's dispatch) -> exactly 4 menu presents.
    CHECK_EQ(r.framesPresented, 4);
    // The watchdog never fired: the scripted flow consumed exactly 41 pumps
    // (menu 0-3, city 4-5, difficulty 6-7, history 8-9, wizard 10-36, create 37-40).
    CHECK_EQ(plat.pumps(), 41);
    // Every screen of the chain presented through the one device.
    CHECK(dev.presentCount() > r.framesPresented);
}

// ---------------------------------------------------------------------------
// TEST B — the city session the flow hands off to, bounded + reproducible.
// ---------------------------------------------------------------------------
TEST(PlayableFlowE2E, SessionDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlayableFlowE2E.SessionDeterministic: real game dir absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }

    play::SdlSessionConfig cfg;
    cfg.gameDir = GameDir();
    cfg.cityPath = kAugsburgPath;           // the city TEST A picked
    cfg.fbW = 320; cfg.fbH = 240;
    cfg.frameCapMs = 0;
    cfg.maxFrames = 5;                      // bounded headless session
    cfg.seed = 0x4711;

    auto run = [&]() {
        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        CHECK(dev.init(cfg.fbW, cfg.fbH, 16, false));
        shim::ScriptedPlatform plat;        // no input: render/present only
        play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);
        sim::ResetEntityArrays();
        io::VfsShutdown();
        return tr;
    };

    play::SdlSessionTrace tr = run();
    std::printf("[flow-e2e] session loaded=%d liveObjects=%d frames=%d "
                "hashStart=0x%llx hashEnd=0x%llx\n",
                (int)tr.loaded, tr.liveObjects, tr.framesPresented,
                (unsigned long long)tr.hashStart, (unsigned long long)tr.hashEnd);

    CHECK(tr.mounted);
    CHECK(tr.loaded);
    CHECK(tr.liveObjects > 0);
    CHECK_EQ(tr.framesPresented, 5);        // exactly the bounded frame count
    CHECK(tr.hashStart != 0);

    // Reproducibility: same seed + same script => identical world witness.
    play::SdlSessionTrace tr2 = run();
    CHECK_EQ(tr2.framesPresented, tr.framesPresented);
    CHECK_EQ(tr2.liveObjects, tr.liveObjects);
    CHECK_EQ(tr2.hashStart, tr.hashStart);
    CHECK_EQ(tr2.hashEnd, tr.hashEnd);
}

// ---------------------------------------------------------------------------
// TEST C — menu round-trip: options -> back -> credits -> back -> quit.
// Asset-free (fallback art), so it always runs.
// ---------------------------------------------------------------------------
TEST(PlayableFlowE2E, MenuRoundTrip) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(kW, kH, 32, false));
    shim::MemFileSystem fs;
    shim::ScriptedPlatform plat;
    plat.quitAfterPumps(400);               // watchdog

    int optX, optY; MenuButtonCentre(gui::MainMenuItem::kGameOptions, optX, optY);
    int crdX, crdY; MenuButtonCentre(gui::MainMenuItem::kCredits, crdX, crdY);
    int qX, qY;     MenuButtonCentre(gui::MainMenuItem::kQuit, qX, qY);

    // Menu: hover game-options (0-2), click pump 3 -> RunOptionsGame.
    plat.scriptAt(0, optX, optY, false);
    plat.scriptAt(3, optX, optY, true);
    // Options screen (first pump 4): one clean frame, then ESC on pump 5 backs out
    // (released before the menu's next pump so it can't read as a menu quit).
    plat.scriptAt(4, 10, 10, false);
    plat.scriptAt(5, 10, 10, false, {kVkEsc});
    // Menu again (pumps 6-9): hover credits, click pump 9 -> RunCreditsScroll.
    plat.scriptAt(6, crdX, crdY, false);
    plat.scriptAt(9, crdX, crdY, true);
    // Credits crawl (first pump 10): one frame, ESC on pump 11 backs out.
    plat.scriptAt(10, 10, 10, false);
    plat.scriptAt(11, 10, 10, false, {kVkEsc});
    // Menu again (pumps 12-15): hover quit, click pump 15 -> close armed -> kQuit.
    plat.scriptAt(12, qX, qY, false);
    plat.scriptAt(15, qX, qY, true);
    plat.scriptAt(16, 10, 10, false);

    play::NativeMenuResult r = play::RunNativeMainMenu(
        dev, plat, fs, /*gameDir=*/"", AugsburgFirstCities(),
        kW, kH, /*frameCapMs=*/0, /*maxFrames=*/-1);

    std::printf("[flow-e2e] roundtrip action=%d menuFrames=%d pumps=%d devPresents=%d\n",
                (int)r.action, r.framesPresented, plat.pumps(), dev.presentCount());

    CHECK(r.action == play::NativeMenuResult::kQuit);
    CHECK(!r.quitByWindow);                 // ended via the Quit dispatch, not the watchdog
    // Deterministic shape: 12 menu iterations (pumps 0-3, 6-9, 12-15) -> 12 menu
    // presents; options consumed pumps 4-5, credits pumps 10-11 -> 16 pumps total.
    CHECK_EQ(r.framesPresented, 12);
    CHECK_EQ(plat.pumps(), 16);
    // Options + credits each presented 2 frames of their own through the device.
    CHECK_EQ(dev.presentCount(), 16);
}

// ---------------------------------------------------------------------------
// TEST D — ESC quits the session; a second menu pass still works in-process.
// ---------------------------------------------------------------------------
TEST(PlayableFlowE2E, EscFromSessionThenMenuAgain) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlayableFlowE2E.EscFromSessionThenMenuAgain: real game dir "
                    "absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    // ---- the session, quit by ESC (the in-game back-to-menu path) ----
    {
        play::SdlSessionConfig cfg;
        cfg.gameDir = GameDir();
        cfg.cityPath = kAugsburgPath;
        cfg.fbW = 320; cfg.fbH = 240;
        cfg.frameCapMs = 0;

        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        CHECK(dev.init(cfg.fbW, cfg.fbH, 16, false));
        shim::ScriptedPlatform plat;
        plat.pressKey(kVkEsc);              // ESC held from frame 0
        play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);
        sim::ResetEntityArrays();
        io::VfsShutdown();

        std::printf("[flow-e2e] esc-session loaded=%d frames=%d quitByEsc=%d cleanQuit=%d\n",
                    (int)tr.loaded, tr.framesPresented, (int)tr.quitByEsc, (int)tr.cleanQuit);
        CHECK(tr.loaded);
        CHECK(tr.framesPresented > 0);
        CHECK(tr.quitByEsc);
        CHECK(tr.cleanQuit);
        CHECK(!tr.quitByWindow);
    }

    // ---- a SECOND RunNativeMainMenu pass in the same process still works ----
    {
        shim::MemoryGraphicsDevice dev; CHECK(dev.init(kW, kH, 32, false));
        shim::MemFileSystem fs;
        shim::ScriptedPlatform plat;
        plat.quitAfterPumps(100);           // watchdog

        int qX, qY; MenuButtonCentre(gui::MainMenuItem::kQuit, qX, qY);
        plat.scriptAt(0, qX, qY, false);    // hover quit
        plat.scriptAt(3, qX, qY, true);     // click -> close armed
        plat.scriptAt(4, qX, qY, false);

        play::NativeMenuResult r = play::RunNativeMainMenu(
            dev, plat, fs, /*gameDir=*/"", AugsburgFirstCities(),
            kW, kH, /*frameCapMs=*/0, /*maxFrames=*/-1);

        std::printf("[flow-e2e] second-menu action=%d frames=%d pumps=%d\n",
                    (int)r.action, r.framesPresented, plat.pumps());
        CHECK(r.action == play::NativeMenuResult::kQuit);
        CHECK(!r.quitByWindow);
        CHECK_EQ(r.framesPresented, 4);     // pumps 0-3 then the armed close
    }
}

// ---------------------------------------------------------------------------
// TEST E — the WAVE-2 integrated session: new-game commit + continuous clock +
// the real-3D CityView3D frame + real HUD artwork, all in one RunSdlSession,
// deterministic across reruns. (The exact session guild_run --play now enters
// after the TEST A menu chain hands over NativeMenuResult::params.)
// ---------------------------------------------------------------------------
TEST(PlayableFlowE2E, IntegratedCity3DSession) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlayableFlowE2E.IntegratedCity3DSession: real game dir "
                    "absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    play::SdlSessionConfig cfg;
    cfg.gameDir = GameDir();
    cfg.cityPath = kAugsburgPath;
    cfg.fbW = 320; cfg.fbH = 240;          // small fb: the whole-city 3D raster
    cfg.frameCapMs = 0;
    cfg.seed = 0x4711;
    // 66 frames x 16 ms (the ScriptedPlatform clock) = 1056 ms > the 994 ms
    // clock-proc cadence -> at least one continuous-clock fire.
    cfg.maxFrames = 66;
    // The wave-2 integration flags (what guild_run --play sets):
    cfg.city3d = true;                     // CityView3D + SessionInput
    cfg.continuousClock = true;            // SessionTick
    cfg.applyNewGame = true;               // ApplyNewGameParams after the load
    // The committed parameter block — the same image the TEST A menu chain
    // produces on NativeMenuResult::params (newgame_apply_e2e working sequence).
    gui::NewGame_ApplyCity(cfg.newGame, "stadt_AUGSBURG", "AUGSBURG", false);
    cfg.newGame.difficulty = 2;
    cfg.newGame.historyFlag = 1;
    gui::NewGame_ApplyPlayer(cfg.newGame, "Test", "Player", /*wappen=*/0,
                             /*gender=*/0, /*faith=*/0);
    gui::NewGame_ApplyProfession(cfg.newGame, /*beruf=*/1);
    gui::NewGame_Commit(cfg.newGame);
    cfg.dumpFramePath = "/tmp/guild_session_city3d.ppm";   // visual artifact

    bool playerNameOk = false, playerKindOk = false;
    auto run = [&](bool checkPlayer) {
        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        CHECK(dev.init(cfg.fbW, cfg.fbH, 16, false));
        shim::ScriptedPlatform plat;       // no clicks/keys: render/clock only
        // Park the cursor mid-screen: (0,0) sits inside the REAL edge-scroll
        // band (Camera_UpdatePan @0x4b365c), which would pan the camera off the
        // city for the whole bounded run — exactly as on the real window.
        plat.setMouse(cfg.fbW / 2, cfg.fbH / 2, /*left=*/false);
        play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);
        if (checkPlayer && tr.playerId != 0) {
            // The committed player lives in the LIVE sim::g_persons (the arrays
            // are still populated until we reset them below).
            if (sim::Person* pl = sim::PersonFindRecordById(tr.playerId)) {
                playerNameOk = std::strcmp(
                    reinterpret_cast<const char*>(pl) + 0x30, "Test") == 0;
                playerKindOk = pl->kind == 6;
            }
        }
        sim::ResetEntityArrays();
        io::VfsShutdown();
        return tr;
    };

    play::SdlSessionTrace tr = run(/*checkPlayer=*/true);
    std::printf("[flow-e2e] city3d session: view3d=%d bound=%d inst=%d nonClear=%d "
                "newGame=%d player=%d cash=%d clockFires=%d syncs=%d day=%d hour=%d "
                "hudArt=%d glyphs=%d frames=%d dump=%d\n",
                (int)tr.view3dActive, tr.view3dBoundObjects, tr.view3dInstances,
                tr.view3dNonClear, (int)tr.newGameApplied, tr.playerId,
                tr.playerCashEnd, tr.clockFires, tr.timeSyncCommits,
                tr.worldDay, tr.worldHour, (int)tr.hudRealArt,
                tr.hudCaptionGlyphs, tr.framesPresented, (int)tr.frameDumped);

    // The session loaded + rendered the REAL 3D city.
    CHECK(tr.mounted);
    CHECK(tr.loaded);
    CHECK(tr.liveObjects > 0);
    CHECK_EQ(tr.framesPresented, cfg.maxFrames);
    CHECK(tr.view3dActive);                 // CityView3D drove the frames
    CHECK(tr.view3dBoundObjects > 0);       // live objects at REAL placements
    CHECK(tr.view3dInstances > 100);        // the whole city walked the frame
    CHECK(tr.view3dNonClear > 500);         // the framebuffer is non-trivial
    CHECK(tr.frameDumped);                  // /tmp PPM artifact written

    // The new-game commit landed in the live world.
    CHECK(tr.newGameApplied);
    CHECK(tr.playerId > 0);
    CHECK(playerNameOk);                    // player +0x30 == "Test"
    CHECK(playerKindOk);                    // the kind-6 player person

    // The continuous game clock ran (TimeBase -> clock proc @0x527778).
    CHECK(tr.clockActive);
    CHECK(tr.clockFires >= 1);              // >= one 994 ms clock fire
    CHECK_EQ(tr.worldHour, 6);              // the 06:00 turn-start sync held

    // The HUD composited with the real converted gilde.gfx bank.
    CHECK(tr.hudActive);
    CHECK(tr.hudRealArt);
    CHECK(tr.hudCaptionGlyphs > 0);

    // DETERMINISM: the whole integrated session is reproducible.
    play::SdlSessionTrace tr2 = run(/*checkPlayer=*/false);
    CHECK_EQ(tr2.framesPresented, tr.framesPresented);
    CHECK_EQ(tr2.view3dBoundObjects, tr.view3dBoundObjects);
    CHECK_EQ(tr2.view3dInstances, tr.view3dInstances);
    CHECK_EQ(tr2.view3dNonClear, tr.view3dNonClear);
    CHECK_EQ(tr2.playerId, tr.playerId);
    CHECK_EQ(tr2.clockFires, tr.clockFires);
    CHECK_EQ(tr2.hashStart, tr.hashStart);
    CHECK_EQ(tr2.hashEnd, tr.hashEnd);

    // Wave-3: the type catalogs (A_Geb/A_Obj, data load @0x5835f8) loaded — the
    // tooltip/info-panel record feeds and the economy leaves read them.
    CHECK(tr.typeTablesLoaded);
}

// ---------------------------------------------------------------------------
// TEST F — LOAD GAME enters the 3D city (wave-3). A partial save (the shipped
// AUGSBURG.cty IS a partial save, header flag bit 1) embeds no scene stream for
// LoadLiveWorld, so the session derives the city from the loaded save's header
// city name (SaveHeader +0x05 == "Augsburg" — the field app/wiring.cpp
// publishes as the city name) and loads scenes.BIN Staedte/stadt_<city>.ed3
// (VIBE_Scene_LoadStadtScene @0x500218).
// ---------------------------------------------------------------------------
TEST(PlayableFlowE2E, LoadGameEntersCity3D) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlayableFlowE2E.LoadGameEntersCity3D: real game dir "
                    "absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    play::SdlSessionConfig cfg;
    cfg.gameDir = GameDir();
    cfg.loadSavePath = kAugsburgPath;   // the menu's kLoadGame savePath contract
    cfg.fbW = 320; cfg.fbH = 240;
    cfg.frameCapMs = 0;
    cfg.maxFrames = 6;
    cfg.seed = 0x4711;
    cfg.city3d = true;                  // NO cfg.cityName3d: the session derives it

    auto run = [&]() {
        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        CHECK(dev.init(cfg.fbW, cfg.fbH, 16, false));
        shim::ScriptedPlatform plat;
        plat.setMouse(cfg.fbW / 2, cfg.fbH / 2, false);  // off the edge bands
        play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);
        sim::ResetEntityArrays();
        io::VfsShutdown();
        return tr;
    };

    play::SdlSessionTrace tr = run();
    std::printf("[flow-e2e] load-game 3d: loadedFromSave=%d view3d=%d city='%s' "
                "bound=%d inst=%d nonClear=%d types=%d\n",
                (int)tr.loadedFromSave, (int)tr.view3dActive,
                tr.city3dCityName.c_str(), tr.view3dBoundObjects,
                tr.view3dInstances, tr.view3dNonClear, (int)tr.typeTablesLoaded);

    CHECK(tr.loaded);
    CHECK(tr.loadedFromSave);                  // entered via LoadLiveWorld
    CHECK(tr.view3dActive);                    // the 3D city came up
    CHECK_EQ(tr.city3dCityName, "Augsburg");   // derived from the save header
    CHECK(tr.view3dInstances > 100);           // the whole city walked the frame
    CHECK(tr.view3dNonClear > 500);            // non-trivial framebuffer
    CHECK(tr.typeTablesLoaded);                // catalogs restored after the load
    // NAMED GAP (pinned, not faked): the standalone scenes.BIN stadt_*.ed3 is
    // the city scene WITHOUT the per-node +512 owner-object ids — those are
    // stamped only in the save-EMBEDDED scene stream (the WorldIo sidecar whose
    // WRITE side is unreconstructed), so RebuildModelByOwner @0x5a8140 binds 0
    // live objects on this fallback path (no pickable entities until the
    // sidecar lands). The city itself renders fully.
    CHECK_EQ(tr.view3dBoundObjects, 0);

    play::SdlSessionTrace tr2 = run();         // deterministic across reruns
    CHECK_EQ(tr2.view3dBoundObjects, tr.view3dBoundObjects);
    CHECK_EQ(tr2.view3dInstances, tr.view3dInstances);
    CHECK_EQ(tr2.hashStart, tr.hashStart);
    CHECK_EQ(tr2.hashEnd, tr.hashEnd);
}

// ---------------------------------------------------------------------------
// TEST G — MOUSE-WHEEL ZOOM (wave-3): the scripted wheel notches ride the new
// shim::MouseState::wheel axis into the REAL camera wheel-zoom branch
// (SessionCamera -> VIBE_Camera_UpdateMovement @0x4b41a8: one notch == zoom
// +0.1 + the Camera_AnchorToTerrain @0x4b2900 eye/pitch recompute).
// ---------------------------------------------------------------------------
TEST(PlayableFlowE2E, WheelZoomChangesCamera3D) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlayableFlowE2E.WheelZoomChangesCamera3D: real game "
                    "dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    play::SdlSessionConfig cfg;
    cfg.gameDir = GameDir();
    cfg.cityPath = kAugsburgPath;
    cfg.fbW = 320; cfg.fbH = 240;
    cfg.frameCapMs = 0;
    cfg.maxFrames = 8;
    cfg.seed = 0x4711;
    cfg.city3d = true;

    auto run = [&](int notches) {
        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        CHECK(dev.init(cfg.fbW, cfg.fbH, 16, false));
        shim::ScriptedPlatform plat;
        plat.setMouse(cfg.fbW / 2, cfg.fbH / 2, false);  // off the edge bands
        if (notches)
            plat.setWheel(notches);     // consumed by the first getMouse()
        play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);
        sim::ResetEntityArrays();
        io::VfsShutdown();
        return tr;
    };

    play::SdlSessionTrace base = run(0);
    play::SdlSessionTrace zoomed = run(2);
    std::printf("[flow-e2e] wheel: base zoom=%.3f notches=%d | wheel zoom=%.3f "
                "notches=%d\n",
                base.zoomEnd, base.wheelNotches, zoomed.zoomEnd,
                zoomed.wheelNotches);

    CHECK_EQ(base.wheelNotches, 0);
    // City-enter boot anchor is zoom 0.33 (gilde.exe 0x506fe9: push 0.33f;
    // call Camera_AnchorToTerrain@0x4b2900). No wheel -> stays at the boot 0.33.
    CHECK(std::fabs(base.zoomEnd - 0.33f) < 1e-5f);
    CHECK_EQ(zoomed.wheelNotches, 2);
    CHECK(std::fabs(zoomed.zoomEnd - 0.53f) < 1e-5f);  // boot 0.33 + 2 notches*0.1

    play::SdlSessionTrace zoomed2 = run(2);    // deterministic across reruns
    CHECK(zoomed2.zoomEnd == zoomed.zoomEnd);
    CHECK_EQ(zoomed2.hashEnd, zoomed.hashEnd);
}

// ---------------------------------------------------------------------------
// TEST H — HOVER TOOLTIP RENDERS REAL RECORD CONTENT (wave-3): park the cursor
// (no click) on a live object's projected screen point; the clickless hover
// pick resolves the live g_objects record, the session feeds its REAL record
// fields (building type byte -> the A_Geb catalog fields @+579/+583 + the
// ComputeSalePrice @0x591480 value), the REAL dispatch (@0x4f7424) classifies
// it as kBuilding, and VIBE_Tooltip_BuildBuilding @0x4f78e4 composites real
// content lines onto the frame.
// ---------------------------------------------------------------------------
namespace {

// Projected screen point of the first live object under the legacy renderer's
// pick camera (the sdl_session_itest FirstObjectScreen precedent).
bool FirstObjectScreenPoint(const play::RealCityRenderer::Options& opt,
                            float* outSX, float* outSY) {
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    if (!assets.vfsBound) { io::VfsShutdown(); return false; }
    sim::ResetEntityArrays();
    io::WorldState world{};
    if (!io::LoadWorld(kAugsburgPath.c_str(), world)) {
        io::VfsShutdown(); return false;
    }
    int slot = -1;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) { slot = i; break; }
    if (slot < 0) { sim::ResetEntityArrays(); io::VfsShutdown(); return false; }

    int cols = opt.gridCols > 0 ? opt.gridCols : 8;
    float world3[3] = {
        opt.originX + (float)(slot % cols) * opt.cellSize, 0.0f,
        opt.originZ + (float)(slot / cols) * opt.cellSize
    };
    float ppu = opt.pixelsPerUnit > 0.0f ? opt.pixelsPerUnit : 1.0f;
    float eye[3] = {
        opt.eyeX - ((float)opt.fbW * 0.5f) / ppu, 0.0f,
        opt.eyeZ - ((float)opt.fbH * 0.5f) / ppu
    };
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, ppu, opt.fbW, opt.fbH);
    bool on = play::ProjectWorldToScreen(cam, world3, outSX, outSY);

    sim::ResetEntityArrays();
    io::VfsShutdown();
    return on;
}

} // namespace

// ---------------------------------------------------------------------------
// TEST I — LIVING CITY (wave 4): persons MOVE through the 3D city session.
// The day-start daily-routine pass (BeginPlayerRound @0x533188 ->
// NpcDaily_DailyRoutineStep @0x4e7e88 through the real-leaf bridge) dispatches
// the live persons to their work building; the real path-follow
// (PathBuildWaypointList @0x43bd70 / WalkStep @0x4093b0) advances them one
// tile per opcode-30 WORLD-clock commit (ExAdvanceGameTick @0x498954); the
// person pass draws them at TileToWorld @0x5c65d4 over the REAL ground.
// The postWorldLoadHook (host/test seam) seeds the person building columns
// (+364 home / +368 work / +388 dest — the columns the director sweeps),
// since nothing in-tree populates them yet (the persons3d-wave3 named gap).
// ---------------------------------------------------------------------------
TEST(PlayableFlowE2E, LivingCityPersonsMove) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlayableFlowE2E.LivingCityPersonsMove: real game dir "
                    "absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    play::SdlSessionConfig cfg;
    cfg.gameDir = GameDir();
    cfg.cityPath = kAugsburgPath;
    cfg.fbW = 320; cfg.fbH = 240;
    cfg.frameCapMs = 0;
    cfg.seed = 0x4711;
    // 200 frames x 16 ms (ScriptedPlatform clock) = 3200 ms => 3 clock-proc
    // fires (994 ms cadence) => >= 3 opcode-30 world commits => 3 tile steps.
    cfg.maxFrames = 200;
    cfg.city3d = true;
    cfg.continuousClock = true;
    cfg.applyNewGame = true;
    gui::NewGame_ApplyCity(cfg.newGame, "stadt_AUGSBURG", "AUGSBURG", false);
    cfg.newGame.difficulty = 2;
    cfg.newGame.historyFlag = 1;
    gui::NewGame_ApplyPlayer(cfg.newGame, "Test", "Player", 0, 0, 0);
    gui::NewGame_ApplyProfession(cfg.newGame, 1);
    gui::NewGame_Commit(cfg.newGame);

    // HOST/TEST seam: seed every live person's daily-director columns from the
    // city's two production buildings (AUGSBURG carries exactly two type-11
    // records, ids 1 and 14): home/dest = the first, work = the second — the
    // exact columns VIBE_NpcAction_DailyRoutineStep @0x4e7e88 dispatches on.
    cfg.postWorldLoadHook = [&]() {
        i32 prodA = 0, prodB = 0;
        for (int i = 0; i < sim::kObjectCapacity; ++i) {
            if (!sim::g_objects[i].alive)
                continue;
            if (!sim::Building_IsProductionKind(sim::g_objects[i].alive))
                continue;
            if (prodA == 0)      prodA = sim::g_objects[i].id;
            else if (prodB == 0) { prodB = sim::g_objects[i].id; break; }
        }
        if (prodA == 0 || prodB == 0)
            return;
        for (int i = 0; i < sim::kPersonCapacity; ++i) {
            if (!sim::PersonIsValidActiveRecord((u16)i))
                continue;
            sim::Person& p = sim::g_persons[i];
            sim::PersonSetByte(&p, 356, 1);          // activeA (byte_12CEA74)
            sim::PersonSetDword(&p, 364, prodA);     // homeBld dword_12CEA7C
            sim::PersonSetDword(&p, 368, prodB);     // workBld dword_12CEA80
            sim::PersonSetDword(&p, 388, prodA);     // destBld dword_12CEA94
            if (p.kind == 9)                          // parents: resolvable model
                sim::PersonSetByte(&p, 0x165, 0x02); // profession (dieb_MANN2)
        }
    };

    auto run = [&]() {
        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        CHECK(dev.init(cfg.fbW, cfg.fbH, 16, false));
        shim::ScriptedPlatform plat;
        plat.setMouse(cfg.fbW / 2, cfg.fbH / 2, false);   // off the edge bands
        play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);
        sim::ResetEntityArrays();
        io::VfsShutdown();
        return tr;
    };

    play::SdlSessionTrace tr = run();
    std::printf("[flow-e2e] living city: moveActive=%d dispatched=%d assigned=%d "
                "steps=%d moved=%d arrivals=%d moving=%d posUpdates=%d "
                "mover=%d tile=(%d,%d) world=(%.1f,%.1f,%.1f) persons3d=%d\n",
                (int)tr.npcMovementActive, tr.dailyDispatched, tr.dailyAssigned,
                tr.moveSteps, tr.personsMoved, tr.moveArrivals, tr.personsMoving,
                tr.movePosUpdates, tr.moverId, tr.moverTileX, tr.moverTileZ,
                tr.moverWorldX, tr.moverWorldY, tr.moverWorldZ,
                tr.personsRendered);

    CHECK(tr.loaded);
    CHECK(tr.view3dActive);
    CHECK(tr.npcMovementActive);            // the movement driver came up
    CHECK(tr.clockFires >= 3);              // the clock drove the session
    CHECK(tr.dailyDispatched >= 3);         // the REAL director dispatched all 3
    CHECK(tr.dailyAssigned >= 3);           // ... and their destinations bound
    CHECK(tr.moveSteps >= 3);               // one path-follow step per commit
    CHECK(tr.personsMoved >= 3);            // waypoint advances actually happened
    CHECK(tr.movePosUpdates >= 3);          // draw positions CHANGED across frames
    CHECK(tr.personsMoving >= 1);           // still en route at exit (long path)
    CHECK(tr.moverId > 0);                  // a person carries a live tile

    // The mover's draw seat sits on the REAL ground: recompute the terrain
    // heightmap independently (the same floor-block chain the session builds —
    // ParseSceneFloorHeights + BuildCityHeightmapFromFloor over the city
    // bounds) and check TileToWorld(@0x5c65d4) of the reported tile matches.
    {
        shim::DiskFileSystem fs(GameDir());
        app::RealGameAssets assets =
            app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {}, false);
        CHECK(assets.vfsBound);
        sim::ResetEntityArrays();
        io::WorldState world{};
        std::vector<u8> blob;
        CHECK(io::LoadWorldEx(kAugsburgPath.c_str(), world, &blob));
        play::CityView3D view;
        CHECK(view.Init(&fs));
        CHECK(view.LoadCityFromWorld(blob));
        view.SetHooks({});
        view.BindWorldObjects();
        float lo[3], hi[3];
        view.WorldBounds(lo, hi);
        render::SceneFloorHeights fl = render::ParseSceneFloorHeights(blob);
        render::Heightmap hm{};
        std::vector<u8> store;
        CHECK(fl.ok);
        CHECK(render::BuildCityHeightmapFromFloor(fl, lo, hi, hm, store));
        float w[3];
        CHECK(render::TileToWorld(&hm, tr.moverTileX, tr.moverTileZ, w));
        CHECK(std::fabs(w[0] - tr.moverWorldX) < 1e-3f);
        CHECK(std::fabs(w[1] - tr.moverWorldY) < 1e-3f);   // REAL ground height
        CHECK(std::fabs(w[2] - tr.moverWorldZ) < 1e-3f);
        CHECK(w[1] >= lo[1] && w[1] <= hi[1] + 2.0f);      // inside the city Y
        sim::ResetEntityArrays();
        io::VfsShutdown();
    }

    // DETERMINISM: the whole moving session is reproducible across reruns.
    play::SdlSessionTrace tr2 = run();
    CHECK_EQ(tr2.dailyDispatched, tr.dailyDispatched);
    CHECK_EQ(tr2.dailyAssigned, tr.dailyAssigned);
    CHECK_EQ(tr2.moveSteps, tr.moveSteps);
    CHECK_EQ(tr2.personsMoved, tr.personsMoved);
    CHECK_EQ(tr2.movePosUpdates, tr.movePosUpdates);
    CHECK_EQ(tr2.moverId, tr.moverId);
    CHECK_EQ(tr2.moverTileX, tr.moverTileX);
    CHECK_EQ(tr2.moverTileZ, tr.moverTileZ);
    CHECK(tr2.moverWorldY == tr.moverWorldY);
    CHECK_EQ(tr2.hashStart, tr.hashStart);
    CHECK_EQ(tr2.hashEnd, tr.hashEnd);
}

TEST(PlayableFlowE2E, HoverTooltipShowsRealBuildingContent) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlayableFlowE2E.HoverTooltipShowsRealBuildingContent: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    play::SdlSessionConfig cfg;
    cfg.gameDir = GameDir();
    cfg.cityPath = kAugsburgPath;
    cfg.fbW = 320; cfg.fbH = 240;
    cfg.textured = false;
    cfg.frameCapMs = 0;
    cfg.maxFrames = 5;
    cfg.seed = 0x4711;
    // legacy renderer (deterministic grid projection for the cursor park)

    play::RealCityRenderer::Options opt;
    opt.fbW = cfg.fbW; opt.fbH = cfg.fbH;
    float sx = 0.0f, sy = 0.0f;
    CHECK(FirstObjectScreenPoint(opt, &sx, &sy));

    auto run = [&]() {
        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        CHECK(dev.init(cfg.fbW, cfg.fbH, 16, false));
        shim::ScriptedPlatform plat;
        plat.setMouse((int)sx, (int)sy, /*left=*/false);   // HOVER only, no click
        play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);
        sim::ResetEntityArrays();
        io::VfsShutdown();
        return tr;
    };

    play::SdlSessionTrace tr = run();
    std::printf("[flow-e2e] hover: feeds=%d (person %d) tooltipFrames=%d "
                "textOps=%d panelOps=%d types=%d clicks=%d\n",
                tr.hoverFeedFrames, tr.hoverPersonFrames, tr.tooltipFrames,
                tr.tooltipTextOps, tr.panelTextOps, (int)tr.typeTablesLoaded,
                tr.clicksHandled);

    CHECK(tr.loaded);
    CHECK(tr.typeTablesLoaded);          // the A_Geb/A_Obj catalogs are live
    CHECK(tr.hoverFeedFrames > 0);       // a REAL record fed the panel layer
    CHECK(tr.tooltipFrames > 0);         // the dispatch built + showed a tooltip
    CHECK(tr.tooltipTextOps > 0);        // real content lines composited
    CHECK_EQ(tr.clicksHandled, 0);       // hover only — no order issued
    CHECK_EQ(tr.ordersIssued, 0);
    CHECK_EQ(tr.hashStart, tr.hashEnd);  // no mutation from hovering

    play::SdlSessionTrace tr2 = run();   // deterministic across reruns
    CHECK_EQ(tr2.hoverFeedFrames, tr.hoverFeedFrames);
    CHECK_EQ(tr2.tooltipFrames, tr.tooltipFrames);
    CHECK_EQ(tr2.tooltipTextOps, tr.tooltipTextOps);

    // CLICK variant: selecting the same object routes the live g_objects record
    // (code/customName/item/upgradeLevel) into the REAL info-panel update
    // (InfoPanel_Update @0x4b84c0 -> BuildBuilding @0x4b64b0) — the selected-
    // entity panel composites real content.
    {
        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        CHECK(dev.init(cfg.fbW, cfg.fbH, 16, false));
        shim::ScriptedPlatform plat;
        // Press on pump 0, RELEASE on pump 1: the real selection chain commits
        // on release (Selection_CommitContact @0x4b950c's commit-on-release
        // gate), exactly like a real click.
        plat.scriptAt(0, (int)sx, (int)sy, /*left=*/true);
        plat.scriptAt(1, (int)sx, (int)sy, /*left=*/false);
        play::SdlSessionTrace trc = play::RunSdlSession(fs, dev, plat, cfg);
        sim::ResetEntityArrays();
        io::VfsShutdown();
        std::printf("[flow-e2e] click: picks=%d sel=%d panelVisible=%d "
                    "panelOps=%d\n",
                    trc.picksHit, trc.selectedId, (int)trc.panelVisible,
                    trc.panelTextOps);
        CHECK(trc.picksHit > 0);             // the click resolved a live object
        CHECK(trc.selectedId != 0);          // the real selection commit latched
        CHECK(trc.panelVisible);             // a built info panel is on screen
        CHECK(trc.panelTextOps > 0);         // real selected-record content lines
    }
}
