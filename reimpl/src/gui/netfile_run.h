#pragma once
// guild::gui — the two main-menu SUB-SCREEN RunXxx bodies for the multiplayer
// mode chooser and the generic file selector, reconstructed 1:1 BEHAVIORALLY the
// same way gui/main_menu_run.{h,cpp} reconstructs Menu_RunMainMenu.
//
//   VIBE_Menu_ChooseNetworkMode @0x529a64 — the "CHOOSENETWORK" form: a 3-radio
//        column (host / search-join / load-saved-profile). Each pick hides the
//        form objects, runs the matching child screen, and on child success arms
//        the close flag (dword_631614 = 1) and marks the function result 1. A
//        cancel edge (dword_672230 || byte_67225C==1) arms the close flag but
//        leaves the result 0. Returns 1 when a child succeeded, else 0.
//          host   -> word_63C740 = 5, Menu_RunHostNetworkSetup
//          search -> word_63C740 = 5, Menu_SearchNetworkGames(0)
//          load   -> word_63C740 = 4, Menu_ChooseNetworkProfile
//        (Note the original sets dword_631614=1 on the host branch unconditionally
//        as well as on success — see the .cpp comment; we model the visible flags.)
//
//   VIBE_Menu_RunFileSelector @0x569668 — the generic .INI/.SAV file picker used
//        for gamedata/cities. It loads the "menu\fileselector" form, enumerates a
//        directory (VIBE_SaveBrowser_EnumerateSaveFiles — REUSED from
//        gui/savebrowser), builds one text-label widget per file into list window 1,
//        and runs the frame loop. A list click selects a row; in "direct" mode
//        (flag bit 1 set) a click immediately commits, Sprintf "dir\\name ext" into
//        the caller buffer, arms dword_631614 and returns 1. In "edit" mode (bit 1
//        clear) a list click copies the name into the edit field, and the OK button
//        (ChildObjectId) commits the edit-field text. Returns 1 on commit, else 0.
//
// The menu then does: Sprintf "\project\gfx\"+sel, GetPrivateProfileString(
// "A - ALLGEMEIN"/"Stadtname"), Map_LoadCityFile — that wiring lives in the menu and
// is NOT reconstructed here (a SEPARATE step).
//
// REUSE (extern, not redefined — ODR): guild::gui::SaveBrowser_EnumerateSaveFiles +
// SaveFileEntry (gui/savebrowser). HOST BOUNDARIES (the RunFrameLoop tick, the
// dword_672228/dword_672230 click/cancel edge source + dword_62D22C hovered id, the
// child sub-screen runners, the form/widget/radiogroup build, the directory listing,
// GetPrivateProfileString) go through an installable hooks block with INERT DEFAULTS
// defined in netfile_run.cpp, so the bodies link in the unified build and are fully
// testable headless.
//
// ODR: the menu BSS words (word_63C740, dword_631614, dword_672228, dword_672230,
// dword_62D22C, byte_67225C, dword_75BF38) have no single canonical mutable home in
// src/; like main_menu_run we model the subset these bodies read/write as faithful
// per-call state structs, keeping the bit values byte-identical.

#include "gui/savebrowser.h"  // SaveBrowser_EnumerateSaveFiles / SaveFileEntry (REUSED)

#include <string>
#include <vector>

namespace guild::gui {

// ===========================================================================
// word_63C740 session-flag values the network-mode chooser writes.
// ===========================================================================
inline constexpr int kNetModeSessHost    = 5; // host branch + search branch
inline constexpr int kNetModeSessProfile = 4; // load-saved-profile branch

// CHOOSENETWORK rich-text string ids (gilde.exe 0x529a64).
inline constexpr int kNetModeTextTitle   = 0x18B2; // window-0 title
inline constexpr int kNetModeTextHost    = 0x18B3; // radio 0 (host)
inline constexpr int kNetModeTextSearch  = 0x18B4; // radio 1 (search / join)
inline constexpr int kNetModeTextProfile = 0x18B5; // radio 2 (load saved profile)
inline constexpr int kNetModeRadioCount  = 3;      // RadioGroup_Create(3, host)

// FILESELECTOR ids (gilde.exe 0x569668).
inline constexpr int kFileSelTextOk     = 0x18D9; // window-3 OK label (edit mode)
inline constexpr int kFileSelListWindow = 1;      // list window the rows build into
inline constexpr int kFileSelEditWindow = 3;      // OK-button / edit window
inline constexpr int kFileSelRowStrideY = 24;     // per-row y step (LOWORD v37 += 24)
inline constexpr int kFileSelEditColor  = 67;     // SetColor on the count field

// The flag-bit-1 ("direct" / immediate-commit) mode selector of RunFileSelector.
inline constexpr int kFileSelDirectBit  = 0x1;

// ===========================================================================
// VIBE_Menu_ChooseNetworkMode @0x529a64
// ===========================================================================

// Which radio row a click landed on (drives the dispatch).
enum class NetModePick { kNone, kHost, kSearch, kProfile };

struct NetModeState {
    int  session = 0;  // word_63C740   (5 host/search, 4 profile)
    int  close = 0;    // dword_631614  (transition armed)
};

struct NetModeRecord {
    int  radioGroup = -1;     // RadioGroup_Create return
    int  radioCount = 0;      // == 3
    int  idHost = -1;         // GetChildObjectId(host)   (radio slot 0)
    int  idSearch = -1;       // GetChildObjectId(search) (radio slot 1)
    int  idProfile = -1;      // GetChildObjectId(profile)
    int  byteState = 0;       // byte_63C8F4 = -2 (0xFE) seed written at build

    static constexpr int kMaxTrace = 64;
    const char* trace[kMaxTrace];
    int  traceCount = 0;
    int  frames = 0;          // frame-loop iterations run
};

struct NetModeHooks {
    virtual ~NetModeHooks() = default;

    // ---- build leaves ----
    // VIBE_GameTick_Finalize(0,0,"Menu\\CHOOSENETWORK") @0x41beb8 -> form handle.
    virtual int  FormLoad() { return -1; }
    virtual void FormCenterChildWindows(int form) { (void)form; }
    virtual void FormSelectWindow(int form, int window) { (void)form; (void)window; }
    // VIBE_Text_RenderRichString(id) @0x59d6e8 -> the rendered text handle.
    virtual int  RenderRichString(int textId) { (void)textId; return -1; }
    // VIBE_Form_GetChildObjectId(form, 0, text) @0x41dea8 -> widget id.
    virtual int  GetChildObjectId(int form, int text) { (void)form; (void)text; return -1; }
    // VIBE_RadioGroup_Create(3, firstId) @0x412728 -> group handle.
    virtual int  RadioGroupCreate(int count, int firstId) { (void)count; (void)firstId; return -1; }
    virtual void RadioGroupFreeSurface(int group) { (void)group; }
    virtual void FormDestroy(int form) { (void)form; }

    // ---- frame loop / edge source ----
    // VIBE_GameLogic_RunFrameLoop @0x4c09a0 — one tick. Nonzero keeps running.
    virtual int  RunFrameLoop(int frame) { (void)frame; return 0; }
    // VIBE_InitStateReader(group) @0x412970 — refresh radio hover/select.
    virtual void InitStateReader(int group) { (void)group; }
    // dword_75BF38 != -1 — a click was committed this frame (gate for the dispatch).
    virtual bool ClickReady(int frame) { (void)frame; return false; }
    // dword_62D22C — the hovered/clicked widget id this frame.
    virtual int  HoverId(int frame) { (void)frame; return -1; }
    // cancel edge: dword_672230 (close request) || byte_67225C == 1 (ESC).
    virtual bool CancelEdge(int frame) { (void)frame; return false; }
    // VIBE_Form_SetObjectsVisible(form, visible) @0x41d634 (hide while a child runs).
    virtual void SetObjectsVisible(int form, int visible) { (void)form; (void)visible; }

    // ---- child sub-screens (REAL siblings where reconstructed; return success) ----
    virtual bool RunHostNetworkSetup() { return false; }   // VIBE_Menu_RunHostNetworkSetup @0x528dac
    virtual bool SearchNetworkGames(int mode) { (void)mode; return false; } // @0x529248
    virtual bool ChooseNetworkProfile() { return false; }  // VIBE_Menu_ChooseNetworkProfile @0x52991c
};

NetModeHooks* SetNetModeHooks(NetModeHooks* hooks);

// gilde.exe 0x529a64 — returns 1 when a child screen succeeded (else 0). `st`
// carries the session/close globals out, `rec` (optional) records build + order,
// `maxFrames` bounds the loop for headless testing.
int Menu_ChooseNetworkMode(NetModeState& st, NetModeRecord* rec, int maxFrames);

// ===========================================================================
// VIBE_Menu_RunFileSelector @0x569668
// ===========================================================================

struct FileSelState {
    int  close = 0;   // dword_631614 (transition armed once a file is committed)
};

struct FileSelBuiltRow {
    int  widgetId; // VIBE_Object_AddTextLabel return
    int  y;        // row y (24*index)
    std::string name; // the file's display name (no extension)
};

struct FileSelRecord {
    int  form = -1;          // FormLoad return
    int  rowCount = 0;       // number of enumerated rows built
    int  idOk = -1;          // window-3 OK widget (edit mode only; -1 in direct mode)
    int  idEdit = -1;        // window-0 edit field (edit mode only)
    bool directMode = false; // flag bit 1
    std::vector<FileSelBuiltRow> rows;

    static constexpr int kMaxTrace = 64;
    const char* trace[kMaxTrace];
    int  traceCount = 0;
    int  frames = 0;
    std::string editFieldText; // last text pushed into the edit field (edit mode)
    int  close = 0;            // dword_631614 — armed by a cancel edge or a commit
};

struct FileSelHooks {
    virtual ~FileSelHooks() = default;

    // ---- build leaves ----
    virtual int  FormLoad() { return -1; }                 // GameTick_Finalize("menu\\fileselector")
    virtual void FormCenterChildWindows(int form) { (void)form; }
    virtual void DragCursorSetSprite() {}                  // VIBE_DragCursor_SetSprite @0x41fcbc
    virtual void FormSelectWindow(int form, int window) { (void)form; (void)window; }
    virtual int  RenderRichString(int textId) { (void)textId; return -1; }
    virtual int  GetChildObjectId(int form, int text) { (void)form; (void)text; return -1; }
    virtual void BuildSliderPanel(int form) { (void)form; }    // VIBE_Hud_BuildSliderPanel @0x4bd388
    // VIBE_Object_AddTextLabel(0, y, ...) @0x41b288 -> row widget id.
    virtual int  AddTextLabel(int y, const std::string& name) { (void)y; (void)name; return -1; }
    virtual void FormDestroy(int form) { (void)form; }

    // ---- directory enumeration (REUSES SaveBrowser_EnumerateSaveFiles) ----
    // The raw directory listing the enumerator filters; default is empty.
    virtual std::vector<std::string> DirectoryListing(const std::string& dir) {
        (void)dir; return {};
    }

    // ---- edit-field model ----
    // VIBE_Object_SetValueOrText(editId, text) @0x41dfec — push text into the field.
    virtual void EditFieldSetText(int editId, const std::string& text) { (void)editId; (void)text; }
    // VIBE_Object_GetDataPtr(editId) @0x41db9c — read the edit-field text back.
    virtual std::string EditFieldGetText(int editId) { (void)editId; return {}; }

    // ---- frame loop / edge source ----
    virtual int  RunFrameLoop(int frame) { (void)frame; return 0; }
    // dword_672230 — close/cancel request -> arms dword_631614 (no commit).
    virtual bool CancelEdge(int frame) { (void)frame; return false; }
    // dword_672228 — a list-row click edge this frame.
    virtual bool ListClickEdge(int frame) { (void)frame; return false; }
    // dword_75BF38 != -1 — the OK button was committed this frame (edit mode).
    virtual bool OkReady(int frame) { (void)frame; return false; }
    // dword_62D22C — the hovered/clicked widget id this frame.
    virtual int  HoverId(int frame) { (void)frame; return -1; }
};

FileSelHooks* SetFileSelHooks(FileSelHooks* hooks);

// gilde.exe 0x569668 — runs the file selector.
//   dir        : the directory passed to the enumerator (a3 / v30).
//   ext        : the extension appended to the committed name (a5).
//   directMode : flag bit 1 (a2 & 1) — immediate commit on list click.
//   listText   : the rich-text id rendered into the title window (a4).
//   out        : receives "dir\\name ext" on commit.
//   rec        : optional build/order record.
//   maxFrames  : loop bound for headless testing.
// Returns 1 when a file was committed (out filled), else 0.
int Menu_RunFileSelector(const std::string& dir, const std::string& ext,
                         bool directMode, int listText, std::string& out,
                         FileSelRecord* rec, int maxFrames);

} // namespace guild::gui
