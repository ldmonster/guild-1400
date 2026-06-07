#pragma once
// guild::gui — radio/button groups for dialogs (mutually-exclusive button sets).
//
// Up to 8 groups live in three aliased global arrays, each a 35-dword (140-byte)
// stride record based at dword_676584:
//   dword_676584[35*g] = count   (record dword [0])
//   dword_676588[35*g] = selected index (record dword [1]; == 676584+4)
//   dword_67658C[35*g + k] = widget index of button k (record dwords [2..]; == 676584+8)
// A group holds up to 32 button widget ids. Selecting button k pushes value 1 to that
// button's widget and value 0 to all the others (mutual exclusion), and records k as
// the selected index.
//
// Recovered originals:
//   VIBE_RadioGroup_Create     @0x412728 — allocate a group, seed it with N buttons.
//   VIBE_RadioGroup_AddButton  @0x412800 — append one button to a group.
//   VIBE_RadioGroup_SetEnabled @0x4128a4 — enable/disable every button in a group.
//   VIBE_Selection_Update      @0x41290c — set the selected button (exclusive).
//
// The per-button effect goes through VIBE_Object_SetValueOrText @0x41dfec; its full
// implementation (slider/edit/anim branches) is owned by the object module and deferred.
// The button branch it takes for these calls — write value to +36 and mirror to +40 —
// is what the group logic depends on and is reproduced here as Object_SetButtonValue.

#include "gui/types.h"

namespace guild::gui {

inline constexpr int kMaxRadioGroups = 8;
inline constexpr int kRadioStrideDwords = 35;  // 140-byte record
inline constexpr int kMaxRadioButtons = 32;    // VIBE_RadioGroup_Create rejects > 32

// Global radio-group tables.  We model the three aliased BSS arrays as one record array
// so the [0]=count / [1]=selected / [2..]=buttons layout matches the original stride.
struct RadioGroup {
    i32 count;            // dword_676584[35*g]
    i32 selected;         // dword_676588[35*g]   (-1 when none)
    i32 button[kRadioStrideDwords - 2]; // dword_67658C[35*g + k]  (33 slots; 32 used)
};
extern RadioGroup g_radioGroups[kMaxRadioGroups]; // base dword_676584

void ResetRadioGroups();

// Button branch of VIBE_Object_SetValueOrText @0x41dfec: for a button widget (btnFlagA
// or btnFlagB set) writes `on` (0/1) to value(+36) and mirrors it to +40. Used by the
// group select/enable paths.  The slider/edit/anim branches are deferred to the object
// module.
char Object_SetButtonValue(int widgetIdx, int on);

// gilde.exe 0x41290c — VIBE_Selection_Update  (group@eax, index@edx)
// Selects button `index` in `group`: pushes value 1 to that button and 0 to the others,
// and records `index` as the group's selected slot.
char Selection_Update(int group, int index);

// gilde.exe 0x412728 — VIBE_RadioGroup_Create  (count, firstButtonId... / cdecl)
// Allocates the first free group, sets its button count, marks selected = -1, and seeds
// it with the `count` button widget ids from `buttons`. Each button gets its btnFlagA
// (+68) set and its radio bit (+444 & ~2) cleared. Returns the group index, or -1 if
// there are already 8 groups or count > 32.
int RadioGroup_Create(int count, const int* buttons);

// gilde.exe 0x412800 — VIBE_RadioGroup_AddButton  (group@eax, widgetIdx@edx)
// Appends one button to `group`, sets its btnFlagA / clears its radio bit, bumps the
// count. If the group had no selection yet (selected == -1) it selects button 0.
int RadioGroup_AddButton(int group, int widgetIdx);

// gilde.exe 0x4128a4 — VIBE_RadioGroup_SetEnabled  (group@eax, enabled@edx)
// Sets every button's disabled flag (+56) to (enabled == 0).
void RadioGroup_SetEnabled(int group, int enabled);

} // namespace guild::gui
