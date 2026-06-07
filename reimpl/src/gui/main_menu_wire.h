#pragma once
// =============================================================================
// guild::gui — compose VIBE_Menu_RunMainMenu @0x529d08 with the REAL reconstructed
// sub-screen RunXxx functions (the 1:1 behavioral menu, end to end).
//
// gui::Menu_RunMainMenu (main_menu_run.*) dispatches to sub-screens through
// MainMenuRunHooks whose inert defaults no-op. RealMainMenuHooks overrides each
// sub-screen runner to invoke the matching reconstructed driver — Menu_EnterChooseCity
// @0x52ee38, Menu_RunLoadGame @0x56a270, Menu_ChooseNetworkMode @0x529a64,
// Menu_RunOptions{Gfx,Sfx,Game} @0x56c21c/0x56c808/0x56cc44, Menu_RunCredits{Window,
// Scroll} @0x529c30/0x56e524, Menu_BuildChooseMissionDialog @0x59b998,
// Menu_RunFileSelector @0x569668, Map_LoadCityFile @0x528bd0 — so the real menu runs
// the real sub-screens. The host frame-loop + click-edge sources (the only true I/O
// boundary) are injected: a native caller feeds the SDL present/input; a test feeds a
// scripted sequence. FormLoadMainMenu does the real Window_Create so the widget build
// runs headless. Backend-agnostic; no Wine.
// =============================================================================
#include "gui/main_menu_run.h"
#include <functional>

namespace guild::gui {

struct RealMainMenuHooks : MainMenuRunHooks {
    // Injected host sources (required for the loop to advance + dispatch). `tick` is
    // the wire's monotonic frame counter; `frame` (in the edge sources) is the menu's
    // loop index — they advance together.
    std::function<int(int tick)>  runFrameLoop;   // nonzero -> keep running
    std::function<bool(int frame)> clickEdge;     // dword_672228 (a left-click edge)
    std::function<int(int frame)>  hoverId;       // dword_62D22C (hovered widget id, -1 none)
    std::function<bool(int frame)> escDown;       // byte_67225C (ESC/quit)

    int subFrames = 64;   // per-sub-screen loop bound (headless); pass -1 for native.

    // Invocation counters (diagnostics / test assertions): how many times each REAL
    // sub-screen actually ran.
    int ranChooseCity = 0, ranLoadGame = 0, ranNetwork = 0;
    int ranOptGfx = 0, ranOptSfx = 0, ranOptGame = 0;
    int ranCreditsWin = 0, ranCreditsScroll = 0;
    int ranMission = 0, ranFileSelector = 0, ranMapLoad = 0;

    // ---- host boundary ----
    int  FormLoadMainMenu() override;       // real Window_Create (mirrors the default)
    int  RunFrameLoop() override;
    bool ClickEdge(int frame) override;
    int  HoverId(int frame) override;
    bool EscDown(int frame) override;

    // ---- sub-screen runners -> the REAL reconstructed RunXxx ----
    bool EnterChooseCity() override;
    bool RunLoadGame() override;
    bool ChooseNetworkMode() override;
    void RunOptionsGfx() override;
    void RunOptionsSfx() override;
    void RunOptionsGame() override;
    void RunCreditsWindow() override;
    void RunCreditsScroll() override;
    bool BuildChooseMissionDialog() override;
    bool RunFileSelector(char* outName, int outCap) override;
    void MapLoadCityFile(int mode, const char* cityName) override;

private:
    int tick_ = 0;
};

// Install `hooks` as the active main-menu hooks (Menu_SetRunHooks). Returns the prior.
MainMenuRunHooks* InstallRealMainMenuHooks(RealMainMenuHooks* hooks);

} // namespace guild::gui
