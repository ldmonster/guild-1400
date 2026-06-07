// gilde.exe 0x533a54 — VIBE_GameLogic_InitOrLoadSession (session bootstrap).
// Namespace guild::app. See session_init.h for the design rationale.
//
// The ORCHESTRATION is translated 1:1 from the decompilation: the exact ordered
// sequence of setup steps for a NEW single-player game, plus the LOAD / NETWORK
// branch points. Each step is invoked through the SetupStep hook table so the
// sequence is verifiable; the world reset, RNG seed and calendar are wired to
// the already-translated modules.
//
// ===========================================================================
// REUSED (extern, not redefined — ODR):
//   world::CityInitParameterTable   (0x577a9c)  — economy parameter-table seed
//   sim::ResetEntityArrays          (entity.h)  — Person/Object/Scene reset
//   crt::Srand                      (rand.h)    — deterministic world RNG seed
//   sim::GameTimeAdvance            (gametime.h)— calendar advance (turn loop)
//   guild::app::session::*          (gamelogic.h)— session-flag bit constants
//
// ===========================================================================
// DEFERRED leaves (forward-declared as hook steps; NOT reconstructed here).
// These are render/GUI/net/command/voice/movie leaves that the original calls
// but which are owned by other clusters (or are OS/engine leaves). Each is
// routed through the SetupStep hook (recorded, observable) so the bootstrap's
// ORDER is intact; a host adapter wires them to the real subsystems. Addresses:
//   * Loading_ShowProgressScreen 0x52ee84 / UpdateProgressBar 0x52effc /
//     FadeOutAndClose 0x52f0bc          (GUI loading screen)        — DEFERRED
//   * Form_SelectWindow 0x41e4cc, Window_PumpMessages 0x4bea64      — DEFERRED (gui/os)
//   * Net_OpenBroadcastSocket 0x43aac0, Net_ConnectToServer 0x43b51c,
//     Net_StartNetworkGame 0x503f78, Net_LoadSavedNetworkGame 0x50442c,
//     Net_RunSyncWaitLoop 0x4beac8       (net transport)            — DEFERRED
//   * Save_LoadGameFile 0x5a7604, Save_WriteGameFile 0x5a348c,
//     Save_ReadThumbnailFile 0x56d870    (save IO)                  — DEFERRED
//   * Scene_SyncWorldOnEnter 0x50456c / SyncMeisterBuildings 0x504ce0 /
//     SyncObjectHeights 0x504e14 / SyncMovableObjects 0x504ef8,
//     Scene_RunMainFrameLoop 0x50f0c0    (scene/render sync)        — DEFERRED
//   * Groundplan_CreateWindow 0x4ae3b8 / DestroyWindow, MapView_LoadBackground-
//     Bmp 0x5438e8, Render_SetupViewTransform 0x5af5f8, Light_EnableDaylight
//     0x504a00, Sky_InitScene 0x4b1e94    (render/gui)              — DEFERRED
//   * Command_QueueInitAndSync 0x4931e0, QueueRequestFlagBlob32 0x494ab4,
//     GetPacketStatusById 0x4939d4, EnqueueCmd15 0x494604, QueueRequestPerm30
//     0x494a50, MarkSyncRangeStart/End 0x493a1c/0x493a28, CheckSyncRangeAcked
//     0x493a34, QueueRequest39 0x494c30, EnqueueInheritanceTransfer 0x5336f0
//                                          (command lockstep)       — DEFERRED
//   * MeisterAi_RequestCmd58/59/115 0x4c9330/0x4c937c/0x4c93f4,
//     Amt_RefreshGuildState 0x4becdc, Amt_AssignGuildMembers 0x480634
//                                          (AI/Amt sync barrier)    — DEFERRED
//   * History_LoadChronicleText 0x4fced0, Tutorial_BuildChapterChain 0x597bd8,
//     Dialog_*/EventPanel_* / Hotkey_AssignDefaults 0x4ff9ac (gui)  — DEFERRED
//   * GameLogic_RunFrameLoop 0x4c09a0 (the frame tick — owned by frameloop.cpp,
//     driven through the spine's RunFrameLoop)                      — DEFERRED
//
#include "app/session_init.h"

#include "crt/rand.h"
#include "sim/entity.h"
#include "sim/gametime.h"
#include "sim/types.h"
#include "world/city.h"

namespace guild::app {

// ---------------------------------------------------------------------------
// Reused-module signatures (extern; declared in their owning headers, included
// above — listed here only to make the dependency explicit at the call sites).
// world::CityInitParameterTable(float), sim::ResetEntityArrays(),
// crt::Srand(u32), sim::GameTimeAdvance(GameTime*, days, secs, mins).
// ---------------------------------------------------------------------------

SessionState& GameSessionState() {
    static SessionState s;
    return s;
}

const char* SetupStepName(SetupStep s) {
    switch (s) {
    case SetupStep::OfficeInitTable:            return "OfficeInitTable";
    case SetupStep::HistorySetInactive:         return "HistorySetInactive";
    case SetupStep::NetLoadServerDll:           return "NetLoadServerDll";
    case SetupStep::NetOpenBroadcastSocket:     return "NetOpenBroadcastSocket";
    case SetupStep::NetConnectToServer:         return "NetConnectToServer";
    case SetupStep::CommandQueueInitAndSync:    return "CommandQueueInitAndSync";
    case SetupStep::LoadingShowProgress:        return "LoadingShowProgress";
    case SetupStep::GameInitWorldAndSounds:     return "GameInitWorldAndSounds";
    case SetupStep::EnqueueInheritanceTransfer: return "EnqueueInheritanceTransfer";
    case SetupStep::NewGameLoadCty:             return "NewGameLoadCty";
    case SetupStep::NewGameSyncScene:           return "NewGameSyncScene";
    case SetupStep::LoadGameLoadSav:            return "LoadGameLoadSav";
    case SetupStep::NetLoadSavedNetworkGame:    return "NetLoadSavedNetworkGame";
    case SetupStep::NetStartNetworkGame:        return "NetStartNetworkGame";
    case SetupStep::CharacterEnsureGateAvatars: return "CharacterEnsureGateAvatars";
    case SetupStep::SeedPlayerStartGold:        return "SeedPlayerStartGold";
    case SetupStep::HistoryLoadChronicleText:   return "HistoryLoadChronicleText";
    case SetupStep::MapViewLoadBackground:      return "MapViewLoadBackground";
    case SetupStep::LoadingFadeOutAndClose:     return "LoadingFadeOutAndClose";
    case SetupStep::GroundplanCreateWindow:     return "GroundplanCreateWindow";
    case SetupStep::SceneRefreshAndDaylight:    return "SceneRefreshAndDaylight";
    case SetupStep::ConfigApplyCameraScroll:    return "ConfigApplyCameraScroll";
    case SetupStep::RenderSetupViewTransform:   return "RenderSetupViewTransform";
    case SetupStep::TutorialBuildChain:         return "TutorialBuildChain";
    case SetupStep::EnterTurnLoop:              return "EnterTurnLoop";
    default:                                    return "?";
    }
}

// gilde.exe 0x58f19c — VIBE_Money_MultiplyByRate (amount@eax, ratePct@dl).
// result = amount * ratePct / 100  (integer; rate byte defaults to 100).
std::int32_t MoneyMultiplyByRate(std::int32_t amount, std::uint8_t ratePct) {
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(amount) * ratePct / 100);
}

// gilde.exe 0x533c5e — new-game starting purse base.
//   if (dword_63C7B4)  v29 = 75000;
//   else               v29 = 1250 - 250 * dword_63C744;
std::int32_t NewGameStartGoldBase(bool cheat, int difficulty) {
    if (cheat)
        return 75000;
    return 1250 - 250 * difficulty;
}

// gilde.exe 0x533a72.. — the entry branch cascade that selects the session mode.
//   if ((flags & 1) != 0) { if ((flags & 4) != 0) net-new else cty }
//   else if ((flags & 0x40) != 0) net-load-save
//   else if ((flags & 2) != 0) load-sav
SessionMode DecodeSessionMode(std::uint16_t flags, std::int32_t netStandalone) {
    using namespace session;
    if (flags & kNewGame)
        return (flags & kNetwork) ? SessionMode::NewNetwork : SessionMode::NewSingle;
    if (flags & kLoadNetSave)
        return SessionMode::LoadNetSave;
    if (flags & kLoadSave)
        return SessionMode::LoadSave;
    (void)netStandalone;
    return SessionMode::Invalid;
}

// ---------------------------------------------------------------------------
// Hook dispatch helper: records the step into ctx.order and forwards to the
// bound hook (if any).
// ---------------------------------------------------------------------------
static void Emit(SessionInitCtx& ctx, SetupStep s, int arg = 0) {
    ctx.order.push_back(s);
    if (ctx.step)
        ctx.step(s, arg, ctx.stepCtx);
}

// ===========================================================================
// gilde.exe 0x533a54 — VIBE_GameLogic_InitOrLoadSession.
// The recovered NEW-game orchestration (LOAD/NETWORK branch points marked).
// ===========================================================================
void InitOrLoadSession(std::uint16_t flags, SessionInitCtx& ctx,
                       int framesPerSession) {
    using namespace session;
    SessionState& st = GameSessionState();
    st.sessionFlags = flags;

    // 0x533a64: v112 = 131079 — the live frame mask (kept for the turn loop).
    st.syncToken = 0;

    ctx.mode = DecodeSessionMode(flags, st.netStandalone);

    // 0x533cXX: a NEW single game keeps the menu difficulty (dword_63C744, set
    // before this function); the load/net branches override it below. We seed
    // our mirror from ctx.difficulty so the start-gold formula reads the right
    // value (the original reads the pre-set global directly).
    if ((flags & kNewGame) != 0 && (flags & kNetwork) == 0)
        st.difficulty = ctx.difficulty;

    // 0x533a6a: if ((flags & 1) && !a1)  VIBE_Office_InitTable();
    //   a1 is the per-player-loop index; the office table is built once, for the
    //   first player of a new game.
    if ((flags & kNewGame) != 0)
        Emit(ctx, SetupStep::OfficeInitTable);

    // 0x533a8e: hLibModule = 0;  if ((flags & 4) != 0) { ... network setup ... }
    if ((flags & kNetwork) != 0) {
        Emit(ctx, SetupStep::HistorySetInactive);          // History_SetActiveFlag(0)
        if ((flags & kHost) != 0) {
            // LoadLibrary(Server.dll) -> GetProcAddress("Init_") -> Init_() ->
            // Sleep(2500) -> Net_OpenBroadcastSocket(0x3039). (Error paths set
            // outroShown=1 and return; modeled as the host bring-up steps.)
            Emit(ctx, SetupStep::NetLoadServerDll);
            Emit(ctx, SetupStep::NetOpenBroadcastSocket);  // port 0x3039 = 12345
        }
    }
    // 0x533b8a: VIBE_Net_ConnectToServer(host, port[, 0])  (host/port or 0,0).
    Emit(ctx, SetupStep::NetConnectToServer);

    // 0x533b95: VIBE_Command_QueueInitAndSync();
    Emit(ctx, SetupStep::CommandQueueInitAndSync);

    // 0x533bc6: loadingActive = 1; Loading_ShowProgressScreen; Form_SelectWindow;
    //           UpdateProgressBar(-1) x2 ; PumpMessages.  (GUI loading screen.)
    st.loadingActive = 1;
    Emit(ctx, SetupStep::LoadingShowProgress);

    // 0x533bf6: VIBE_Game_InitWorldAndSounds() — the WORLD RESET + economy seed.
    // This is the heart of "set up a fresh world": it reseeds the calendar to
    // day 2 / 02:00, clears the round/outro latches, registers the day-cycle
    // clock proc, resets the He/entity tables, seeds the city economy parameter
    // table, loads the ambient sound banks, and inits the cutscene/animal/object
    // tables. We REUSE the translated leaf cores: sim::ResetEntityArrays (the
    // Person/Object/Scene arrays) + world::CityInitParameterTable (the 28-good
    // economy seed). The remaining leaves (He/EventPanel/StatusText/Animal/
    // Cutscene resets, sound-bank loads) are render/audio/gui plumbing and are
    // dispatched through the hook.
    GameInitWorldAndSounds_Body(ctx);
    Emit(ctx, SetupStep::GameInitWorldAndSounds);

    // 0x533c0e: if ((flags & 1) && dword_63C78C) EnqueueInheritanceTransfer(...,1)
    if ((flags & kNewGame) != 0 && st.inheritDynasty)
        Emit(ctx, SetupStep::EnqueueInheritanceTransfer);

    // ---- mode-specific world load (0x533c28 cascade) ----------------------
    bool worldLoadOk = true;
    switch (ctx.mode) {
    case SessionMode::NewSingle:
        // 0x533e..: sprintf("%s/%s.cty","gamedata/cities",cityName);
        //           if (!Save_LoadGameFile(path,...,1)) -> full shutdown+return.
        // The .cty seed IS the initial population + player + buildings + map. We
        // reuse the world reset (already done) and seed the deterministic RNG so
        // the spawned world is reproducible; the actual file decode is DEFERRED
        // (save IO cluster) and dispatched through the hook.
        crt::Srand(ctx.rngSeed);
        ctx.rngSeeded = true;
        Emit(ctx, SetupStep::NewGameLoadCty);
        // 0x533fXX: EnqueueInheritanceTransfer; Scene_SyncWorldOnEnter /
        //           SyncMeisterBuildings / SyncObjectHeights ; UpdateProgressBar.
        Emit(ctx, SetupStep::NewGameSyncScene);
        break;

    case SessionMode::NewNetwork:
        // 0x533ca6: if (netStandalone != -1) { if (&0x40) Net_LoadSavedNetwork-
        //           Game(byte_122F878) else Net_StartNetworkGame(.,1);
        //           difficulty = 2 } else error.
        if (st.netStandalone != -1) {
            st.difficulty = 2;
            crt::Srand(ctx.rngSeed);
            ctx.rngSeeded = true;
            Emit(ctx, (flags & kLoadNetSave) ? SetupStep::NetLoadSavedNetworkGame
                                             : SetupStep::NetStartNetworkGame);
        } else {
            worldLoadOk = false; // error: Dialog + ShutdownWorld + FadeOut + ret
        }
        break;

    case SessionMode::LoadNetSave:
        // 0x533d10: difficulty = 2; if (netStandalone != -1) Net_LoadSavedNet-
        //           workGame(saveName) else error.
        st.difficulty = 2;
        if (st.netStandalone != -1)
            Emit(ctx, SetupStep::NetLoadSavedNetworkGame);
        else
            worldLoadOk = false;
        break;

    case SessionMode::LoadSave:
        // 0x533d3X: difficulty = byte_12335BA; if (!Save_LoadGameFile(saveName,
        //           ...,1)) -> error. Then DayCycle_BuildTimeTable; Character_
        //           GetIndex_Thunk -> byte_11BC2FC.
        st.difficulty = ctx.difficulty;
        Emit(ctx, SetupStep::LoadGameLoadSav);
        break;

    case SessionMode::Invalid:
    default:
        worldLoadOk = false;
        break;
    }

    if (!worldLoadOk) {
        // The error paths (Dialog_ShowMessageBox + Game_ShutdownWorldAnd-
        // Subsystems + Loading_FadeOutAndClose, or the full 13-step teardown on
        // a .cty open failure) terminate the bootstrap before the turn loop.
        st.outroShown = 1;
        return;
    }

    // ---- post-load common (LABEL_17 @0x533fXX) ----------------------------
    // 0x534XX: VIBE_Character_EnsureGateAvatars(a2);
    Emit(ctx, SetupStep::CharacterEnsureGateAvatars);

    // 0x534XX: if ((flags & 1) && (flags & 4)==0)  seed each player's purse.
    //   base = cheat ? 75000 : 1250 - 250*difficulty ;
    //   for each player j: if (isPlayer[j] && kind in {6,7})
    //       gold = Money_MultiplyByRate(base, cityRate);
    //       EnqueueCmd15(personId[j], -1, gold, cityRate);
    if ((flags & kNewGame) != 0 && (flags & kNetwork) == 0) {
        const std::int32_t base = NewGameStartGoldBase(ctx.cheatStartGold,
                                                       st.difficulty);
        ctx.startGoldBase = base;
        const std::uint8_t cityRate = 100; // byte_6477A1 default for a fresh city
        Emit(ctx, SetupStep::SeedPlayerStartGold, base);
        for (const auto& p : ctx.players) {
            if (p.isPlayer && (p.kind == 6 || p.kind == 7)) {
                const std::int32_t gold = MoneyMultiplyByRate(base, cityRate);
                ctx.startGoldByPlayer.push_back(gold);
                // EnqueueCmd15(p.personId, -1, gold, cityRate) — DEFERRED leaf.
            }
        }
    }

    // 0x534XX: if ((flags & 1)) History_LoadChronicleText(a2);
    if ((flags & kNewGame) != 0)
        Emit(ctx, SetupStep::HistoryLoadChronicleText);

    // 0x534XX: PumpMessages; MapView_LoadBackgroundBmp; PumpMessages;
    Emit(ctx, SetupStep::MapViewLoadBackground);

    // 0x534XX: Loading_FadeOutAndClose(a2); loadingActive = 0;
    st.loadingActive = 0;
    Emit(ctx, SetupStep::LoadingFadeOutAndClose);

    // 0x534XX: Groundplan_GetWappenLabelId -> Groundplan_CreateWindow;
    //          Universe_SwitchActiveSlot(0).
    Emit(ctx, SetupStep::GroundplanCreateWindow);

    // 0x534XX: if ((flags & 1) && (flags & 0x40)==0) {
    //   Scene_RefreshBuildingEffects; Light_EnableDaylight; Hotkey_AssignDefaults }
    if ((flags & kNewGame) != 0 && (flags & kLoadNetSave) == 0)
        Emit(ctx, SetupStep::SceneRefreshAndDaylight);

    // 0x534XX: Config_ApplyCameraAndScrollSettings();
    Emit(ctx, SetupStep::ConfigApplyCameraScroll);

    // 0x534XX: Render_SetupViewTransform(... view rect / fov ...);
    Emit(ctx, SetupStep::RenderSetupViewTransform);

    // 0x534XX: if ((flags & 0x80)) { Tutorial_BuildChapterChain;
    //          Light_SetGrayColor(0,248); QueueRequestSlotReset28(...) }
    if ((flags & kTutorial) != 0)
        Emit(ctx, SetupStep::TutorialBuildChain);

    // ---- the lockstep turn loop (0x534XX) ---------------------------------
    // while (!outroShown && RunFrameLoop(0x20007,...) && roundCounter < 800) {
    //   per-round: GameTick_BeginRound or per-player turn scan; the sync
    //   barriers (MarkSyncRangeStart/End, MeisterAi_RequestCmd*), autosave
    //   (Gamedata\Saves\Autosave.SAV) every round for a single game, the
    //   per-day clock advance (+30 min until hour 23) via GameTime_Advance,
    //   then GameTick_BeginPlayerRound and roundCounter++. }
    //
    // The frame tick (RunFrameLoop) is owned by frameloop.cpp; the per-round
    // economy cascade is owned by sim/turn_driver.cpp. Here we mark that the
    // turn loop is ENTERED and run a bounded number of day-clock advances using
    // the REAL calendar (sim::GameTimeAdvance), so reaching the loop and the
    // clock arithmetic are observable. Mutations route through DEFERRED hooks.
    Emit(ctx, SetupStep::EnterTurnLoop);
    ctx.reachedTurnLoop = true;
    RunBoundedTurnLoop(ctx, framesPerSession);
}

// ---------------------------------------------------------------------------
// GameInitWorldAndSounds (gilde.exe 0x52f2ec) leaf body — the world-reset core.
// Reuses the translated entity-array reset + the city economy-parameter seed;
// the remaining He/EventPanel/StatusText/sound-bank/cutscene leaves are routed
// through the hook (they belong to other clusters). Reproduces the global
// re-init the bootstrap depends on (round/outro/victory latches, clock seed).
// ---------------------------------------------------------------------------
void GameInitWorldAndSounds_Body(SessionInitCtx& ctx) {
    SessionState& st = GameSessionState();
    // 0x52f2fX: dword_63CC2C = 1; LODWORD(qword_13CE852) = 2; dword_63CC30 = 0;
    //           dword_63CC34 = 0; byte_63CC41 = 0;
    st.roundCounter = 1;       // dword_63CC2C
    st.outroShown   = 0;       // dword_63CC30
    st.reinitRequest = 0;      // dword_63CC34
    st.victoryFlag  = 0;       // byte_63CC41
    // qword_13CE852 day = 2 (the world starts on calendar day 2). The clock is
    // owned by sim/gametime; here we reset our mirror to the new-game day/hour.
    sim::GameTime& clk = WorldClock();
    clk.day = 2;
    clk.hour = 0; clk.minute = 0; clk.second = 0;

    // 0x52f37X: City_InitParameterTable() — the 28-good economy seed (REUSED).
    world::CityInitParameterTable(1.0f);

    // The entity arrays the spawned population lives in are reset by the
    // world-reset path (Building_ResetAllBuildings / World_ResetPersonTable in
    // the load path). REUSE the translated reset so a fresh world starts empty.
    sim::ResetEntityArrays();
    ctx.worldInited = true;
}

// ---------------------------------------------------------------------------
// The bounded turn-loop clock cascade. The original's per-day path advances the
// clock +30 minutes per command until the hour reaches 23 (0x17), then rolls
// over to the next day at 06:00. We run `frames` such day-advances with the
// REAL calendar so the arithmetic is observable; the command/sync/autosave/AI
// leaves are DEFERRED (routed through the hook each iteration).
// ---------------------------------------------------------------------------
void RunBoundedTurnLoop(SessionInitCtx& ctx, int frames) {
    (void)ctx; // the per-round command/AI/render leaves are DEFERRED hooks
    SessionState& st = GameSessionState();
    sim::GameTime& clk = WorldClock();
    for (int f = 0; f < frames && !st.outroShown; ++f) {
        // Per-day: advance +30 min until hour >= 23 (the original's
        // while (WORD2(time) < 0x17) GameTime_Advance(&t,0,0,30)).
        while (clk.hour < 23)
            sim::GameTimeAdvance(&clk, 0, 0, 30);
        // Then roll to the next day at 06:00 (GameTime_Advance(+24h) then
        // GameTime_Set(06:00) in the single-player branch).
        sim::GameTimeAdvance(&clk, 24, 0, 0);
        clk.hour = kDayStartHour; clk.minute = 0; clk.second = 0;
        ++st.roundCounter;          // dword_63CC2C++ per round
        if (st.roundCounter >= 800) // loop bound
            break;
    }
}

// The single world clock mirror (qword_13CE852). Owned here as the bootstrap's
// view of the calendar; the per-frame clock proc updates the engine global.
sim::GameTime& WorldClock() {
    static sim::GameTime c{2, 0, 0, 0};
    return c;
}

} // namespace guild::app
