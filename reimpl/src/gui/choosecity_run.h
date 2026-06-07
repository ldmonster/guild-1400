#pragma once
// guild::gui — the NEW-GAME entry funnel + the city-choice screen, reconstructed 1:1
// BEHAVIORALLY from gilde.exe:
//
//   VIBE_Menu_EnterChooseCity @0x52ee38  — the tiny "New Game" funnel the main menu calls.
//       word_63C740 = 0; DragCursor_SetSprite(this,0); byte_63CC1D = 1;
//       result = RunChooseCity(0, a2);
//       if (!result)  { Surface_ColorFill(dword_62D210); Window_RenderEntityList(1773); }
//       return result.   (result == 1 => a city was confirmed + the chain ran.)
//
//   VIBE_Menu_RunChooseCity @0x52e6d8 — form "Menu\CHOOSECITY" (+ "..._HEADER"); the
//       3D city-tower scene (sp_STADTTURM + the A_Stadtwahl.esc cutscene); enumerate the
//       ".CTY"/".NET" files in "gamedata/cities"; for each, read the city name, spawn a
//       "stadt_<name>" map marker (Map_SpawnCityPointMarker) and register its info text;
//       per frame pick the nearest object under the cursor, and when it is a "stadt_" marker
//       show its $C name + _STADTAUSWAHL_<name>_BESCHR description; on confirm
//       (dword_75BF38 == 1210 || enterKey 28) chain ChooseCharacterIntroVariant ->
//       RunChooseHistory and arm dword_631614 = 1 / v80 = 1; on cancel
//       (dword_672230 right-click || byte_67225C == 1 esc) arm dword_631614 = 1 and return 0.
//
// REUSE — this module does NOT redefine the new-game models. The city-pick predicates and
// the parameter block live in gui/newgame_setup.h (ChooseCity_IsConfirm /
// ChooseCity_IsCityObject / ChooseCity_Extension / kCityDir / kCityNamePrefix /
// NewGameParams / the NewGame_* flow + NewGameSink) and the char-create wiring in
// gui/charcreate.h.  Host boundaries (the frame loop, the 3D scene/universe, the form build,
// the .CTY enumeration + marker spawn, the per-frame pick, and the chained sub-screens
// ChooseCharacterIntroVariant / RunChooseHistory) go through an installable Hooks struct
// with INERT DEFAULTS in the .cpp so the function links headless and is fully testable.
//
// ODR: no global owned elsewhere is defined here.  The BSS words this pair reads/writes
// (word_63C740, byte_63CC1D, dword_631614, dword_631720/24, dword_75BF38, byte_67225C,
// dword_672230, dword_67221C) have no single canonical mutable home in src/; we model the
// subset this function touches as one ChooseCityState struct, matching the bit values.

#include "gui/newgame_setup.h"  // ChooseCity_* predicates, NewGameParams, kCity* (REUSED)

#include <string>
#include <vector>

namespace guild::gui {

// ===========================================================================
// Recovered constants (byte-for-byte from the decompile).
// ===========================================================================
// The form names loaded by GameTick_Finalize (0x52e912 / 0x52e99a). The _HEADER form is
// the title strip; the main form carries the name/description windows (windows 1 & 2).
//   (kFormChooseCity / kFormChooseCityHeader come from gui/newgame_setup.h — reused.)
// The 3D city-tower object + its pennant animation (0x52ecb7 / 0x52ecfc).
inline constexpr const char* kCityTowerObject   = "sp_STADTTURM";
inline constexpr const char* kCityPennantAnim   = "sonstiges\\wimpel_STADTTURM.baf";
inline constexpr const char* kCityPennantSubmesh = "wimpel_STADTTURM";
// The intro cutscene script (0x52e8f8).
inline constexpr const char* kCityCutscene      = "Startmenu/A_Stadtwahl.esc";
// Per-city text keys (sprintf'd then upper-cased): info (0x52e802) + description (0x52eb56),
// and the status-text marker key (0x52e86f).
inline constexpr const char* kCityInfoKeyFmt    = "_STADTAUSWAHL_%s_INFO+0";
inline constexpr const char* kCityBeschrKeyFmt  = "_STADTAUSWAHL_%s_BESCHR+0";
inline constexpr const char* kCityStatusKeyFmt  = "stadt_%s";

// The pick radius/flags passed to FindNearestObjectAt (0x52ea03): (mouseX,mouseY,96,1).
inline constexpr int kCityPickRadius = 96;

// The confirm widget id (dword_75BF38 == 1210) and the Enter key code (byte_67225C == 28)
// and the Esc key code (byte_67225C == 1).  (kCity confirm id is checked via
// ChooseCity_IsConfirm in gui/newgame_setup.h.)
inline constexpr int kCityConfirmId = 1210;
inline constexpr int kKeyEnter      = 28;
inline constexpr int kKeyEsc        = 1;

// byte_63CC1D — set to 1 by EnterChooseCity (a "this is the new-game / city flow" marker).
inline constexpr int kEnterChooseCityMarker = 1;
// Window_RenderEntityList(1773) — the redraw issued when the city flow was cancelled.
inline constexpr int kCancelRenderList = 1773;

// ===========================================================================
// Reconstructed state — the BSS words this pair reads/writes (the screen's own faithful
// copy; bit values match the original exactly).
// ===========================================================================
struct ChooseCityState {
    // ---- EnterChooseCity funnel ----
    int  sessionFlags = 0;       // word_63C740 (= 0 at entry; caller ORs |1 on success)
    int  enterMarker  = 0;       // byte_63CC1D (set to 1 on entry)

    // ---- RunChooseCity ----
    bool network = false;        // v76 / a1: network game -> ".NET" + the "already chosen
                                 // a city via the file selector" short-circuit branch.
    int  close   = 0;            // dword_631614 (armed on confirm OR cancel/esc)
    int  pickedObject = 0;       // dword_631724 (the FindNearestObjectAt result)
    int  hoverCity    = 0;       // dword_631720 (the picked obj IF a "stadt_" marker)

    // The hovered city's recovered name (the part after "stadt_"), updated on hover-change.
    std::string hoverCityName;

    // Result: v80 — 1 when a city was confirmed AND the chain succeeded, else 0.
    int  result = 0;
};

// ===========================================================================
// A single enumerated city (the per-file record built in the enumeration loop @0x52e797).
// ===========================================================================
struct CityEntry {
    std::string file;     // the matched ".CTY"/".NET" file base (the enumerator yields it)
    std::string name;     // v64 — the city name read from the save header
    int         marker = 0; // Map_SpawnCityPointMarker(name) result (0 == spawn failed)
    int         infoText = -1; // FindTextArrayIndex("_STADTAUSWAHL_<NAME>_INFO+0")
    bool        registered = false; // StatusText_Register("stadt_<name>", info) ran
};

// ===========================================================================
// Host-boundary hooks (installable; INERT DEFAULTS in the .cpp).
// ===========================================================================
struct ChooseCityHooks {
    virtual ~ChooseCityHooks() = default;

    // ---- EnterChooseCity funnel leaves ----
    // VIBE_DragCursor_SetSprite @0x41fcbc.
    virtual void DragCursorSetSprite(int sprite) { (void)sprite; }
    // VIBE_Surface_ColorFill @0x423b6c (dword_62D210) — the cancel-path screen wipe.
    virtual void SurfaceColorFill() {}
    // VIBE_Window_RenderEntityList @0x4134f0.
    virtual void RenderEntityList(int which) { (void)which; }

    // ---- scene / universe setup (0x52e70d..) ----
    // VIBE_Universe_SwitchActiveSlot @0x5b4a24 + Object_SetPosition/Translation +
    // SkyColor_BlendBandLighting — the 3D backdrop the towers stand in.
    virtual void SceneSetup() {}
    // VIBE_Object_AttachToUniverseNode("sp_STADTTURM") + Light_BuildObjectCache +
    // Character_LoadObjectAnimation(pennant) @0x52ecb7. Returns the tower object (0==none).
    virtual int  SpawnCityTower() { return 0; }
    // VIBE_Cutscene_LoadAndRunScript + RunScriptLoop("Startmenu/A_Stadtwahl.esc").
    virtual void RunIntroCutscene() {}
    // Teardown of the tower object (Character_UpdateSubMeshes + Object_DetachAndRelease).
    virtual void DestroyCityTower(int tower) { (void)tower; }

    // ---- city-file enumeration (0x52e75c + the per-file body) ----
    // VIBE_SaveBrowser_EnumerateSaveFiles("gamedata/cities", ext, &out) — returns the file
    // count; fills `outFiles` with the matched base names (one per city).  `ext` is the
    // ChooseCity_Extension(network) result (".CTY"/".NET").
    virtual int  EnumerateCityFiles(const char* dir, const char* ext,
                                    std::vector<std::string>& outFiles) {
        (void)dir; (void)ext; (void)outFiles; return 0;
    }
    // VIBE_Vfs_OpenFile + VIBE_Save_LoadHeaderAndThumbnail @0x52e797: read the city name
    // out of the file header.  Returns true on success and writes the name into `outName`.
    virtual bool ReadCityName(const std::string& file, std::string& outName) {
        (void)file; outName = file; return true;
    }
    // VIBE_Map_SpawnCityPointMarker @0x52e2d0 — spawn the "stadt_<name>" map marker.
    // Returns the marker object handle (0 == failed; that city is then skipped).
    virtual int  SpawnCityMarker(const std::string& cityName) { (void)cityName; return 0; }
    // VIBE_Text_FindTextArrayIndex @0x44e0d8 for the upper-cased "_STADTAUSWAHL_<N>_INFO+0"
    // key.  Returns the text-array index or -1.
    virtual int  FindTextIndex(const std::string& upperKey) { (void)upperKey; return -1; }
    // VIBE_StatusText_Register @0x4bcc80 — register "stadt_<name>" -> the info text.
    virtual void RegisterStatusText(const std::string& key, const std::string& text) {
        (void)key; (void)text;
    }
    // Teardown of one spawned marker (Object_DetachAndRelease @0x5b4258).
    virtual void DestroyMarker(int marker) { (void)marker; }

    // ---- form build (0x52e912 / 0x52e99a) ----
    // VIBE_GameTick_Finalize(0,0,name) — load a form; returns the form handle (-1 none).
    virtual int  FormLoad(const char* name) { (void)name; return -1; }
    // Window_PositionAtCoord_Thunk + Form_PositionChildWindows + Form_SelectWindow.
    virtual void FormPositionAndSelect(int form, int window) { (void)form; (void)window; }
    // VIBE_Form_SetObjectsVisible @0x41d634 (hide/show during the confirm chain).
    virtual void FormSetVisible(int form, int visible) { (void)form; (void)visible; }
    // VIBE_Form_Destroy @0x41da04.
    virtual void FormDestroy(int form) { (void)form; }

    // ---- per-frame ----
    // VIBE_GameLogic_RunFrameLoop @0x4c09a0 — one tick. Returns nonzero to keep running.
    virtual int  RunFrameLoop(int frame) { (void)frame; return 0; }
    // VIBE_Pick_FindNearestObjectAt @0x5b5a38 (mouseX,mouseY,96,1).  Returns the object
    // handle under the cursor, or 0 for none.
    virtual int  PickNearestObject(int frame) { (void)frame; return 0; }
    // dword_67221C — nonzero when the pick is a "selectable" hit this frame (gates the
    // hover) vs a click on empty space (which, with right-click/esc, cancels).
    virtual bool PickIsHover(int frame) { (void)frame; return false; }
    // The picked object's name (for the "stadt_" prefix test).  Empty => no name.
    virtual std::string ObjectName(int object) { (void)object; return std::string(); }
    // VIBE_Text_RenderRichString — show the hovered city's $C name / description.
    virtual void RenderCityInfo(const std::string& cityName, int infoText) {
        (void)cityName; (void)infoText;
    }

    // ---- input edges ----
    // dword_75BF38 — the clicked widget id this frame (0 == none; 1210 == confirm).
    virtual int  ClickedWidgetId(int frame) { (void)frame; return 0; }
    // byte_67225C — the key/edge code this frame (28 == Enter, 1 == Esc, 0 == none).
    virtual int  KeyCode(int frame) { (void)frame; return 0; }
    // dword_672230 — a right-click (cancel) edge this frame.
    virtual bool RightClick(int frame) { (void)frame; return false; }

    // ---- chained sub-screens (the confirm path, 0x52ed59 / 0x52ed89) ----
    // VIBE_Menu_ChooseCharacterIntroVariant @0x52e4e0 — returns nonzero to proceed.
    virtual bool ChooseCharacterIntroVariant() { return false; }
    // VIBE_Menu_RunChooseHistory @0x52d684 — returns nonzero on confirm.
    virtual bool RunChooseHistory() { return false; }
};

// Install hooks (null restores the inert defaults). Returns the previous hooks.
ChooseCityHooks* Menu_SetChooseCityHooks(ChooseCityHooks* hooks);

// ===========================================================================
// Recorded run (testable): the enumeration + the build/cleanup call order + the confirm.
// ===========================================================================
struct ChooseCityRecord {
    std::vector<CityEntry> cities;     // the enumerated + marker-spawned cities
    int  towerObject = 0;             // SpawnCityTower result
    int  headerForm  = -1;            // kFormChooseCityHeader handle
    int  mainForm    = -1;            // kFormChooseCity handle
    std::string extension;            // the matched ".CTY"/".NET"

    // Frame outcome.
    int  frames = 0;
    bool confirmed = false;           // a confirm edge (1210 / Enter) fired
    bool cancelled = false;           // a cancel edge (right-click / Esc) fired
    bool chainIntroRan = false;       // ChooseCharacterIntroVariant called
    bool chainHistoryRan = false;     // RunChooseHistory called
    std::string confirmedCity;        // the city name confirmed (hoverCityName at confirm)

    // Call-order trace (the e2e asserts a deterministic ordered tag sequence).
    static constexpr int kMaxTrace = 256;
    const char* trace[kMaxTrace];
    int  traceCount = 0;
};

// ===========================================================================
// gilde.exe 0x52e6d8 — VIBE_Menu_RunChooseCity.
// Runs the scene/form build + enumeration + the per-frame pick/hover/confirm loop +
// cleanup. `st` carries network in and the flags/result out; `rec` (optional) records the
// enumeration + order + confirm; `maxFrames` bounds the loop headless.  Returns
// st.result (1 on confirmed+chained, 0 on cancel) — matching the original's `return v80`.
int Menu_RunChooseCity(ChooseCityState& st, ChooseCityRecord* rec, int maxFrames);

// gilde.exe 0x52ee38 — VIBE_Menu_EnterChooseCity.
// The funnel: word_63C740 = 0; DragCursor_SetSprite(0); byte_63CC1D = 1; r = RunChooseCity;
// on cancel (!r) Surface_ColorFill + RenderEntityList(1773).  Returns the RunChooseCity
// result (1 confirmed, 0 cancelled).
int Menu_EnterChooseCity(ChooseCityState& st, ChooseCityRecord* rec, int maxFrames);

} // namespace guild::gui
