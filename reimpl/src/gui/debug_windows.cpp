#include "gui/debug_windows.h"

namespace guild::gui {

namespace {
DebugCommandSink  g_defaultDebugSink;
DebugCommandSink* g_debugSink = &g_defaultDebugSink;

// Sequential child-object id allocator, standing in for VIBE_Form_GetChildObjectId /
// VIBE_Window_AddChildWindow / VIBE_Object_AddToWindow.  Only relative ordering and
// distinctness matter for the content/wiring tests.
int g_nextObj = 1000;
int AllocObj() { return g_nextObj++; }
} // namespace

void DebugWindow_SetCommandSink(DebugCommandSink* sink) {
    g_debugSink = sink ? sink : &g_defaultDebugSink;
}

// ===========================================================================
// ShowDummies @0x5362c4
// ===========================================================================
// The original: Window_Create(32,96,400,640,1044); SetColor(.,67); walk the dummy scene
// list (dword_63CD40 count).  Header "Dummy...Info$A" + separator "====...$A".  For each
// node: build a working buffer, append the localized name; if the supermap lookup fails
// (no smap / pos out of range) append "No smap or pos out of range  ", else if the tile
// type is 0 or 10 append "smap-pos is blocked  ".  Then RenderRichString("%s:$6T%s$A",
// nodeName, statusBuf).
std::vector<DebugLine> DebugWindow_BuildDummies(const std::vector<DummyNode>& nodes) {
    std::vector<DebugLine> lines;
    lines.push_back({kDummyHeader, {}});
    lines.push_back({kDummySep, {}});
    for (const DummyNode& n : nodes) {
        std::string status;
        if (!n.hasTile) {
            status = kDummyNoSmap;                       // No smap or pos out of range
        } else if (n.tileType == 0 || n.tileType == 10) {
            status = kDummyBlocked;                      // smap-pos is blocked
        }
        // The line carries the node name + the status text; both are strings, modelled
        // by storing them in `format` is not 1:1, so keep the format literal and the two
        // string operands implicit (they are tested via the status classification above).
        DebugLine line;
        line.format = std::string(kDummyLineFmt) + "|" + n.name + "|" + status;
        lines.push_back(line);
    }
    return lines;
}

// ===========================================================================
// ShowScriptInfo @0x5364e4
// ===========================================================================
std::vector<DebugLine> DebugWindow_BuildScriptInfo(
    const std::vector<ScriptEntry>& scripts, int runningCount) {
    std::vector<DebugLine> lines;
    lines.push_back({"$C", {}});
    lines.push_back({kScriptHeaderCount, {runningCount}});
    lines.push_back({kScriptColumns, {}});
    lines.push_back({kScriptSep, {}});
    i32 total = 0;
    for (const ScriptEntry& s : scripts) {
        if (!s.active)
            continue;
        // "%i  %s   %i   %i   %i$A" : id, name, funcCount, varCount, usedMem
        lines.push_back({kScriptRowFmt, {s.id, s.funcCount, s.varCount, s.usedMem}});
        total += s.usedMem;   // v0 += VIBE_Script_GetVariableAddress(...) accumulator
    }
    lines.push_back({kScriptTotalFmt, {total}});
    return lines;
}

// ===========================================================================
// ShowMarketInfo @0x536840
// ===========================================================================
std::vector<DebugLine> DebugWindow_BuildMarketInfo(
    const std::vector<MarketRegion>& regions) {
    std::vector<DebugLine> lines;
    lines.push_back({"$C", {}});
    // Outer loop: up to 4 regions, stop at the first with an empty name (byte_13CD6A0[v4]).
    int emitted = 0;
    for (const MarketRegion& r : regions) {
        if (emitted >= kMarketRegions || r.name.empty())
            break;
        lines.push_back({kMarketRegionHeader, {}});  // "$N%s:$A" (name arg)
        lines.push_back({kMarketSep, {}});
        int rowCount = 0;
        for (const MarketRow& row : r.rows) {
            if (rowCount >= kMarketRowsPerRegion)
                break;
            ++rowCount;
            if (!row.present)   // word_13C3B60[..] && dword_13C3B64[..]
                continue;
            // "%s:$3T %i $5T %2f$7T %T$9T %T  %i %i %i %i$A"
            lines.push_back({kMarketRowFmt,
                             {row.protId, row.marketPrice, row.colF, row.colG,
                              row.priceA, row.priceB, row.priceC, row.priceD}});
        }
        ++emitted;
    }
    return lines;
}

// ===========================================================================
// ShowPotions @0x537394
// ===========================================================================
std::vector<DebugLine> DebugWindow_BuildPotions(
    const std::vector<PotionPlayer>& players) {
    std::vector<DebugLine> lines;
    lines.push_back({"$C", {}});
    for (const PotionPlayer& p : players) {
        if (!p.present)
            continue;
        bool headerEmitted = false;
        for (const PotionRow& row : p.rows) {
            if (!headerEmitted) {
                // Lazily emit "$N%s:$A" + separator before the first matching row (the
                // original's `if (!v6)` guard inside the per-item loop).
                lines.push_back({kPotionHeader, {}});
                lines.push_back({kPotionSep, {}});
                headerEmitted = true;
            }
            // "%s$3T %i$A" : label (2*itemId+2151), count
            lines.push_back({kPotionRowFmt, {2 * row.itemId + 2151, row.count}});
        }
    }
    return lines;
}

// ===========================================================================
// ShowPlayerList @0x537884
// ===========================================================================
std::vector<PlayerListRow> DebugWindow_BuildPlayerListRows(
    const std::vector<PlayerListEntry>& players, int nobodyHeCount) {
    std::vector<PlayerListRow> rows;
    bool right = false;
    // Row 0: the "NIEMAND" (nobody) entry — handle 0, always left column.
    rows.push_back({kPlayerNobodyFmt, kPlayerButtonId, 0, nobodyHeCount, false});
    int rowCount = 1;  // v13 starts at 1 after the NIEMAND row
    for (const PlayerListEntry& p : players) {
        if (!p.present)
            continue;
        rows.push_back({kPlayerRowFmt, kPlayerButtonId, p.handle, p.heCount, right});
        ++rowCount;
        // After 63 rows, switch to the second (right) column for subsequent rows.
        if (rowCount >= kPlayerListPageRows) {
            right = true;
            rowCount = 0;
        }
    }
    return rows;
}

// ===========================================================================
// PlayerActionMenu @0x5374ec
// ===========================================================================
// Mode-name table dword_5360B0[3] (0x5360bc), 32-byte stride, 15 entries.
const char* const kActionModeNames[kActionModeCount] = {
    "DUMMY_NPC", "MEISTER_NPC", "POLITICIAN_NPC", "AI_NPC", "NPC",
    "PLAYER", "REMOTE_PLAYER", "RELATIVE_PLAYER", "USED_PLAYER", "INVISIBLE_NPC",
    "ESKORTE", "RICHTER", "TMP_NPC", "PEST", "DEAD_PLAYER",
};

const char* DebugWindow_ActionModeName(int mode) {
    if (mode < 0 || mode >= kActionModeCount)
        return "";
    return kActionModeNames[mode];
}

// The original emits the title ("$Z$[%1N4$]$N$L") + mode line, then 4 buttons via
// RenderRichString("%ib[..]$A", 1210); each button's GetChildObjectId is captured.
ActionMenuLayout DebugWindow_BuildActionMenu(int formId) {
    ActionMenuLayout l{};
    l.formId = formId;
    l.killObj     = AllocObj();  // [Kill player]
    l.showHeObj   = AllocObj();  // [Show He]
    l.marriageObj = AllocObj();  // [Marriage]
    l.getChildObj = AllocObj();  // [Get Child]
    return l;
}

PlayerAction DebugWindow_ActionForClick(const ActionMenuLayout& l, int clickedObj) {
    if (clickedObj == l.killObj)     return PlayerAction::kKill;
    if (clickedObj == l.showHeObj)   return PlayerAction::kShowHe;
    if (clickedObj == l.marriageObj) return PlayerAction::kMarriage;
    if (clickedObj == l.getChildObj) return PlayerAction::kGetChild;
    return PlayerAction::kNone;
}

// ===========================================================================
// SceneChecker @0x537bd4
// ===========================================================================
SceneCheckerLayout DebugWindow_BuildSceneChecker(int formId) {
    SceneCheckerLayout l{};
    l.formId     = formId;
    l.dummiesObj = AllocObj();  // %ib Dummies checken...$N
    l.scriptObj  = AllocObj();  // %ib Script-Info$N
    l.memoryObj  = AllocObj();  // %ib Memory-Info$N
    l.marketObj  = AllocObj();  // %ib Market-Info$N
    l.theatreObj = AllocObj();  // %ib[Theatre]$N
    l.potionsObj = AllocObj();  // %ib[Show potions]$N
    l.playerObj  = AllocObj();  // %ib[Show player]$N
    return l;
}

SceneCheckerAction DebugWindow_SceneActionForClick(const SceneCheckerLayout& l,
                                                   int clickedObj) {
    if (clickedObj == l.dummiesObj) return SceneCheckerAction::kDummies;
    if (clickedObj == l.scriptObj)  return SceneCheckerAction::kScriptInfo;
    if (clickedObj == l.memoryObj)  return SceneCheckerAction::kMemoryInfo;
    if (clickedObj == l.marketObj)  return SceneCheckerAction::kMarketInfo;
    if (clickedObj == l.theatreObj) return SceneCheckerAction::kTheatre;
    if (clickedObj == l.potionsObj) return SceneCheckerAction::kPotions;
    if (clickedObj == l.playerObj)  return SceneCheckerAction::kPlayerList;
    return SceneCheckerAction::kNone;
}

// ===========================================================================
// ShowSupermap @0x537fc8 (also the SceneChecker window-1 render) tile classification.
// ===========================================================================
SupermapCell DebugWindow_ClassifySupermapTile(int tileType, int tileGray) {
    SupermapCell c{0, 0, true};
    switch (tileType) {
        case 0:
        case 10:
        case 13: {
            // grayscale floor: palette index = tileGray + 32, clamped to 255.
            int g = tileGray + 32;
            if (g > 255) g = 255;
            c.color = g;
            c.altFlag = (tileType == 13) ? -1 : 0;
            break;
        }
        case 11:
            c.color = 0x60;  // 96
            c.altFlag = 0;
            break;
        case 12:
            c.color = 0;
            c.altFlag = 0;
            break;
        default:
            // The original draws with the tile's gray pair (color = v20[3*g]); here the
            // caller supplies the resolved gray as `tileGray`.
            c.color = tileGray;
            c.altFlag = 0;
            break;
    }
    return c;
}

} // namespace guild::gui
