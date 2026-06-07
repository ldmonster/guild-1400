#include "gui/radiogroup.h"
#include "gui/object.h"

#include <cstdint>

namespace guild::gui {

// Base dword_676584; the three labels 676584/676588/67658C are +0/+1/+2 dword views.
RadioGroup g_radioGroups[kMaxRadioGroups];

void ResetRadioGroups() {
    for (auto& g : g_radioGroups) g = RadioGroup{};
}

// Button branch of VIBE_Object_SetValueOrText @0x41dfec.
char Object_SetButtonValue(int widgetIdx, int on) {
    if (widgetIdx == -1 || widgetIdx > 512)
        return static_cast<char>(on);
    Widget& w = g_widgets[widgetIdx]; // 740*a1 + dword_69FFB4
    // The full original first handles type 'A' (anim) and 'E' (edit); for a button
    // (neither anim-sub-flag nor edit) it falls to the btnFlag branch below.
    if (w.btnFlagA() || w.btnFlagB()) {
        unsigned char v = static_cast<unsigned char>(on);
        w.value()       = v; // +36
        w.valueMirror() = v; // +40
    }
    return static_cast<char>(on);
}

// gilde.exe 0x41290c — VIBE_Selection_Update
char Selection_Update(int group, int index) {
    RadioGroup& g = g_radioGroups[group]; // 35*group
    char result = static_cast<char>(group);
    for (int i = 0; i < g.count; ++i) {
        // 140*group byte stride == 35*group dwords; button[i] == dword_67658C[35*g + i].
        if (i == index)
            result = Object_SetButtonValue(g.button[i], 1);
        else
            result = Object_SetButtonValue(g.button[i], 0);
    }
    g.selected = index; // dword_676588[35*group]
    return result;
}

// gilde.exe 0x412728 — VIBE_RadioGroup_Create
int RadioGroup_Create(int count, const int* buttons) {
    // Find the first free group: scan while dword_676584[35*n] (count) is nonzero.
    int n = 0;
    if (g_radioGroups[0].count) {
        int v3 = 0;
        do {
            v3 += kRadioStrideDwords; // += 35
            ++n;
        } while (v3 < 280 /* 8*35 */ && g_radioGroups[n].count);
    }
    if (n >= kMaxRadioGroups)
        return -1;
    if (count > kMaxRadioButtons) {
        // original: VIBE_ErrorLog_ReportMessage("d2_CreateRadioGroup: Too many objecthandles...")
        return -1;
    }

    RadioGroup& g = g_radioGroups[n];
    g.count    = count;  // dword_676584[35*n]
    g.selected = -1;     // dword_676588[35*n]
    for (int i = 0; i < count; ++i) {
        int id = buttons[i];
        g.button[i] = id;                         // dword_67658C[35*n + i]
        g_widgets[id].btnFlagA()  = 1;            // +68 = 1
        g_widgets[id].radioFlag() &= ~2u;         // +444 &= ~2
    }
    return n;
}

// gilde.exe 0x412800 — VIBE_RadioGroup_AddButton
int RadioGroup_AddButton(int group, int widgetIdx) {
    RadioGroup& g = g_radioGroups[group]; // 35*group
    g.button[g.count] = widgetIdx;        // dword_67658C[count + 35*group]
    g_widgets[widgetIdx].btnFlagA()  = 1;       // +68 = 1
    g_widgets[widgetIdx].radioFlag() &= ~2u;    // +444 &= ~2
    int prevSel = g.selected;             // dword_676588[35*group]
    ++g.count;                            // ++dword_676584[35*group]
    if (prevSel == -1)
        return Selection_Update(group, 0);
    return 740 * widgetIdx; // original returns the byte offset (result)
}

// gilde.exe 0x4128a4 — VIBE_RadioGroup_SetEnabled
void RadioGroup_SetEnabled(int group, int enabled) {
    RadioGroup& g = g_radioGroups[group]; // 35*group
    int disabled = (enabled == 0) ? 1 : 0; // v5 = a2 == 0
    for (int i = 0; i < g.count; ++i) {
        // *(740*button[i] + dword_69FFB4 + 56) = disabled
        g_widgets[g.button[i]].disabledA() = disabled;
    }
}

} // namespace guild::gui
