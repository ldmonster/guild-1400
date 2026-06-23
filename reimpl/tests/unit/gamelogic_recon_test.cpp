// Golden-vector tests for the GameLogic orchestrators (src/play/gamelogic_recon).
// Each test drives one orchestrator with RecordingGameLogicHooks and asserts the
// EXACT leaf-call sequence and the run-state mutations against the values read
// from the Hex-Rays decompile. Self-contained; no game assets.
#include "tests/framework/test.h"

#include <string>
#include <vector>

#include "play/gamelogic_recon.h"

using namespace guild::play;
namespace M = guild::play::fmask;
namespace S = guild::play::sflag;

namespace {

// Is `needle` a subsequence of `trace` (order preserved, gaps allowed)?
bool isSubsequence(const std::vector<std::string>& trace,
                   const std::vector<std::string>& needle) {
    size_t j = 0;
    for (const auto& s : trace) {
        if (j < needle.size() && s == needle[j]) ++j;
    }
    return j == needle.size();
}

bool contains(const std::vector<std::string>& trace, const std::string& name) {
    for (const auto& s : trace) if (s == name) return true;
    return false;
}

int count(const std::vector<std::string>& trace, const std::string& name) {
    int n = 0; for (const auto& s : trace) if (s == name) ++n; return n;
}

} // namespace

// ---------------------------------------------------------------------------
// RunFrameLoop — always-run pump happens first, in order.
// ---------------------------------------------------------------------------
TEST(GameLogicReconFrame, PumpThenLatchAlwaysFirst) {
    GameLogicState st;
    RecordingGameLogicHooks h;
    int r = RunFrameLoop(st, h, 0);
    // The first two calls are always PumpMessages then LatchMouseState.
    CHECK(h.trace.size() >= 2);
    CHECK_EQ(h.trace[0], std::string("windowPumpMessages"));
    CHECK_EQ(h.trace[1], std::string("inputLatchMouseState"));
    // No menu pop pending -> advanced-sim flag is 1.
    CHECK_EQ(r, 1);
    // Mask 0 -> no gated work after the pump (camera + input poll still run).
    CHECK(!contains(h.trace, "widgetDispatchMouseClick"));
    CHECK(!contains(h.trace, "scriptStepAllActive"));
    // frameCounter advanced exactly once.
    CHECK_EQ(st.frameCounter, 1);
}

// Window-closed at the top returns 1 immediately (only the pump ran).
TEST(GameLogicReconFrame, QuitReturnsOneNoGatedWork) {
    GameLogicState st; st.quit = true;
    RecordingGameLogicHooks h;
    int r = RunFrameLoop(st, h, 0xFFFFFFFFu);
    CHECK_EQ(r, 1);
    CHECK_EQ(h.trace.size(), static_cast<size_t>(2)); // pump + latch only
    CHECK(!contains(h.trace, "widgetDispatchMouseClick"));
    CHECK_EQ(st.frameCounter, 0); // returned before the counter bump
}

// Mask bits gate the matching leaves; order is preserved.
TEST(GameLogicReconFrame, MaskBitsGateAndOrder) {
    GameLogicState st;
    st.sessionActive = true;
    RecordingGameLogicHooks h;
    const guild::u32 mask = M::kWidgetMouse | M::kInputCommandPoll | M::kScripts |
                            M::kNetworkCommand;
    RunFrameLoop(st, h, mask);

    CHECK(contains(h.trace, "widgetDispatchMouseClick"));
    CHECK(contains(h.trace, "heRunMessageBoxHandlers"));
    CHECK(contains(h.trace, "commandFlushSendQueue"));
    CHECK(contains(h.trace, "commandExecCommands"));
    CHECK(contains(h.trace, "scriptStepAllActive"));
    // Verified call order: widget mouse -> input/cmd poll -> network pump -> scripts.
    CHECK(isSubsequence(h.trace, {"widgetDispatchMouseClick",
                                  "heRunMessageBoxHandlers",
                                  "commandFlushSendQueue",
                                  "commandReceiveAndQueue",
                                  "commandExecCommands",
                                  "scriptStepAllActive"}));
    // Network pump triad runs in the fixed flush->receive->exec order.
    CHECK(isSubsequence(h.trace, {"commandFlushSendQueue",
                                  "commandReceiveAndQueue",
                                  "commandExecCommands"}));
}

// Input-suppress bit blocks HUD mouse even with HudMouse set.
TEST(GameLogicReconFrame, InputSuppressBlocksHudMouse) {
    GameLogicState st;
    RecordingGameLogicHooks h;
    RunFrameLoop(st, h, M::kHudMouse | M::kInputSuppress);
    CHECK(!contains(h.trace, "hudHandleMouseClick"));

    RecordingGameLogicHooks h2; GameLogicState st2;
    RunFrameLoop(st2, h2, M::kHudMouse);
    CHECK(contains(h2.trace, "hudHandleMouseClick"));
}

// Render world only runs when not skipping AND world is ready.
TEST(GameLogicReconFrame, RenderWorldGatedByReadyAndSkip) {
    GameLogicState st; st.renderWorldReady = true;
    RecordingGameLogicHooks h;
    RunFrameLoop(st, h, M::kRenderWorld);
    CHECK(isSubsequence(h.trace, {"sceneGraphCullOctree",
                                  "characterFlushPendingMesh",
                                  "renderRenderMainViewFrame"}));

    // World not ready -> no render trio.
    GameLogicState st2; st2.renderWorldReady = false;
    RecordingGameLogicHooks h2;
    RunFrameLoop(st2, h2, M::kRenderWorld);
    CHECK(!contains(h2.trace, "renderRenderMainViewFrame"));
}

// Interactions re-entered when GameObjects gated, interactions enabled, and a
// decompress blob is present (decompressStateBlob true on host; recorder false
// -> falls to the fade-unregister branch). Verify the false-branch path.
TEST(GameLogicReconFrame, GameObjectsFalseBlobReleasesFades) {
    GameLogicState st; st.decompressBusy = true;
    RecordingGameLogicHooks h; // decompressStateBlob() -> false
    RunFrameLoop(st, h, M::kGameObjects);
    CHECK(contains(h.trace, "fadeUnregisterAll"));
    CHECK(!contains(h.trace, "fadeUpdateAll"));
}

// ---------------------------------------------------------------------------
// Interactions — head/tail bookkeeping is fixed regardless of list contents.
// ---------------------------------------------------------------------------
TEST(GameLogicReconInteract, HeadTailOrder) {
    GameLogicState st;
    RecordingGameLogicHooks h;
    int r = Interactions(st, h);
    // First call is the shape-anim advance; then the final coord reset, font
    // validate, and state update in that order.
    CHECK_EQ(h.trace.front(), std::string("shapeAnimAdvanceFrames"));
    CHECK(isSubsequence(h.trace, {"shapeAnimAdvanceFrames",
                                  "coordPush",
                                  "propertyValidate",
                                  "stateUpdate"}));
    CHECK_EQ(r, 0);                 // stateUpdate(font) default
    CHECK_EQ(st.interactionCounter, 1); // ++dword_62D238
}

// ---------------------------------------------------------------------------
// ProcessTurnActions — the conditional event-handler spawns + close-out.
// ---------------------------------------------------------------------------
TEST(GameLogicReconTurn, HandlersPresentNoSpawn) {
    GameLogicState st; st.turnPlayer = 3;
    // RecordingGameLogicHooks.heFindFirstHandlerByFilter -> true => no spawn,
    // commandGetPacketStatusById -> true => the ack spin runs zero times.
    RecordingGameLogicHooks h;
    ProcessTurnActions(st, h);
    CHECK(!contains(h.trace, "commandQueueRequestSlotReset28"));
    // Always-run skeleton present, in order.
    CHECK(isSubsequence(h.trace, {"personGetFamilyRecord",
                                  "gameTimeGetSeasonFromDay",
                                  "buildingPopulateOccupantList",
                                  "dayCycleBuildTimeTable",
                                  "commandQueueRequestFlagBlob32",
                                  "timeBaseSetProcInterval",
                                  "sceneActivateAndRefreshCharacters",
                                  "amtBuildOfficeInfoText",
                                  "musicSetTrackFade"}));
    // The "process interval on" precedes the handler-filter probes.
    CHECK(isSubsequence(h.trace, {"timeBaseSetProcInterval",
                                  "heFindFirstHandlerByFilter"}));
}

// When no handler exists, the spawns fire (slot-reset 28). Override the probe.
TEST(GameLogicReconTurn, MissingHandlersSpawn) {
    struct NoHandlers : RecordingGameLogicHooks {
        bool heFindFirstHandlerByFilter(int) override {
            note("heFindFirstHandlerByFilter"); return false;
        }
    } h;
    GameLogicState st;
    ProcessTurnActions(st, h);
    // 124 + 132 always-spawn (133 gated separately, 102/112/104 gated by autoplay
    // off -> also spawn). At least the unconditional ones fired.
    CHECK(count(h.trace, "commandQueueRequestSlotReset28") >= 2);
    CHECK(isSubsequence(h.trace, {"lightSetGrayColorThunk",
                                  "commandQueueRequestSlotReset28"}));
}

// Network flag inserts the two sync-wait barriers.
TEST(GameLogicReconTurn, NetworkInsertsSyncWaits) {
    GameLogicState st; st.sessionFlags = S::kNetwork;
    RecordingGameLogicHooks h;
    ProcessTurnActions(st, h);
    CHECK_EQ(count(h.trace, "netRunSyncWaitLoop"), 2);

    GameLogicState st2; // no network
    RecordingGameLogicHooks h2;
    ProcessTurnActions(st2, h2);
    CHECK_EQ(count(h2.trace, "netRunSyncWaitLoop"), 0);
}

// Round-fade UI: the scroll report sub-loop only runs when the fade is shown.
TEST(GameLogicReconTurn, RoundFadeShowsReportScroll) {
    GameLogicState st; st.roundFadeActive = true;
    RecordingGameLogicHooks h;
    ProcessTurnActions(st, h);
    CHECK(contains(h.trace, "scrollOpen"));
    CHECK(contains(h.trace, "scrollClose"));

    GameLogicState st2; st2.roundFadeActive = false;
    RecordingGameLogicHooks h2;
    ProcessTurnActions(st2, h2);
    CHECK(!contains(h2.trace, "scrollOpen"));
}

// ---------------------------------------------------------------------------
// RunTurnTransition — fade-out, balance-sheet scroll, accumulator close,
// optional debt cutscene.
// ---------------------------------------------------------------------------
TEST(GameLogicReconTransition, ClearsDecompressAndArmsFade) {
    GameLogicState st; st.decompressBusy = true; st.roundFadeActive = true;
    RecordingGameLogicHooks h;
    RunTurnTransition(st, h);
    CHECK_EQ(st.decompressBusy, false);       // dword_11BC27C = 0
    CHECK_EQ(st.fadeArmed, true);             // dword_63CC68 = 1 inside report
    CHECK(isSubsequence(h.trace, {"personGetFamilyRecord",
                                  "objectSetPosition",
                                  "objectSetWorldTranslation",
                                  "scrollOpen",
                                  "scrollClose"}));
}

// No round fade -> no report scroll, no UI; family record + object placement
// still happen, then the debt check.
TEST(GameLogicReconTransition, NoFadeSkipsReport) {
    GameLogicState st; st.roundFadeActive = false;
    RecordingGameLogicHooks h;
    RunTurnTransition(st, h);
    CHECK(!contains(h.trace, "scrollOpen"));
    CHECK(contains(h.trace, "personCheckDebtRatioCritical"));
    CHECK(contains(h.trace, "objectSetWorldTranslation"));
}

// Critical debt triggers the cutscene; cleanup only when the debt flag is set.
TEST(GameLogicReconTransition, CriticalDebtRunsCutsceneAndCleanup) {
    struct Critical : RecordingGameLogicHooks {
        bool personCheckDebtRatioCritical(int) override {
            note("personCheckDebtRatioCritical"); return true;
        }
    } h;
    GameLogicState st; st.debtCleanup = true; st.turnPlayer = 2;
    RunTurnTransition(st, h);
    CHECK(contains(h.trace, "cutsceneExecMainFunc"));
    // CleanupTurnHandlers ran (its tail light call) and reset the flag.
    CHECK_EQ(st.debtCleanup, false);

    // Same critical debt but no cleanup flag -> cutscene runs, no cleanup.
    struct Critical2 : RecordingGameLogicHooks {
        bool personCheckDebtRatioCritical(int) override {
            note("personCheckDebtRatioCritical"); return true;
        }
    } h2;
    GameLogicState st2; st2.debtCleanup = false;
    RunTurnTransition(st2, h2);
    CHECK(contains(h2.trace, "cutsceneExecMainFunc"));
}

// ---------------------------------------------------------------------------
// CleanupTurnHandlers — no active slot locks input; the recorder reports no
// matching slot so it takes the lock path.
// ---------------------------------------------------------------------------
TEST(GameLogicReconCleanup, NoSlotLocksInput) {
    GameLogicState st; st.inputLocked = false;
    RecordingGameLogicHooks h;
    CleanupTurnHandlers(st, h, 0);
    // The inert slot scan finds nothing -> dword_63CC30 = 1.
    CHECK_EQ(st.inputLocked, true);
    // Took the early-return path: no handler-free / cutscene-remove work.
    CHECK(!contains(h.trace, "heFreeHandlerEntry"));
    CHECK(!contains(h.trace, "cutsceneRemoveById"));
}

// ---------------------------------------------------------------------------
// SetupHomeSweetHome — with inert queries (no entities) nothing fires; the
// function still runs cleanly. Then a single-type host stub fires its burst.
// ---------------------------------------------------------------------------
TEST(GameLogicReconSetup, InertNoEntitiesNoCommands) {
    GameLogicState st;
    RecordingGameLogicHooks h; // personQueryBegin -> null for every type
    SetupHomeSweetHome(st, h, 100);
    CHECK(!contains(h.trace, "buildingSetObjectParent"));
    CHECK(!contains(h.trace, "commandQueueRequest17"));
    CHECK(!contains(h.trace, "commandQueueRequestGuardTarget61"));
    // Every starter type was probed exactly once (the 12 QueryBegin sites:
    // types 6, 32, 42, 30, 31, 18, 20, 50, 33, 25, 52, 54).
    CHECK(count(h.trace, "personQueryBegin") == 12);
}

// House present -> "Home-Sweet-Home" naming + the 4 opening action requests.
TEST(GameLogicReconSetup, HousePresentQueuesOpeningBurst) {
    struct HouseOnly : RecordingGameLogicHooks {
        // Return non-null only for the type-6 house query (the first call).
        int calls = 0;
        void* personQueryBegin(int a, int b, int c) override {
            note("personQueryBegin");
            return (calls++ == 0) ? reinterpret_cast<void*>(0x1) : nullptr;
            (void)a;(void)b;(void)c;
        }
    } h;
    GameLogicState st;
    SetupHomeSweetHome(st, h, 100);
    CHECK(contains(h.trace, "buildingSetObjectParent"));
    // The house branch queues exactly four QueueRequest17 action bursts.
    CHECK_EQ(count(h.trace, "commandQueueRequest17"), 4);
}

// Guard present -> three guard-target spawns (the for(i<3) loop).
TEST(GameLogicReconSetup, GuardPresentSpawnsThreeTargets) {
    struct GuardOnly : RecordingGameLogicHooks {
        int calls = 0;
        void* personQueryBegin(int, int, int) override {
            note("personQueryBegin");
            // type 6 (call 0) null, type 32 (call 1) present, rest null.
            return (calls++ == 1) ? reinterpret_cast<void*>(0x1) : nullptr;
        }
    } h;
    GameLogicState st;
    SetupHomeSweetHome(st, h, 100);
    CHECK_EQ(count(h.trace, "commandQueueRequestGuardTarget61"), 3);
}

// ===========================================================================
// Wave-21 — Interactions per-record dispatch + Gfx_CrossFadeStep.
// ===========================================================================

// Empty interaction list -> the walk body runs zero times; only the trailing
// font-finalize bookkeeping runs.
TEST(GameLogicReconInteractions, EmptyListBookkeepingOnly) {
    RecordingGameLogicHooks h;
    GameLogicState st;  // interactionList == nullptr
    Interactions(st, h);
    CHECK(!contains(h.trace, "objectUpdate"));
    CHECK(contains(h.trace, "propertyValidate"));   // _FONT validate
    CHECK(contains(h.trace, "stateUpdate"));        // font state
    CHECK_EQ(st.interactionCounter, 1);             // ++dword_62D238
}

// Helper: run Interactions over a single synthetic record of a given type.
static void RunOne(InteractionRecord& r, RecordingGameLogicHooks& h) {
    InteractionRecord* list[1] = {&r};
    GameLogicState st;
    st.interactionList = list;
    st.interactionListCount = 1;
    r.meshHandle = 1;     // pass the skip gate
    Interactions(st, h);
}

TEST(GameLogicReconInteractions, DispatchObjectUpdate) {
    InteractionRecord r; r.typeByte = 0x41;
    RecordingGameLogicHooks h; RunOne(r, h);
    CHECK(contains(h.trace, "objectUpdate"));
}
TEST(GameLogicReconInteractions, DispatchBuilding) {
    InteractionRecord r; r.typeByte = 0x42;
    RecordingGameLogicHooks h; RunOne(r, h);
    CHECK(contains(h.trace, "buildingUpdate"));
}
TEST(GameLogicReconInteractions, DispatchEntityChild) {
    InteractionRecord r; r.typeByte = 0x40;
    RecordingGameLogicHooks h; RunOne(r, h);
    CHECK(contains(h.trace, "entityChildProcess"));
}
TEST(GameLogicReconInteractions, DispatchCrossFadeAt0x44) {
    InteractionRecord r; r.typeByte = 0x44;
    RecordingGameLogicHooks h; RunOne(r, h);
    CHECK(contains(h.trace, "gfxCrossFadeStep"));
}
TEST(GameLogicReconInteractions, DispatchInteractionLogicAt0x45) {
    InteractionRecord r; r.typeByte = 0x45;
    RecordingGameLogicHooks h; RunOne(r, h);
    CHECK(contains(h.trace, "entityInteractionLogic"));
}
TEST(GameLogicReconInteractions, DispatchBlitClippedRowsAt0x47) {
    InteractionRecord r; r.typeByte = 71;
    RecordingGameLogicHooks h; RunOne(r, h);
    CHECK(contains(h.trace, "widgetBlitClippedRows"));
}
TEST(GameLogicReconInteractions, DispatchScrollBarAt9) {
    InteractionRecord r; r.typeByte = 9;
    RecordingGameLogicHooks h; RunOne(r, h);
    CHECK(contains(h.trace, "widgetDrawScrollBar"));
}
TEST(GameLogicReconInteractions, DispatchPhysicsAt4) {
    InteractionRecord r; r.typeByte = 4;
    RecordingGameLogicHooks h; RunOne(r, h);
    CHECK(contains(h.trace, "physicsUpdateRec"));
}
TEST(GameLogicReconInteractions, DispatchType0x43AnimApply) {
    InteractionRecord r; r.typeByte = 0x43;
    RecordingGameLogicHooks h; RunOne(r, h);
    CHECK(contains(h.trace, "animationApply"));
    CHECK(contains(h.trace, "stateFinalize"));
}

// Type 17 countdown decrements the record's life.
TEST(GameLogicReconInteractions, Type17Countdown) {
    InteractionRecord r; r.typeByte = 17; r.lifeVal = 5; r.meshHandle = 1;
    InteractionRecord* list[1] = {&r};
    GameLogicState st; st.interactionList = list; st.interactionListCount = 1;
    st.freezeCountdowns = 0;
    RecordingGameLogicHooks h;
    Interactions(st, h);
    CHECK_EQ((int)r.lifeVal, 4);
}

// Skip gate: meshHandle==0 OR ctx52!=0 -> no dispatch.
TEST(GameLogicReconInteractions, SkipGate) {
    InteractionRecord r; r.typeByte = 0x41; r.meshHandle = 0;  // empty mesh
    InteractionRecord* list[1] = {&r};
    GameLogicState st; st.interactionList = list; st.interactionListCount = 1;
    RecordingGameLogicHooks h;
    Interactions(st, h);
    CHECK(!contains(h.trace, "objectUpdate"));
}

// --- Gfx_CrossFadeStep (0x41e814) ------------------------------------------
namespace {
struct RecFade : CrossFadeHooks {
    int fadeParams = 0, rows = 0, tore = 0, lastAlpha = 0;
    void setFadeParams(int, int, int, int a) override { ++fadeParams; lastAlpha = a; }
    void copyRow(int) override { ++rows; }
    void teardown() override { ++tore; }
};
}

TEST(GfxCrossFade, NullSurfaceNoOp) {
    CrossFadeRec rec; rec.surface = 0; rec.alpha = 10;
    RecFade h;
    CHECK_EQ(GfxCrossFadeStep(rec, h), 0);
    CHECK_EQ(rec.alpha, 10);               // untouched
    CHECK_EQ(h.fadeParams, 0);
}

TEST(GfxCrossFade, AlphaAdvanceFadeParams) {
    CrossFadeRec rec; rec.surface = 1; rec.alpha = 100;
    RecFade h;
    int a = GfxCrossFadeStep(rec, h);
    CHECK_EQ(a, 108);                       // +8
    CHECK_EQ(h.fadeParams, 1);              // <= 255 -> SetFadeParams
    CHECK_EQ(h.rows, 0);
    CHECK_EQ(h.lastAlpha, 108);
}

TEST(GfxCrossFade, OverflowCopiesRows) {
    CrossFadeRec rec; rec.surface = 1; rec.alpha = 250; rec.height = 4;
    RecFade h;
    int a = GfxCrossFadeStep(rec, h);
    CHECK_EQ(a, 258);                       // > 255
    CHECK_EQ(h.fadeParams, 0);
    CHECK_EQ(h.rows, 4);                    // one copy per row
}

TEST(GfxCrossFade, TeardownPast288) {
    CrossFadeRec rec; rec.surface = 1; rec.alpha = 250; rec.height = 0; rec.age = 300;
    RecFade h;
    GfxCrossFadeStep(rec, h);
    CHECK_EQ(h.tore, 1);                    // age > 288 -> teardown
}
