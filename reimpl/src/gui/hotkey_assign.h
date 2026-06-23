#pragma once
// guild::gui — building-hotkey assignment WINDOW (0x4ff070): the modal orchestration.
//
// gilde.exe 0x4ff070 — VIBE_Hotkey_OpenAssignWindow loads form "special\geb_hotkeys",
// builds an 11-row radio list (one per hotkey slot) showing each slot's assigned
// building (label "%2N8~ %1G") + object (label "%1s"), and runs a modal loop where the
// user can ASSIGN the current selection to the selected slot or CLEAR the selected slot.
// On open it validates every existing assignment.
//
// The slot *table* logic (validate / assign / clear / the 11-slot byte_122DC10 table)
// is already reconstructed 1:1 in guild::play (src/play/input_recon4_hotkey.{h,cpp}):
//   VIBE_Hotkey_ValidateAssignments @0x4fee18, VIBE_Hotkey_AssignFromSelection @0x4fef54,
//   guild::play::g_hotkeySlots / kHotkeySlotCount.  This module REUSES those — it does
//   NOT redefine the table — and adds the window-builder layer that was deferred there
//   (input_recon4_hotkey.h:37 notes 0x4ff070 as the "huge Form/Hud/RadioGroup" leaf).
//
// What this module reconstructs from 0x4ff070 (the part that is engine logic, not the
// SDL/Vulkan widget plumbing):
//   - the 11-row build sequence (one radio button per slot; per-slot building/object
//     label rows; building present -> "%2N8~ %1G" label color 67, absent -> empty
//     label color 66; object present -> "%1s" label color 67),
//   - the modal loop's CLEAR action (slot.building/object := -1),
//   - the modal loop's ASSIGN action (delegates to play::Hotkey_AssignFromSelection),
//   - the per-frame button-enable logic (clear enabled iff the selected row holds a
//     building; assign enabled iff a row is selected),
//   - the rebuild-on-change (after assign/clear the row list is rebuilt: LABEL_2).
//
// The form load, radio-group widget creation, text labels and the modal frame loop are
// the SDL/Vulkan widget boundary (rules 3-4); they are routed through HotkeyWindowHooks
// so the build/dispatch logic is testable in isolation.

#include "guild/common/types.h"
#include "play/input_recon4_hotkey.h"   // guild::play hotkey table (REUSED, not redefined)

#include <vector>

namespace guild::gui {

using guild::i32;

// gilde.exe 0x620b54 — the building row label format ("%2N8~ %1G").
inline constexpr const char* kHotkeyBuildingLabelFmt = "%2N8~ %1G";
// gilde.exe 0x620b60 — the object row label format ("%1s").
inline constexpr const char* kHotkeyObjectLabelFmt = "%1s";
// Row label colors (the +112 widget color field the original writes).
inline constexpr int kHotkeyColorPresent = 67;   // building/object label present
inline constexpr int kHotkeyColorAbsent  = 66;   // "no building" placeholder label

// One radio row as the builder lays it out (the visible model for one slot).
struct HotkeyWindowRow {
    int  slot = 0;             // 0..10
    bool hasBuilding = false;  // dword_122DC14[3*slot] != -1
    i32  buildingId = -1;
    bool hasObject = false;    // dword_122DC18[3*slot] != -1 (and object resolves)
    i32  objectId = -1;
    int  buildingLabelColor = kHotkeyColorAbsent; // 67 when present, 66 when absent
    int  objectLabelColor = kHotkeyColorPresent;  // 67 (only emitted when object present)
};

// Cross-module leaves for the window builder. Defaults route to the play-layer table
// resolvers; inert for the GUI sinks.
struct HotkeyWindowHooks {
    // VIBE_Building_FindById @0x587b20 — does the slot's building still exist?
    // Default: true (so a slot with buildingId != -1 shows as present).
    bool (*buildingExists)(i32 buildingId) = nullptr;
    // VIBE_Object_FindObjectById @0x583a70 — does the slot's object still exist?
    // Default: true.
    bool (*objectExists)(i32 objectId) = nullptr;
    // Pump one modal frame: false ends the loop (RunFrameLoop == 0 / ESC / 'X').
    // Default: ends immediately.
    bool (*pumpFrame)() = nullptr;
    // The selected radio row (-1 = none). Default: -1.
    int (*selectedRow)() = nullptr;
    // Per-frame action: 0 = none, 1 = assign clicked, 2 = clear clicked. Default: 0.
    int (*pendingAction)() = nullptr;
};

void SetHotkeyWindowHooks(const HotkeyWindowHooks& hooks);
const HotkeyWindowHooks& GetHotkeyWindowHooks();

// gilde.exe 0x4ff070 (build loop 0x4ff1d1..0x4ff3ae) — assemble the 11 radio rows from
// the current play-layer slot table. Reflects each slot's building/object presence and
// the per-row label colors. (The widget creation itself is host-side; this is the model.)
std::vector<HotkeyWindowRow> Hotkey_BuildRows();

// 0x4ff498 — assign button enabled iff a row is selected.
bool Hotkey_WindowAssignEnabled(int selectedRow);
// 0x4ff465 — clear button enabled iff a row is selected AND that row holds a building.
bool Hotkey_WindowClearEnabled(int selectedRow);

// gilde.exe 0x4ff070 — VIBE_Hotkey_OpenAssignWindow. Validates the table, then runs the
// modal loop: each frame, an ASSIGN action calls play::Hotkey_AssignFromSelection on the
// selected row and a CLEAR action clears the selected slot; either rebuilds the rows
// (LABEL_2). Returns the number of frames pumped (test observability; the original
// returns void). The rows after the last rebuild are placed in `outRows` when non-null.
int Hotkey_OpenAssignWindow(std::vector<HotkeyWindowRow>* outRows = nullptr);

} // namespace guild::gui
