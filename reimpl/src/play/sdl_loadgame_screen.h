#pragma once
// =============================================================================
// guild::play — NATIVE "load game" screen (the main menu's save browser).
//
//   VIBE_Menu_RunLoadGame @0x56a270 — the main menu's Load-Game handler. Form
//   "menu\\loadgame_new" (GameTick_Finalize); title rich text 0x1864 ==
//   `_OPTIONEN_MENUE_LOAD`; Hud_BuildSliderPanel(532, 360, win, 130); then
//   VIBE_SaveBrowser_LoadSlotMetadata @0x569d00:
//     * VIBE_SaveBrowser_EnumerateSaveFiles @0x569530 scans "gamedata/saves"
//       (@0x624f30) for ".SAV" (@0x624f08) over the VFS tree — directory
//       enumeration order, capped at 16 files (0x569d64),
//     * each file's header is read via VIBE_Save_LoadHeaderAndThumbnail
//       @0x5a7af0 ("rb" VFS open); files whose header mode flag has bit 1 set
//       ((hdr+4) & 2 — the partial/.cty/session-snapshot format) are DROPPED
//       (0x569e1a),
//     * VIBE_SaveBrowser_FindSaveSlot @0x569c50 places each survivor into the
//       16-row x 544-byte slot table: name "QUICKSAVE" -> row 1, "AUTOSAVE" ->
//       row 0, else the header slot tag byte_649D50 (2..15); a slot-tag of 1
//       with a non-QUICKSAVE name, a zero tag on a non-reserved name, and
//       duplicate rows are rejected (-1),
//     * empty rows get the localized placeholder label (sprintf of the
//       dword_8C9A04 text == `_OPTIONEN_LOAD_GAME_SLOT_INFO_LEER` with the row
//       index) + rich text 0x18D4.
//   The frame loop (VIBE_GameLogic_RunFrameLoop): dword_672230 (close) ->
//   dword_631614 = 1; a click edge (dword_672228) on an occupied row — gated by
//   byte_63CC40 -> VIBE_Dialog_RunMessageBox(dword_8C9A08 ==
//   `_OPTIONEN_LOAD_GAME_SICHERHEITS_ABFRAGE`, 257) — sprintfs
//   "Gamedata\\Saves\\%s.SAV" (@0x624f40) from the slot name (row+25) into
//   byte_122F530, sets word_63C740 = 10 and dword_631614 = 1, returns 1.
//
// TWO-LAYER (the established screen pattern, cf. sdl_choosehistory_screen):
// the FLOW logic is the 1:1 reconstruction gui::Menu_RunLoadGame
// (gui/loadgame_run, @0x56a270); this module is its native SDL/Vulkan HOST. It
// builds the REAL 16-row slot table from the save directory through
// shim::IFileSystem by driving the same reconstructed leaves the original's
// LoadSlotMetadata drives (io::SaveBrowserEnumerateSaveFiles @0x569530,
// io::SaveLoadHeaderAndThumbnail @0x5a7af0, io::SaveBrowserFindSaveSlot
// @0x569c50), feeds the gui run's hooks per frame (click edge / hovered slot
// widget / close), renders the list (real localized `_OPTIONEN_*` texts when
// textbin is present) and presents through IGraphicsDevice.
//
// NAMED BRIDGE (rule 8, documented not faked): the original's browser DROPS
// partial-format saves ((hdr+4) & 2). Today the session writer
// (play::SaveLiveWorld) emits exactly that partial format because the FULL
// .SAV tail is still a named gap (see progress/session-save.md) — so with the
// strict gate our own quicksaves would never list. `includePartialSaves`
// (default true) admits them; set it false for the byte-faithful gate. Remove
// the toggle once the FULL save tail lands.
//
// VIEW (1:1): the screen renders the real localized title (0x1864), the 16
// scrollable 130px slot rows each with the real 160x120 save thumbnail
// (VIBE_Save_LoadHeaderAndThumbnail @0x5a7af0 -> word_13CED76 raw bitmap) on the
// left and the save name (gold, Object_SetColor 66) + date/wealth info line at
// x+168, the scrollbar (Hud_BuildSliderPanel @0x4bd388), and the empty-slot
// placeholder rows ("Свободный слот N" / 0x18D4), over the real _OPTIONEN_PIC
// background. The pixel-exact retained-mode form-window chrome is approximated by
// the shared parchment-panel style (the other native menu screens use the same).
// =============================================================================
#include <cstdint>
#include <string>
#include <vector>

#include "guild/common/types.h"

namespace guild::shim { class IGraphicsDevice; class IPlatform; class IFileSystem; }

namespace guild::play {

class MenuAssets;  // play/menu_assets.h (real gilde.gfx backdrop, optional)

// Host-side widget ids handed to gui::Menu_RunLoadGame's slot table (the
// original's are AddChildWindow returns; ours are stable synthetic ids).
inline constexpr int kLoadGameSlotWidgetBase = 0x4C00;
// Slot count — the original's 16-row x 544-byte table (8704 / 544).
inline constexpr int kLoadGameSlotCount = 16;

struct LoadGameScreenConfig {
    std::string gameDir;            // mounted game dir (textbin + gfx); empty = no assets
    // Save enumeration source. When null, a DiskFileSystem over gameDir is used
    // (the live app); tests pass a listDir-capable fake with .SAV fixtures.
    shim::IFileSystem* fs = nullptr;
    // VFS tree root the browser scan resolves "gamedata/saves" against, and the
    // real-cased dir the files are opened from (mirrors play::EnumerateSaves).
    std::string treeRoot    = "Resources";
    std::string openDirReal = "Resources/gamedata/Saves";
    int fbW = 800, fbH = 600;
    int maxFrames  = -1;            // -1 = until pick/back/close (tests bound it)
    int frameCapMs = 16;            // per-frame sleep; 0 = uncapped
    bool confirmGate = false;       // byte_63CC40 — confirm box 257 before loading
    // NAMED BRIDGE (see header comment): admit partial-format saves. false ==
    // the original's strict (hdr+4)&2 drop @0x569e1a.
    bool includePartialSaves = true;
};

// One browser slot as the native screen sees it (the original's 544-byte row,
// reduced to the load-bearing fields + the file identity the session needs).
struct LoadGameSlotView {
    bool        occupied = false;   // row[12]
    std::string saveName;           // header name (row+25 — feeds "Gamedata\Saves\%s.SAV")
    std::string fileName;           // the enumerated loose file (browser record +9)
    std::string openPath;           // real-cased path the session can open via the fs
    guild::u32  version = 0;        // header magic
    guild::u8   slotTag = 0;        // header byte_649D50
    bool        partial = false;    // header flag bit 1 ((hdr+4) & 2)
    // The 160x120 RGB save preview (VIBE_Save_LoadHeaderAndThumbnail @0x5a7af0,
    // word_13CED76 raw-bitmap widget @0x569d00) — the per-row screenshot. Empty
    // when the file carried no thumbnail (or the slot is empty).
    std::vector<guild::u8> thumbnail; // kThumbWidth*kThumbHeight*3 bytes, or empty
    // Header info for the occupied-row detail line (rich 0x18D3): the in-game
    // player name (header name96 @+0x88) and the display wealth (@+0x54).
    std::string playerName;
    guild::u32  wealth = 0;
};

struct LoadGameScreenResult {
    bool confirmed = false;         // a save was picked -> proceed (gui run returned 1)
    bool back      = false;         // ESC / back row / window-close -> cancel
    bool quitByWindow = false;
    int  slot = -1;                 // the matched slot row (0..15)
    int  saveCount = 0;             // occupied slots listed
    std::string loadPath;           // byte_122F530 — "Gamedata\Saves\<name>.SAV" (1:1)
    std::string savePath;           // the picked slot's real openPath (for the session)
    std::string saveName;           // the picked slot's header name
    int  sessionFlags = 0;          // word_63C740 (== 10 on a confirmed pick)
    int  framesPresented = 0;
    int  hoveredRow = -1;
    bool usedRealText = false;      // real _OPTIONEN_* texts found + rendered
};

// The localized texts the screen renders (loaded by NAME from textbin so index
// alignment is irrelevant; English fallbacks otherwise).
struct LoadGameTexts {
    std::string title;     // _OPTIONEN_MENUE_LOAD+0                    (rich id 0x1864)
    std::string emptyFmt;  // _OPTIONEN_LOAD_GAME_SLOT_INFO_LEER+0/+1   (dword_8C9A04 fmt)
    std::string slotInfo0; // _OPTIONEN_LOAD_GAME_SLOT_INFO+0          (occupied row caption)
    std::string slotInfo1; // _OPTIONEN_LOAD_GAME_SLOT_INFO+1          (occupied row caption)
    std::string confirm;   // _OPTIONEN_LOAD_GAME_SICHERHEITS_ABFRAGE+0 (box 257 text)
};
bool LoadLoadGameTexts(const std::string& gameDir, LoadGameTexts& out);

// Build the 16-row slot view from the save directory — the load-bearing part of
// VIBE_SaveBrowser_LoadSlotMetadata @0x569d00 driven over `fs` through the REAL
// reconstructed leaves (enumerate @0x569530 / header @0x5a7af0 / slot @0x569c50).
// Returns the number of occupied slots. `out` must hold kLoadGameSlotCount rows.
int LoadGameBuildSlotViews(shim::IFileSystem& fs, const LoadGameScreenConfig& cfg,
                           LoadGameSlotView* out);

// Per-row layout of the load panel — NATIVE 800x600, CENTER-TRANSLATED by
// ((W-800)/2,(H-600)/2) (the proven main-menu model; PositionAtCoord mode-2
// @0x41d964). The form `menu\loadgame_new` (forms.BIN) places the outer window
// WIN0 at (128,136) 449x575 and the slot-list body WIN1 at (168,184) 390x493.
// VIBE_SaveBrowser_LoadSlotMetadata @0x569d00 builds each slot as a 130px-tall
// child window (AddChildWindow(0, 130*slot, .., 16, ..)) holding a 160x120 raw
// save thumbnail (Widget_AddRawBitmapToWindow(0,0,160,..)) plus a text label at
// x+168 (Object_AddTextLabel(168,..)) and a date/wealth sub-line — the list
// scrolls (Hud_BuildSliderPanel(532,360,..) @0x4bd388) when it exceeds the body.
// So: rowH=130; each row = [thumb 160x120 at rowX][name+info at rowX+168]; the
// 16 rows scroll inside the body viewport (py..py+viewH), with the scrollbar at
// scrollX. backX..backH is a synthetic back hit box (the real screen has none —
// ESC closes; kept for the host/tests). All in screen pixels (center-translated).
struct LoadGameScreenLayout {
    int px, py, pw, ph;            // body window WIN1 rect (screen px)
    int viewH;                     // visible list height (clipped to the screen)
    int cx, titleY;                // centre x + title baseline (title window)
    int rowH;                      // 130 (AddChildWindow y-stride @0x569e92)
    int rowX, rowW;                // row left + width (screen px)
    int thumbW, thumbH, nameDX;    // 160x120 thumbnail + name x-offset (168)
    int scrollX, scrollW;          // scrollbar track (Hud_BuildSliderPanel x=532)
    int backX, backY, backW, backH;
    int ox, oy;                    // center-translate offset ((W-800)/2,(H-600)/2)
    int W, H;                      // framebuffer
    // Row rect for slot i AT scroll offset `scroll` (px). Off-viewport rows return
    // a rect outside [py,py+viewH); the renderer/hit-test clip on that.
    void RowRect(int i, int scroll, int& rx, int& ry, int& rw, int& rh) const; // i:0..15
    // 0..15 = slot row, kLoadGameSlotCount (16) = the back row, -1 = none.
    int  HitRow(int mx, int my, int scroll) const;
    // Total scrollable content height (16 rows) and the max scroll offset.
    int  contentH() const { return kLoadGameSlotCount * rowH; }
    int  maxScroll() const { int m = contentH() - viewH; return m > 0 ? m : 0; }
};
LoadGameScreenLayout LoadGameComputeLayout(int W, int H);

// Render + run the load-game screen until a save is picked (confirm) or
// back/ESC/close (cancel). Drives gui::Menu_RunLoadGame (@0x56a270) underneath.
// `device` MUST be init()'d to cfg.fbW x cfg.fbH.
LoadGameScreenResult RunLoadGameScreen(shim::IGraphicsDevice& device,
                                       shim::IPlatform& plat,
                                       const LoadGameScreenConfig& cfg);

} // namespace guild::play
