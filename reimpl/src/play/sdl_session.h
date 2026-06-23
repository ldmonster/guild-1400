#pragma once
// =============================================================================
// guild::play — NATIVE INTERACTIVE CITY SESSION (PLAYABLE_PLAN: native Linux play).
//
// The actual "play the game" loop, backend-agnostic. The reconstruction renders
// the real city with its own (reconstructed, 1:1) SOFTWARE rasterizer into an
// IGraphicsDevice framebuffer; the device PRESENTS it. On the real host that
// device is shim::VulkanGraphicsDevice (Vulkan swapchain -> the SDL window,
// replacing DirectDraw/Direct3D) and the platform is shim::SdlVulkanPlatform
// (SDL2 window + input, replacing Win32/DInput); SDL2 also drives audio. NONE of
// this needs Wine — Wine was only ever the off-line verification oracle.
//
// This module is the device/platform-AGNOSTIC core so it is fully testable
// headless (shim::MemoryGraphicsDevice + shim::ScriptedPlatform) AND drives the
// real Vulkan+SDL window unchanged. The caller constructs + initializes the
// device (for Vulkan: configureSwapchain() BEFORE init(fbW,fbH,16,false)) and the
// platform window, then hands them in.
//
// Per frame the loop:
//   (1) renders the real loaded city through play::RealCityRenderer::Render into
//       device.backbuffer() (real AGF meshes from Objects.BIN, optionally textured
//       from Textures.BIN),
//   (2) device.present()  — Vulkan blits the framebuffer to the swapchain image,
//   (3) plat.pumpMessages() — false (window close) ends the session,
//   (4) reads plat.getMouse()/keyDown():
//         * left click   -> RealCityRenderer::Pick at the cursor; if it hits a
//                           live object, issue the current-cursor-mode order on it
//                           through the REAL CommandQueue (play::IssueWorldClick /
//                           the order-apply handler) so the world mutates,
//         * SPACE        -> advance one real game-day (play::RunGameDay),
//         * arrows / screen-edge -> pan the camera (play::CameraControl),
//         * ESC          -> quit,
//   (5) frame-caps via plat.sleepMs(cfg.frameCapMs).
//
// Determinism: HashFullWorld is sampled at start and end as a witness; with a
// scripted platform + fixed seed the whole trace is reproducible (the play-layer
// determinism rig applies — ZeroWorldGlobals on load, Srand before each hash).
// =============================================================================
#include <cstdint>
#include <functional>
#include <string>

#include "gui/newgame_setup.h"   // gui::NewGameParams (the new-game commit block)

namespace guild::shim { class IFileSystem; class IGraphicsDevice; class IPlatform;
                        class IAudioDevice; }

namespace guild::play {

// Cursor/order tool the left click issues on the picked object (mirrors
// play::CursorMode; kept as a small int here to avoid leaking the enum).
struct SdlSessionConfig {
    std::string gameDir;                                       // assets root (MountRealGameAssets)
    std::string cityPath = "Resources/gamedata/Cities/AUGSBURG.cty";
    std::string iniName  = "Gilde.INI";
    std::string objectsArchive  = "Resources/Objects.BIN";
    std::string texturesArchive = "Resources/Textures.BIN";    // "" -> untextured
    int  fbW = 800, fbH = 600;                                 // device MUST be init'd to this x16bpp
    bool textured = true;
    int  maxFrames = -1;                                       // -1 = until quit (window close/ESC)
    int  frameCapMs = 16;                                      // sleep per frame; 0 = uncapped
    bool advanceDayOnSpace = true;                             // SPACE -> RunGameDay
    unsigned seed = 0x4711;                                    // determinism seed
    // Cursor mode for the left-click order (6 == kConquer, the reliable record-
    // mutating order per the play-layer memory). See play::CursorMode.
    int  clickCursorMode = 6;

    // ---- wave-2 wired session subsystems (all reconstructed modules) -------
    // Optional real audio device: when set, play::SessionAudio brings up the
    // real sound stack (SoundSystem + MusicDirector + the 0x4c09a0 audio tick +
    // the market-ambience loop) for the session. Null = silent (headless).
    shim::IAudioDevice* audioDev = nullptr;
    bool hud = true;            // play::SessionHud overlay (money/date/status bar)
    bool persons = true;        // RealCityRenderer person pass (scanPersons +
                                // InitPersonAnims; real character meshes + poses)
    bool atmosphere = true;     // play::SessionAtmos day-cycle/weather driver
    bool selectFeedback = true; // play::SessionSelect (real selection commit /
                                // VIBE_Selection_Reset on empty click; right-click
                                // clears; HUD shows the selected entity)
    bool quickSaveKeys = true;  // F5 -> SaveLiveWorld(Quicksave.SAV), F9 -> load it
    float personAnimStep = 1.0f; // pose-driver ticks folded per rendered frame

    // ---- wave-2 SESSION INTEGRATION (all ADDITIVE; defaults keep the wave-1
    //      behaviour byte-identical for the existing scripted test binaries) ---

    // REAL 3D CITY VIEW: render through play::CityView3D (the whole-city real-3D
    // universe chain over the .cty's EMBEDDED scene stream, io::LoadWorldEx) and
    // drive picking/selection through play::SessionInput (the reconstructed
    // RunFrameLoop input chain @0x4c09a0/0x414a38/0x4b950c) instead of the
    // synthetic top-down RealCityRenderer grid. The camera is the full
    // SessionCamera pose (eye + pitch/yaw euler, terrain-followed eye height).
    bool city3d = false;
    // Fallback city scene name for city3d when the loaded file carries no
    // embedded scene stream (e.g. a partial .SAV quicksave: SaveLiveWorld does
    // not serialize the scene): "AUGSBURG" -> scenes/Staedte/stadt_AUGSBURG.ed3.
    // Wave-3: when EMPTY, the session derives the city itself — the loaded
    // save's header city name (SaveHeader +0x05, e.g. "Augsburg" in the .cty
    // seeds) then the INI [General] Stadt (ReturnedString @0x122EE50) — see
    // progress/session-ui-feeds-wave3.md. Only when every source fails does
    // the session fall back to the legacy view.
    std::string cityName3d;

    // CONTINUOUS GAME CLOCK: play::SessionTick (the reconstructed TimeBase /
    // clock-proc / opcode-30 world-commit chain). Per frame OnFrame(elapsedMs);
    // the HUD date/time reads tick.worldTime(); a day-end gate fast-forwards to
    // 23:00, runs play::RunGameDay (the same transition SPACE triggers), rolls
    // to the next day 06:00 and resumes. SPACE day-advance stays active.
    bool continuousClock = false;
    int  gameSpeedLevel  = 2;    // dword_1233558 = 40 * level (0..4)

    // NEW-GAME COMMIT: when set (and loadSavePath is empty), run
    // play::ApplyNewGameParams(newGame, newGame.difficulty, queue) right after
    // the .cty world load — exactly the original's call site (0x533e03 after
    // 0x533d4b) — so the chosen player + parents + start gold land in the live
    // sim::g_persons. newGame is NativeMenuResult::params from the menu chain.
    bool applyNewGame = false;
    gui::NewGameParams newGame{};

    // LOAD GAME: when non-empty, the session enters from this save instead of
    // cfg.cityPath (play::LoadLiveWorld over the partial .SAV) and skips
    // ApplyNewGameParams (the player already lives in the save).
    std::string loadSavePath;

    // REAL HUD ARTWORK: feed a real gilde.gfx SHAPBANK blob through the
    // reconstructed conversion chain (play::SetHudSpriteBankFromGfx ->
    // ShapeBankConvertNew @0x5d80a8 -> ShapeConvertRgbTo16 @0x5d7c0c) so the
    // HUD sprite blits draw real converted depth-1 banks. Pixels only.
    bool realHudArt = true;
    // The gfx object the slot-icon bank comes from (the RenderHud drawSprite
    // gfx id; record 1403 of the 1806-record gilde.gfx table).
    int  hudIconGfxId = 1403;

    // TOOLTIPS + INFO PANELS: per-frame play::SessionPanelsInputs (clickless
    // hover pick at the cursor -> the REAL tooltip dispatch @0x4f7424 + info
    // panel @0x4b84c0) composited by SessionHud. Pixels only.
    bool uiPanels = true;

    // Write the final rendered frame as a binary PPM (P6) for visual
    // verification ("" = off).
    std::string dumpFramePath;

    // ---- wave-4 LIVING CITY (persons move in the session; additive) ----------
    // NPC movement wiring (active only with city3d && persons): the real
    // path-follow bridge (wire_npc_movement: PathBuildWaypointList @0x43bd70 /
    // WalkStep @0x4093b0) over a walkable grid sized to the session's REAL
    // terrain heightmap, stepped once per opcode-30 WORLD-clock commit (the
    // ExAdvanceGameTick @0x498954 per-tick world cascade; without the
    // continuous clock, once per SPACE game-day). Day starts run the REAL
    // daily-routine director (NpcDaily_DailyRoutineStep @0x4e7e88 via
    // play::SessionNpcDailyAssign) so dispatched persons get destinations.
    // Moving persons render at TileToWorld(@0x5c65d4) positions over the real
    // ground; with no destinations assigned, behavior is unchanged.
    bool npcMovement = true;
    // HOST/TEST seam (NOT an engine feature): invoked once right after the
    // world load + new-game commit, BEFORE the 3D view binds — a scripted
    // harness can stage live-record state here (e.g. seed the person building
    // columns +0x16C/+0x170/+0x184 the daily director dispatches against),
    // exactly what the e2e harnesses do outside the session. Null = no-op.
    std::function<void()> postWorldLoadHook;
};

struct SdlSessionTrace {
    bool mounted = false;          // Objects.BIN mounted + assets mounted
    bool loaded  = false;          // city loaded into the live world
    int  liveObjects = 0;
    int  persons = 0;
    int  framesPresented = 0;      // device.present() calls that succeeded
    int  clicksHandled = 0;        // left-click edges processed
    int  picksHit = 0;             // clicks that resolved to a live object
    int  ordersIssued = 0;         // orders enqueued+applied from clicks
    int  daysAdvanced = 0;         // SPACE-driven game-days run
    int  lastPickedId = 0;
    bool quitByWindow = false;     // pumpMessages() returned false
    bool quitByEsc = false;        // ESC pressed
    bool cleanQuit = false;        // loop exited via quit (not maxFrames)
    std::uint64_t hashStart = 0;   // HashFullWorld after load
    std::uint64_t hashEnd   = 0;   // HashFullWorld at session end

    // ---- wave-2 wired-subsystem outcomes ------------------------------------
    bool hudActive = false;        // SessionHud initialized + composited
    bool audioActive = false;      // SessionAudio brought the real stack up
    bool atmosActive = false;      // SessionAtmos ran per frame
    int  personsRendered = 0;      // person roster entries drawn (last frame)
    int  hudCaptionGlyphs = 0;     // last HUD frame's caption glyph count
    int  lastBrightness = 0;       // SessionAtmos brightness (0..600, last frame)
    int  selectedId = 0;           // SessionSelect current selection at exit
    int  quickSaves = 0;           // F5 quicksaves written
    int  quickLoads = 0;           // F9 quickloads applied

    // ---- wave-2 session-integration outcomes (additive) ---------------------
    bool view3dActive = false;     // CityView3D rendered the session (cfg.city3d)
    int  view3dInstances = 0;      // instances drawn last frame (whole city)
    int  view3dNonClear = 0;       // non-sky pixels last frame (frame non-trivial)
    int  view3dBoundObjects = 0;   // live objects bound to real city placements
    int  view3dSmokeSystems = 0;   // (W8) chimney-smoke particle systems spawned
    int  view3dSunLitVerts  = 0;   // (W8) per-vertex sun-NdotL shades last frame
    int  view3dSceneLights  = 0;   // (W8) scene light nodes collected last frame
    int  view3dGaitFlips    = 0;   // (W8) NPC clip flips (gait<->idle) applied
    bool view3dAnimalsActive = false; // (W8) ambient-animal pool inited this session
    int  view3dAnimalTicks  = 0;   // (W8) Animal_Update calls driven
    int  view3dAnimalCount  = 0;   // (W8) live animals at exit (g_animalCount)
    // ---- wave-9 frame-enrichment outcomes (W9-FRAME-ENRICH) -----------------
    int  view3dFlagObjects   = 0;  // (W9) flag objects RefreshFlagAnimation produced
    int  view3dVegRelit      = 0;  // (W9) vg_/pfl_ type-4 meshes relit last frame
    int  view3dReflectiveMeshes = 0; // (W9) scene meshes with a reflective material
    int  view3dAnimalsDrawn  = 0;  // (W9) ambient animals drawn through char path (peak)
    bool view3dMapOpened    = false; // (W8) the overview map was opened ('M')
    int  view3dMapMarkers   = 0;   // (W8) markers drawn on the overview map
    bool newGameApplied = false;   // ApplyNewGameParams committed (gate passed)
    int  playerId = 0;             // the created kind-6 player person id (0 = none)
    int  playerCashEnd = 0;        // player record cash word (+0x0A) at exit
    bool loadedFromSave = false;   // session entered via cfg.loadSavePath
    bool clockActive = false;      // SessionTick drove the continuous clock
    int  clockFires = 0;           // clock-proc fires (994 ms cadence) total
    int  timeSyncCommits = 0;      // opcode-30 world clock commits total
    int  worldDay = 0;             // tick.worldTime().day at exit
    int  worldHour = 0;            // tick.worldTime().hour at exit
    bool hudRealArt = false;       // a real converted gilde.gfx bank fed the HUD
    int  tooltipFrames = 0;        // frames a tooltip was visible
    bool panelVisible = false;     // info panel visible at exit
    int  atmosLightRebuilds = 0;   // ApplyAtmosLightingFrame rebuilds applied
    bool frameDumped = false;      // cfg.dumpFramePath written

    // ---- wave-3 session UI feeds (additive) ----------------------------------
    bool typeTablesLoaded = false; // A_Geb/A_Obj catalogs loaded (data load @0x5835f8)
    std::string city3dCityName;    // 3D fallback city used ("" = embedded .cty scene)
    int  hoverFeedFrames = 0;      // frames a REAL hover record fed the panel layer
    int  hoverPersonFrames = 0;    // ... of which were person hovers (legacy pick)
    int  tooltipTextOps = 0;       // last frame's composited tooltip text lines
    int  panelTextOps = 0;         // last frame's composited info-panel text lines
    int  wheelNotches = 0;         // mouse-wheel notches the camera consumed (sum)
    float zoomEnd = 0.0f;          // SessionCamera zoom fraction at exit (flt_6316DC)

    // ---- wave-4 living city (additive) ----------------------------------------
    bool npcMovementActive = false; // the movement driver + grid were installed
    int  dailyDispatched = 0;      // persons the REAL daily director dispatched
    int  dailyAssigned = 0;        // movement destinations bound from dispatches
    int  moveSteps = 0;            // StepNpcMovement invocations (commit cadence)
    int  personsMoved = 0;         // entity-waypoint advances total (tallies)
    int  moveArrivals = 0;         // entities that reached their destination
    int  personsMoving = 0;        // persons with an active path at session end
    int  movePosUpdates = 0;       // frames a moved person's draw position changed
    int  moverId = 0;              // first person with a live movement tile at exit
    int  moverTileX = 0;           // ... its record tile (+0x6C/+0x70)
    int  moverTileZ = 0;
    float moverWorldX = 0.0f;      // ... TileToWorld(@0x5c65d4) over the session
    float moverWorldY = 0.0f;      //     terrain (the REAL ground height) — the
    float moverWorldZ = 0.0f;      //     draw seat the person pass used
};

// Run the interactive city session. `device` MUST already be init()'d to
// cfg.fbW x cfg.fbH x 16bpp (and, for Vulkan, configureSwapchain()'d before init
// for on-screen present); `plat`'s main window MUST already be created. Mounts the
// assets, loads cfg.cityPath, then runs the render/present/input loop until quit
// (or cfg.maxFrames frames). Returns the trace. Safe headless (dummy SDL drivers /
// MemoryGraphicsDevice + ScriptedPlatform).
SdlSessionTrace RunSdlSession(shim::IFileSystem& fs, shim::IGraphicsDevice& device,
                              shim::IPlatform& plat, const SdlSessionConfig& cfg);

} // namespace guild::play
