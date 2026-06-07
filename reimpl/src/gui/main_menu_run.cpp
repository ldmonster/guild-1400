#include "gui/main_menu_run.h"

#include "gui/main_menu.h"   // MainMenu_Select / kMainMenuButtons (reused)
#include "gui/widget_create.h" // Widget_AddSpriteToWindow (REAL sibling)
#include "gui/gui_widgetn.h"   // Widget_SetTextColor      (REAL sibling)
#include "gui/radiogroup.h"    // RadioGroup_Create        (REAL sibling)
#include "gui/object.h"        // g_widgets (set widget +88)
#include "gui/window.h"        // Window_Create / g_currentWindowId (REAL sibling)

#include <cstring>

namespace guild::gui {

// ===========================================================================
// gilde.exe 0x529d08 — the CD-track table (dword_63C8F8 != 0, RandNext()%3).
// ===========================================================================
static const char* CdTrackPath(int idx) {
    switch (idx) {
        case kCdTrackRittersleut:   return "cd1\\Rittersleut.mp3";        // v22 == 0
        case kCdTrackMauerUndTor:   return "cd1\\MauerUndTor.mp3";        // v22 == 1
        case kCdTrackKraeuterPhiol: return "cd2\\KraeuterUndPhiolen.mp3"; // v22 == 2
        default: return nullptr;
    }
}

// ===========================================================================
// Inert default hooks. The form-load default does a REAL Window_Create so the real
// Widget_AddSpriteToWindow build path runs against a live window slot headless.
// ===========================================================================
namespace {

struct DefaultRunHooks : MainMenuRunHooks {
    int  FormLoadMainMenu() override {
        // Mirror GameTick_Finalize("MENU\\MAIN_MENU"): create the form-backing window
        // and cache it as current (g_currentWindowId). 300x320 = the _MAIN_MENU_RAHMEN
        // frame size; geometry is not load-bearing for the headless build.
        int slot = Window_Create(0, 0, 300, 320, 0);
        return slot; // form handle stand-in == the window slot
    }
    void FormPositionAndSelect(int) override {}
    void FormDestroy(int form) override {
        if (form >= 0) Window_Destroy(form);
    }
};

DefaultRunHooks g_defaultRunHooks;
MainMenuRunHooks* g_runHooks = &g_defaultRunHooks;

} // namespace

MainMenuRunHooks* Menu_SetRunHooks(MainMenuRunHooks* hooks) {
    MainMenuRunHooks* prev = g_runHooks;
    g_runHooks = hooks ? hooks : &g_defaultRunHooks;
    return prev;
}

// ---- trace helper -----------------------------------------------------------
static void Trace(MainMenuRunRecord* rec, const char* tag) {
    if (rec && rec->traceCount < MainMenuRunRecord::kMaxTrace)
        rec->trace[rec->traceCount++] = tag;
}

// gilde.exe 0x529d08 — build one column button: AddSpriteToWindow(32, y, 174, win),
// widget +88 = 1 (label flag), Widget_SetTextColor(id, 300). Records into `rec`.
static int BuildButton(int y, MainMenuRunRecord* rec) {
    int id = Widget_AddSpriteToWindow(static_cast<i16>(kRunMenuButtonX),
                                      static_cast<i16>(y), kRunMenuButtonSprite,
                                      g_currentWindowId);
    if (id >= 0 && id < kMaxWidgets)
        g_widgets[id].at<i32>(88) = 1;            // *(dword*)(base + 740*id + 88) = 1
    Widget_SetTextColor(id, static_cast<i16>(kRunMenuTextColor));
    if (rec && rec->buttonCount < 16) {
        MainMenuBuiltButton& b = rec->buttons[rec->buttonCount++];
        b.widgetId = id;
        b.x = kRunMenuButtonX;
        b.y = y;
        b.gfx = kRunMenuButtonSprite;
        b.textColor = kRunMenuTextColor;
        b.label88 = (id >= 0 && id < kMaxWidgets) ? g_widgets[id].at<i32>(88) : 0;
    }
    return id;
}

// ===========================================================================
// VIBE_Menu_RunMainMenu @0x529d08.
// ===========================================================================
int Menu_RunMainMenu(MainMenuRunState& st, MainMenuRunRecord* rec, int maxFrames) {
    MainMenuRunHooks* h = g_runHooks;

    // ---- recorded build ids (these are the widget ids the dispatch matches against) ----
    // -1 == "not built this layout" (the original inits the optional slots to -1).
    int idNewGame = -1, idLoad = -1, idMultiplayer = -1, idGameOptions = -1;
    int idCredits = -1, idQuit = -1, idGfxOptions = -1, idSfxOptions = -1;
    int idMission = -1, idNetworkCity = -1, idCreditsWindow = -1, idHistory = -1;

    // ============================ PREAMBLE (in order) ============================
    h->PumpMessages();              Trace(rec, "PumpMessages");
    h->LatchMouseState();           Trace(rec, "LatchMouseState");
    // v86=v87=v88=v70=v79=v71=v81 = -1 (the optional mission/history/network/credits-win ids)
    idGfxOptions = idSfxOptions = idMission = idNetworkCity = idCreditsWindow = idHistory = -1;
    st.escDown = 0;                 // byte_67225C = 0
    h->ScriptResetCurrentHandle();  Trace(rec, "ScriptResetCurrentHandle");

    // Fade in, then spin RunFrameLoop until the fade record's bit 0x04 is set.
    void* fade = h->FadeRegisterIn(); Trace(rec, "FadeRegisterIn");
    while (!h->FadeComplete(fade)) {
        h->RunFrameLoop();
        if (maxFrames >= 0 && rec && rec->frames > maxFrames) break; // headless safety
    }
    Trace(rec, "FadeInComplete");

    // Two universe-slot scene loads.
    h->UniverseSwitchAndReset(0);   Trace(rec, "Universe0");
    h->SceneLoadFromStream("scenes/*ChooseCity.ed3"); Trace(rec, "SceneChooseCity");
    h->PumpMessages();
    h->UniverseSwitchAndReset(1);   Trace(rec, "Universe1");
    h->SceneLoadFromStream("scenes/*spielerauswahl.ed3"); Trace(rec, "SceneSpieler");

    h->SetupViewTransform();        Trace(rec, "SetupViewTransform");
    st.sessionFlags = 0;            // word_63C740 = 0
    // memset the 140-byte session-params block dword_122F4A0 (modeled as the flag reset).

    h->RenderEntityScene();
    h->RenderEntityList();

    int form = h->FormLoadMainMenu();   Trace(rec, "FormLoadMainMenu"); // "MENU\\MAIN_MENU"
    h->FormPositionAndSelect(form);     Trace(rec, "FormPositionAndSelect");
    int winSlot = g_currentWindowId;    // dword_62D230 (the form-backing window)

    // ============================ WIDGET BUILD (gfx 174) =========================
    Trace(rec, "BuildBegin");
    idNewGame     = BuildButton(kRunMenuY_NewGame, rec);     // v75 / v80
    idLoad        = BuildButton(kRunMenuY_Load, rec);        // v76 / v85
    idMultiplayer = BuildButton(kRunMenuY_Multiplayer, rec); // v19 / v82
    idGameOptions = BuildButton(kRunMenuY_GameOptions, rec); // v78 / v73
    idCredits     = BuildButton(kRunMenuY_Credits, rec);     // v69 / v83
    idQuit        = BuildButton(kRunMenuY_Quit, rec);        // v77 / v84

    if (st.missionMode) {                                    // if (dword_63C7CC)
        idMission       = BuildButton(kRunMenuY_Mission, rec);     // v66 / v88
        idNetworkCity   = BuildButton(kRunMenuY_NetworkCity, rec); // v67 / v70
        idCreditsWindow = BuildButton(kRunMenuY_CreditsWin, rec);  // v40 / v71
    } else {
        idGfxOptions    = BuildButton(kRunMenuY_GfxOptions, rec);  // v65 / v86
        idSfxOptions    = BuildButton(kRunMenuY_SfxOptions, rec);  // v20 / v87
    }
    Trace(rec, "BuildEnd");

    // RadioGroup_Create(8, firstId == NewGame).  The original passes &v75 (the first
    // button id, contiguous on its stack); we pass the 8 built radio-slot ids in order.
    int radioButtons[kRunMenuRadioCount];
    {
        int n = 0;
        radioButtons[n++] = idNewGame;
        radioButtons[n++] = idLoad;
        radioButtons[n++] = idMultiplayer;
        radioButtons[n++] = idGameOptions;
        radioButtons[n++] = idCredits;
        radioButtons[n++] = idQuit;
        if (st.missionMode) {
            radioButtons[n++] = idMission;
            radioButtons[n++] = idNetworkCity;
        } else {
            radioButtons[n++] = idGfxOptions;
            radioButtons[n++] = idSfxOptions;
        }
        // n == 8 here (RadioGroup_Create(8, ...)); the mission layout's third optional
        // button (creditsWindow) is built but NOT a radio member (only 8 radio slots).
        (void)n;
    }
    int radioGroup = RadioGroup_Create(kRunMenuRadioCount, radioButtons);
    Trace(rec, "RadioGroupCreate");

    // Random CD track (dword_63C8F8).
    int cdTrackLoaded = -1;
    if (st.cdMusic) {                       // if (dword_63C8F8)
        int v22 = h->RandMod3();            // (unsigned)RandNext() % 3
        const char* path = CdTrackPath(v22);
        if (path) {
            st.cdTrack = h->AudioLoadTrack(path, 3); // dword_63C760 = LoadTrack(path, 3)
            cdTrackLoaded = v22;
            Trace(rec, "CdTrackLoad");
        }
    }

    // Unregister the fade-in; register the fade-out (BLACK, out, 90, 10).
    if (fade) h->FadeUnregister(fade);
    h->FadeRegisterOut();           Trace(rec, "FadeRegisterOut");
    st.escDown = 0;                 // byte_67225C = 0 (again, after the build)
    h->AudioSetGlobalVolume(2000);  Trace(rec, "Volume2000");

    // Version text label (8, screenH-20, version) coloured 67.
    int versionLabel = h->CreateTextLabel(8, /*screenH-20*/ -20, "version");
    h->ObjectSetColor(versionLabel, 67);
    Trace(rec, "VersionLabel");

    // ---- record the build ----
    if (rec) {
        rec->radioGroup = radioGroup;
        rec->radioCountArg = kRunMenuRadioCount;
        rec->radioFirstButton = idNewGame;
        rec->versionLabel = versionLabel;
        rec->versionLabelColor = 67;
        rec->missionTrio = (st.missionMode != 0);
        rec->cdTrackLoaded = cdTrackLoaded;
        rec->idNewGame = idNewGame;       rec->idLoad = idLoad;
        rec->idMultiplayer = idMultiplayer; rec->idGameOptions = idGameOptions;
        rec->idCredits = idCredits;       rec->idQuit = idQuit;
        rec->idGfxOptions = idGfxOptions; rec->idSfxOptions = idSfxOptions;
        rec->idMission = idMission;       rec->idNetworkCity = idNetworkCity;
        rec->idCreditsWindow = idCreditsWindow; rec->idHistory = idHistory;
    }

    // ============================ MAIN FRAME LOOP ================================
    Trace(rec, "LoopBegin");
    int frame = 0;
    int runMore;
    char selName[96];
    do {
        // InitStateReader(radioGroup); if (state byte) RenderEntityList.
        int stateByte = h->InitStateReader(radioGroup);
        if (stateByte & 0xFF)
            h->RenderEntityList();

        // The click edge (dword_672228) + hovered widget id (dword_62D22C).
        st.escDown = h->EscDown(frame) ? 1 : 0;   // byte_67225C
        if (h->ClickEdge(frame)) {
            int hov = h->HoverId(frame);          // dword_62D22C
            if (hov != -1) {
                // --- dispatch chain (== each recorded widget id) ---
                if (hov == idNewGame) {
                    // v80: hide; Gui_Nop; if EnterChooseCity { |1; close } else re-show.
                    h->SetVisibleRecursive(winSlot, 0);
                    h->GuiNop();
                    if (h->EnterChooseCity()) {       Trace(rec, "EnterChooseCity:ok");
                        st.sessionFlags |= kRunSessNewGame; // word_63C740 |= 1
                        st.close = 1;                       // dword_631614 = 1
                    } else {                          Trace(rec, "EnterChooseCity:cancel");
                        h->SetVisibleRecursive(winSlot, 1); // LABEL_55
                    }
                } else if (hov == idMission) {
                    // v81: hide; if !BuildChooseMissionDialog re-show; else mission setup.
                    h->SetVisibleRecursive(winSlot, 0);
                    if (!h->BuildChooseMissionDialog()) { Trace(rec, "Mission:cancel");
                        h->SetVisibleRecursive(winSlot, 1); // LABEL_55
                    } else {                          Trace(rec, "Mission:ok");
                        h->FormatMissionBuildingName();
                        st.missionTutorial = 1;             // byte_63CC1D = 1 ("Tutorial")
                        st.sessionFlags = kRunSessMission;  // word_63C740 = 137
                        st.close = 1;                       // dword_631614 = 1
                    }
                } else if (hov == idNetworkCity) {
                    // v70: file-selector -> Map_LoadCityFile(1, name); re-show.
                    h->SetVisibleRecursive(winSlot, 0);
                    if (h->RunFileSelector(selName, (int)sizeof(selName))) { Trace(rec, "NetCity:load");
                        h->MapLoadCityFile(1, selName);
                    }
                    h->SetVisibleRecursive(winSlot, 1);     // LABEL_55
                } else if (hov == idHistory) {
                    // v79: file-selector -> dword_63C798=1; close; |8 then |1; re-show.
                    h->SetVisibleRecursive(winSlot, 0);
                    if (h->RunFileSelector(selName, (int)sizeof(selName))) { Trace(rec, "History:load");
                        st.historyChosen = 1;               // dword_63C798 = 1
                        st.close = 1;                       // dword_631614 = 1
                        st.sessionFlags |= kRunSessHistory; // word_63C740 |= 8
                    }
                    h->SetVisibleRecursive(winSlot, 1);
                    st.sessionFlags |= kRunSessNewGame;     // word_63C740 |= 1
                } else if (hov == idLoad) {
                    // v85: hide; if RunLoadGame close; re-show.
                    h->SetVisibleRecursive(winSlot, 0);
                    if (h->RunLoadGame()) {           Trace(rec, "RunLoadGame:ok");
                        st.close = 1;                       // dword_631614 = 1
                    }
                    h->SetVisibleRecursive(winSlot, 1);     // LABEL_55
                } else if (hov == idMultiplayer) {
                    // v82: hide; if ChooseNetworkMode close; else re-show + word_63C740=0.
                    h->SetVisibleRecursive(winSlot, 0);
                    if (h->ChooseNetworkMode()) {     Trace(rec, "Multiplayer:ok");
                        st.close = 1;                       // dword_631614 = 1
                    } else {                          Trace(rec, "Multiplayer:cancel");
                        h->SetVisibleRecursive(winSlot, 1);
                        st.sessionFlags = 0;                // word_63C740 = 0
                    }
                } else if (hov == idGfxOptions) {
                    // v86: hide; RunOptionsGfx; re-show; v89=1; if dword_63CC38 close.
                    h->SetVisibleRecursive(winSlot, 0);
                    h->RunOptionsGfx();               Trace(rec, "GfxOptions");
                    h->SetVisibleRecursive(winSlot, 1);
                    if (h->GfxRestartRequested()) {         // dword_63CC38
                        st.restartDisplay = 1;
                        st.close = 1;                       // dword_631614 = 1
                    }
                } else if (hov == idSfxOptions) {
                    // v87: hide; RunOptionsSfx; re-show.
                    h->SetVisibleRecursive(winSlot, 0);
                    h->RunOptionsSfx();               Trace(rec, "SfxOptions");
                    h->SetVisibleRecursive(winSlot, 1);
                } else if (hov == idGameOptions) {
                    // v73: hide; RunOptionsGame; re-show.
                    h->SetVisibleRecursive(winSlot, 0);
                    h->RunOptionsGame();              Trace(rec, "GameOptions");
                    h->SetVisibleRecursive(winSlot, 1);
                } else if (hov == idQuit) {
                    // v84: dword_63CC30=1; dword_63CC48=1; dword_631614=1.
                    st.outroShown = 1;                      // dword_63CC30 = 1
                    st.quit = 1;                            // dword_63CC48 = 1
                    st.close = 1;                           // dword_631614 = 1
                    Trace(rec, "Quit");
                } else if (hov == idCreditsWindow) {
                    // v71: RunCreditsWindow (no flag changes, no re-show in the original).
                    h->RunCreditsWindow();            Trace(rec, "CreditsWindow");
                } else if (hov == idCredits) {
                    // v83: hide; (CD: vol 1000 + LoadTrack ZumGutenEnd); RunCreditsScroll;
                    //      re-show; (CD: vol 1000 + StartTrack).
                    h->SetVisibleRecursive(winSlot, 0);
                    if (st.cdMusic) {
                        h->AudioSetGlobalVolume(1000);
                        h->AudioLoadTrack("cd2\\ZumGutenEnd.mp3", 0);
                    }
                    h->RunCreditsScroll();            Trace(rec, "CreditsScroll");
                    h->SetVisibleRecursive(winSlot, 1);
                    if (st.cdMusic) {
                        h->AudioSetGlobalVolume(1000);
                        h->AudioStartTrack(st.cdTrack);
                    }
                }
            }
        }

        // LABEL_23: the spine quit signal (byte_67225C == 1).
        if (st.escDown == 1) {
            st.quit = 1;        // dword_63CC48 = 1
            st.close = 1;       // dword_631614 = 1
            Trace(rec, "EscQuit");
        }

        ++frame;
        if (rec) rec->frames = frame;

        // do { ... } while (RunFrameLoop(...))
        runMore = h->RunFrameLoop();
        if (maxFrames >= 0 && frame >= maxFrames) runMore = 0; // headless bound
    } while (runMore);
    Trace(rec, "LoopEnd");

    // ============================ CLEANUP (in order) ============================
    h->WidgetDestroyByType(versionLabel);  Trace(rec, "DestroyVersionLabel");
    h->RadioGroupFreeSurface(radioGroup);  Trace(rec, "RadioGroupFree");
    h->FormDestroy(form);                  Trace(rec, "FormDestroy");
    h->RenderEntityScene();
    h->UniverseSwitchAndReset(0);          Trace(rec, "CleanupUniverse0");
    h->UniverseSwitchAndReset(1);          Trace(rec, "CleanupUniverse1");
    h->AudioSetGlobalVolume(1000);         Trace(rec, "Volume1000");
    if (st.cdTrack) {                      // if (dword_63C760)
        h->AudioStopTrack(st.cdTrack);     Trace(rec, "StopTrack");
        // spin RunFrameLoop until the track's fade-byte; Sleep(1000).
        h->RunFrameLoop();
    }
    h->AudioSetGlobalVolume(5000);         Trace(rec, "Volume5000");
    return 5000;                           // return VIBE_Audio_SetGlobalVolume(5000)
}

} // namespace guild::gui
