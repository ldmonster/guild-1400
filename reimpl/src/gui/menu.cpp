#include "gui/menu.h"

namespace guild::gui {

// gilde.exe 0x56dccc — the seven AddSpriteToWindow(32, Y, 174, win) calls, in build
// order: y = 10, 56, 102, 148, 194, 240, 332.
const int kOptionsButtonY[kOptionsButtonCount] = {10, 56, 102, 148, 194, 240, 332};

OptionsItem Menu_OptionsSelect(int buttonIndex) {
    switch (buttonIndex) {
        case 0: return OptionsItem::kLoad;   // y=10  v31
        case 1: return OptionsItem::kSave;   // y=56  v8
        case 2: return OptionsItem::kGame;   // y=102 v36
        case 3: return OptionsItem::kGfx;    // y=148 v37
        case 4: return OptionsItem::kSfx;    // y=194 v38
        case 5: return OptionsItem::kQuit;   // y=240 v35
        case 6: return OptionsItem::kResume; // y=332 v34
        default: return OptionsItem::kResume; // out of range -> no-op / resume
    }
}

bool Menu_OptionEnabled(OptionsItem item, int sessionFlags) {
    // Mission mode (0x80) disables Load and Save.
    if ((sessionFlags & kFlagMission) != 0 &&
        (item == OptionsItem::kLoad || item == OptionsItem::kSave))
        return false;
    // Network mode (0x4): the original disables one of Load/Save unless flag 0x10 is
    // also set; we model the common case where Save stays available (host) and Load is
    // disabled for non-host network clients (0x4 without 0x10).
    if ((sessionFlags & kFlagNetwork) != 0 && (sessionFlags & 0x10) == 0 &&
        item == OptionsItem::kLoad)
        return false;
    return true;
}

namespace {
MenuCommandSink g_defaultSink;
MenuCommandSink* g_sink = &g_defaultSink;
} // namespace

void Menu_SetCommandSink(MenuCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

OptionsItem Menu_DispatchOption(int buttonIndex, int sessionFlags) {
    OptionsItem item = Menu_OptionsSelect(buttonIndex);
    bool network = (sessionFlags & kFlagNetwork) != 0;
    switch (item) {
        case OptionsItem::kLoad:   g_sink->RunLoad();          break;
        case OptionsItem::kSave:   g_sink->RunSave(network);   break;
        case OptionsItem::kGame:   g_sink->RunGameOptions();   break;
        case OptionsItem::kGfx:    g_sink->RunGfxOptions();    break;
        case OptionsItem::kSfx:    g_sink->RunSfxOptions();    break;
        case OptionsItem::kQuit:   g_sink->RunQuitConfirm();   break;
        case OptionsItem::kResume: g_sink->Resume();           break;
    }
    return item;
}

} // namespace guild::gui
