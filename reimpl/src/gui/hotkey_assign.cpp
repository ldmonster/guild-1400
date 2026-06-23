#include "gui/hotkey_assign.h"

namespace guild::gui {

namespace {

// ---- defaults for the cross-module leaves -------------------------------------------
bool DefBuildingExists(i32) { return true; }
bool DefObjectExists(i32) { return true; }
bool DefPump() { return false; }
int  DefSelectedRow() { return -1; }
int  DefPendingAction() { return 0; }

HotkeyWindowHooks g_hooks = {
    &DefBuildingExists, &DefObjectExists, &DefPump, &DefSelectedRow, &DefPendingAction,
};

} // namespace

void SetHotkeyWindowHooks(const HotkeyWindowHooks& hooks) {
    g_hooks = hooks;
    if (!g_hooks.buildingExists) g_hooks.buildingExists = &DefBuildingExists;
    if (!g_hooks.objectExists)   g_hooks.objectExists   = &DefObjectExists;
    if (!g_hooks.pumpFrame)      g_hooks.pumpFrame      = &DefPump;
    if (!g_hooks.selectedRow)    g_hooks.selectedRow    = &DefSelectedRow;
    if (!g_hooks.pendingAction)  g_hooks.pendingAction  = &DefPendingAction;
}

const HotkeyWindowHooks& GetHotkeyWindowHooks() { return g_hooks; }

// gilde.exe 0x4ff1d1..0x4ff3ae — the 11-row build loop.
std::vector<HotkeyWindowRow> Hotkey_BuildRows() {
    std::vector<HotkeyWindowRow> rows;
    rows.reserve(guild::play::kHotkeySlotCount);
    // The original walks dword_122DC14[3*i] / dword_122DC18[3*i]; reuse the play-layer
    // slot table (g_hotkeySlots) — the same byte_122DC10 block.
    for (int i = 0; i < guild::play::kHotkeySlotCount; ++i) {
        const guild::play::HotkeySlot& s = guild::play::g_hotkeySlots[i];
        HotkeyWindowRow r;
        r.slot = i;
        // 0x4ff22d: v7 = dword_122DC14[i]; if (v7 != -1) v6 = FindById(v7);
        r.buildingId = s.buildingId;
        bool buildingPresent = (s.buildingId != -1) && g_hooks.buildingExists(s.buildingId);
        r.hasBuilding = buildingPresent;
        // 0x4ff24a: if (dword_122DC18[i] != -1) ObjectById = FindObjectById(...);
        r.objectId = s.objectId;
        bool objectPresent = (s.objectId != -1) && g_hooks.objectExists(s.objectId);
        r.hasObject = objectPresent;
        // 0x4ff25f: if (v6 /*building*/) { "%2N8~ %1G" label color 67 }
        //           else                 { dword_8CA78C placeholder label color 66 }
        r.buildingLabelColor = buildingPresent ? kHotkeyColorPresent : kHotkeyColorAbsent;
        // 0x4ff2d5: if (ObjectById) { "%1s" label color 67 }
        r.objectLabelColor = kHotkeyColorPresent;
        rows.push_back(r);
    }
    return rows;
}

// 0x4ff498 — SetEnabled(assign, selectedRow != -1).
bool Hotkey_WindowAssignEnabled(int selectedRow) {
    return selectedRow != -1;
}

// 0x4ff465 — SetEnabled(clear, selectedRow != -1 && dword_122DC14[3*row] != -1).
bool Hotkey_WindowClearEnabled(int selectedRow) {
    if (selectedRow < 0 || selectedRow >= guild::play::kHotkeySlotCount)
        return false;
    return guild::play::g_hotkeySlots[selectedRow].buildingId != -1;
}

// gilde.exe 0x4ff070 — VIBE_Hotkey_OpenAssignWindow.
int Hotkey_OpenAssignWindow(std::vector<HotkeyWindowRow>* outRows) {
    // 0x4ff07c: ValidateAssignments() — drop stale assignments before display.
    guild::play::Hotkey_ValidateAssignments();

    // 0x4ff132..0x4ff3cd: build the radio rows (LABEL_2 entry).
    std::vector<HotkeyWindowRow> rows = Hotkey_BuildRows();

    int frames = 0;
    // 0x4ff3db: while (1) { ...; if (!RunFrameLoop(...)) break; if (changed) goto LABEL_2; }
    while (g_hooks.pumpFrame()) {
        ++frames;
        int row = g_hooks.selectedRow();
        int action = g_hooks.pendingAction();
        bool changed = false;

        if (action == 2) {
            // 0x4ff405: clear the selected slot (byte_122DC10[..]+4/+8 := -1).
            if (row >= 0 && row < guild::play::kHotkeySlotCount) {
                guild::play::g_hotkeySlots[row].buildingId = -1;
                guild::play::g_hotkeySlots[row].objectId   = -1;
                changed = true;                 // v14 = 1
            }
        } else if (action == 1) {
            // 0x4ff452: AssignFromSelection(row, ...) — delegate to the play-layer.
            if (row >= 0 && row < guild::play::kHotkeySlotCount) {
                guild::play::Hotkey_AssignFromSelection(row, 0);
                changed = true;                 // v14 = 1
            }
        }

        // The per-frame enable updates (modelled; observable via the *Enabled queries).
        (void)Hotkey_WindowClearEnabled(row);
        (void)Hotkey_WindowAssignEnabled(row);

        if (changed) {
            // 0x4ff4bb: goto LABEL_2 — rebuild the row list from the mutated table.
            rows = Hotkey_BuildRows();
        }
    }

    if (outRows)
        *outRows = rows;
    return frames;
}

} // namespace guild::gui
