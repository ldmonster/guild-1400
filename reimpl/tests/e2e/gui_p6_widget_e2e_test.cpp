// E2E test for the P6 (Wave 29) GUI coverage slice. Runs a large deterministic
// "new-game setup" GUI scenario that drives EVERY recovered function in one composed
// flow against a scripted form/text/world runtime, then proves the whole flow is
// byte-identical across two independent runs (determinism). When the real game dir is
// present (europe_guild_1400_original/ or $GUILD_GAME_DIR) it additionally asserts the
// forms.BIN asset these screens are loaded from exists — the GUARDED real-asset anchor.
#include "test.h"

#include "gui/gui_widgetn.h"
#include "gui/hud_actionsn.h"
#include "gui/menu_dialogsn.h"
#include "gui/action_target_pickn.h"
#include "gui/object.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

namespace {

// A scripted runtime + recorded trace shared by all the lambdas in one run.
struct Run {
    std::vector<std::string> trace;
    int menuFrames = 0;
    int warnFrames = 0;
    int bookFrames = 0;
};
Run* g_run = nullptr;

void ResetAllModules() {
    ResetGuiWidgetN();
    ResetHudActionsN();
    ResetMenuDialogsN();
    ResetActionTargetPick();
}

// Run the entire composed scenario once, recording everything into `out`.
void RunScenario(std::vector<std::string>& out) {
    Run run;
    g_run = &run;
    ResetAllModules();

    // ---- (1) status-text table cleared at screen entry --------------------
    for (int i = 0; i < kStatusTextCountN; ++i) g_statusTextMarker[i] = i + 7;
    int cleared = StatusText_ClearTable();
    run.trace.push_back("clear:" + std::to_string(cleared));

    // ---- (2) choose player count (user picks 4) ---------------------------
    run.menuFrames = 2;
    MenuDialogsNHooks mh = *MenuDialogsNHooks_Default();
    mh.gameTickFinalize = [](i16, i16, const char* f){ g_run->trace.push_back(std::string("form:")+f); return 1; };
    mh.formGetChildObjectId = [](int, int, int){ return 5; };
    mh.objectSetValueOrText = [](int, int, int, int c){ g_run->trace.push_back("seed:"+std::to_string(c)); };
    mh.gameLogicRunFrameLoop = [](int, int, const void*){ return g_run->menuFrames-- > 0 ? 1 : 0; };
    mh.objectGetDataPtr = [](int){ return 4; };
    SetMenuDialogsNHooks(&mh);
    g_playerCount = 0;             // -> clamps to 2 for the seed, then user sets 4
    g_lastMenuAction = kActionOk;
    int ok = Menu_ChoosePlayerCount();
    run.trace.push_back("pc_ok:" + std::to_string(ok) + ":" + std::to_string((int)g_playerCount));

    // ---- (3) mission warning (4 players -> shown), mission picked ---------
    run.warnFrames = 1;
    mh.missionPickRandomByType = [](std::uint8_t s){ return (int)s + 5; };
    mh.gameTickFinalize = [](i16, i16, const char* f){ g_run->trace.push_back(std::string("warn:")+f); return 2; };
    mh.gameLogicRunFrameLoop = [](int, int, const void*){ return g_run->warnFrames-- > 0 ? 1 : 0; };
    mh.missionRunFailureDialog = [](int m, int){ g_run->trace.push_back("fail:"+std::to_string(m)); };
    SetMenuDialogsNHooks(&mh);
    g_missionTypeSeed = 3;         // mission id will be 8
    int missionRv = Menu_ShowMissionWarning();
    SetMenuDialogsNHooks(nullptr);
    run.trace.push_back("mission:" + std::to_string(missionRv));

    // ---- (4) abduct target pick + prompt target select -------------------
    ActionTargetPickHooks ah = *ActionTargetPickHooks_Default();
    ah.amtRunOfficeOverviewWindow = [](const TargetPickConfig* c, const void*, const void*) {
        if (c) g_run->trace.push_back("pick:flag=" + std::to_string(c->flag) +
                                      ",kind=" + std::to_string((int)c->kind) +
                                      ",pay=" + std::to_string(c->payload));
        return 0;
    };
    ah.hudUpdateEdgeScroll = [](int, int b, int c){ g_run->trace.push_back("edge:"+std::to_string(b)+","+std::to_string(c)); };
    SetActionTargetPickHooks(&ah);
    ActionDialog_BeginAbductTargetPick(0x77);
    ActionDialog_PromptTargetSelect(0, 0, 8, 9);
    SetActionTargetPickHooks(nullptr);

    // ---- (5) hud book loops + contact classification ---------------------
    run.bookFrames = 1;
    HudActionsNHooks hh = *HudActionsNHooks_Default();
    hh.statusTextRegister = [](const char* k, int){ g_run->trace.push_back(std::string("reg:")+k);
                                                    return std::string(k) == "ob_MEISTERBRIEF" ? 11 : 22; };
    hh.gameLogicRunFrameLoop = [](int, int, const void*){ return g_run->bookFrames-- > 0 ? 1 : 0; };
    hh.meisterRunMasterCertificate = [](){ g_run->trace.push_back("meister"); };
    hh.personnelRunStaffBook = [](){ g_run->trace.push_back("staff"); };
    hh.tradeRegisterEinkaufContact = [](int o){ g_run->trace.push_back("trade:"+std::to_string(o)); };
    SetHudActionsNHooks(&hh);
    g_clickedStatusId = 22;        // personnel book clicked
    Hud_RunMeisterPersonalBookLoop(0);
    g_selectedObject = 32;         // a market -> trade contact
    Hud_UpdateSelectedObjectContact(0);
    SetHudActionsNHooks(nullptr);

    g_run = nullptr;
    out = run.trace;
}

bool ResolveGameDir(std::string& dir) {
    const char* env = std::getenv("GUILD_GAME_DIR");
    if (env && env[0]) { dir = env; return true; }
    const char* cands[] = { "europe_guild_1400_original",
                            "../europe_guild_1400_original" };
    for (const char* c : cands) {
        std::string f = std::string(c) + "/Resources/forms.BIN";
        if (FILE* fp = std::fopen(f.c_str(), "rb")) { std::fclose(fp); dir = c; return true; }
    }
    return false;
}

} // namespace

TEST(GuiP6E2E, FullSetupFlowIsDeterministic) {
    std::vector<std::string> run1, run2;
    RunScenario(run1);
    RunScenario(run2);

    // The whole composed GUI flow must be byte-identical across two runs.
    CHECK_EQ((int)run1.size(), (int)run2.size());
    bool identical = run1.size() == run2.size();
    for (size_t i = 0; i < run1.size() && i < run2.size(); ++i)
        if (run1[i] != run2[i]) identical = false;
    CHECK(identical);

    // Spot-check the key transitions are present and correct.
    CHECK(!run1.empty());
    CHECK(run1[0] == "clear:6400");
    bool sawForm = false, sawWarn = false, sawPick = false, sawTrade = false, sawStaff = false;
    for (const auto& e : run1) {
        if (e == "form:Menu\\CHOOSE_PLAYERCOUNT") sawForm = true;
        if (e == "warn:Menu\\GET_MISSION_WARNING") sawWarn = true;
        if (e.rfind("pick:flag=1024,kind=6", 0) == 0) sawPick = true;
        if (e == "trade:32") sawTrade = true;
        if (e == "staff") sawStaff = true;
    }
    CHECK(sawForm);
    CHECK(sawWarn);
    CHECK(sawPick);
    CHECK(sawTrade);
    CHECK(sawStaff);
}

TEST(GuiP6E2E, RealFormsAssetGuard) {
    std::string dir;
    if (!ResolveGameDir(dir)) {
        std::printf("[ SKIP ] real game dir not found; set GUILD_GAME_DIR to enable.\n");
        return;
    }
    // The GUI screens these functions drive are loaded from forms.BIN.
    std::string formsPath = dir + "/Resources/forms.BIN";
    FILE* fp = std::fopen(formsPath.c_str(), "rb");
    if (!fp) {
        std::printf("[ SKIP ] forms.BIN not found at %s; set GUILD_GAME_DIR to a real install.\n",
                    formsPath.c_str());
        CHECK(true);
        return;
    }
    {
        std::fseek(fp, 0, SEEK_END);
        long sz = std::ftell(fp);
        std::fclose(fp);
        std::printf("[ INFO ] forms.BIN = %ld bytes at %s\n", sz, formsPath.c_str());
        CHECK(sz > 0);
    }
}
