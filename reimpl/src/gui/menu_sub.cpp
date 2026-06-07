#include "gui/menu_sub.h"

namespace guild::gui {

// gilde.exe 0x56e30c — AddSpriteToWindow(32, Y, 174, win): Y = 56, 102, 148.
const int kSubTabButtonY[kSubTabButtonCount] = {56, 102, 148};

SubTab Menu_SubTabSelect(int buttonIndex) {
    switch (buttonIndex) {
        case 0: return SubTab::kGame; // y=56  v4
        case 1: return SubTab::kGfx;  // y=102 v11
        case 2: return SubTab::kSfx;  // y=148 v7
        default: return SubTab::kGame;
    }
}

namespace {
SubTabCommandSink g_defaultSink;
SubTabCommandSink* g_sink = &g_defaultSink;
} // namespace

void Menu_SetSubTabCommandSink(SubTabCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

SubTab Menu_DispatchSubTab(int buttonIndex) {
    SubTab tab = Menu_SubTabSelect(buttonIndex);
    switch (tab) {
        case SubTab::kGame: g_sink->RunGame(); break;
        case SubTab::kGfx:  g_sink->RunGfx();  break;
        case SubTab::kSfx:  g_sink->RunSfx();  break;
    }
    return tab;
}

} // namespace guild::gui
