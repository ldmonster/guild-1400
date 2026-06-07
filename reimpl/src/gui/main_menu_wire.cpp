// guild::gui — compose Menu_RunMainMenu with the real sub-screen RunXxx. See header.
#include "gui/main_menu_wire.h"

#include "gui/window.h"            // Window_Create / g_currentWindowId (real sibling)
#include "gui/choosecity_run.h"
#include "gui/loadgame_run.h"
#include "gui/netfile_run.h"
#include "gui/options_run.h"
#include "gui/credits_run.h"
#include "gui/mission_load_run.h"

#include <cstring>
#include <string>

namespace guild::gui {

// Mirror DefaultRunHooks::FormLoadMainMenu — a REAL Window_Create so the menu's
// Widget_AddSpriteToWindow build runs into a live window slot headless.
int RealMainMenuHooks::FormLoadMainMenu() {
    return Window_Create(0, 0, 300, 320, 0);
}

int  RealMainMenuHooks::RunFrameLoop()      { return runFrameLoop ? runFrameLoop(tick_++) : 0; }
bool RealMainMenuHooks::ClickEdge(int f)    { return clickEdge ? clickEdge(f) : false; }
int  RealMainMenuHooks::HoverId(int f)      { return hoverId ? hoverId(f) : -1; }
bool RealMainMenuHooks::EscDown(int f)      { return escDown ? escDown(f) : false; }

bool RealMainMenuHooks::EnterChooseCity() {
    ++ranChooseCity;
    ChooseCityState s;
    return Menu_EnterChooseCity(s, nullptr, subFrames) != 0;
}

bool RealMainMenuHooks::RunLoadGame() {
    ++ranLoadGame;
    LoadGameRunState s;
    return Menu_RunLoadGame(s, nullptr, subFrames) != 0;
}

bool RealMainMenuHooks::ChooseNetworkMode() {
    ++ranNetwork;
    NetModeState s;
    return Menu_ChooseNetworkMode(s, nullptr, subFrames) != 0;
}

void RealMainMenuHooks::RunOptionsGfx() {
    ++ranOptGfx;
    OptionsRunState s;
    (void)Menu_RunOptionsGfx(s, nullptr, subFrames);
}

void RealMainMenuHooks::RunOptionsSfx() {
    ++ranOptSfx;
    OptionsRunState s;
    (void)Menu_RunOptionsSfx(s, nullptr, subFrames);
}

void RealMainMenuHooks::RunOptionsGame() {
    ++ranOptGame;
    OptionsRunState s;
    (void)Menu_RunOptionsGame(s, nullptr, subFrames);
}

void RealMainMenuHooks::RunCreditsWindow() {
    ++ranCreditsWin;
    CreditsRunState s;
    Menu_RunCreditsWindow(s, nullptr, subFrames);
}

void RealMainMenuHooks::RunCreditsScroll() {
    ++ranCreditsScroll;
    CreditsRunState s;
    Menu_RunCreditsScroll(s, nullptr, subFrames);
}

bool RealMainMenuHooks::BuildChooseMissionDialog() {
    ++ranMission;
    int seed[kMissionRowCount] = {0};
    MissionDialogResult res;
    return Menu_BuildChooseMissionDialog(seed, res, subFrames) != 0;
}

bool RealMainMenuHooks::RunFileSelector(char* outName, int outCap) {
    ++ranFileSelector;
    std::string out;
    // The menu calls the file selector on the city dir in direct-pick mode.
    int r = Menu_RunFileSelector("gamedata/cities", ".INI", /*directMode=*/true,
                                 /*listText=*/0, out, nullptr, subFrames);
    if (r && outName && outCap > 0) {
        std::strncpy(outName, out.c_str(), (size_t)outCap - 1);
        outName[outCap - 1] = '\0';
    }
    return r != 0;
}

void RealMainMenuHooks::MapLoadCityFile(int mode, const char* cityName) {
    ++ranMapLoad;
    Map_LoadCityFile(mode, cityName, nullptr);
}

MainMenuRunHooks* InstallRealMainMenuHooks(RealMainMenuHooks* hooks) {
    return Menu_SetRunHooks(hooks);
}

} // namespace guild::gui
