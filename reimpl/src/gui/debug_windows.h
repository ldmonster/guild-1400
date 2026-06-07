#pragma once
// guild::gui — the remaining debug / utility window builders.
//
// gilde.exe ships a small cluster of developer/debug overlays, reachable from the
// "scene checker" menu (misc\scenechecker).  Each is a modal frame loop that creates a
// 400x640 software window (Window_Create(32,96,400,640,1044)) — except the menu/popup
// builders which load a .form — and emits localized "rich-string" content lines.  This
// module recovers, 1:1:
//
//   VIBE_DebugWindow_ShowDummies      @0x5362c4 — list every "dummy" scene node + whether
//       its supermap tile is blocked ("smap-pos is blocked"/"No smap or pos out of range").
//   VIBE_DebugWindow_ShowScriptInfo   @0x5364e4 — table of active scripts (id/name/funcCnt/
//       varCnt/usedMem) + a running total memory line.
//   VIBE_DebugWindow_ShowMarketInfo   @0x536840 — per-region market price table.
//   VIBE_DebugWindow_ShowPotions      @0x537394 — per-player potion inventory.
//   VIBE_DebugWindow_PlayerActionMenu @0x5374ec — popup (misc\Messagebox_BIG) with 4 action
//       buttons (Kill player / Show He / Marriage / Get Child) wired to game commands.
//   VIBE_DebugWindow_ShowPlayerList   @0x537884 — two-column clickable player list (each row
//       opens the PlayerActionMenu for that player).
//   VIBE_DebugWindow_SceneChecker     @0x537bd4 — the root menu (misc\scenechecker): 7 action
//       buttons that launch the windows above + a live supermap render in window slot 1.
//   VIBE_DebugWindow_ShowSupermap     @0x537fc8 — standalone supermap render (misc\ShowSupermap).
//
// What this module owns and translates 1:1 is each builder's *content / window-tree*:
//   - the recovered .form names + window geometry,
//   - the exact emitted line list (text ids / format strings, in order),
//   - the child-window set + ids/positions,
//   - the menu entry set + the entry -> action/command wiring.
// The modal frame loop (VIBE_GameLogic_RunFrameLoop), the glyph/text engine
// (VIBE_Text_RenderRichString), the supermap paintbox (VIBE_Paintbox_DrawScaledRegion)
// and the window/widget alloc leaves live in other clusters; they are forward-declared
// and routed through small mockable hooks so the content/layout/wiring is testable in
// isolation.  Mutating actions go through a command sink (mock).

#include "gui/types.h"
#include <array>
#include <string>
#include <vector>

namespace guild::gui {

// ---------------------------------------------------------------------------
// The standard debug-window geometry, shared by every Window_Create debug screen
// (ShowDummies / ShowScriptInfo / ShowMemoryInfo / ShowMarketInfo / ShowPotions /
// ShowPlayerList).  Window_Create(32, 96, 400, 640, 1044).
// ---------------------------------------------------------------------------
inline constexpr int kDbgWinX = 32;
inline constexpr int kDbgWinY = 96;
inline constexpr int kDbgWinW = 400;
inline constexpr int kDbgWinH = 640;
inline constexpr i32 kDbgWinFlags = 1044;  // 0x414
inline constexpr int kDbgWinColor = 67;    // Object_SetColor(window-backing, 67)

// The frame-loop "form id" arg the debug windows pass to RunFrameLoop (decimal 415687).
inline constexpr int kDbgFrameLoop = 415687;

// ---------------------------------------------------------------------------
// .form resource names (recovered byte-for-byte).
// ---------------------------------------------------------------------------
inline constexpr const char* kFormSceneChecker  = "misc\\scenechecker";
inline constexpr const char* kFormShowSupermap  = "misc\\ShowSupermap";
inline constexpr const char* kFormMessageboxBig = "misc\\Messagebox_BIG";

// ---------------------------------------------------------------------------
// One emitted debug content line (header lines + per-row data lines).
//   `format` is the localized format string (or "$C"/separator header literal);
//   `args` are the integer arguments substituted into it, in order.  String args are
// modelled by their numeric id (text/label id) so the line list is comparable.
// ---------------------------------------------------------------------------
struct DebugLine {
    std::string format;     // RenderRichString format string / header literal
    std::vector<i32> args;  // integer args (in call order)
};

// ===========================================================================
// VIBE_DebugWindow_ShowDummies @0x5362c4
// ===========================================================================
// One dummy scene node: its name + whether its supermap tile is walkable.
struct DummyNode {
    std::string name;     // dword_122FCC0[i] node name
    bool hasTile = true;  // false => "No smap or pos out of range" (smap/pos lookup failed)
    int  tileType = 0;    // heightmap tile type byte; 0 or 10 => "smap-pos is blocked"
};
// Header literals (aDummyInfoA / aA_6).
inline constexpr const char* kDummyHeader = "Dummy\t\t\t\t\t\t\t\t\t\tInfo$A";
inline constexpr const char* kDummySep    = "========================================$A";
inline constexpr const char* kDummyBlocked = "smap-pos is blocked  ";
inline constexpr const char* kDummyNoSmap  = "No smap or pos out of range  ";
inline constexpr const char* kDummyLineFmt = "%s:$6T%s$A";
// Build the content line list: header, separator, then one "%s:$6T%s$A" line per node
// whose status text is "blocked"/"out of range" when the tile is bad, else empty.
std::vector<DebugLine> DebugWindow_BuildDummies(const std::vector<DummyNode>& nodes);

// ===========================================================================
// VIBE_DebugWindow_ShowScriptInfo @0x5364e4
// ===========================================================================
struct ScriptEntry {
    bool active = false;   // emitted only when slot[0] != 0 || (slot[164] & 1)
    i32 id = 0;            // +128
    std::string name;      // the script name (string arg)
    i32 funcCount = 0;     // +144
    i32 varCount = 0;      // +148
    i32 usedMem = 0;       // VIBE_Script_GetVariableAddress(slot)
};
inline constexpr int kScriptSlots  = 128;   // loop 0..330752 step 2584 -> 128 slots
inline constexpr int kScriptStride = 2584;
inline constexpr const char* kScriptHeaderCount = "Scripts running no:%i$N";
inline constexpr const char* kScriptColumns =
    "Id       Name     \t\t\t      FunctionCnt  VarCnt   UsedMem$A";
inline constexpr const char* kScriptSep =
    "============================================================$A";
inline constexpr const char* kScriptRowFmt = "%i  %s   %i   %i   %i$A";
inline constexpr const char* kScriptTotalFmt = "Total-Memory:%i$N";
// Build the content lines: "$C", count line (running script count), column header,
// separator, one row per active script, then the "Total-Memory" line (sum of usedMem
// over the active rows, matching the original's `v0` accumulator).
std::vector<DebugLine> DebugWindow_BuildScriptInfo(const std::vector<ScriptEntry>& scripts,
                                                   int runningCount);

// ===========================================================================
// VIBE_DebugWindow_ShowMarketInfo @0x536840
// ===========================================================================
// One market commodity row within a region.
struct MarketRow {
    bool present = false;  // emitted only when word_13C3B60[..] && dword_13C3B64[..]
    i32 protId = 0;        // resolved prototype id (the string label arg, +1 in the original)
    i32 priceA = 0; i32 priceB = 0; i32 priceC = 0; i32 priceD = 0; // +4..+7 columns
    i32 marketPrice = 0;   // VIBE_Building_ComputeMarketPrice(prot, 100)
    i32 colF = 0; i32 colG = 0;  // converted float columns (modelled as ints)
};
struct MarketRegion {
    std::string name;                 // byte_13CD6A0[..] region name (header arg)
    std::vector<MarketRow> rows;      // up to 62 commodities
};
inline constexpr int kMarketRegions = 4;     // outer loop bound (v26 < 4)
inline constexpr int kMarketRowsPerRegion = 62;
inline constexpr const char* kMarketRegionHeader = "$N%s:$A";
inline constexpr const char* kMarketSep = "=================$A";
inline constexpr const char* kMarketRowFmt = "%s:$3T %i $5T %2f$7T %T$9T %T  %i %i %i %i$A";
// Build the content lines: "$C", then for each region (max 4, until name empty): a
// header + separator + one row per present commodity.
std::vector<DebugLine> DebugWindow_BuildMarketInfo(const std::vector<MarketRegion>& regions);

// ===========================================================================
// VIBE_DebugWindow_ShowPotions @0x537394
// ===========================================================================
struct PotionRow {
    i32 itemId = 0;   // *v5 (label = 2*itemId+2151)
    i32 count = 0;    // +7
};
struct PotionPlayer {
    bool present = false;       // word_12CE910[i] != -1
    std::string name;           // player name (header arg)
    std::vector<PotionRow> rows;
};
inline constexpr int kPotionPlayers = 768;  // loop 0..205824 step 268
inline constexpr const char* kPotionHeader = "$N%s:$A";
inline constexpr const char* kPotionSep    = "=================$A";
inline constexpr const char* kPotionRowFmt = "%s$3T %i$A";
// Build the content lines: "$C", then for each present player with >=1 potion row a
// header + separator (emitted lazily before the first row) + one "%s$3T %i$A" row each.
std::vector<DebugLine> DebugWindow_BuildPotions(const std::vector<PotionPlayer>& players);

// ===========================================================================
// VIBE_DebugWindow_ShowPlayerList @0x537884
// ===========================================================================
// Two side-by-side child windows (each 400x320 at x=0 / x=320), 16-flag, paged at 63
// rows.  Row 0 is always the "NIEMAND" (nobody) entry; the rest are live players.
inline constexpr int kPlayerListChildW = 400;
inline constexpr int kPlayerListChildH = 320;
inline constexpr int kPlayerListRightX = 320;     // second child window x origin
inline constexpr int kPlayerListChildFlags = 16;
inline constexpr int kPlayerListPageRows = 63;    // v13 >= 63 -> switch to second column
inline constexpr int kPlayerListMaxRows  = 768;   // word_12CE910 walk bound
inline constexpr const char* kPlayerNobodyFmt = "%ib[NIEMAND]$6THeCnt:%i$A"; // row 0
inline constexpr const char* kPlayerRowFmt    = "%ib[%1N4]$6THeCnt:%i$A";    // live rows
inline constexpr int kPlayerButtonId = 1210;  // %ib argument (clickable button id)

struct PlayerListEntry {
    bool present = false;  // word_12CE910[i] != -1
    i32 handle = 0;        // player handle (the %1N4 name arg)
    i32 heCount = 0;       // VIBE_He_CountMatchingHandlers(player)
};
// The window tree the builder allocates.
struct PlayerListLayout {
    int mainWindow = -1;
    int leftWindow = -1;   // AddChildWindow(0,   0, 400, 320, 16, main)
    int rightWindow = -1;  // AddChildWindow(320, 0, 400, 320, 16, main)
};
// One emitted player-list row (the "NIEMAND" header plus each present player).
struct PlayerListRow {
    std::string format;  // kPlayerNobodyFmt / kPlayerRowFmt
    i32 buttonId;        // 1210
    i32 handle;          // 0 for NIEMAND, else player handle
    i32 heCount;
    bool rightColumn;    // true once the 63-row page break moves to the second window
};
// Build the row list: "NIEMAND" first, then one row per present player; rows past index
// 62 (kPlayerListPageRows) move to the right column.  Returns the rows in emit order.
std::vector<PlayerListRow> DebugWindow_BuildPlayerListRows(
    const std::vector<PlayerListEntry>& players, int nobodyHeCount);

// ===========================================================================
// VIBE_DebugWindow_PlayerActionMenu @0x5374ec
// ===========================================================================
// Popup loaded from misc\Messagebox_BIG with 4 action buttons.  The mode-name table
// (dword_5360B0[3], 32-byte stride) maps a player's mode byte to a display name.
inline constexpr int kActionModeStride = 32;
inline constexpr int kActionModeCount  = 15;
// The 15 mode names, recovered byte-for-byte from the 0x5360bc table.
extern const char* const kActionModeNames[kActionModeCount];
const char* DebugWindow_ActionModeName(int mode);  // bounds-checked lookup

inline constexpr const char* kActionTitleFmt = "$Z$[%1N4$]$N$L";
inline constexpr const char* kActionModeFmt  = "Mode:%s$A";
inline constexpr const char* kActionKill     = "%ib[Kill player]$A";
inline constexpr const char* kActionShowHe   = "%ib[Show He]$A";
inline constexpr const char* kActionMarriage = "%ib[Marriage]$A";
inline constexpr const char* kActionGetChild = "%ib[Get Child]$A";
inline constexpr int kActionButtonId = 1210;

enum class PlayerAction { kNone, kKill, kShowHe, kMarriage, kGetChild };

// The 4 button child-object ids the menu builds (GetChildObjectId per button).
struct ActionMenuLayout {
    int formId = -1;
    int killObj = -1;     // [Kill player]
    int showHeObj = -1;   // [Show He]
    int marriageObj = -1; // [Marriage]
    int getChildObj = -1; // [Get Child]
};

struct DebugCommandSink {
    virtual ~DebugCommandSink() = default;
    // [Kill player] -> Building_AdjustStockAndNotify (negative health delta).
    virtual void KillPlayer(int /*player*/) {}
    // [Marriage] -> EnqueueBuildingActionStart("marriage") + delta packets.
    virtual void Marriage(int /*player*/, int /*partner*/) {}
    // [Get Child] -> EnqueueObjectInteraction(8, ...).
    virtual void GetChild(int /*motherPlayer*/, int /*fatherPlayer*/) {}
    // [Show He] toggles object visibility only (no command).
};
void DebugWindow_SetCommandSink(DebugCommandSink* sink);

// Build the action-menu layout (assigns child-object ids for the 4 buttons).
ActionMenuLayout DebugWindow_BuildActionMenu(int formId);
// Map a clicked child-object id to its PlayerAction (the dword_62D22C == id dispatch).
PlayerAction DebugWindow_ActionForClick(const ActionMenuLayout& l, int clickedObj);

// ===========================================================================
// VIBE_DebugWindow_SceneChecker @0x537bd4 — the root menu.
// ===========================================================================
// 7 action buttons, each a "%ib ...$N" line in form window 0; window slot 1 holds the
// live supermap render.  Each button click launches one of the windows above.
enum class SceneCheckerAction {
    kNone, kDummies, kScriptInfo, kMemoryInfo, kMarketInfo, kTheatre, kPotions, kPlayerList
};
inline constexpr const char* kSceneDummies   = "%ib Dummies checken...$N";
inline constexpr const char* kSceneScript     = "%ib Script-Info$N";
inline constexpr const char* kSceneMemory     = "%ib Memory-Info$N";
inline constexpr const char* kSceneMarket     = "%ib Market-Info$N";
inline constexpr const char* kSceneTheatre    = "%ib[Theatre]$N";
inline constexpr const char* kScenePotions    = "%ib[Show potions]$N";
inline constexpr const char* kScenePlayer     = "%ib[Show player]$N";
inline constexpr int kSceneButtonId = 1210;
inline constexpr int kSceneSupermapWindow = 1;  // SelectWindow(form, 1) — supermap render

// The 7 menu-button child-object ids the SceneChecker builds (in build order).
struct SceneCheckerLayout {
    int formId = -1;
    int dummiesObj = -1;
    int scriptObj = -1;
    int memoryObj = -1;
    int marketObj = -1;
    int theatreObj = -1;
    int potionsObj = -1;
    int playerObj = -1;
};
SceneCheckerLayout DebugWindow_BuildSceneChecker(int formId);
SceneCheckerAction DebugWindow_SceneActionForClick(const SceneCheckerLayout& l,
                                                   int clickedObj);

// ===========================================================================
// VIBE_DebugWindow_ShowSupermap @0x537fc8 — standalone supermap render.
// ===========================================================================
inline constexpr int kSupermapAnchor   = 3;       // Window_PositionAtCoord(form, 3)
inline constexpr int kSupermapFrameLoop = 161735;  // RunFrameLoop arg
inline constexpr int kSupermapRefreshMod = 8;      // render every (frame % 8 == 0)
inline constexpr int kSupermapWindow   = 1;        // SelectWindow(form, 1)

// The supermap render walks a WxW tile grid (a1[8] = width, square) and emits one
// paintbox "draw" per tile.  Tile-type byte semantics (recovered from the switch):
//   0 / 10 / 13 : grayscale floor (palette index = tileGray + 32, clamped to 255);
//                 type 13 sets the "alt" flag to -1.
//   11          : draw with color 0x60, flag 0.
//   12          : draw grayscale 0, flag 0.
//   else        : draw with the tile's gray pair (v20[3*g], v20[3*g+2]).
// Tile -> (color, altFlag) classification, for the render-content test.
struct SupermapCell { int color; int altFlag; bool drawn; };
SupermapCell DebugWindow_ClassifySupermapTile(int tileType, int tileGray);

} // namespace guild::gui
