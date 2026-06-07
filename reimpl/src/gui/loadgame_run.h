#pragma once
// guild::gui — VIBE_Menu_RunLoadGame @0x56a270, the FULL load-game / save-browser body.
//
// gui/loadgame.{h,cpp} already recovers the HIGH-LEVEL dispatch (LoadGame_MatchSlot /
// LoadGame_BuildPath + a generic frame-loop shape) and gui/savebrowser.{h,cpp} +
// io/save_browser.{h,cpp} recover the save-file enumeration + slot-list build. MISSING —
// and reconstructed here 1:1, the way main_menu_run.cpp reconstructs RunMainMenu — is the
// COMPLETE function body of VIBE_Menu_RunLoadGame:
//
//   * the preamble: GameTick_Finalize(0,0,"menu\\loadgame_new") form load;
//     Form_PositionChildWindows(form,1); Window_PositionAtCoord_Thunk(form,2);
//     Form_SelectWindow(form,0); Hud_SyncWindowColors(dword_62D230);
//     DragCursor_SetSprite(_,0); Form_SelectWindow(0,2); Text_RenderRichString(0x1864 =
//     6244, the title); Form_SelectWindow(0,0); win = Form_GetWindowId(form,1);
//     Hud_BuildSliderPanel(532,360,win,dword_62D230,130,...); then
//     SaveBrowser_LoadSlotMetadata(form,1,scratch,slotTable,"gamedata/saves",0) which
//     enumerates the ".SAV" files, loads each header/thumbnail, drops excluded saves
//     (header flag bit 0x2) and lays them into a 16-entry, 544-byte-stride slot table.
//
//   * the do/while VIBE_GameLogic_RunFrameLoop loop with the click-edge dispatch: when
//     dword_672230 (close) is set -> dword_631614 = 1; when dword_672228 (click edge) is
//     set, walk the 544-byte slot rows (v8 += 544, v8 < 8704) and break on the row whose
//     present flag (row[12]) is set, whose obj id (row+8) != -1 and whose widget id
//     (row+4) == dword_62D22C (the hovered widget). On a match, gated by
//     (!byte_63CC40 || RunMessageBox(257)): Crt_Sprintf "Gamedata\\Saves\\%s.SAV" from
//     the slot name (&row[25]) -> copy into byte_122F530 (the global load-path buffer) ->
//     word_63C740 = 10 (load-savegame session flag) -> dword_631614 = 1 -> v20 = 1.
//
//   * cleanup: Form_Destroy(form); return v20 (1 == a save was chosen, else 0).
//
// REUSE (no ODR redefine): LoadGame_BuildPath (gui/loadgame.h) builds the SAV path;
// SaveBrowser_EnumerateSaveFiles + SaveBrowser_BuildSlots (gui/savebrowser.h) drive the
// list build behind the form-load hook. HOST BOUNDARIES (form/widget/slider build, the
// RunFrameLoop tick, the dword_672228/dword_62D22C click-edge source, the save-file
// enumeration + header load, the confirm message-box) go through an installable hooks
// block with INERT DEFAULTS defined in loadgame_run.cpp so it links in the unified build
// and is fully testable headless.
//
// ODR: this module defines no global owned elsewhere. The session/close flags this
// function reads/writes (word_63C740, dword_631614, byte_63CC40, dword_672228/30,
// dword_62D22C, dword_75BF08) live scattered across domain reconstructions with no single
// canonical mutable home; we model the subset this function touches as one reconstructed
// LoadGameRunState (the screen's own copy of the BSS words), bit values byte-identical.

#include "gui/types.h"

#include <string>
#include <vector>

namespace guild::gui {

// ===========================================================================
// Literal arguments of the original (gilde.exe 0x56a270).
// ===========================================================================
inline constexpr const char* kRunLoadGameForm = "menu\\loadgame_new"; // 0x56a29c
inline constexpr int kRunLoadGameTitle        = 0x1864;  // 6244  RenderRichString @0x56a2e3
inline constexpr const char* kRunLoadSaveDir  = "gamedata/saves";     // 0x56a34d

// Slider panel: Hud_BuildSliderPanel(532, 360, win, dword_62D230, 130, ...).  0x56a32f.
inline constexpr int kRunLoadPanelW    = 532;
inline constexpr int kRunLoadPanelH    = 360;
inline constexpr int kRunLoadPanelRows = 130;

// Slot table geometry — v15[8704], rows of 544 (the v8 += 544 / v8 < 8704 walk).
inline constexpr int kRunLoadSlotStride   = 544;   // 0x220 bytes per row
inline constexpr int kRunLoadSlotTableLen = 8704;  // total
inline constexpr int kRunLoadSlotCount    = kRunLoadSlotTableLen / kRunLoadSlotStride; // 16

// Per-row fields read by the match loop (0x56a392..).
inline constexpr int kRunLoadOffWidgetId = 4;   // *(row+4)  == dword_62D22C -> this slot hovered
inline constexpr int kRunLoadOffObjId    = 8;   // *(row+8)  (-1 == empty)
inline constexpr int kRunLoadOffPresent  = 12;  // row[12]   (non-zero == populated)
inline constexpr int kRunLoadOffName     = 25;  // &row[25]  the slot's save name (text)

// word_63C740 session flag set on a confirmed pick: 10 (load savegame).  0x56a426.
inline constexpr int kRunSessLoad = 10;

// The confirm message-box id (gated by byte_63CC40). 0x56a49f.
inline constexpr int kRunLoadConfirmBox = 257;

// ===========================================================================
// Reconstructed screen state — the BSS words this function reads/writes.
// (The screen's own faithful copy; bit values match the original exactly.)
// ===========================================================================
struct LoadGameRunState {
    // IN: whether the confirm-before-load gate is active (byte_63CC40). When set, a slot
    // pick is only accepted if RunMessageBox(257) returns true.
    int  confirmGate = 0;     // byte_63CC40

    // IN: the currently-active save obj id (dword_75BF08), used by the secondary
    // obj-id-equality branch of the match loop. -1 == none active.
    int  activeSaveId = -1;   // dword_75BF08

    // OUT: session/close flags the dispatch mutates.
    int  sessionFlags = 0;    // word_63C740  (== 10 on a confirmed load)
    int  close = 0;           // dword_631614 (transition armed -> next-frame exit)

    // OUT: the global load-path buffer (byte_122F530) — the chosen "Gamedata\\Saves\\..".
    std::string loadPath;     // byte_122F530
};

// ===========================================================================
// A parsed slot row (only the fields the build + match loop touch). These are the rows of
// the original's v15[8704] table; the build fills them, the loop matches against them.
// ===========================================================================
struct LoadGameSlot {
    bool        present  = false; // row[12] != 0
    int         widgetId = -1;    // *(row+4)  (the AddChildWindow id the click hits)
    int         objId    = -1;    // *(row+8)  (-1 == empty)
    std::string name;             // &row[25]
};

// ===========================================================================
// Host-boundary hooks (installable; INERT DEFAULTS in the .cpp).
// ===========================================================================
struct LoadGameRunHooks {
    virtual ~LoadGameRunHooks() = default;

    // ---- preamble: form + title + slider panel (the build) ----
    // GameTick_Finalize(0,0,"menu\\loadgame_new") @0x41beb8 — load the form. Returns the
    // form handle (>=0 ok, -1 fail).
    virtual int  FormLoad(const char* form) { (void)form; return -1; }
    // Form_PositionChildWindows(form,1) @0x41d990 + Window_PositionAtCoord_Thunk(form,2)
    // @0x41d964 + Form_SelectWindow(form,0) @0x41e4cc + Hud_SyncWindowColors @0x4bd5dc +
    // DragCursor_SetSprite(_,0) @0x41fcbc.
    virtual void FormPosition(int form) { (void)form; }
    // Form_SelectWindow(0,2) @0x41e4cc; Text_RenderRichString(title) @0x59d6e8;
    // Form_SelectWindow(0,0).
    virtual void RenderTitle(int titleId) { (void)titleId; }
    // Form_GetWindowId(form,1) @0x41e544 — the list window id.
    virtual int  GetWindowId(int form) { (void)form; return -1; }
    // Hud_BuildSliderPanel(532,360,win,dword_62D230,130,...) @0x4bd388.
    virtual void BuildSliderPanel(int w, int h, int win, int rows) {
        (void)w; (void)h; (void)win; (void)rows;
    }
    // SaveBrowser_LoadSlotMetadata(form,1,scratch,slotTable,dir,0) @0x569d00 — enumerate
    // the ".SAV" files + load headers and lay them into the 16-slot table. The default
    // produces no slots (no real save dir headless). `outSlots` is the filled slot grid.
    virtual void LoadSlotMetadata(int form, const char* saveDir,
                                  std::vector<LoadGameSlot>& outSlots) {
        (void)form; (void)saveDir; (void)outSlots;
    }
    // Form_Destroy(form) @0x41da04.
    virtual void FormDestroy(int form) { (void)form; }

    // ---- frame / click-edge source ----
    // VIBE_GameLogic_RunFrameLoop @0x4c09a0 — one tick. Nonzero to keep running.
    virtual int  RunFrameLoop() { return 0; }
    // dword_672230 — the close/escape flag this frame.
    virtual bool CloseRequested(int frame) { (void)frame; return false; }
    // dword_672228 — a left-click EDGE occurred this frame.
    virtual bool ClickEdge(int frame) { (void)frame; return false; }
    // dword_62D22C — the hovered widget id this frame, or -1.
    virtual int  HoverId(int frame) { (void)frame; return -1; }

    // ---- confirm gate ----
    // RunMessageBox(257, ...) @0x569a30 — true == the user confirmed the load.
    virtual bool ConfirmLoad() { return true; }
};

// Install hooks (null restores the inert defaults). Returns the previous hooks.
LoadGameRunHooks* LoadGame_SetRunHooks(LoadGameRunHooks* hooks);

// ===========================================================================
// Recorded build/dispatch (testable): the preamble build + an ordered call trace.
// ===========================================================================
struct LoadGameRunRecord {
    // Build:
    int   form = -1;             // FormLoad return
    int   listWindow = -1;       // GetWindowId return
    bool  sliderBuilt = false;   // BuildSliderPanel fired
    int   sliderW = 0, sliderH = 0, sliderRows = 0;
    int   titleId = 0;           // RenderTitle arg (== 6244)
    int   slotCount = 0;         // slots LoadSlotMetadata produced (occupied + placeholder)
    int   occupiedSlots = 0;     // populated rows (row[12] != 0)

    // Dispatch:
    int   matchedSlot = -1;      // the slot index the click resolved to (-1 == none)
    bool  confirmAsked = false;  // ConfirmLoad() was called (confirm gate active)

    // Call-order trace (the e2e asserts a deterministic ordered sequence of tags).
    static constexpr int kMaxTrace = 128;
    const char* trace[kMaxTrace];
    int   traceCount = 0;

    int   frames = 0;            // frame-loop iterations run
};

// ===========================================================================
// gilde.exe 0x56a392 — the slot scan. Walk the 544-byte rows, skip rows whose present
// flag (row[12]) is clear or whose obj id (row+8) is -1, return the index of the row whose
// widget id (row+4) equals the hovered widget id (dword_62D22C), else -1. (The original's
// secondary obj-id-equality branch only re-targets the active save and never selects a
// different row; the primary path is the widget-id match.)
// ===========================================================================
int LoadGameRun_MatchSlot(const std::vector<LoadGameSlot>& slots, int hoveredWidgetId);

// ===========================================================================
// VIBE_Menu_RunLoadGame @0x56a270.
// Runs the full preamble + slot build + frame loop + cleanup. `st` carries the gate/active
// id in and the session/close flags + chosen path out. `rec` (optional) records the build
// + call order for tests. `maxFrames` bounds the loop for headless testing (the original
// spins until RunFrameLoop returns 0). Returns 1 when a save was chosen (the original's
// eax/v20), else 0.
int Menu_RunLoadGame(LoadGameRunState& st, LoadGameRunRecord* rec, int maxFrames);

} // namespace guild::gui
