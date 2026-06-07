#include "gui/loadgame.h"

// guild::gui — the Load Game screen.
//
// gilde.exe 0x56a270.  See loadgame.h for the recovery notes.  The slider panel build,
// the slot-metadata loader, the confirm message-box and the form lifecycle live in
// unowned/already-translated modules (routed through the host sink); this module owns the
// slot-match -> path-build -> session-flag dispatch and the frame-loop shape.

namespace guild::gui {

// gilde.exe 0x56a392 — the slot scan.  The original walks the 544-byte rows (v8 += 544,
// v8 < 8704), skipping rows whose present flag (row[12]) is clear or whose obj id
// (*(row+8)) is -1, and breaks on the row whose widget id (*(row+4)) equals the hovered
// id (dword_62D22C).  We model the primary widget-id match.
int LoadGame_MatchSlot(const SaveSlot* slots, int count, int hoveredWidgetId) {
    for (int i = 0; i < count; ++i) {
        const SaveSlot& s = slots[i];
        if (!s.present)           // row[12] == 0 -> skip
            continue;
        if (s.objId == -1)        // *(row+8) == -1 -> empty -> skip
            continue;
        if (s.widgetId == hoveredWidgetId) // dword_62D22C == *(row+4)
            return i;
    }
    return -1;
}

// gilde.exe 0x56a3f5 — Crt_Sprintf(buf, "Gamedata\\Saves\\%s.SAV", slotName).
std::string LoadGame_BuildPath(const std::string& slotName) {
    return "Gamedata\\Saves\\" + slotName + ".SAV";
}

// gilde.exe 0x56a270 — the load-game frame loop + slot dispatch.
LoadGameResult LoadGame_Run(LoadGameHost& host, const SaveSlot* slots, int count,
                            int maxFrames) {
    LoadGameResult r; // v20 = 0 (nothing chosen)
    int frames = 0;
    while (host.RunFrame()) {                 // 0x56a360 RunFrameLoop
        if (host.CloseRequested())            // 0x56a36d dword_672230
            break;                            // dword_631614 = 1 -> loop ends next frame
        if (host.Clicked()) {                 // 0x56a380 dword_672228
            int slot = LoadGame_MatchSlot(slots, count, host.HoveredWidget());
            if (slot >= 0) {
                // 0x56a49f: if (!byte_63CC40 || RunMessageBox(257)) accept the load.
                bool accept = !host.ConfirmGateActive() || host.ConfirmLoad();
                if (accept) {
                    r.chosen      = true;     // v20 = 1
                    r.slot        = slot;
                    r.path        = LoadGame_BuildPath(slots[slot].name); // 0x56a3f5
                    r.sessionFlag = kSessionLoad;  // word_63C740 = 10  (0x56a426)
                    break;                    // dword_631614 = 1 -> close
                }
            }
        }
        if (maxFrames > 0 && ++frames >= maxFrames)
            break;                            // test guard (not in original)
    }
    return r;
}

} // namespace guild::gui
