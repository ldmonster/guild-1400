#include "gui/save_name_input.h"

namespace guild::gui {

namespace {

bool DefPump() { return false; }
int  DefLastKey() { return 0; }
std::string DefFieldText() { return std::string(); }
bool DefCancel() { return false; }

SaveNameInputHooks g_hooks = { &DefPump, &DefLastKey, &DefFieldText, &DefCancel };

} // namespace

void SetSaveNameInputHooks(const SaveNameInputHooks& hooks) {
    g_hooks = hooks;
    if (!g_hooks.pumpFrame)       g_hooks.pumpFrame       = &DefPump;
    if (!g_hooks.lastKey)         g_hooks.lastKey         = &DefLastKey;
    if (!g_hooks.fieldText)       g_hooks.fieldText       = &DefFieldText;
    if (!g_hooks.cancelRequested) g_hooks.cancelRequested = &DefCancel;
}

const SaveNameInputHooks& GetSaveNameInputHooks() { return g_hooks; }

// gilde.exe 0x56a700 — VIBE_Menu_RunSaveNameInput.
bool Menu_RunSaveNameInput(std::string& buf) {
    // 0x56a71a..0x56a780: register field, color 66, seed text == buf, focus, size 272,
    // set the button callback. (All widget plumbing — host-side.)
    int confirmed = 0;                       // v7

    // 0x56a799: while (RunFrameLoop(...))
    while (g_hooks.pumpFrame()) {
        // 0x56a7a2: if (byte_67225C == 28) { copy field text -> buf; close; v7 = 1; }
        if (g_hooks.lastKey() == kKeyEnter) {
            buf = g_hooks.fieldText();        // 0x56a7ad..c8: the data-ptr copy loop
            confirmed = 1;                    // 0x56a7d3: v7 = 1;
            // 0x56a7ce: dword_631614 = 1 — request close; the loop ends next pump.
        }
        // 0x56a7f1: if (dword_672230 || !dword_62D328) dword_631614 = 1; — host close.
        if (g_hooks.cancelRequested()) {
            // 0x56a7df: request close; loop ends. v7 stays as-is (0 unless ENTER hit).
        }
    }
    // 0x56a7f7: Widget_DestroyByType(...); return v7.
    return confirmed != 0;
}

} // namespace guild::gui
