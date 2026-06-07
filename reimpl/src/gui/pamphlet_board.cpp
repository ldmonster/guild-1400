#include "gui/pamphlet_board.h"

namespace guild::gui {

namespace {

PamphletCommandSink  g_defaultSink;
PamphletCommandSink* g_sink = &g_defaultSink;

constexpr int kPamObjBase = 2400;

} // namespace

void PamphletBoard_SetCommandSink(PamphletCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// gilde.exe 0x549238 (layout half).
//   v6 = 1; v30 (odd slot) = 1; for each handler while v6 < 5:
//     if (FindRecordById(author)) {
//       SelectWindow(form, v30) RenderRichString(5410, flagByte+5413, *author);
//       SelectWindow(form, 2*v6) RenderRichString(5411,2) -> GetChildObjectId stored;
//       remember handler entity; ++v6; v30 += 2; SetVisibleRecursive(win,1);
//     }
PamphletLayout PamphletBoard_Build(const std::vector<Pamphlet>& pamphlets) {
    PamphletLayout l{};
    l.form = kFormPamphlet;

    int v6 = 1; // 1-based entry counter (the original starts v6=1 and stops at >=5)
    for (const auto& p : pamphlets) {
        if (v6 >= 5) break;            // at most 4 entries
        if (p.author == 0) continue;   // FindRecordById failed -> skip
        PamphletEntry e{};
        e.handler  = p.handler;
        e.oddSlot  = 2 * v6 - 1;       // 1, 3, 5, 7
        e.evenSlot = 2 * v6;           // 2, 4, 6, 8
        e.objectId = kPamObjBase + (v6 - 1);
        l.entries.push_back(e);
        ++v6;
    }
    return l;
}

// gilde.exe 0x549238 (wiring half).
//   if (dword_62D22C matches a stored object) { handler = stored;
//     if (CheckSkillRequirement) { copy timestamp; QueueRequestEntity29(handler);
//       on success RequestBuildOp90; } }
int PamphletBoard_Dispatch(const PamphletLayout& l, int clickedObj, bool passesSkill) {
    for (const auto& e : l.entries) {
        if (e.objectId == clickedObj) {
            if (passesSkill)
                g_sink->Sign(e.handler); // entity-29 + op-90
            return e.handler;
        }
    }
    return -1;
}

} // namespace guild::gui
