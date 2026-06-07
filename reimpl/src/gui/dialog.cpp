#include "gui/dialog.h"
#include "gui/input.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/radiogroup.h"

#include <cstdint>

namespace guild::gui {

// gilde.exe 0x4ad6f0 / 0x569a30 — .form name selection by kind flags.
const char* Dialog_FormForFlags(int flags) {
    if (flags & kMsgFlagAutosize)
        return "misc\\Messagebox_Autosize";
    if (flags & kMsgFlagBig)
        return "misc\\Messagebox_BIG";
    if (flags & kMsgFlagVeryBig)
        return "misc\\Messagebox_VERY_BIG";
    if (flags & kMsgFlagNonePerga)
        return "misc\\Messagebox_NONE_PERGA";
    return "misc\\Messagebox";
}

// gilde.exe 0x4ad6f0 — button-group build loop.
//   v9 = RadioGroup_Create(0, ...);
//   for (i=0; i < curWin.objCount; ++i)
//     id = curWin.childList[i];
//     if (widget[id].type != '@') RadioGroup_AddButton(v9, id);
//   Selection_Update(v9, 0);
int Dialog_BuildButtonGroup(int winSlot) {
    if (winSlot < 0 || winSlot >= kMaxWindows)
        return -1;
    Window& win = g_windows[winSlot];

    int group = RadioGroup_Create(0, nullptr); // empty group, filled by AddButton below
    if (group < 0)
        return -1;

    i32* list = WindowChildList(winSlot);     // *(dword_62D298 + 24)
    int count = win.objCount();               // *(dword_62D298 + 26) >> 16
    for (int i = 0; i < count; ++i) {
        int id = list[i];
        // *(dword_69FFB4 + 740*id + 24) != 64 : skip the window-backing widget ('@').
        if (g_widgets[id].type() != kTypeWindow)
            RadioGroup_AddButton(group, id);
    }
    Selection_Update(group, 0);               // VIBE_Selection_Update(v9, 0)
    return group;
}

// gilde.exe 0x4ad6f0 — result resolution inside the modal loop.
int Dialog_ResolveResult(int clickedId, int selectedIndex, bool rightClick) {
    // OK pressed on this box's window: return the selected button index + 1.
    if (clickedId == kIdOk)
        return selectedIndex + 1; // dword_676588[group] + 1
    // Cancel pressed, or a right-click: return 0.
    if (clickedId == kIdCancel || rightClick)
        return 0;
    return -1; // neither: the modal loop keeps running
}

} // namespace guild::gui
