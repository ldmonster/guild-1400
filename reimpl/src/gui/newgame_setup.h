#pragma once
// guild::gui — the NEW-GAME setup flow (city -> history -> player -> profession).
//
// Reached from the main menu's "New Game" button (VIBE_Menu_EnterChooseCity @0x52ee38).
// The original threads four screens that progressively fill the global new-game parameter
// block, then arms the session-start command (dword_631614 = 1, word_63C740 |= 1|8):
//
//   VIBE_Menu_RunChooseCity        @0x52e6d8  form "Menu\CHOOSECITY" (+ "..._HEADER")
//       enumerates city files (".CTY", or ".NET" in network mode) in "gamedata/cities",
//       picks the clicked city marker (name prefix "stadt_"); on confirm (clickedId 1210
//       / Enter key 28) chains -> ChooseCharacterIntroVariant -> RunChooseHistory.
//   VIBE_Menu_RunChooseHistory     @0x52d684  form "Menu\CHOOSEHISTORY"
//       a 4-button radio group selecting the history/difficulty flag (1 / 2 / 0);
//       VIBE_History_SetActiveFlag(flag); then runs RunChoosePlayer, then for a non-intro
//       path RunChoosePlayer success -> ChooseCharacterIntro==0 -> ChooseProfession.
//   VIBE_Menu_RunChoosePlayer      @0x52ccd8  form "Menu\CHOOSEPLAYER"  (6 sub-windows)
//       collects player Name (Vorname), Familienname (Nachname), Wappen (coat-of-arms,
//       8 buttons gfx 1342..1349), Geschlecht (gender 0/1), Glauben (faith 0/1).  Page
//       state machine (0=name,1=family,2=gender,3=faith,4=wappen,5=commit); commits the
//       INI block ([Network] Name/Familienname/Wappen/Geschlecht/Glauben) and the globals
//       (String, byte_122F4CA, dword_122F4A4, byte_122F4A8, byte_122F4A9).
//   VIBE_Menu_ChooseProfession     @0x52c50c  form "Menu\CHOOSEPROFESSION"
//       an 8-button grid (x=96*(i%3)+100, y=80*(i/3)+100, gfx=(berufByte)+1349) selecting
//       the starting profession (BERUF); on click stores
//       BuildingType_ComputeVariantIndex(berufByte,1) into SHIBYTE(dword_122F4A0) and arms
//       the start command (dword_122F528 = 1555, dword_631614 = 1).
//
// This module recovers the SCREEN LAYOUT/WIRING and the PARAMETER COLLECTION (which screen
// builds which buttons, the button->transition chain, and how city/history/name/gender/
// faith/wappen/profession funnel into a session-params struct + the emitted start command).
// The 3D-scene/cutscene setup, the .CTY enumeration leaf (REUSED from gui/savebrowser),
// the rich-text renderer, the form loader and the radio-group runner are reused/forward-
// declared; every mutation (INI write, session flags, the start command) goes through a
// command/session hook so the flow is testable in isolation.
//
// ODR: RunOptionsMain/SubTabs (gui/menu*), the widget-create leaves (gui/widget_create),
// the form loader (gui/form_loader/form_asset) and SaveBrowser_EnumerateSaveFiles
// (gui/savebrowser) are REUSED, not redefined.

#include "gui/main_menu.h"  // session-flag constants (kSession*) — REUSED, not redefined
#include "gui/types.h"

#include <string>

namespace guild::gui {

// ===========================================================================
// Recovered form names (byte-for-byte from the GameTick_Finalize calls).
// ===========================================================================
inline constexpr const char* kFormChooseCityHeader = "Menu\\CHOOSECITY_HEADER";
inline constexpr const char* kFormChooseCity       = "Menu\\CHOOSECITY";
inline constexpr const char* kFormChooseHistory    = "Menu\\CHOOSEHISTORY";
inline constexpr const char* kFormChoosePlayer     = "Menu\\CHOOSEPLAYER";
inline constexpr const char* kFormChooseProfession = "Menu\\CHOOSEPROFESSION";

// City file extensions matched by VIBE_SaveBrowser_EnumerateSaveFiles (0x52e75c):
// ".CTY" for a local game, ".NET" for a network game.  Directory "gamedata/cities".
inline constexpr const char* kCityDir       = "gamedata/cities";
inline constexpr const char* kCityExtLocal  = ".CTY";
inline constexpr const char* kCityExtNet    = ".NET";
// A city map object is identified by the name prefix "stadt_" (0x52ea33, StrncmpN(...,6)).
inline constexpr const char* kCityNamePrefix = "stadt_";

// ===========================================================================
// CHOOSEPROFESSION button grid — gilde.exe 0x52c50c.
// 8 buttons; button i at  x = 96*(i%3)+100,  y = 80*(i/3)+100,  gfx = beruf[i] + 1349.
// Each widget: +72 = 1 (clickable), +444 = 3, +440 = 69 ('E').  Click i ->
// BuildingType_ComputeVariantIndex(beruf[i], 1).
// ===========================================================================
inline constexpr int kProfessionCount   = 8;
inline constexpr int kProfessionColumns  = 3;
inline constexpr int kProfessionCellW    = 96;
inline constexpr int kProfessionCellH    = 80;
inline constexpr int kProfessionOriginX  = 100;
inline constexpr int kProfessionOriginY  = 100;
inline constexpr int kProfessionGfxBase  = 1349; // gfx = berufByte + 1349

// Button i grid position (mirrors the AddToWindow x/y arguments).
int Profession_ButtonX(int index);
int Profession_ButtonY(int index);
// gfx id for profession-button i given its profession byte.
int Profession_ButtonGfx(int berufByte);

// ===========================================================================
// CHOOSEPLAYER wappen grid — gilde.exe 0x52ccd8.
// 8 coat-of-arms buttons, gfx 1342..1349; laid out in a grid sized to the window:
//   columns = (windowWidthPx - 32) / 48 ;  button i at
//   x = 48*(i%columns)+80,  y = 48*(i/columns)+32,  gfx = 1342 + i.
// ===========================================================================
inline constexpr int kWappenCount   = 8;
inline constexpr int kWappenGfxBase  = 1342; // gfx = 1342 + i  (also widget +8 id)
inline constexpr int kWappenCellPx   = 48;
inline constexpr int kWappenOriginX  = 80;
inline constexpr int kWappenOriginY  = 32;
inline constexpr int kWappenMarginPx = 32;   // (winW - 32) / 48 == column count

// Column count for the wappen grid given the player-window pixel width.
int Wappen_ColumnCount(int windowWidthPx);
int Wappen_ButtonX(int index, int columns);
int Wappen_ButtonY(int index, int columns);
inline int Wappen_ButtonGfx(int index) { return kWappenGfxBase + index; }

// ===========================================================================
// CHOOSEPLAYER page state machine — gilde.exe 0x52ccd8 (the `v2` selector).
// 0 = name (Vorname), 1 = family (Nachname), 2 = gender, 3 = faith, 4 = wappen,
// 5 = commit.  Enter (key 28) advances name/family/wappen pages; the gender/faith pages
// advance on a radio click.  At page 5 the block is committed.
// ===========================================================================
enum class PlayerPage {
    kName    = 0,
    kFamily  = 1,
    kGender  = 2,
    kFaith   = 3,
    kWappen  = 4,
    kCommit  = 5,
};

// ===========================================================================
// The new-game session parameters this flow collects (the global block at
// 0x122F4A0.. plus word_63C740).  Defaults mirror the INI fallbacks the original reads
// from [Network] (Vorname/Nachname/Wappen 0/Geschlecht 0/Glauben 0).
// ===========================================================================
struct NewGameParams {
    // City (RunChooseCity).
    std::string cityName;        // the "stadt_<name>" map id of the picked city
    std::string cityFile;        // the matched ".CTY"/".NET" file (extension stripped)
    bool        network = false; // network game -> ".NET" extension, word_63C740 & 4

    // History/difficulty (RunChooseHistory): 1, 2 or 0 (History_SetActiveFlag arg).
    int historyFlag = 0;

    // Player identity (RunChoosePlayer).
    std::string firstName;       // [Network] Name      (Vorname)   -> global String
    std::string familyName;      // [Network] Familienname (Nachname) -> byte_122F4CA
    int wappen  = 0;             // [Network] Wappen 0..7 (widget id - 1342); dword_122F4A4
    int gender  = 0;             // [Network] Geschlecht 0/1          byte_122F4A8
    int faith   = 0;             // [Network] Glauben 0/1             byte_122F4A9

    // Profession (ChooseProfession): the picked profession byte + its computed variant.
    int profession = -1;         // beruf byte (table dword_527604), -1 = none chosen yet
    int professionVariant = -1;  // BuildingType_ComputeVariantIndex(beruf,1) -> SHIBYTE(122F4A0)

    // Session flags armed (word_63C740) when the flow completes.
    int sessionFlags = 0;
    bool started = false;        // dword_631614 set + dword_122F528 = 1555 -> start command
};

// Wappen id (widget +8 / dword_122F4A4) <-> 0-based index (== gfx - 1342).
inline int Wappen_IdForIndex(int index)  { return kWappenGfxBase + index; }
inline int Wappen_IndexForId(int id)     { return id - kWappenGfxBase; }

// gilde.exe 0x52d4af / 0x52d9f9 — the start command value written into dword_122F528 when
// the profession is committed (1555), and the magic written into dword_122F4A0 low word
// (LOBYTE=0, *(WORD*)(122F4A0+1)=4608) on the wappen commit.
inline constexpr int kStartCommandValue = 1555;   // dword_122F528

// ===========================================================================
// Session / command hook (mockable).  The flow's side effects — the 3D scene, the
// cutscenes, the INI writes, the History flag, BuildingType_ComputeVariantIndex and the
// emitted start command — are routed through this sink so the parameter collection +
// transitions are testable.  A real adapter forwards to the sim/world/io clusters.
// ===========================================================================
struct NewGameSink {
    virtual ~NewGameSink() = default;
    // VIBE_History_SetActiveFlag @0x4fd218 — record the history/difficulty flag.
    virtual void SetHistoryFlag(int flag) { (void)flag; }
    // VIBE_BuildingType_ComputeVariantIndex @0x589cb0 — beruf byte -> variant index.
    // The reference impl returns the byte unchanged (identity), matching a 1-variant build.
    virtual int ComputeProfessionVariant(int berufByte) { return berufByte; }
    // The emitted session-start command (dword_631614 = 1; dword_122F528 = 1555;
    // word_63C740 |= 1|8).  `p` is the fully-collected parameter block.
    virtual void StartSession(const NewGameParams& p) { (void)p; }
};
void NewGame_SetSink(NewGameSink* sink);

// ---------------------------------------------------------------------------
// gilde.exe 0x52e6d8 — RunChooseCity (the city-pick model).  Given the directory listing
// and the network flag, returns the city extension to match (".CTY" / ".NET").
const char* ChooseCity_Extension(bool network);

// gilde.exe 0x52e6d8 — resolve the picked city.  `clickedId` is dword_75BF38 and
// `enterKey` is byte_67225C: a confirm happens when clickedId == 1210 or enterKey == 28.
// Returns true when the click confirms a city selection.
bool ChooseCity_IsConfirm(int clickedId, int enterKey);

// gilde.exe 0x52ea33 — whether `objectName` names a city marker (prefix "stadt_").
bool ChooseCity_IsCityObject(const std::string& objectName);

// gilde.exe 0x52d684 — RunChooseHistory: map the clicked button slot (0..3) to the
// History_SetActiveFlag value.  Buttons in build order: 0 -> flag 1, 1 -> flag 2,
// 2 -> flag 0; the 4th (slot 3, id 1155) is Cancel.  Returns -1 for Cancel/out of range.
int ChooseHistory_FlagForButton(int buttonSlot);

// gilde.exe 0x52c50c — ChooseProfession: map a clicked widget id (gfx id) to the
// profession byte (id - 1349), or -1 when the id is outside the 1349..1356 button range.
int ChooseProfession_ByteForWidgetId(int widgetId, const int* berufTable);

// ===========================================================================
// The whole flow, driven from collected screen inputs (for the e2e test + a real driver).
// Each step mutates `p`; the final Commit emits the start command via the sink.
// ===========================================================================

// Step 1 — apply a city pick: record the city name/file + network flag into `p`.
// Mirrors the RunChooseCity confirm path (city marker -> ChooseCharacterIntroVariant ->
// RunChooseHistory).  Returns true (the original returns v77 = 1 on confirm).
bool NewGame_ApplyCity(NewGameParams& p, const std::string& cityName,
                       const std::string& cityFile, bool network);

// Step 2 — apply the history button (RunChooseHistory): set p.historyFlag and call the
// sink's SetHistoryFlag.  `buttonSlot` 0..2 select a flag; 3 (Cancel) returns false.
bool NewGame_ApplyHistory(NewGameParams& p, int buttonSlot);

// Step 3 — apply the player identity block (RunChoosePlayer commit): copy the five
// fields into `p` (these are the values the original writes to [Network] + the globals).
void NewGame_ApplyPlayer(NewGameParams& p, const std::string& firstName,
                         const std::string& familyName, int wappenIndex, int gender,
                         int faith);

// Step 4 — apply the profession pick (ChooseProfession): set p.profession +
// p.professionVariant (via the sink's ComputeProfessionVariant).  Returns true (the
// original returns v23 = 1 on a click).
bool NewGame_ApplyProfession(NewGameParams& p, int berufByte);

// Step 5 — commit: arm the session-start command.  Sets p.started, p.sessionFlags
// (|= kSessionNewGame | kSessionHistory, |= kSessionNetwork when p.network), and fires
// the sink's StartSession.  Mirrors the RunChooseHistory tail (word_63C740 |= 8) + the
// ChooseProfession commit (dword_122F528 = 1555, dword_631614 = 1).
void NewGame_Commit(NewGameParams& p);

} // namespace guild::gui
