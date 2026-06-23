#pragma once
// =============================================================================
// guild::play — FULL HEADLESS SESSION INTEGRATOR (PLAYABLE_PLAN Milestone M5).
//
// Every prior wave proved ONE arc of the program:
//   mode_fsm / interactive / run_interactive_app  the boot->menu->session SPINE
//                                                 (the REAL MenuRunMainMenu dispatch
//                                                 + MenuMainDecide quit decision)
//   real_city_render / render_binder SetRealBridges  the REAL city render (real AGF
//                                                 meshes + terrain in the live frame)
//   input_command + the P5 slice apply              click -> REAL order packet ->
//                                                 opcode-80 apply (folded mutation)
//   turn_economy / world_digest                     a REAL game-day + the world hash
//
// This module is the M5 CAPSTONE: it assembles ALL of them into ONE scripted
// end-to-end session that proves the game is actually PLAYABLE, driving the REAL
// spine the binary uses rather than re-deciding anything:
//
//   boot ->  MainMenu (REAL app::MenuRunMainMenu dispatch via play::ModeFsm)
//        ->  the player clicks New/Load (REAL gui::MainMenu_Dispatch arms word_63C740)
//        ->  the FSM lands InGame (REAL app::MenuMainDecide kRunSession)
//        ->  LOAD the real city into the live world (app::MountRealGameAssets +
//            io::LoadWorld) and run N interactive frames rendering the REAL city
//            (play::RealCityRenderer / WorldRenderer over the live g_objects)
//        ->  inject a scripted P5 ACTION (a conquer order) that MUTATES the world
//            (play::IssueWorldClick -> opcode-80 apply -> a folded record change)
//        ->  render again — the frame VISIBLY changes (a building despawns)
//        ->  the player issues QUIT (REAL ESC -> dword_63CC48) -> the spine's outer
//            loop exits cleanly (REAL MenuMainDecide kQuit, ModeFsm::done()).
//
// It collects a SESSION TRACE (the ordered FSM mode path, frames rendered, the
// menu->play transition, the action's classified kind/target, the world hash
// before/after the action, and the clean-quit flag) and is fully DETERMINISTIC:
// the SAME (city, script, seed) reproduce a byte-identical trace on every rerun.
//
// ADDITIVE (new file): it CALLS public sibling APIs only (ModeFsm, MenuRunMainMenu,
// MenuMainDecide, IssueWorldClick, RunEconomyTurn, WorldRenderer, HashFullWorld,
// MountRealGameAssets / io::LoadWorld); it does NOT edit any owned module and
// installs no global of its own beyond the per-run command-apply handler the input
// module already exposes. Headless + GUARDED: the caller supplies the filesystem +
// graphics device, and the real-asset path is gated on the city asset being present.
// =============================================================================
#include <cstdint>
#include <string>
#include <vector>

#include "guild/common/types.h"
#include "play/input_command.h"   // CursorMode
#include "play/mode_fsm.h"        // GameMode (trace vocabulary)
#include "shim/IFileSystem.h"

namespace guild::shim { class IGraphicsDevice; }

namespace guild::play {

// ===========================================================================
// The scripted SESSION SCRIPT: which menu button the player clicks to start the
// session, how many real-rendered frames to run before/after the action, the P5
// action to inject, and the seeds. Defaults drive the canonical M5 path: a New
// Game click -> a few frames -> a conquer order -> a few more frames -> quit.
// ===========================================================================
struct SessionScript {
    // -- the menu choice that arms the session (the REAL dispatch routes it) ---
    // The hovered main-menu radio slot the player clicks: 0 = New Game, 1 = Load.
    int  menuButton   = 0;            // gui radio slot (0 New Game / 1 Load)
    int  menuMaxFrames = 4;           // bound on each MenuRunMainMenu pass

    // -- the interactive in-game frames around the action -----------------------
    int  framesBeforeAction = 3;      // real-rendered frames after load, pre-action
    int  framesAfterAction  = 2;      // real-rendered frames after the action

    // -- the scripted P5 action (a world-view click -> a unit ORDER) ------------
    float      actionSX = 0.0f, actionSY = 0.0f;  // cursor (fb px); 0,0 -> first object
    float      actionPickRadius = 64.0f;
    CursorMode actionMode = CursorMode::kConquer;  // WARE conquer (kind 6): enqueues +
                                                   // applies -> a folded record change
    bool       actionAttackAllowed = false;

    // -- framebuffer geometry for the real render ------------------------------
    int  fbW = 128, fbH = 96;

    // -- determinism seeds ------------------------------------------------------
    std::uint32_t econSeed = 0xA065B;  // game-day economy + hash baseline seed
};

// ===========================================================================
// The collected SESSION TRACE — the observable proof of one full M5 run.
// ===========================================================================
struct SessionTrace {
    // -- spine / FSM (the REAL menu<->session state machine) -------------------
    bool                  bootedMenu     = false;  // FSM started in MainMenu
    bool                  menuToPlay     = false;  // MainMenu -> a session sub-mode armed
    GameMode              sessionMode    = GameMode::kMainMenu; // which session armed
    int                   sessionFlags   = 0;      // word_63C740 the menu armed
    bool                  enteredInGame  = false;  // the session went live (kInGame)
    bool                  cleanQuit      = false;  // ESC -> MenuMainDecide kQuit, FSM done()
    std::vector<GameMode> fsmPath;                 // the ordered ModeFsm transition path
    int                   menuRuns       = 0;      // MenuRunMainMenu passes run

    // -- load (the REAL city into the live world) ------------------------------
    bool          loaded       = false;
    std::uint32_t personCount  = 0;
    std::uint32_t objectCount  = 0;

    // -- the real-rendered interactive frames ----------------------------------
    int  framesRendered   = 0;        // total real-render frames (before + after)
    int  preActionFrames  = 0;
    int  postActionFrames = 0;
    int  frameBeforeNonClear = 0;     // non-clear px of the last pre-action frame
    int  frameAfterNonClear  = 0;     // non-clear px of the last post-action frame
    int  frameBeforeObjects  = 0;
    int  frameAfterObjects   = 0;
    std::string frameBeforePath;      // dumped BMP (when a FileDumpGraphicsDevice)
    std::string frameAfterPath;
    // FNV-1a over the device backbuffer of the last pre/post-action frame — a
    // content-sensitive frame witness. (The nonClear COUNT alone is blind to a
    // same-coverage layout change, e.g. the same number of uniform quads at
    // different grid cells after a despawn — wave-3 level-shaded fill.)
    std::uint64_t frameBeforeHash = 0;
    std::uint64_t frameAfterHash  = 0;

    // -- the scripted P5 action ------------------------------------------------
    bool actionIssued   = false;      // IssueWorldClick issued an order
    bool actionEnqueued = false;      // the order hit the send ring (was applied)
    int  actionKind     = 0;          // classified OrderKind
    i32  actionTarget   = 0;          // picked target entity id

    // -- the game-day the action's turn advanced -------------------------------
    int  economyPasses  = 0;

    // -- determinism oracle (world hashes; fold order = loop order) ------------
    std::uint64_t hashBeforeAction = 0;   // HashFullWorld() before the action
    std::uint64_t hashAfterAction  = 0;   // ... after the action's apply
    std::uint64_t hashAfterDay     = 0;   // ... after the game-day (== post frame world)

    // The action (its order + the day it advanced) changed the world.
    bool actionChangedWorld() const { return hashBeforeAction != hashAfterDay; }
    // The action's ORDER alone (pre game-day) mutated the world.
    bool orderChangedWorld() const { return hashBeforeAction != hashAfterAction; }

    // The full M5 invariant: booted the real menu, transitioned to play, loaded the
    // real city, rendered real frames, the action changed the world, quit cleanly.
    bool ok() const {
        return bootedMenu && menuToPlay && enteredInGame && loaded &&
               framesRendered > 0 && actionChangedWorld() && cleanQuit;
    }
};

// ===========================================================================
// RunFullSession — the whole M5 loop over a REAL city.
//
//   1. boot the REAL menu spine (play::ModeFsm + app::MenuRunMainMenu): click the
//      scripted menu button -> the REAL gui::MainMenu_Dispatch arms word_63C740 ->
//      the REAL app::MenuMainDecide routes it to a session sub-mode -> enterInGame,
//   2. LOAD the real city `<UPPER(city)>.cty` into the live sim arrays
//      (app::MountRealGameAssets + io::LoadWorld),
//   3. run `framesBeforeAction` interactive frames rendering the REAL city through
//      the real pipeline into `dev` (if non-null) — dumping the last one,
//   4. HashFullWorld() -> hashBeforeAction,
//   5. inject the scripted P5 ACTION (play::IssueWorldClick -> the opcode-80 apply,
//      mutating the picked live object) -> HashFullWorld() -> hashAfterAction,
//   6. advance ONE game-day (play::RunEconomyTurn) + the per-day witness ->
//      HashFullWorld() -> hashAfterDay,
//   7. run `framesAfterAction` real-rendered frames (the post-action world) into
//      `dev` — dumping the last one (it VISIBLY differs from the pre-action frame),
//   8. issue QUIT (REAL ESC -> the FSM's MenuMainDecide kQuit) -> the spine exits
//      cleanly (ModeFsm::done()).
//
// `dev` must already be init()'d to `script.fbW`x`script.fbH`x16bpp, or be null to
// skip the dumps (the hashes still compute). The VFS is bound for the duration and
// shut down on return; the live arrays are left with the final (post-day) world.
//
// Returns the SESSION TRACE. The SAME (fs, gameDir, city, script) reproduce a
// byte-identical trace on every call (determinism).
SessionTrace RunFullSession(shim::IFileSystem* fs, const std::string& gameDir,
                            const std::string& cityName, const SessionScript& script,
                            shim::IGraphicsDevice* dev = nullptr);

// ===========================================================================
// RunFullSessionSynthetic — the SAME scripted session arc on a SYNTHETIC live
// world (no assets, no device): boot the real menu spine -> seed a small world in
// the live arrays -> N render frames -> the P5 action -> a game-day -> N frames ->
// quit. Exposed so the unit tier can assert the FSM transitions in order, the
// action mutates the world, and quit terminates the loop — all deterministically,
// without shipped assets. `persons`/`objects` seed the synthetic world size.
// ===========================================================================
SessionTrace RunFullSessionSynthetic(std::uint32_t worldSeed, int persons, int objects,
                                     const SessionScript& script);

} // namespace guild::play
