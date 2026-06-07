#pragma once
// guild::gui — VIBE_Menu_RunMainMenu @0x529d08, the FULL boot->play main-menu body.
//
// gui/main_menu.{h,cpp} already recovers the LAYOUT (kMainMenuButtons y-table, x=32,
// sprite 174) and the per-button click->transition dispatch (MainMenu_Dispatch /
// MainMenu_Select + the MainMenuCommandSink). app/menu_loop.{h,cpp} owns the outer
// menu<->session driver and the input latch. MISSING — and reconstructed here 1:1 — is
// the COMPLETE function body of VIBE_Menu_RunMainMenu:
//
//   * the preamble: PumpMessages + LatchMouseState; init the optional-button ids = -1;
//     byte_67225C = 0; Script_ResetCurrentHandle; Fade_Register(BLACK,in) then spin the
//     frame loop until the fade record's bit 4 (0x10) is set; the two universe-slot scene
//     loads (Universe_SwitchActiveSlot(0/1) + Reset + Scene_LoadFromStream ChooseCity.ed3
//     / spielerauswahl.ed3); Render_SetupViewTransform; word_63C740 = 0; the 140-byte
//     dword_122F4A0 memset; Window_RenderEntityScene/List; the "MENU\MAIN_MENU" form load
//     (GameTick_Finalize) + Window_PositionAtCoord_Thunk(form, 3) + Form_SelectWindow.
//
//   * the EXACT 8-sprite-button build (gfx 174, widget+88 = 1, SetTextColor 300) in
//     creation order, with the dword_63C7CC optional trio; RadioGroup_Create(8, firstId);
//     the random CD track (dword_63C8F8); Fade out; the version text label.
//
//   * the do/while RunFrameLoop main loop with InitStateReader + the dword_672228
//     click-edge dispatch on dword_62D22C against EVERY recorded widget id (the 8 main
//     items + mission/history/network-city/credits-window/file-selector branches) with the
//     precise word_63C740 / dword_631614 / dword_63CC30 / dword_63CC48 / dword_63CC38 /
//     dword_63C798 / byte_63CC1D mutations, the byte_67225C quit signal, and the full
//     cleanup sequence.
//
// REUSE: the widget build calls the REAL reconstructed siblings (Window_Create,
// Widget_AddSpriteToWindow, Widget_SetTextColor, RadioGroup_Create, gui::MainMenu_Select).
// HOST BOUNDARIES (RunFrameLoop tick, the click-edge source, fade/audio/scene/universe,
// form/object label, the sub-screen runners) go through an installable hooks block with
// INERT DEFAULTS defined in main_menu_run.cpp so the function links in the unified build
// and is fully testable headless.
//
// ODR: this module DEFINES no global owned elsewhere. The menu session flags
// (word_63C740, dword_631614, dword_63CC30/48/38, dword_63C798, dword_63C7CC, dword_63C8F8,
// dword_672228, dword_62D22C, byte_67225C, byte_63CC1D, dword_63C760) live in many
// domain-specific reconstructions with no single canonical mutable-int home; we model the
// subset this function reads/writes as one reconstructed MainMenuRunState struct (the
// menu's own copy of the BSS words it touches), keeping the bit values byte-identical.

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// Layout constants (gilde.exe 0x529d08) — the optional-trio rows + label/CD ids.
// ===========================================================================
inline constexpr int kRunMenuButtonX      = 32;   // every AddSpriteToWindow x
inline constexpr int kRunMenuButtonSprite = 174;  // gfx id
inline constexpr int kRunMenuTextColor    = 300;  // Widget_SetTextColor arg
inline constexpr int kRunMenuRadioCount   = 8;    // RadioGroup_Create(8, ...)

// The six fixed rows (build order 0..5), then the dword_63C7CC-dependent trio.
inline constexpr int kRunMenuY_NewGame     = 10;
inline constexpr int kRunMenuY_Load        = 53;
inline constexpr int kRunMenuY_Multiplayer = 96;
inline constexpr int kRunMenuY_GameOptions = 139;
inline constexpr int kRunMenuY_Credits     = 268;
inline constexpr int kRunMenuY_Quit        = 311;
// dword_63C7CC != 0  (mission mode): mission y=139, network-city y=182, credits-window y=225
inline constexpr int kRunMenuY_Mission     = 139;
inline constexpr int kRunMenuY_NetworkCity = 182;
inline constexpr int kRunMenuY_CreditsWin  = 225;
// dword_63C7CC == 0  (normal): gfx-options y=182, sfx-options y=225
inline constexpr int kRunMenuY_GfxOptions  = 182;
inline constexpr int kRunMenuY_SfxOptions  = 225;

// word_63C740 session-flag bits (== gui::kSession* in main_menu.h; restated locally).
inline constexpr int kRunSessNewGame = 0x0001; // word_63C740 | 1
inline constexpr int kRunSessHistory = 0x0008; // word_63C740 | 8
inline constexpr int kRunSessMission = 137;    // word_63C740 = 137 (0x89: |1|8|0x80)

// The random CD-track index -> file string (dword_63C8F8 != 0, RandNext()%3).
inline constexpr int kCdTrackRittersleut    = 0; // cd1\Rittersleut.mp3
inline constexpr int kCdTrackMauerUndTor    = 1; // cd1\MauerUndTor.mp3
inline constexpr int kCdTrackKraeuterPhiol  = 2; // cd2\KraeuterUndPhiolen.mp3

// ===========================================================================
// Reconstructed menu state — the BSS words this function reads/writes.
// (The menu's own faithful copy; bit values match the original exactly.)
// ===========================================================================
struct MainMenuRunState {
    // Build-time selector (gilde.exe dword_63C7CC): nonzero => the mission/network/credits-
    // window trio replaces the gfx/sfx-options pair in the column.
    int  missionMode = 0;     // dword_63C7CC

    // CD music present (dword_63C8F8): when set a random track is loaded at startup.
    int  cdMusic = 0;         // dword_63C8F8
    int  cdTrack = 0;         // dword_63C760 (the loaded track handle; 0 == none)

    // Session/close flags the dispatch mutates.
    int  sessionFlags = 0;    // word_63C740
    int  close = 0;           // dword_631614 (a transition armed -> loop's next-frame exit)
    int  outroShown = 0;      // dword_63CC30
    int  quit = 0;            // dword_63CC48
    int  restartDisplay = 0;  // dword_63CC38
    int  historyChosen = 0;   // dword_63C798
    int  missionTutorial = 0; // byte_63CC1D (1 after a mission dialog)

    // ESC / quit-key edge (byte_67225C). The frame loop ORs it into the quit flags.
    int  escDown = 0;         // byte_67225C
};

// ===========================================================================
// Host-boundary hooks (installable; INERT DEFAULTS in the .cpp).
// ===========================================================================
// Per-frame click context (the original reads dword_672228 click edge + dword_62D22C
// hovered widget id). hoverId is the WIDGET INDEX returned by AddSpriteToWindow (the same
// ids recorded during the build), or -1 for none.
struct MainMenuRunHooks {
    virtual ~MainMenuRunHooks() = default;

    // ---- frame / window pump ----
    // VIBE_Window_PumpMessages @0x4bea64 + VIBE_Input_LatchMouseState @0x40dab8.
    virtual void PumpMessages() {}
    virtual void LatchMouseState() {}
    // VIBE_GameLogic_RunFrameLoop @0x4c09a0 — one tick. Returns nonzero to keep running.
    virtual int  RunFrameLoop() { return 0; }
    // VIBE_Window_RenderEntityScene @0x41523c / VIBE_Window_RenderEntityList @0x4134f0.
    virtual void RenderEntityScene() {}
    virtual void RenderEntityList() {}
    // VIBE_Script_ResetCurrentHandle @0x445d7c.
    virtual void ScriptResetCurrentHandle() {}

    // ---- click edge source (dword_672228 / dword_62D22C) ----
    // Whether a left-click EDGE occurred this frame (dword_672228 != 0).
    virtual bool ClickEdge(int frame) { (void)frame; return false; }
    // The hovered widget id this frame (dword_62D22C), or -1 for none.
    virtual int  HoverId(int frame) { (void)frame; return -1; }
    // Whether ESC/quit (byte_67225C == 1) is asserted this frame.
    virtual bool EscDown(int frame) { (void)frame; return false; }

    // ---- fade ----
    // VIBE_Fade_Register @0x41f0e8 (in: duration/delay from the screen-size dwords).
    // Returns the fade RECORD handle; bit 4 (0x10) of *handle is set when the fade
    // completes. The inert default returns a fade that completes on the first poll.
    virtual void* FadeRegisterIn() { return nullptr; }
    // The fade record's "complete" bit (byte0 & 0x10). Inert default: always complete.
    virtual bool  FadeComplete(void* rec) { (void)rec; return true; }
    virtual void  FadeUnregister(void* rec) { (void)rec; }
    // VIBE_Fade_Register(..., 90, 10) — the fade-OUT registered after the build.
    virtual void  FadeRegisterOut() {}

    // ---- audio ----
    // VIBE_Audio_LoadTrack @0x439ed0; returns the track handle (0 == none).
    virtual int   AudioLoadTrack(const char* path, int mode) { (void)path; (void)mode; return 0; }
    // VIBE_Audio_StartTrack @0x439f8c / StopTrack @0x43a2fc / SetGlobalVolume @0x43a3dc.
    virtual void  AudioStartTrack(int handle) { (void)handle; }
    virtual void  AudioStopTrack(int handle) { (void)handle; }
    virtual void  AudioSetGlobalVolume(int vol) { (void)vol; }
    // RandNext()%3 — the CD track selector (gilde.exe VIBE_Util_RandNext @0x5cb8bc).
    virtual int   RandMod3() { return 0; }

    // ---- 3D scene / universe ----
    // VIBE_Universe_SwitchActiveSlot @0x5b4a24 + VIBE_Universe_ResetCurrentSlot @0x5b44c4.
    virtual void  UniverseSwitchAndReset(int slot) { (void)slot; }
    // VIBE_Scene_LoadFromStream @0x5e7e38; returns nonzero on success.
    virtual int   SceneLoadFromStream(const char* path) { (void)path; return 1; }
    // VIBE_Render_SetupViewTransform @0x5af5f8.
    virtual void  SetupViewTransform() {}

    // ---- form / object-label leaves ----
    // VIBE_GameTick_Finalize(0,0,"MENU\\MAIN_MENU") @0x41beb8 — load the form. Returns the
    // form handle; the default ALSO sets the current window (g_currentWindowId) so the real
    // Widget_AddSpriteToWindow siblings build into a live window slot headless.
    virtual int   FormLoadMainMenu() { return -1; }
    // VIBE_Window_PositionAtCoord_Thunk(form, 3) @0x41d964 + VIBE_Form_SelectWindow @0x41e4cc.
    virtual void  FormPositionAndSelect(int form) { (void)form; }
    // VIBE_Form_Destroy @0x41da04.
    virtual void  FormDestroy(int form) { (void)form; }
    // VIBE_Object_SetVisibleRecursive @0x41dd98 (the form-backing window slot).
    virtual void  SetVisibleRecursive(int winSlot, int visible) { (void)winSlot; (void)visible; }
    // VIBE_Object_CreateTextLabel @0x41b494 (x, y, text) + VIBE_Object_SetColor @0x41e614.
    virtual int   CreateTextLabel(int x, int y, const char* text) { (void)x; (void)y; (void)text; return -1; }
    virtual void  ObjectSetColor(int obj, int color) { (void)obj; (void)color; }
    // VIBE_Widget_DestroyByType @0x414f98 (the version label teardown).
    virtual void  WidgetDestroyByType(int widget) { (void)widget; }
    // VIBE_RadioGroup_FreeSurface_Thunk @0x41287c.
    virtual void  RadioGroupFreeSurface(int group) { (void)group; }
    // VIBE_InitStateReader @0x412970 — refresh the radio group's hover/select state.
    // Returns the low byte == "needs a render-list pass" (the original tests (_BYTE)v26).
    virtual int   InitStateReader(int group) { (void)group; return 0; }
    // VIBE_Gui_Nop @0x413570.
    virtual void  GuiNop() {}

    // ---- sub-screen runners (each returns success where the original branches on it) ----
    virtual bool  EnterChooseCity() { return false; }          // VIBE_Menu_EnterChooseCity @0x52ee38
    virtual bool  RunLoadGame() { return false; }              // VIBE_Menu_RunLoadGame @0x56a270
    virtual bool  ChooseNetworkMode() { return false; }        // VIBE_Menu_ChooseNetworkMode @0x529a64
    virtual void  RunOptionsGfx() {}                           // VIBE_Menu_RunOptionsGfx @0x56c21c
    virtual void  RunOptionsSfx() {}                           // VIBE_Menu_RunOptionsSfx @0x56c808
    virtual void  RunOptionsGame() {}                          // VIBE_Menu_RunOptionsGame @0x56cc44
    virtual void  RunCreditsWindow() {}                        // VIBE_Menu_RunCreditsWindow @0x529c30
    virtual void  RunCreditsScroll() {}                        // VIBE_Menu_RunCreditsScroll @0x56e524
    // VIBE_Menu_BuildChooseMissionDialog @0x59b998; returns nonzero to proceed.
    virtual bool  BuildChooseMissionDialog() { return false; }
    virtual void  FormatMissionBuildingName() {}               // VIBE_Menu_FormatMissionBuildingName @0x59b8cc
    // VIBE_Menu_RunFileSelector @0x569668; returns true when a file was chosen; outName
    // receives the selected base name (used to build "\project\gfx\"+name then read the
    // city's "A - ALLGEMEIN"/"Stadtname"). The default returns false (no file picked).
    virtual bool  RunFileSelector(char* outName, int outCap) { (void)outName; (void)outCap; return false; }
    // VIBE_Map_LoadCityFile @0x528bd0 (mode 0 = single, 1 = network host).
    virtual void  MapLoadCityFile(int mode, const char* cityName) { (void)mode; (void)cityName; }
    // After dword_63CC38 (restart display) was set by a gfx-options change.
    virtual int   GfxRestartRequested() { return 0; }          // dword_63CC38
};

// Install hooks (null restores the inert defaults). Returns the previous hooks.
MainMenuRunHooks* Menu_SetRunHooks(MainMenuRunHooks* hooks);

// ===========================================================================
// Recorded build (testable): every AddSpriteToWindow / SetTextColor / label88 call.
// ===========================================================================
struct MainMenuBuiltButton {
    int  widgetId;  // the AddSpriteToWindow return value (radio slot 0..7)
    int  x;         // always 32
    int  y;         // the per-row y
    int  gfx;       // always 174
    int  textColor; // always 300
    int  label88;   // widget +88 (always 1)
};

// The recorded preamble/build/cleanup of one Menu_RunMainMenu run (for the e2e order
// assertions + the unit build assertions). Filled in by Menu_RunMainMenu.
struct MainMenuRunRecord {
    // Build:
    MainMenuBuiltButton buttons[16];
    int   buttonCount = 0;       // 8 (6 fixed + 2 or 3 in the trio overlap -> radio of 8)
    int   radioGroup = -1;       // RadioGroup_Create return
    int   radioCountArg = 0;     // == 8
    int   radioFirstButton = -1; // the NewGame widget id passed to RadioGroup_Create
    int   versionLabel = -1;     // CreateTextLabel return
    int   versionLabelColor = 0; // ObjectSetColor arg (67)
    bool  missionTrio = false;   // dword_63C7CC layout used
    int   cdTrackLoaded = -1;    // the random CD track index, or -1 if cdMusic off

    // Call-order trace (the e2e asserts a deterministic ordered sequence of tags).
    static constexpr int kMaxTrace = 256;
    const char* trace[kMaxTrace];
    int   traceCount = 0;

    // Per-button widget ids (so tests can map an action to its widget id and script a
    // click on it). Index by MainMenuRunAction.
    int   idNewGame = -1, idLoad = -1, idMultiplayer = -1, idGameOptions = -1;
    int   idCredits = -1, idQuit = -1, idGfxOptions = -1, idSfxOptions = -1;
    int   idMission = -1, idNetworkCity = -1, idCreditsWindow = -1, idHistory = -1;

    int   frames = 0;            // frame-loop iterations run
};

// ===========================================================================
// VIBE_Menu_RunMainMenu @0x529d08.
// Runs the full preamble + build + frame loop + cleanup. `st` carries the build-time
// selectors in (missionMode/cdMusic) and the resulting session/quit flags out. `rec`
// (optional) records the build + call order for tests. `maxFrames` bounds the loop for
// headless testing (the original spins until RunFrameLoop returns 0). Returns the final
// global volume (5000), matching the original's `return VIBE_Audio_SetGlobalVolume(5000)`.
int Menu_RunMainMenu(MainMenuRunState& st, MainMenuRunRecord* rec, int maxFrames);

} // namespace guild::gui
