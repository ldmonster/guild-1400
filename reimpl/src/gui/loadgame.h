#pragma once
// guild::gui — the Load Game screen.
//
// gilde.exe 0x56a270 — VIBE_Menu_RunLoadGame  (form "MENU\LOADGAME_NEW", title 6244).
//
// Loads the load-game form, positions it, renders the title (rich-string 6244), builds a
// 130-row slider panel (Hud_BuildSliderPanel(532,360,...)) and fills it with the save-slot
// metadata (SaveBrowser_LoadSlotMetadata into an 8704-byte / 544-stride table of up to 16
// slots), then runs the shared frame loop.  Each slot row holds {present?, widgetId,
// objId, name}.  When the hovered widget (dword_62D22C) matches a populated slot's widget
// id, the original (optionally behind a confirm message-box, gated by byte_63CC40) builds
// the path "Gamedata\\Saves\\%s.SAV" from the slot name, copies it into the global load
// path buffer (byte_122F530), sets the session flag word_63C740 = 10 (load), raises the
// close flag and returns 1 (a save was chosen).  Cancel / no-selection returns 0.
//
// What this module recovers + exposes for isolated testing:
//   * the slot-table stride/capacity (544-byte rows, 8704-byte table = 16 slots) and the
//     per-row field offsets actually read (present flag +12, widget id +4, obj id +8,
//     name +25);
//   * the slot-match -> path-build ("Gamedata\\Saves\\%s.SAV") -> session-flag (10)
//     dispatch, incl. the optional confirm gate;
//   * the OK/Cancel/close + return-value semantics.
//
// ODR: the slider panel, the slot-metadata loader, the confirm message-box and the form
// lifecycle live in already-translated/unowned modules — routed through the sink.

#include "gui/types.h"
#include <string>

namespace guild::gui {

// ---------------------------------------------------------------------------
// String ids / table geometry (the literal arguments of the original).
// ---------------------------------------------------------------------------
inline constexpr const char* kLoadGameForm = "menu\\loadgame_new"; // 0x56a29c
inline constexpr int kLoadGameTitle        = 6244;  // 0x56a2e3 RenderRichString

// Slider panel: VIBE_Hud_BuildSliderPanel(532, 360, win, ..., 130, ...).  0x56a32f.
inline constexpr int kLoadPanelW    = 532;
inline constexpr int kLoadPanelH    = 360;
inline constexpr int kLoadPanelRows = 130;

// Save-slot metadata table — 0x56a34d / the v8 += 544 loop (v8 < 8704).
inline constexpr int kSlotStride   = 544;  // bytes per row (544 * idx)
inline constexpr int kSlotTableLen = 8704; // total table size
inline constexpr int kSlotCount    = kSlotTableLen / kSlotStride; // 16

// Per-row field offsets actually read in the match loop (0x56a392..).
inline constexpr int kSlotOffWidgetId = 4;  // *(row+4)  == dword_62D22C  -> this slot hovered
inline constexpr int kSlotOffObjId    = 8;  // *(row+8)  (-1 == empty)
inline constexpr int kSlotOffPresent  = 12; // row[12]   (non-zero == populated)
inline constexpr int kSlotOffName     = 25; // &row[25]  the slot's save name (text)

// The session flag set when a slot is chosen: word_63C740 = 10 (load savegame).  0x56a426.
inline constexpr int kSessionLoad = 10;

// A parsed slot row (only the fields the match loop reads).
struct SaveSlot {
    bool        present  = false; // row[12] != 0
    int         widgetId = -1;    // *(row+4)
    int         objId    = -1;    // *(row+8)  (-1 == empty)
    std::string name;             // &row[25]
};

// ---------------------------------------------------------------------------
// Slot match — gilde.exe 0x56a392.  Given the hovered widget id and the slot table,
// return the index of the populated, non-empty slot whose widget id matches, or -1.
// (The original also has a secondary obj-id comparison for the active slot; the primary
// path is the widget-id match, which we model.)
// ---------------------------------------------------------------------------
int LoadGame_MatchSlot(const SaveSlot* slots, int count, int hoveredWidgetId);

// gilde.exe 0x56a3f5 — build the SAV path for a chosen slot: "Gamedata\\Saves\\%s.SAV".
std::string LoadGame_BuildPath(const std::string& slotName);

// ---------------------------------------------------------------------------
// Host sink (mockable).  The confirm message-box (257, gated by byte_63CC40), the form
// lifecycle and the frame loop live in unowned modules.
// ---------------------------------------------------------------------------
struct LoadGameHost {
    virtual ~LoadGameHost() = default;
    // VIBE_GameLogic_RunFrameLoop: true while the loop should keep running.
    virtual bool RunFrame() { return false; }
    // The hovered widget id this frame (dword_62D22C); -1 if none.
    virtual int HoveredWidget() { return -1; }
    // dword_672228: a click/confirm happened this frame.
    virtual bool Clicked() { return false; }
    // dword_672230: the close/escape flag.
    virtual bool CloseRequested() { return false; }
    // byte_63CC40: whether the confirm-before-load gate is active.
    virtual bool ConfirmGateActive() { return false; }
    // VIBE_Dialog_RunMessageBox(257, ...): true == user confirmed the load.
    virtual bool ConfirmLoad() { return true; }
};

// The result of the load-game screen.
struct LoadGameResult {
    bool        chosen = false; // a slot was selected + confirmed (return value v20)
    int         slot   = -1;    // the chosen slot index
    std::string path;           // the "Gamedata\\Saves\\..." path written to byte_122F530
    int sessionFlag    = 0;     // word_63C740 (10 on success)
};

// gilde.exe 0x56a270 — run the load-game screen against the host.  Drives the frame loop;
// on a confirmed slot pick, builds the path + sets the load session flag and stops.
// Returns whether a save was chosen (the original's eax/v20).
LoadGameResult LoadGame_Run(LoadGameHost& host, const SaveSlot* slots, int count,
                            int maxFrames);

} // namespace guild::gui
