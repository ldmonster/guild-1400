#include "gui/form.h"
#include "gui/object.h"
#include "gui/window.h"

#include <cstdint>

namespace guild::gui {

Form g_forms[kMaxForms]; // dword_676A60
int  g_currentFormId = 0; // dword_62D258

void ResetGuiState() {
    for (auto& f : g_forms) f = Form{};
    g_currentFormId = 0;
    ResetWindows();
    ResetWidgets();
}

namespace {
// Window slot for a form's logical window group ("group").
//   dword_676A60[171*form + 1 + group] == g_forms[form].windowId(group)
int WindowSlotForGroup(int formId, int group) {
    return g_forms[formId].windowId(group);
}
} // namespace

// gilde.exe 0x41e4cc — VIBE_Form_SelectWindow
int Form_SelectWindow(int formId, int winSlot) {
    if (formId)
        g_currentFormId = formId; // dword_62D258

    // Validate: winSlot in [0, form.windowCount].  Note the original uses `>` (so the
    // upper bound is inclusive of windowCount).
    if (winSlot > g_forms[g_currentFormId].windowCount() || winSlot < 0) {
        // original: VIBE_ErrorLog_ReportMessage("d2_SetForm(): invalid win number")
        return 0;
    }

    g_currentWindowId = g_forms[g_currentFormId].windowId(winSlot); // dword_62D230
    g_currentWindow   = &g_windows[g_currentWindowId];              // dword_62D298
    return 1;
}

// gilde.exe 0x41db20 — VIBE_Form_GetObjectPtr
int Form_GetObjectPtr(int localId, int group) {
    int slot = WindowSlotForGroup(g_currentFormId, group);
    Window& win = g_windows[slot];
    // Bound: localId <= (*(int*)((char*)w + 26) >> 16) == word@+28 == objCount().
    if (localId <= win.objCount())
        return WindowChildList(slot)[localId]; // dword_69FFB4 + 740*idx (we return idx)
    return -1; // original returns 0 (null ptr); -1 is our "no index" sentinel
}

// gilde.exe 0x41dc10 — VIBE_Form_GetObjectDataPtr
i32 Form_GetObjectDataPtr(int localId, int group) {
    int slot = WindowSlotForGroup(g_currentFormId, group);
    Window& win = g_windows[slot];
    if (localId <= win.objCount())
        return Object_GetDataPtr(WindowChildList(slot)[localId]);
    return 0;
}

// gilde.exe 0x41dd14 — VIBE_Form_GetObjectAnimPtr
i32 Form_GetObjectAnimPtr(int localId, int group) {
    int slot = WindowSlotForGroup(g_currentFormId, group);
    Window& win = g_windows[slot];
    if (localId <= win.objCount()) {
        int idx = WindowChildList(slot)[localId];
        Widget& child = g_widgets[idx]; // dword_69FFB4 + 740*idx
        if (child.type() == kTypeAnim)  // == 65 'A'
            // *(v4 + 12): the 3D/anim record pointer (kept in the widget data table).
            return static_cast<i32>(reinterpret_cast<std::intptr_t>(WidgetData(idx)));
    }
    return 0;
}

// gilde.exe 0x41dea8 — VIBE_Form_GetChildObjectId
int Form_GetChildObjectId(int form, int group, int child) {
    int slot;
    if (form == -1)
        slot = group;                              // direct window slot
    else
        slot = g_forms[form].windowId(group);      // dword_676A60[171*form + 1 + group]

    Window& win = g_windows[slot];
    if (child <= win.objCount())
        return WindowChildList(slot)[child]; // *(v4[6] + 4*child)
    return -1;
}

} // namespace guild::gui
