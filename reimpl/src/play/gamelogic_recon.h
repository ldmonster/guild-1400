#pragma once
// gilde.exe — GameLogic per-frame / per-turn ORCHESTRATORS, reconstructed 1:1.
// Namespace guild::play.
//
// This module reconstructs the EXACT control flow / gates / call order of the
// six GameLogic orchestrators from the Hex-Rays decompile:
//
//   0x4c09a0  VIBE_GameLogic_RunFrameLoop        -> RunFrameLoop
//   0x4139a8  VIBE_GameLogic_Interactions        -> Interactions
//   0x52f8d0  VIBE_GameLogic_ProcessTurnActions  -> ProcessTurnActions
//   0x5310a4  VIBE_GameLogic_RunTurnTransition   -> RunTurnTransition
//   0x530e50  VIBE_GameLogic_CleanupTurnHandlers -> CleanupTurnHandlers
//   0x52aa34  VIBE_GameLogic_SetupHomeSweetHome  -> SetupHomeSweetHome
//
// SCOPE / RULES OBSERVED
// ----------------------
//  * Rule 1 (1:1): the branch structure, the bit tests on the feature mask,
//    the run-state global predicates (dword_631638 "skip render", word_63CC5C
//    "hover slot", dword_631614 "menu-pop pending", etc.), the integer math
//    (>>16 fixed-point reads, the per-good 2/4/8 production split with its
//    div/clamp), the string-copy loops, and the call ORDER are all preserved.
//  * Rule 13 / wiring: the MANY leaf calls (input poll, fade sweeps, scene
//    walk, command apply, person/building updates, text render, ...) are routed
//    through the IGameLogicHooks struct below. The DEFAULT implementation is
//    INERT (records the call into a trace; returns benign defaults) so the
//    orchestration can be verified in isolation; a live host overrides the
//    hooks to call the already-reconstructed leaves listed in the module report
//    (input_command.cpp, menu_recon_transition.cpp fade sweeps, command_*.cpp,
//    sim/building*.cpp, world/amt*.cpp, ai/meister_events.cpp, ...).
//  * ODR: VIBE_GameLogic_RunFrameLoop (0x4c09a0) ALSO exists as the partial
//    GameApp::RunFrameLoop in src/app/frameloop.cpp (mask-gated, drops the
//    run-state predicates). This is a DIFFERENT symbol in a DIFFERENT namespace
//    (guild::play vs guild::app) and is a MORE FAITHFUL translation (keeps the
//    run-state predicates the app/ one explicitly omits). No clash.
//
//  Engine globals the original reads/writes are modeled as named fields on the
//  GameLogicState struct so the predicate logic is reconstructed verbatim. Each
//  field carries the original global address in a comment.
#include <cstdint>
#include <string>
#include <vector>

#include "guild/common/types.h"

namespace guild::play {

// ---------------------------------------------------------------------------
// Frame-loop feature-mask bits (param a1/edx -> local v54, also dword_11BC2D0).
// Mirrors src/app/gamelogic.h::mask but kept local so this file is standalone.
// ---------------------------------------------------------------------------
namespace fmask {
constexpr u32 kInputCommandPoll = 0x00000001; // bit0
constexpr u32 kWidgetMouse      = 0x00000004; // bit2
constexpr u32 kQuickJump        = 0x00000010; // bit4
constexpr u32 kHudSelection     = 0x00000020; // bit5
constexpr u32 kRenderWorld      = 0x00000040; // bit6
constexpr u32 kGameObjects      = 0x00000080; // bit7
constexpr u32 kHudMouse         = 0x00000100; // bit8
constexpr u32 kScripts          = 0x00000200; // bit9
constexpr u32 kTooltips         = 0x00000400; // bit10
constexpr u32 kHudLabels        = 0x00001000; // bit12
constexpr u32 kOptionsPanels    = 0x00002000; // bit13
constexpr u32 kWeatherSky       = 0x00004000; // bit14
constexpr u32 kCombatSelect     = 0x00008000; // bit15 (suppress)
constexpr u32 kHeadlessSuppress = 0x00010000; // bit16 (suppress)
constexpr u32 kNetworkCommand   = 0x00020000; // bit17
constexpr u32 kDayCycleMusic    = 0x00040000; // bit18
constexpr u32 kCombatScroll     = 0x00080000; // bit19
constexpr u32 kInputSuppress    = 0x00100000; // bit20 (suppress)
constexpr u32 kAutosaveSuppress = 0x00200000; // bit21 (suppress)
} // namespace fmask

// ---------------------------------------------------------------------------
// Session flags (word_63C740) read by the turn orchestrators.
// ---------------------------------------------------------------------------
namespace sflag {
constexpr u16 kNewGame   = 0x0001; // &1
constexpr u16 kNetwork   = 0x0004; // &4  (advances clock + net sync waits)
constexpr u16 kAiMeister = 0x0008; // &8  (drives the heavy AI building visibility pass)
constexpr u16 kAutoplay  = 0x0080; // &0x80 (suppresses the per-turn handler spawns)
} // namespace sflag

// ===========================================================================
// IGameLogicHooks — every leaf call the six orchestrators make.
//
// Method names follow the original call-site name (minus the VIBE_ prefix). A
// few return values feed the orchestration's own branches, so those methods
// return; the rest are void side effects. The default base records each call.
// ===========================================================================
struct IGameLogicHooks {
    virtual ~IGameLogicHooks() = default;

    // ---- RunFrameLoop leaves (0x4c09a0) ----------------------------------
    virtual void windowPumpMessages() {}                  // 0x4bea64
    virtual void inputLatchMouseState() {}                // 0x40dab8
    virtual void inputResetMouseButtonState() {}          // 0x40c87c
    virtual void widgetDispatchMouseClick() {}            // 0x421594
    virtual void hudHandleMouseClick() {}                 // 0x4bc280
    virtual void eventPanelSelectActiveSlot() {}          // 0x4c5938
    virtual void heRunMessageBoxHandlers() {}             // 0x4c6eb4
    virtual bool cutsceneProcessActive() { return false; }// 0x4ac31c
    virtual void commandFlushSendQueue() {}               // 0x4934cc
    virtual void commandReceiveAndQueue() {}              // 0x493ebc
    virtual void commandExecCommands() {}                 // 0x494088
    virtual void scriptStepAllActive(int locked) { (void)locked; } // 0x445250
    virtual void gameObjectDispatchInteractions() {}      // 0x40e6c0
    virtual void sceneGraphCullOctree() {}                // 0x5f09f0
    virtual void characterFlushPendingMesh() {}           // 0x426924
    virtual void renderRenderMainViewFrame() {}           // 0x5b6074
    virtual void weatherUpdateSky() {}                    // 0x4c0040
    virtual void fadeUpdateAll() {}                       // 0x41f47c
    virtual void fadeUnregisterAll() {}                   // 0x41f4b8
    virtual bool decompressStateBlob() { return false; }  // 0x423500
    virtual void decompressionFinalize() {}               // 0x4235dc
    virtual void inputPollMouseDevice() {}                // 0x40d388
    virtual void cameraComputeWorldTarget(u32 mask) { (void)mask; } // 0x4c0864
    virtual void renderPresentFrame() {}                  // 0x4349e4
    virtual void quickChatUpdateWindow() {}               // 0x4bff90
    virtual void inputHandleGameSpeedKeys() {}            // 0x4ff800
    virtual void hudFindModeIndex() {}                    // 0x4bea2c

    // ---- Interactions leaves (0x4139a8) ----------------------------------
    virtual void shapeAnimAdvanceFrames() {}              // 0x5d8b10
    virtual void coordPush(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; } // 0x5d8ae8
    virtual void buildingUpdate() {}                      // 0x40e2b4
    virtual void objectUpdate() {}                        // 0x40eea0
    virtual void physicsUpdate() {}                       // 0x5d8e80
    virtual void entityChildProcess() {}                  // 0x418f34
    virtual void stateGetCurrent(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; } // 0x40e728
    virtual int  stateUpdate(int handle) { (void)handle; return 0; } // 0x40e9e8
    virtual int  propertyValidate(const char* name) { (void)name; return 0; } // 0x40dfd4
    virtual void animationApply() {}                      // 0x415b78
    // ---- Wave-21: the rest of the Interactions per-record dispatch ----------
    virtual int  stateUpdateHandle(int handle) { (void)handle; return 0; }     // 0x40e9e8
    virtual void entityAnimationUpdate(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; } // 0x4244f8
    virtual void entityInteractionLogic(int nodeId) { (void)nodeId; }          // 0x41078c
    virtual void physicsUpdateRec(int label) { (void)label; }                  // 0x5d8e80
    virtual void widgetDrawScrollBar(int nodeId) { (void)nodeId; }             // 0x4121c4
    virtual void widgetDrawScrollThumb(int nodeId) { (void)nodeId; }           // 0x40ecb0
    virtual void widgetDrawCheckbox(int nodeId) { (void)nodeId; }              // 0x4137bc
    virtual void widgetBlitClippedRows(int nodeId) { (void)nodeId; }           // 0x412668
    virtual void gfxCrossFadeStep(int nodeId, int extent) { (void)nodeId;(void)extent; } // 0x41e814
    virtual void stateGetCurrent4(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; } // 0x40e728
    virtual void stateFinalize(int stateHandle) { (void)stateHandle; }         // 0x41e57c

    // ---- Turn-orchestrator leaves (0x52f8d0 / 0x5310a4 / ...) -------------
    virtual void* personGetFamilyRecord(int city) { (void)city; return nullptr; } // 0x58c408
    virtual u8   gameTimeGetSeasonFromDay() { return 0; }  // 0x58339c
    virtual void* fadeRegister(int w, int h, const char* color, int a, int b) {
        (void)w;(void)h;(void)color;(void)a;(void)b; return nullptr; }            // 0x41f0e8
    virtual void fadeUnregister(void* fade) { (void)fade; }                       // 0x41f18c
    // Returns the fade record's current flag byte (orig reads *(_BYTE*)fade & 4).
    virtual u8   fadeFlags(void* fade) { (void)fade; return 0; }
    virtual void netRunSyncWaitLoop() {}                   // 0x4beac8
    virtual void buildingPopulateOccupantList() {}         // 0x59287c
    virtual void dayCycleBuildTimeTable() {}               // 0x4b2438
    virtual u32  commandQueueRequestFlagBlob32(int phase) { (void)phase; return 0; } // 0x494ab4
    virtual bool commandGetPacketStatusById(u32 id) { (void)id; return true; }    // 0x4939d4
    virtual void amtRefreshGuildState() {}                 // 0x4becdc
    virtual void timeBaseSetProcInterval(int on) { (void)on; }                    // 0x44e3ac
    virtual bool heFindFirstHandlerByFilter(int kind) { (void)kind; return false; } // 0x4c63f8
    virtual void commandQueueRequestSlotReset28(int kind) { (void)kind; }         // 0x4948c8
    virtual void formSetObjectsVisible(int form, int vis) { (void)form;(void)vis; } // 0x41d634
    virtual void eventPanelToggleVisible() {}              // 0x4c5824
    virtual void* scrollOpen() { return nullptr; }         // 0x4be8d8
    virtual void scrollClose() {}                          // 0x4be960
    virtual void scrollUpdateAnimation() {}                // 0x4be990
    virtual void textRenderRichString(u32 id) { (void)id; }// 0x59d6e8
    virtual void windowRenderEntityList() {}               // 0x4134f0
    virtual void guiNop() {}                               // 0x413570
    virtual int  meisterAiSumApEvents() { return 0; }      // 0x4c7134
    virtual void commandQueueRequest16(int delta) { (void)delta; }                // 0x494630
    virtual void npcActionQueueRandomActions() {}          // 0x5766d4
    virtual void sceneActivateAndRefreshCharacters() {}    // 0x506df4
    virtual void groundplanSetWidgetsVisible(int v) { (void)v; }                  // 0x4ae678
    virtual void groundplanFadeInScene() {}                // 0x4b0758
    virtual void groundplanDestroyWidgets() {}             // 0x4ae828
    virtual void* personQueryBegin(int kind, int sub, int type) { (void)kind;(void)sub;(void)type; return nullptr; } // 0x586c20
    virtual void* personIterNext() { return nullptr; }     // 0x586a6c
    virtual void* gameObjectQueryFind(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; return nullptr; } // 0x5857fc
    virtual void* gameObjectIterNext() { return nullptr; } // 0x58529c
    virtual bool characterIsObjectForTurn(int rec) { (void)rec; return false; }   // 0x453228
    // 0x401894 VIBE_Character_SetVisible — the default delegates to the reconstructed
    // guild::sim::SetVisible (rule 13 bind; out-of-line in gamelogic_recon.cpp so the
    // header stays free of the character_ai dependency). With an invalid record (the
    // inert query path's rec=0) SetVisible takes the original's reportError branch.
    virtual void characterSetVisible(int rec, int vis);                           // 0x401894
    virtual int  personSumCurrencyHeld(int rec) { (void)rec; return 0; }          // 0x59152c
    virtual int  buildingMapTypeToCategory(int t) { (void)t; return 0; }          // 0x5878b0
    virtual int  buildingGetUpgradeLevel() { return 0; }   // 0x58fc84
    virtual int  buildingValueComputeRoomWorth() { return 0; }                    // 0x59116c
    virtual int  mathRandomModulo(u32 m) { (void)m; return 0; }                   // 0x58b89c
    virtual void commandEnqueueBuildingActionStart() {}    // 0x49441c
    virtual void commandEnqueueCmd15() {}                  // 0x494604
    virtual void commandEnqueueBuildingActionEnd() {}      // 0x49444c
    virtual bool characterIsActiveTypeForTurn(int rec) { (void)rec; return false; } // 0x452660
    virtual bool characterIsAiControllableForTurn(int rec) { (void)rec; return false; } // 0x4532d0
    virtual bool npcActionFindInteractionTarget(int rec) { (void)rec; return false; } // 0x4e79c0
    // 0x405504 VIBE_Character_StandUp — the default delegates to the reconstructed
    // guild::sim::StandUp (rule 13 bind; out-of-line in gamelogic_recon.cpp). The
    // original's actor is the resolved action handle dword_12CEA94[218*i]; the inert
    // retarget loop has no record table, so it passes 0 (StandUp returns early). A
    // host wiring the real record table threads the live handle here.
    virtual void characterStandUp(int actor);              // 0x405504
    virtual void charActionInsertActionVararg() {}         // 0x40c1e4
    virtual void scriptFinish() {}                         // 0x443f38
    virtual void commandQueueRequestPair33() {}            // 0x494b04
    virtual void amtBuildOfficeInfoText() {}               // 0x483414
    virtual void musicSetTrackFade() {}                    // 0x581c48

    // ---- RunTurnTransition extra leaves (0x5310a4) ------------------------
    virtual void objectSetPosition() {}                    // 0x5af38c
    virtual void objectSetWorldTranslation() {}            // 0x5af50c
    virtual void dragCursorSetSprite() {}                  // 0x41fcbc
    virtual void cameraComputeWorldTargetTurn() {}         // 0x4c0864 (turn-transition site)
    virtual void formCenterChildWindows() {}               // 0x41d6ac
    virtual void formSelectWindow(int idx) { (void)idx; }  // 0x41e4cc
    virtual void gameTimePackToRecord() {}                 // 0x583304
    virtual bool buildingValueComputeProductionWorth() { return false; } // 0x58fe68
    virtual int  buildingCollectOwnedByPerson() { return 0; } // 0x5918e0
    virtual void amtComputeOfficeWages() {}                // 0x57b480
    virtual bool taxCollectOfficeAllTaxes() { return false; } // 0x57b214
    virtual int  buildingCollectByCityHandle() { return 0; } // 0x591870
    virtual void surfaceColorFill() {}                     // 0x423b6c
    virtual void windowCreateScrollButtons() {}            // 0x419ad8
    virtual void audioStartVoiceSample() {}                // 0x447214
    virtual bool personCheckDebtRatioCritical(int rec) { (void)rec; return false; } // 0x591ff0
    virtual void cutsceneExecMainFunc() {}                 // 0x4ab55c

    // ---- CleanupTurnHandlers extra leaves (0x530e50) ----------------------
    virtual void heFreeHandlerEntry() {}                   // 0x4c6144
    virtual void lightSetGrayColorThunk() {}               // 0x5c6af0
    virtual bool cutsceneActorHasParticipant() { return false; } // 0x4abec8
    virtual void cutsceneRemoveById() {}                   // 0x4ac860

    // ---- SetupHomeSweetHome extra leaves (0x52aa34) -----------------------
    virtual void buildingSetObjectParent() {}              // 0x58820c
    virtual void buildingBuildFlagNodeList() {}            // 0x5880b4
    virtual void commandQueueRequest17() {}                // 0x49465c
    virtual void commandQueueRequestGuardTarget61() {}     // 0x49514c
    virtual void commandMarkSyncRangeStart() {}            // 0x493a1c
    virtual void commandMarkSyncRangeEnd() {}              // 0x493a28
    virtual bool commandCheckSyncRangeAcked() { return true; } // 0x493a34

    // RunFrameLoop is re-entered by the turn orchestrators (fade/scroll pump);
    // the hook lets a recorder count those re-entries without recursion.
    virtual int  runFrameLoopReentrant(u32 mask) { (void)mask; return 0; } // 0x4c09a0

    // Default trace recorder. Every method above that records does so by calling
    // this; the recording base (RecordingGameLogicHooks) overrides it to append.
    virtual void note(const char* /*name*/) {}
};

// ---------------------------------------------------------------------------
// GameLogicState — the engine run-state globals the orchestrators branch on.
// Defaults reproduce a fresh, non-network, single-player tick.
// ---------------------------------------------------------------------------
struct GameLogicState {
    // RunFrameLoop run-state.
    u32  featureMask         = 0;       // dword_11BC2D0 (published copy of a1)
    bool quit                = false;   // byte_63CC14 (window closed -> return 1)
    i32  menuPopPending      = 0;       // dword_631614 (>0 suppresses world work)
    i32  skipFrame           = 0;       // dword_631638 (skip render/sim this frame)
    bool sessionActive       = false;   // byte_63CC40
    bool inputLocked         = false;   // dword_63CC30 (modal handler owns input)
    bool decompressBusy      = false;   // dword_11BC27C
    bool renderWorldReady    = false;   // dword_631E74
    bool charactersDirty     = false;   // dword_631E70
    bool dragSelecting        = false;  // dword_11BC24C
    bool atmosWildlife        = false;  // dword_63C900
    bool atmosAmbient         = false;  // dword_63C904
    bool atmosMusic           = false;  // dword_63C8F8
    bool interactionsEnabled  = false;  // dword_63C8EC
    i16  hoverSlot            = -1;     // word_63CC5C (-1 = no hover)
    u8   optionsKey           = 0;      // byte_67225C (UI key state)
    u16  sessionFlags         = 0;      // word_63C740

    // Turn-orchestrator run-state.
    u32  turnPlayer          = 0;       // (unsigned __int16)word_63CC5C
    bool roundFadeActive     = false;   // dword_63C79C (turn fade UI shown)
    bool fadeArmed           = false;   // dword_63CC68 (set by RunTurnTransition)
    bool debtCleanup         = false;   // dword_63CC44 (triggers CleanupTurnHandlers)
    i32  formId              = -1;      // dword_631768

    // Counters mutated each frame (so evolution is observable in tests).
    i32  frameCounter        = 0;       // dword_631634
    i32  interactionCounter  = 0;       // dword_62D238

    // ---- Interactions walk (0x4139a8) state ------------------------------
    // The interaction record list (dword_62D26C[]). Engine-owned; the inert
    // default is empty (the walk body runs zero times). A host points this at
    // the live record array so the dispatch switch runs.
    struct InteractionRecord** interactionList = nullptr; // dword_62D26C
    int  interactionListCount = 0;       // number of valid (non-null) slots
    i32  focusedNodeId        = -1;      // dword_62D22C (focused/dragged node)
    int  screenW              = 800;     // (dword_69FFB8>>16)+2
    int  screenH              = 600;     // dword_69FFBC>>16
    i32  defaultBorder        = 0;       // dword_64A1A2 (packed reinit border)
    u8   freezeCountdowns     = 0;       // byte_62D25C (type 17 gate)
    int  fontHandle           = 0;       // dword_62D244 (the last font state)
};

// ===========================================================================
// InteractionRecord — one entry of the interaction list (dword_62D26C[]) walked
// by VIBE_GameLogic_Interactions (0x4139a8). Modeled by the byte offsets the
// dispatch switch reads (the same 740-byte scene-node layout the scene-update
// pass uses). All coord fields are 16.16 fixed-point (>>16 for the px value).
// ===========================================================================
struct InteractionRecord {
    i32 id        = 0;    // +0   identity (compared vs dword_62D22C)
    i32 style     = 0;    // +12  font/shape handle
    i32 x         = 0;    // +14  (16.16)
    i32 y         = 0;    // +16  (16.16)
    i32 h         = 0;    // +18  (16.16)
    i32 w         = 0;    // +20  (16.16)
    i32 clipX     = 0;    // +26  (16.16)
    i32 clipW     = 0;    // +28  (16.16)
    i32 clipH     = 0;    // +30  (16.16)
    i32 clipY     = 0;    // +32  (16.16)
    u8  typeByte  = 0;    // +24  dispatch selector
    i32 meshHandle= 0;    // +4   (must be nonzero or the record is skipped)
    i32 child44   = 0;    // +44  child anim record (0 => emit reinit border)
    i32 ctx52     = 0;    // +52  (skip if nonzero)
    i32 anim444   = 0;    // +444 anim-flag byte
    i16 lifeVal   = 0;    // +26  countdown (type 17) / content stamp
    i32 pressed   = 0;    // +40  pressed/active state
    i32 flag76    = 0;    // +76
    i32 flag88    = 0;    // +88
    i32 flag92    = 0;    // +92
    i32 hover64   = 0;    // +64
    i32 state110  = 0;    // +110 (16.16 state handle)
    const char* label116 = nullptr; // +116 label for type 0x41
    u8  label120  = 0;    // +120 inline label flag for type 0x44

    // Accessors used by the dispatch switch (named to mirror the decompile).
    i16 life() const { return lifeVal; }
    void setLife(i16 v) { lifeVal = v; }
    i32 state110OrZero() const { return state110 >> 16; }
    bool pressedFlag() const { return pressed != 0; }
    bool flag76Nonzero() const { return flag76 != 0; }
    bool flag88Nonzero() const { return flag88 != 0; }
    bool flag92Nonzero() const { return flag92 != 0; }
    bool hover64Nonzero() const { return hover64 != 0; }
};

// ===========================================================================
// The six orchestrators. `st` is the live engine state; `h` the leaf hooks.
// Return values match the original (RunFrameLoop returns the "ran a frame"
// flag; the others are void).
// ===========================================================================
int  RunFrameLoop(GameLogicState& st, IGameLogicHooks& h, u32 featureMask);
int  Interactions(GameLogicState& st, IGameLogicHooks& h);

// ---------------------------------------------------------------------------
// gilde.exe 0x41e814 — VIBE_Gfx_CrossFadeStep (the 0x44 dispatch target).
// Pure step logic; GPU leaves injected via CrossFadeHooks. See the .cpp.
// ---------------------------------------------------------------------------
struct CrossFadeRec {
    i32 surface = 0;  // [0]
    i32 stride  = 0;  // [4]
    i32 rowsLo  = 0;  // [3]
    i32 dstX    = 0;  // [2]
    i32 alpha   = 0;  // byte +28 (decompile's v3[7]): fade alpha AND teardown timer
    i32 height  = 0;  // [5]
};
struct CrossFadeHooks {
    virtual ~CrossFadeHooks() = default;
    virtual void setFadeParams(int a, int b, int c, int alpha) {
        (void)a;(void)b;(void)c;(void)alpha; }
    virtual void copyRow(int row) { (void)row; }
    virtual void teardown() {}
};
int  GfxCrossFadeStep(CrossFadeRec& rec, CrossFadeHooks& h);
void ProcessTurnActions(GameLogicState& st, IGameLogicHooks& h);
void RunTurnTransition(GameLogicState& st, IGameLogicHooks& h);
void CleanupTurnHandlers(GameLogicState& st, IGameLogicHooks& h, u32 player);
void SetupHomeSweetHome(GameLogicState& st, IGameLogicHooks& h, u32 buildingId);

// ---------------------------------------------------------------------------
// RecordingGameLogicHooks — INERT default that appends each leaf-call name to a
// trace, returning the same benign defaults as IGameLogicHooks. Used by the
// golden-vector tests to assert the exact step ORDER.
// ---------------------------------------------------------------------------
struct RecordingGameLogicHooks : IGameLogicHooks {
    std::vector<std::string> trace;
    void note(const char* name) override { trace.push_back(name); }

#define GLR_REC(method, body)                                                  \
    body

    void windowPumpMessages() override { note("windowPumpMessages"); }
    void inputLatchMouseState() override { note("inputLatchMouseState"); }
    void inputResetMouseButtonState() override { note("inputResetMouseButtonState"); }
    void widgetDispatchMouseClick() override { note("widgetDispatchMouseClick"); }
    void hudHandleMouseClick() override { note("hudHandleMouseClick"); }
    void eventPanelSelectActiveSlot() override { note("eventPanelSelectActiveSlot"); }
    void heRunMessageBoxHandlers() override { note("heRunMessageBoxHandlers"); }
    bool cutsceneProcessActive() override { note("cutsceneProcessActive"); return false; }
    void commandFlushSendQueue() override { note("commandFlushSendQueue"); }
    void commandReceiveAndQueue() override { note("commandReceiveAndQueue"); }
    void commandExecCommands() override { note("commandExecCommands"); }
    void scriptStepAllActive(int) override { note("scriptStepAllActive"); }
    void gameObjectDispatchInteractions() override { note("gameObjectDispatchInteractions"); }
    void sceneGraphCullOctree() override { note("sceneGraphCullOctree"); }
    void characterFlushPendingMesh() override { note("characterFlushPendingMesh"); }
    void renderRenderMainViewFrame() override { note("renderRenderMainViewFrame"); }
    void weatherUpdateSky() override { note("weatherUpdateSky"); }
    void fadeUpdateAll() override { note("fadeUpdateAll"); }
    void fadeUnregisterAll() override { note("fadeUnregisterAll"); }
    bool decompressStateBlob() override { note("decompressStateBlob"); return false; }
    void decompressionFinalize() override { note("decompressionFinalize"); }
    void inputPollMouseDevice() override { note("inputPollMouseDevice"); }
    void cameraComputeWorldTarget(u32) override { note("cameraComputeWorldTarget"); }
    void renderPresentFrame() override { note("renderPresentFrame"); }
    void quickChatUpdateWindow() override { note("quickChatUpdateWindow"); }
    void inputHandleGameSpeedKeys() override { note("inputHandleGameSpeedKeys"); }
    void hudFindModeIndex() override { note("hudFindModeIndex"); }

    void shapeAnimAdvanceFrames() override { note("shapeAnimAdvanceFrames"); }
    void coordPush(int, int, int, int) override { note("coordPush"); }
    void buildingUpdate() override { note("buildingUpdate"); }
    void objectUpdate() override { note("objectUpdate"); }
    void physicsUpdate() override { note("physicsUpdate"); }
    void entityChildProcess() override { note("entityChildProcess"); }
    void stateGetCurrent(int, int, int, int) override { note("stateGetCurrent"); }
    int  stateUpdate(int) override { note("stateUpdate"); return 0; }
    int  propertyValidate(const char*) override { note("propertyValidate"); return 0; }
    void animationApply() override { note("animationApply"); }
    int  stateUpdateHandle(int) override { note("stateUpdateHandle"); return 0; }
    void entityAnimationUpdate(int, int, int, int) override { note("entityAnimationUpdate"); }
    void entityInteractionLogic(int) override { note("entityInteractionLogic"); }
    void physicsUpdateRec(int) override { note("physicsUpdateRec"); }
    void widgetDrawScrollBar(int) override { note("widgetDrawScrollBar"); }
    void widgetDrawScrollThumb(int) override { note("widgetDrawScrollThumb"); }
    void widgetDrawCheckbox(int) override { note("widgetDrawCheckbox"); }
    void widgetBlitClippedRows(int) override { note("widgetBlitClippedRows"); }
    void gfxCrossFadeStep(int, int) override { note("gfxCrossFadeStep"); }
    void stateGetCurrent4(int, int, int, int) override { note("stateGetCurrent4"); }
    void stateFinalize(int) override { note("stateFinalize"); }

    void* personGetFamilyRecord(int) override { note("personGetFamilyRecord"); return nullptr; }
    u8   gameTimeGetSeasonFromDay() override { note("gameTimeGetSeasonFromDay"); return 0; }
    void* fadeRegister(int, int, const char*, int, int) override { note("fadeRegister"); return nullptr; }
    void fadeUnregister(void*) override { note("fadeUnregister"); }
    u8   fadeFlags(void*) override { note("fadeFlags"); return 4; } // armed -> loop exits immediately
    void netRunSyncWaitLoop() override { note("netRunSyncWaitLoop"); }
    void buildingPopulateOccupantList() override { note("buildingPopulateOccupantList"); }
    void dayCycleBuildTimeTable() override { note("dayCycleBuildTimeTable"); }
    u32  commandQueueRequestFlagBlob32(int) override { note("commandQueueRequestFlagBlob32"); return 0; }
    bool commandGetPacketStatusById(u32) override { note("commandGetPacketStatusById"); return true; }
    void amtRefreshGuildState() override { note("amtRefreshGuildState"); }
    void timeBaseSetProcInterval(int) override { note("timeBaseSetProcInterval"); }
    bool heFindFirstHandlerByFilter(int) override { note("heFindFirstHandlerByFilter"); return true; } // already present -> skip spawns
    void commandQueueRequestSlotReset28(int) override { note("commandQueueRequestSlotReset28"); }
    void formSetObjectsVisible(int, int) override { note("formSetObjectsVisible"); }
    void eventPanelToggleVisible() override { note("eventPanelToggleVisible"); }
    void* scrollOpen() override { note("scrollOpen"); return nullptr; }
    void scrollClose() override { note("scrollClose"); }
    void scrollUpdateAnimation() override { note("scrollUpdateAnimation"); }
    void textRenderRichString(u32) override { note("textRenderRichString"); }
    void windowRenderEntityList() override { note("windowRenderEntityList"); }
    void guiNop() override { note("guiNop"); }
    int  meisterAiSumApEvents() override { note("meisterAiSumApEvents"); return 0; }
    void commandQueueRequest16(int) override { note("commandQueueRequest16"); }
    void npcActionQueueRandomActions() override { note("npcActionQueueRandomActions"); }
    void sceneActivateAndRefreshCharacters() override { note("sceneActivateAndRefreshCharacters"); }
    void groundplanSetWidgetsVisible(int) override { note("groundplanSetWidgetsVisible"); }
    void groundplanFadeInScene() override { note("groundplanFadeInScene"); }
    void groundplanDestroyWidgets() override { note("groundplanDestroyWidgets"); }
    void* personQueryBegin(int, int, int) override { note("personQueryBegin"); return nullptr; }
    void* personIterNext() override { note("personIterNext"); return nullptr; }
    void* gameObjectQueryFind(int, int, int, int) override { note("gameObjectQueryFind"); return nullptr; }
    void* gameObjectIterNext() override { note("gameObjectIterNext"); return nullptr; }
    bool characterIsObjectForTurn(int) override { note("characterIsObjectForTurn"); return false; }
    void characterSetVisible(int, int) override { note("characterSetVisible"); }
    int  personSumCurrencyHeld(int) override { note("personSumCurrencyHeld"); return 0; }
    int  buildingMapTypeToCategory(int) override { note("buildingMapTypeToCategory"); return 0; }
    int  buildingGetUpgradeLevel() override { note("buildingGetUpgradeLevel"); return 0; }
    int  buildingValueComputeRoomWorth() override { note("buildingValueComputeRoomWorth"); return 0; }
    int  mathRandomModulo(u32) override { note("mathRandomModulo"); return 0; }
    void commandEnqueueBuildingActionStart() override { note("commandEnqueueBuildingActionStart"); }
    void commandEnqueueCmd15() override { note("commandEnqueueCmd15"); }
    void commandEnqueueBuildingActionEnd() override { note("commandEnqueueBuildingActionEnd"); }
    bool characterIsActiveTypeForTurn(int) override { note("characterIsActiveTypeForTurn"); return false; }
    bool characterIsAiControllableForTurn(int) override { note("characterIsAiControllableForTurn"); return false; }
    bool npcActionFindInteractionTarget(int) override { note("npcActionFindInteractionTarget"); return false; }
    void characterStandUp(int) override { note("characterStandUp"); }
    void charActionInsertActionVararg() override { note("charActionInsertActionVararg"); }
    void scriptFinish() override { note("scriptFinish"); }
    void commandQueueRequestPair33() override { note("commandQueueRequestPair33"); }
    void amtBuildOfficeInfoText() override { note("amtBuildOfficeInfoText"); }
    void musicSetTrackFade() override { note("musicSetTrackFade"); }

    void objectSetPosition() override { note("objectSetPosition"); }
    void objectSetWorldTranslation() override { note("objectSetWorldTranslation"); }
    void dragCursorSetSprite() override { note("dragCursorSetSprite"); }
    void cameraComputeWorldTargetTurn() override { note("cameraComputeWorldTargetTurn"); }
    void formCenterChildWindows() override { note("formCenterChildWindows"); }
    void formSelectWindow(int) override { note("formSelectWindow"); }
    void gameTimePackToRecord() override { note("gameTimePackToRecord"); }
    bool buildingValueComputeProductionWorth() override { note("buildingValueComputeProductionWorth"); return false; }
    int  buildingCollectOwnedByPerson() override { note("buildingCollectOwnedByPerson"); return 0; }
    void amtComputeOfficeWages() override { note("amtComputeOfficeWages"); }
    bool taxCollectOfficeAllTaxes() override { note("taxCollectOfficeAllTaxes"); return false; }
    int  buildingCollectByCityHandle() override { note("buildingCollectByCityHandle"); return 0; }
    void surfaceColorFill() override { note("surfaceColorFill"); }
    void windowCreateScrollButtons() override { note("windowCreateScrollButtons"); }
    void audioStartVoiceSample() override { note("audioStartVoiceSample"); }
    bool personCheckDebtRatioCritical(int) override { note("personCheckDebtRatioCritical"); return false; }
    void cutsceneExecMainFunc() override { note("cutsceneExecMainFunc"); }

    void heFreeHandlerEntry() override { note("heFreeHandlerEntry"); }
    void lightSetGrayColorThunk() override { note("lightSetGrayColorThunk"); }
    bool cutsceneActorHasParticipant() override { note("cutsceneActorHasParticipant"); return false; }
    void cutsceneRemoveById() override { note("cutsceneRemoveById"); }

    void buildingSetObjectParent() override { note("buildingSetObjectParent"); }
    void buildingBuildFlagNodeList() override { note("buildingBuildFlagNodeList"); }
    void commandQueueRequest17() override { note("commandQueueRequest17"); }
    void commandQueueRequestGuardTarget61() override { note("commandQueueRequestGuardTarget61"); }
    void commandMarkSyncRangeStart() override { note("commandMarkSyncRangeStart"); }
    void commandMarkSyncRangeEnd() override { note("commandMarkSyncRangeEnd"); }
    bool commandCheckSyncRangeAcked() override { note("commandCheckSyncRangeAcked"); return true; }

    int  runFrameLoopReentrant(u32) override { note("runFrameLoopReentrant"); return 0; }
#undef GLR_REC
};

} // namespace guild::play
