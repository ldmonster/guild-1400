#pragma once
// gilde.exe 0x533a54 — VIBE_GameLogic_InitOrLoadSession (session bootstrap).
// Namespace guild::app.
//
// This is the NEW-GAME / LOAD / NETWORK session bootstrap: given the session
// flags (word_63C740), it sets up a fresh game — loads the city/world (the
// `<city>.cty` seed), spawns the initial population + the player, initialises
// the economy/offices, seeds the calendar clock, and enters the lockstep turn
// loop. The app spine (VIBE_GameLogic_MainEntryAndShutdown @0x534bbc) calls it
// once per player slot after the main menu chooses a session.
//
// SCOPE OF THIS MODULE. The original is a 3456-byte function whose body is a
// SEQUENCE of setup steps interleaved with the lockstep turn loop. The
// load-bearing, reconstructable property is the *ordered orchestration*: which
// setup step runs when, gated on which session-flag bit. That order is recovered
// here 1:1 as an explicit SetupStep enum (the enum value IS the invocation
// order) driven through a settable HOOK TABLE — exactly the pattern already used
// by sim/turn_driver.{h,cpp} (the per-turn orchestration) and app/app_init.cpp
// (the spine). The real city/world bootstrap (world::CityInitParameterTable),
// entity-array reset (sim::ResetEntityArrays), RNG seed (crt::Srand) and the
// calendar clock (sim::GameTimeAdvance) are REUSED, not redefined.
//
// The render / GUI / net / command-lockstep leaves the original calls
// (Loading_*, Groundplan_*, Scene_Sync*, Net_*, Command_QueueRequest*,
// RunFrameLoop, the per-round MeisterAi/Amt sync barriers) are NOT owned by this
// module; they are routed through the hook table as recorded "leaf" steps and a
// host wires the real subsystems. The DEFERRED set is listed in the .cpp.
//
// SESSION FLAGS. The flag bit constants live once, in app/gamelogic.h
// (namespace guild::app::session); this module REUSES them (no redefinition).
#include <cstdint>
#include <string>
#include <vector>

#include "app/gamelogic.h"          // guild::app::session::* flag constants
#include "guild/common/types.h"
#include "sim/types.h"              // guild::sim::GameTime (the calendar record)

namespace guild::app {

// ===========================================================================
// Session-state globals (recovered byte-for-byte from the InitOrLoadSession and
// Game_InitWorldAndSounds decompilations; offset comments give the original
// symbol + absolute address, imagebase 0x400000). These are the scattered
// run-state globals the bootstrap reads/writes; the cold-IDB static-init values
// (confirmed via get_bytes) are the field initialisers below.
// ===========================================================================
struct SessionState {
    // word_63C740 (0x63C740): the session-flag bitmask the whole bootstrap
    // branches on (see session::* in gamelogic.h). Static image: 0.
    std::uint16_t sessionFlags = 0;     // +0x00  word_63C740

    // dword_63C744 (0x63C744): difficulty (0..4). New game keeps the menu value;
    // load forces 2 (load-save) ; network-save forces 2. Static image: 0.
    std::int32_t  difficulty = 0;       // dword_63C744

    // dword_63C79C (0x63C79C): "local player is the round owner / show UI".
    // (Read by the turn loop / news scroll.) Static image: 1.
    std::int32_t  isRoundOwner = 1;     // dword_63C79C

    // byte_63CC28 (0x63CC28): run flags. Bit 0x08 => "this peer drives the
    // heavy AI/Amt passes" (host). Static image: 0.
    std::uint8_t  runFlags = 0;         // byte_63CC28

    // dword_764CE0 (0x764CE0): net standalone flag. -1 == single-player / host.
    // Static image: 0 (no socket yet — set by Net_ConnectToServer).
    std::int32_t  netStandalone = 0;    // dword_764CE0

    // word_63CC5C (0x63CC5C): index of the current player-6 (human) Person slot
    // scanned this turn; advanced by the round scan. Static image: 0.
    std::int16_t  humanPersonIndex = 0; // word_63CC5C

    // dword_63CC30 (0x63CC30): "game-over / abort" latch. When set, the turn
    // loop exits. Static image: 0.
    std::int32_t  outroShown = 0;       // dword_63CC30

    // dword_63CC34 (0x63CC34): "resolution change requested" / re-init request
    // (also the menu's player-loop counter). Static image: 0.
    std::int32_t  reinitRequest = 0;    // dword_63CC34

    // dword_63CC2C (0x63CC2C): the round counter. InitWorldAndSounds seeds it to
    // 1; the loop bounds on `< 800`. Static image: 0 (1 after world init).
    std::int32_t  roundCounter = 0;     // dword_63CC2C

    // dword_63CC20 (0x63CC20): per-round player sub-index cursor. Static image 0.
    std::int32_t  playerCursor = 0;     // dword_63CC20

    // byte_63CC41 (0x63CC41): "victory condition met this turn". Static image 0.
    std::uint8_t  victoryFlag = 0;      // byte_63CC41

    // dword_11BC2D0 (0x11BC2D0): the round sync token / last error code (also the
    // frame-loop feature-mask publish slot). Static image: 0.
    std::int32_t  syncToken = 0;        // dword_11BC2D0

    // dword_62D314 (0x62D314): "loading screen active" gate (1 during bootstrap,
    // 0 once the world is shown). Static image: 0.
    std::int32_t  loadingActive = 0;    // dword_62D314

    // ---- new-game seed inputs (the parameters a fresh game is built from) ----
    // ReturnedString (0x122EE50): the chosen city name (g_cityName), used to
    // build "gamedata/cities/<city>.cty". 1:1 with the spine's g_cityName.
    std::string   cityName = "Augsburg";        // ReturnedString @0x122EE50
    // dword_63C7B4 (0x63C7B4): cheat/debug flag — when set, the per-player
    // starting purse is forced to 75000 instead of the difficulty formula.
    std::int32_t  cheatStartGold = 0;           // dword_63C7B4
    // dword_63C7D0 (0x63C7D0): "autosave enabled". Static image: 1.
    std::int32_t  autosaveEnabled = 1;          // dword_63C7D0
    // dword_63C78C (0x63C78C): "inherit a prior dynasty" flag (enqueues an
    // inheritance-transfer command at new-game start). Static image: 0.
    std::int32_t  inheritDynasty = 0;           // dword_63C78C
    // dword_631294 (0x631294): network sync-verify id (-1 == none). Static -1.
    std::int32_t  netSyncVerifyId = -1;         // dword_631294

    // Returns true when this peer is the authoritative round driver — it runs
    // the heavy AI/Amt cascade and the host-only autosave:
    //   (runFlags & 8) != 0 || netStandalone == -1.
    bool DrivesHeavyPasses() const {
        return (runFlags & 8) != 0 || netStandalone == -1;
    }
};

// gilde.exe 0x533a64 — v112 = 131079 (0x20007): the live-game frame-loop feature
// mask the turn loop passes to RRunFrameLoop. Bits: 0x00001|0x00002|0x00004 |
// 0x20000 = input/cmd-poll + scripts? -> in fact 0x20007 = kInputCommandPoll(1)
// | bit1 | kWidgetMouse(4) | kNetworkCommand(0x20000). Reproduced verbatim.
constexpr std::uint32_t kLiveGameFrameMask = 131079; // 0x20007

// gilde.exe 0x533a5e — v112 init; round-sync token written before the sync wait.
constexpr std::int32_t  kRoundSyncToken = 147591;    // 0x24087

// gilde.exe — the day's start hour the new game's clock is set to (GameTime_Set
// to 06:00:00; the per-frame loop advances +30 min until hour reaches 23).
constexpr int kDayStartHour = 6;

// ===========================================================================
// The ordered setup steps of the session bootstrap. The enum value IS the
// invocation order for a NEW single-player game; the address comment gives the
// original call site inside VIBE_GameLogic_InitOrLoadSession @0x533a54. The
// LOAD / NETWORK branches reuse a subset (see SessionMode below); steps that
// only run on a given branch are marked.
// ===========================================================================
enum class SetupStep : int {
    // ---- mode-independent preamble ----------------------------------------
    OfficeInitTable = 0,        // 0x533a72 VIBE_Office_InitTable  (new game only)
    HistorySetInactive,         // 0x533a9c VIBE_History_SetActiveFlag(0) (network)
    NetLoadServerDll,           // 0x533ab0 LoadLibrary(Server.dll)+Init_  (host)
    NetOpenBroadcastSocket,     // 0x533b48 VIBE_Net_OpenBroadcastSocket(0x3039) (host)
    NetConnectToServer,         // 0x533b8a VIBE_Net_ConnectToServer(host,port)
    CommandQueueInitAndSync,    // 0x533b95 VIBE_Command_QueueInitAndSync
    LoadingShowProgress,        // 0x533bc6 VIBE_Loading_ShowProgressScreen (+pump)
    GameInitWorldAndSounds,     // 0x533bf6 VIBE_Game_InitWorldAndSounds (world reset+seed)
    EnqueueInheritanceTransfer, // 0x533c1a VIBE_Command_EnqueueInheritanceTransfer (new+inherit)
    // ---- mode-specific world load -----------------------------------------
    NewGameLoadCty,             // 0x533eXX VIBE_Save_LoadGameFile("<city>.cty")  (new)
    NewGameSyncScene,           // 0x533fXX Scene_SyncWorldOnEnter/Meister/ObjectHeights (new)
    LoadGameLoadSav,            // 0x533d3X VIBE_Save_LoadGameFile(g_saveName)     (load)
    NetLoadSavedNetworkGame,    // 0x533cXX VIBE_Net_LoadSavedNetworkGame          (netload)
    NetStartNetworkGame,        // 0x533cXX VIBE_Net_StartNetworkGame              (net)
    // ---- post-load common --------------------------------------------------
    CharacterEnsureGateAvatars, // 0x534XX VIBE_Character_EnsureGateAvatars
    SeedPlayerStartGold,        // 0x534XX new game: per-player EnqueueCmd15 start purse
    HistoryLoadChronicleText,   // 0x534XX VIBE_History_LoadChronicleText  (new)
    MapViewLoadBackground,      // 0x534XX VIBE_MapView_LoadBackgroundBmp
    LoadingFadeOutAndClose,     // 0x534XX VIBE_Loading_FadeOutAndClose (loadingActive=0)
    GroundplanCreateWindow,     // 0x534XX VIBE_Groundplan_CreateWindow
    SceneRefreshAndDaylight,    // 0x534XX RefreshBuildingEffects/Light_EnableDaylight/Hotkey defaults (new)
    ConfigApplyCameraScroll,    // 0x534XX VIBE_Config_ApplyCameraAndScrollSettings
    RenderSetupViewTransform,   // 0x534XX VIBE_Render_SetupViewTransform
    TutorialBuildChain,         // 0x534XX VIBE_Tutorial_BuildChapterChain (tutorial)
    // ---- the lockstep turn loop -------------------------------------------
    EnterTurnLoop,              // 0x534XX top of the while(!outro && RunFrameLoop && round<800)
    Count
};

const char* SetupStepName(SetupStep s);

// ===========================================================================
// Session mode decode. The bootstrap selects exactly one of these from the
// session-flag bitmask at entry; the decode mirrors the original branch order
// (new beats load beats net inside the &1 / &2 / &0x40 / &4 cascade).
// ===========================================================================
enum class SessionMode : int {
    NewSingle = 0,  // &1 && !&4              -> load <city>.cty
    NewNetwork,     // &1 && &4               -> Net_StartNetworkGame
    LoadSave,       // !&1 && !&0x40 && &2    -> Save_LoadGameFile(saveName)
    LoadNetSave,    // &0x40                  -> Net_LoadSavedNetworkGame
    Invalid         // none of the above (error path)
};

// Decodes the session mode from the flag bitmask exactly as the original's
// nested branch (`if (&1) { if (&4) ... else cty } else if (&0x40) ... else if
// (&2) sav`). `netStandalone` selects the network sub-branch (-1 == local).
SessionMode DecodeSessionMode(std::uint16_t flags, std::int32_t netStandalone);

// ===========================================================================
// Setup hook: every orchestration step invokes the bound function for its
// SetupStep (default: a recording no-op). `arg` carries the per-step operand the
// original passes (e.g. the difficulty for the gold formula, the faction id for
// the per-player gold command). The real city/world/RNG bootstrap is dispatched
// internally for GameInitWorldAndSounds / NewGameLoadCty regardless of the hook.
// ===========================================================================
using SetupStepFn = void (*)(SetupStep step, int arg, void* ctx);

// ===========================================================================
// Driver context: the synthetic state + bound hooks the orchestration drives.
// The driver fills `order` with the SetupStep sequence actually invoked (for the
// sequence-verification tests) and routes every step through `step`.
// ===========================================================================
struct SessionInitCtx {
    // --- bound hook (default: record into `order`) ---
    SetupStepFn step = nullptr;
    void*       stepCtx = nullptr;

    // --- new-game seed parameters (mirror the spine's INI/cmdline globals) ---
    std::string cityName = "Augsburg";   // ReturnedString @0x122EE50
    std::string profession = "";         // g_berufName @0x63C7DC (player profession)
    std::uint32_t rngSeed = 0;           // the deterministic world seed (crt::Srand)
    int         difficulty = 0;          // dword_63C744 menu value
    bool        cheatStartGold = false;  // dword_63C7B4

    // --- synthetic player population (the factions a fresh game spawns) ------
    // Each entry: a player faction the new-game gold seed iterates (the original
    // walks byte_12CE918[player] && kind in {6,7}; here a flat list of factions).
    struct Player {
        std::int32_t personId = -1;  // dword_12CE914[j] entity id
        std::uint8_t kind = 6;       // byte_12CE912[j] (6 = human, 7 = heir)
        bool         isPlayer = true;// byte_12CE918[j] (player-controlled)
    };
    std::vector<Player> players;

    // --- recorded results ---
    std::vector<SetupStep> order;          // every step invoked, in order
    SessionMode mode = SessionMode::Invalid;
    bool reachedTurnLoop = false;          // EnterTurnLoop was invoked
    std::vector<std::int32_t> startGoldByPlayer;  // gold seeded per player (in order)
    std::int32_t startGoldBase = 0;        // the pre-rate base purse used
    bool worldInited = false;              // GameInitWorldAndSounds ran (real reset)
    bool rngSeeded = false;                // crt::Srand was called
};

// ===========================================================================
// gilde.exe 0x58f19c — VIBE_Money_MultiplyByRate(amount@eax, ratePct@dl).
// The per-player starting purse is scaled by the city money-rate byte
// (byte_6477A1) via this helper: result = amount * ratePct / 100. Reproduced
// (the rate byte defaults to 100 == identity for a fresh city).
std::int32_t MoneyMultiplyByRate(std::int32_t amount, std::uint8_t ratePct);

// gilde.exe 0x533c5e — the new-game starting-purse base before the rate scale:
//   cheat -> 75000 ; else 1250 - 250 * difficulty.
std::int32_t NewGameStartGoldBase(bool cheat, int difficulty);

// ===========================================================================
// gilde.exe 0x533a54 — VIBE_GameLogic_InitOrLoadSession.
// Runs the session bootstrap for `flags` on the supplied synthetic state,
// invoking each setup step in the recovered order through ctx.step and recording
// the sequence in ctx.order. Reuses the real world reset (sim::ResetEntityArrays
// + world::CityInitParameterTable), the RNG seed (crt::Srand) and the calendar
// (sim::GameTimeAdvance); routes render/gui/net/command leaves through the hook.
// `framesPerSession` bounds the turn loop (the original blocks on the real loop;
// a bounded count keeps the bootstrap deterministic for tests/headless runs).
// ===========================================================================
void InitOrLoadSession(std::uint16_t flags, SessionInitCtx& ctx,
                       int framesPerSession);

// The single global session-state instance (mirrors the scattered globals).
SessionState& GameSessionState();

// gilde.exe 0x52f2ec — VIBE_Game_InitWorldAndSounds (the world-reset core). Re-
// inits the round/outro/victory latches + calendar seed, reuses the translated
// entity-array reset (sim::ResetEntityArrays) + economy seed (world::CityInit-
// ParameterTable). Called by InitOrLoadSession; exposed for direct testing.
void GameInitWorldAndSounds_Body(SessionInitCtx& ctx);

// The bounded lockstep turn-loop clock cascade (the day-advance arithmetic of
// the original's turn loop, using the real sim::GameTimeAdvance). `frames`
// bounds the otherwise-blocking loop.
void RunBoundedTurnLoop(SessionInitCtx& ctx, int frames);

// The bootstrap's mirror of the world calendar (qword_13CE852 @0x13CE852): a
// fresh game starts on day 2 / 06:00. Exposed for inspection in tests.
sim::GameTime& WorldClock();

} // namespace guild::app
