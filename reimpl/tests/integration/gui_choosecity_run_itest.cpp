// Integration tests for gui::Menu_RunChooseCity / Menu_EnterChooseCity — scripted
// hover+confirm returns the chosen city, runs the ChooseCharacterIntroVariant ->
// RunChooseHistory chain and arms dword_631614 / v80; a cancel (right-click/esc) returns 0.
#include "test.h"

#include "gui/choosecity_run.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A scripted-frame harness. One city ("stadt_BERLIN", marker 555) is enumerated; on
// `confirmFrame` the cursor hovers it and a confirm edge fires; the chain return values
// are configurable. The loop runs until maxFrames.
struct DriveHooks : ChooseCityHooks {
    int  cityMarker = 555;
    std::string cityName = "stadt_BERLIN";

    int  hoverFrame   = 0;   // frame the cursor hovers the city marker
    int  confirmFrame = 0;   // frame the confirm edge (1210) fires
    int  cancelFrame  = -1;  // frame a right-click cancel fires (-1 = never)
    int  escFrame     = -1;  // frame an Esc (byte_67225C==1) fires
    bool useEnterKey  = false; // confirm via Enter(28) instead of click id 1210

    bool introOK   = true;
    bool historyOK = true;

    int  framesToRun = 3;

    // recorded
    int  introCalls = 0;
    int  historyCalls = 0;
    std::string renderedCity;
    int  renderedInfo = -999;

    int EnumerateCityFiles(const char*, const char*,
                           std::vector<std::string>& out) override {
        out = {"BERLIN"};
        return 1;
    }
    bool ReadCityName(const std::string&, std::string& outName) override {
        outName = "BERLIN"; return true;
    }
    int SpawnCityMarker(const std::string&) override { return cityMarker; }
    int FindTextIndex(const std::string&) override { return 42; }

    int RunFrameLoop(int frame) override { return frame < framesToRun ? 1 : 0; }

    int PickNearestObject(int frame) override {
        return frame >= hoverFrame ? cityMarker : 0;
    }
    bool PickIsHover(int frame) override { return frame >= hoverFrame; }
    std::string ObjectName(int object) override {
        return object == cityMarker ? cityName : std::string();
    }
    void RenderCityInfo(const std::string& name, int info) override {
        renderedCity = name; renderedInfo = info;
    }

    int ClickedWidgetId(int frame) override {
        return (!useEnterKey && frame == confirmFrame) ? 1210 : 0;
    }
    int KeyCode(int frame) override {
        if (useEnterKey && frame == confirmFrame) return 28; // Enter
        if (frame == escFrame) return 1;                     // Esc
        return 0;
    }
    bool RightClick(int frame) override { return frame == cancelFrame; }

    bool ChooseCharacterIntroVariant() override { ++introCalls; return introOK; }
    bool RunChooseHistory() override { ++historyCalls; return historyOK; }
};

} // namespace

TEST(ChooseCityRunI, HoverThenConfirm_ChainsAndArmsFlags) {
    DriveHooks hooks;
    hooks.hoverFrame = 0;
    hooks.confirmFrame = 1;
    hooks.framesToRun = 3;
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    st.network = false;
    ChooseCityRecord rec;
    int r = Menu_RunChooseCity(st, &rec, /*maxFrames*/ 5);

    Menu_SetChooseCityHooks(prev);

    // Confirm -> chain ran -> result 1, close armed.
    CHECK_EQ(r, 1);
    CHECK_EQ(st.result, 1);          // v80 = 1
    CHECK_EQ(st.close, 1);           // dword_631614 = 1
    CHECK_EQ(hooks.introCalls, 1);   // ChooseCharacterIntroVariant
    CHECK_EQ(hooks.historyCalls, 1); // RunChooseHistory
    CHECK(rec.confirmed);
    CHECK(rec.chainIntroRan);
    CHECK(rec.chainHistoryRan);
    CHECK_EQ(rec.confirmedCity, std::string("BERLIN")); // the hovered city's recovered name
    // The hover render showed the city's name + its info text.
    CHECK_EQ(hooks.renderedCity, std::string("BERLIN"));
    CHECK_EQ(hooks.renderedInfo, 42);
}

TEST(ChooseCityRunI, ConfirmViaEnterKey) {
    DriveHooks hooks;
    hooks.hoverFrame = 0;
    hooks.confirmFrame = 1;
    hooks.useEnterKey = true;   // byte_67225C == 28
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    ChooseCityRecord rec;
    int r = Menu_RunChooseCity(st, &rec, 5);
    Menu_SetChooseCityHooks(prev);

    CHECK_EQ(r, 1);
    CHECK(rec.chainHistoryRan);
}

TEST(ChooseCityRunI, ConfirmButIntroCancelled_NoChainNoArm) {
    DriveHooks hooks;
    hooks.confirmFrame = 0;
    hooks.introOK = false;   // ChooseCharacterIntroVariant returns 0 -> abort chain
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    ChooseCityRecord rec;
    int r = Menu_RunChooseCity(st, &rec, 5);
    Menu_SetChooseCityHooks(prev);

    CHECK_EQ(r, 0);                  // not confirmed
    CHECK_EQ(st.result, 0);          // v80 stays 0
    CHECK_EQ(hooks.introCalls, 1);
    CHECK_EQ(hooks.historyCalls, 0); // never reached
    CHECK(!rec.confirmed);
}

TEST(ChooseCityRunI, ConfirmButHistoryCancelled_NoArm) {
    DriveHooks hooks;
    hooks.confirmFrame = 0;
    hooks.introOK = true;
    hooks.historyOK = false; // RunChooseHistory returns 0
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    ChooseCityRecord rec;
    int r = Menu_RunChooseCity(st, &rec, 5);
    Menu_SetChooseCityHooks(prev);

    CHECK_EQ(r, 0);
    CHECK_EQ(hooks.introCalls, 1);
    CHECK_EQ(hooks.historyCalls, 1);
    CHECK(!rec.confirmed);
}

TEST(ChooseCityRunI, NetworkConfirm_ShortCircuitsChain) {
    DriveHooks hooks;
    hooks.confirmFrame = 0;
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    st.network = true;   // v76 set -> confirm arms directly, skips the chain
    ChooseCityRecord rec;
    int r = Menu_RunChooseCity(st, &rec, 5);
    Menu_SetChooseCityHooks(prev);

    CHECK_EQ(r, 1);
    CHECK_EQ(st.result, 1);
    CHECK_EQ(st.close, 1);
    CHECK_EQ(hooks.introCalls, 0);   // chain skipped on the network path
    CHECK_EQ(hooks.historyCalls, 0);
    CHECK(rec.confirmed);
}

TEST(ChooseCityRunI, RightClickCancel_ReturnsZeroArmsClose) {
    DriveHooks hooks;
    hooks.hoverFrame = 99;   // never hover a city
    hooks.confirmFrame = -1; // never confirm
    hooks.cancelFrame = 1;   // right-click cancel
    hooks.framesToRun = 3;
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    ChooseCityRecord rec;
    int r = Menu_RunChooseCity(st, &rec, 5);
    Menu_SetChooseCityHooks(prev);

    CHECK_EQ(r, 0);                  // cancel -> v80 = 0
    CHECK_EQ(st.close, 1);           // dword_631614 = 1 set on cancel
    CHECK(rec.cancelled);
    CHECK_EQ(hooks.introCalls, 0);
}

TEST(ChooseCityRunI, EscCancel_ReturnsZero) {
    DriveHooks hooks;
    hooks.hoverFrame = 99;
    hooks.confirmFrame = -1;
    hooks.escFrame = 0;      // byte_67225C == 1 (Esc)
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    ChooseCityRecord rec;
    int r = Menu_RunChooseCity(st, &rec, 5);
    Menu_SetChooseCityHooks(prev);

    CHECK_EQ(r, 0);
    CHECK_EQ(st.close, 1);
    CHECK(rec.cancelled);
}

TEST(ChooseCityRunI, EnterFunnel_Cancel_WipesAndRenders) {
    // Menu_EnterChooseCity: word_63C740=0, DragCursor(0), byte_63CC1D=1, run; on cancel
    // it does Surface_ColorFill + RenderEntityList(1773).
    struct FunnelHooks : DriveHooks {
        int dragSprite = -1;
        int colorFills = 0;
        int renderListId = -1;
        void DragCursorSetSprite(int s) override { dragSprite = s; }
        void SurfaceColorFill() override { ++colorFills; }
        void RenderEntityList(int which) override { renderListId = which; }
    } hooks;
    hooks.hoverFrame = 99;
    hooks.confirmFrame = -1;
    hooks.escFrame = 0;       // cancel
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    ChooseCityRecord rec;
    int r = Menu_EnterChooseCity(st, &rec, 5);
    Menu_SetChooseCityHooks(prev);

    CHECK_EQ(r, 0);
    CHECK_EQ(st.sessionFlags, 0);             // word_63C740 = 0 at entry
    CHECK_EQ(st.enterMarker, 1);              // byte_63CC1D = 1
    CHECK_EQ(hooks.dragSprite, 0);            // DragCursor_SetSprite(this, 0)
    CHECK_EQ(hooks.colorFills, 1);            // cancel path wipes
    CHECK_EQ(hooks.renderListId, 1773);       // Window_RenderEntityList(1773)
}

TEST(ChooseCityRunI, EnterFunnel_Confirm_NoWipe) {
    struct FunnelHooks : DriveHooks {
        int colorFills = 0;
        void SurfaceColorFill() override { ++colorFills; }
    } hooks;
    hooks.confirmFrame = 0;   // confirm immediately
    ChooseCityHooks* prev = Menu_SetChooseCityHooks(&hooks);

    ChooseCityState st;
    ChooseCityRecord rec;
    int r = Menu_EnterChooseCity(st, &rec, 5);
    Menu_SetChooseCityHooks(prev);

    CHECK_EQ(r, 1);
    CHECK_EQ(hooks.colorFills, 0);  // confirm -> no cancel wipe
}
