// Unit tests for the info-panel content builders (gilde.exe 0x4b6454..0x4b7468).
// Golden vectors: the exact form names, header format strings, widget call sequence and
// slot-global writes each builder produces, driven through a recording InfoPanelHost.
#include "test.h"

#include "gui/infopanel_build.h"
#include "gui/object.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A recording host: hands out stable widget ids, captures the call log.
struct RecHost : InfoPanelHost {
    std::vector<std::string> log;
    int nextWidget = 100;
    int formId = 7;
    int halfH = 50;

    int LoadForm(const char* name) override {
        log.push_back(std::string("LoadForm:") + name);
        return formId;
    }
    void SelectWindow(int form, int sub) override {
        log.push_back("Select:" + std::to_string(form) + "," + std::to_string(sub));
    }
    int CurrentWindowHalfHeight() override { return halfH; }
    void RenderRichString(const char* fmt) override {
        log.push_back(std::string("Text:") + fmt);
    }
    int AddObject(int gfx, int y, int objId) override {
        log.push_back("Obj:" + std::to_string(gfx) + "," + std::to_string(y) + "," + std::to_string(objId));
        return nextWidget++;
    }
    int AddSprite(int x, int y, int /*gfxId*/) override {
        log.push_back("Sprite:" + std::to_string(x) + "," + std::to_string(y));
        return nextWidget++;
    }
    int AddSlider(int x, int y, int /*value*/, int /*range*/, int /*maxVal*/,
                  int /*gfxBase*/, int /*flags*/) override {
        log.push_back("Slider:" + std::to_string(x) + "," + std::to_string(y));
        return nextWidget++;
    }
    void SetColor(int w, int c) override { log.push_back("Color:" + std::to_string(w) + "," + std::to_string(c)); }
    void SetTextColor(int w, int c) override { log.push_back("TextColor:" + std::to_string(w) + "," + std::to_string(c)); }
    void SetValueOrText(int w, int /*t*/, int /*a3*/, int v, int /*a5*/) override {
        log.push_back("Value:" + std::to_string(w) + "," + std::to_string(v));
    }
    void DestroyWidget(int w) override { log.push_back("Destroy:" + std::to_string(w)); }
    int BuildingUpgradeLevel(const void*) override { return 42; }
};

bool logHas(const RecHost& h, const std::string& s) {
    for (auto& e : h.log)
        if (e == s) return true;
    return false;
}

} // namespace

TEST(GuiInfoPanelBuild, AddIconSpriteSequence) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    int w = InfoPanel_AddIconSprite(10, 20, 180);
    CHECK(w == 100);
    // sprite at x+3=13, then Color 67, TextColor 96.
    CHECK_EQ(h.log[0], std::string("Sprite:13,20"));
    CHECK_EQ(h.log[1], std::string("Color:100,67"));
    CHECK_EQ(h.log[2], std::string("TextColor:100,96"));
    // +88 caption flag was written into the widget array.
    CHECK_EQ(guild::gui::g_widgets[100].at<guild::i32>(88), 1);
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildObjectFreePanelOnly) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    g_infoPanel.form = 5; // panel occupied -> no build
    InfoObjectRecord obj; obj.code = 100;
    int r = InfoPanel_BuildObject(obj, nullptr, nullptr, false, false);
    CHECK(r == -1);
    CHECK(h.log.empty());
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildObjectExcludedCodeRange) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    InfoObjectRecord obj; obj.code = 148; // in [146,151] -> no build
    int r = InfoPanel_BuildObject(obj, nullptr, nullptr, false, false);
    CHECK(r == -1);
    CHECK(h.log.empty());
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildObjectNoHostRecord) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    InfoObjectRecord obj; obj.code = 30;
    int r = InfoPanel_BuildObject(obj, nullptr, nullptr, false, false);
    CHECK(r == 7);
    CHECK_EQ(h.log[0], std::string("LoadForm:panel\\infopanel_gebaeude"));
    CHECK(logHas(h, "Text:$Z%s"));            // no host record -> bare subtitle
    // icon at gfx 72, y = halfHeight-1 = 49, obj id = code+206 = 236.
    CHECK(logHas(h, "Obj:72,49,236"));
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildObjectWithNamedHost) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    InfoObjectRecord obj; obj.code = 30;
    InfoObjectRecord room; room.code = 5; room.customName = "Lager";
    int r = InfoPanel_BuildObject(obj, &room, nullptr, false, false);
    CHECK(r == 7);
    CHECK(logHas(h, "Text:$Z%s$A>%s<$A%s"));  // named host -> ">name<" line
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildBuildingFairgroundNoSlider) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    InfoBuildingRecord b; b.code = kBuildingTypeFairground; // 71
    int r = InfoPanel_BuildBuilding(b, 0, 0, false, true);
    CHECK(r == 7);
    CHECK(logHas(h, "LoadForm:panel\\infopanel_gebaeude"));
    CHECK(logHas(h, "Text:$Z%s$A%s"));
    // fairground -> no slider.
    CHECK_EQ(g_infoPanel.slider, -1);
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildBuildingNormalHasSlider) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    InfoBuildingRecord b; b.code = 10; b.item = 0xFFFF;
    int r = InfoPanel_BuildBuilding(b, 0x10, 1, true, true);
    CHECK(r == 7);
    // slider created, value-set with upgrade level 42.
    CHECK(g_infoPanel.slider != -1);
    CHECK(logHas(h, "Value:" + std::to_string(g_infoPanel.slider) + ",42"));
    // icon object id = code+1010 = 1020.
    CHECK(logHas(h, "Obj:72,50,1020"));
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildTransporterNeedsOwner) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    InfoObjectRecord t; t.code = 8;
    int r = InfoPanel_BuildTransporter(t, /*ownerResolved=*/false, 50, 60);
    CHECK(r == -1);
    CHECK(h.log.empty());
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildTransporterFull) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    InfoObjectRecord t; t.code = 8;
    int r = InfoPanel_BuildTransporter(t, true, 50, 60);
    CHECK(r == 7);
    CHECK(logHas(h, "LoadForm:panel\\infopanel_transporter"));
    CHECK(logHas(h, "Text:$Z%2N1$A%s"));
    // transporter icon gfx 48, y=halfHeight-1=49, code+206=214.
    CHECK(logHas(h, "Obj:48,49,214"));
    // window 2 selected at the tail.
    CHECK(logHas(h, "Select:7,2"));
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildStandardEmpty) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    int r = InfoPanel_BuildStandard(nullptr);
    CHECK(r == 7);
    CHECK(logHas(h, "LoadForm:panel\\infopanel_standard"));
    // two corner sprites at y 140 and 166.
    CHECK(logHas(h, "Sprite:3,140"));
    CHECK(logHas(h, "Sprite:3,166"));
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildStandardOwnBuildingMarket) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    InfoBuildingRecord b; b.code = kBuildingTypeMarket; // 30
    int r = InfoPanel_BuildStandard(&b);
    CHECK(r == 7);
    CHECK(logHas(h, "Text:$Z%s$A%s$A%s"));
    // market (type 30) -> the extra sprite at 166.
    CHECK(g_infoPanel.sprite3 != -1);
    CHECK(logHas(h, "Sprite:3,166"));
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildPersonAggregate) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    InfoPersonRecord p; p.aggregate = true; p.aggregateCount = 3; p.portraitObj = 0;
    int r = InfoPanel_BuildPerson(p);
    CHECK(r == 7);
    CHECK(logHas(h, "LoadForm:panel\\infopanel_personen"));
    CHECK(logHas(h, "Text:%i %s"));
    // null portrait -> placeholder object 1222.
    CHECK(logHas(h, "Obj:56,50,1222"));
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, BuildPersonDetail) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    InfoPersonRecord p; p.nameCode = 5; p.classByte = 6; p.job1 = 11; p.satisfaction = 80;
    int r = InfoPanel_BuildPerson(p);
    CHECK(r == 7);
    CHECK(logHas(h, "Text:%s %i%%"));
    // satisfaction slider present.
    CHECK(g_infoPanel.slider != -1);
    // class 6 -> the extra job icon (gfx 48 at half-8 = 42).
    CHECK(logHas(h, "Obj:48,42,0"));
    // group 11 -> the two tiled-row anchors (1406 / 1407).
    CHECK(logHas(h, "Obj:126,25,1406"));
    CHECK(logHas(h, "Obj:140,25,1407"));
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuild, ResetDetailSlots) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);
    g_infoPanel.detail[0] = 200;
    g_infoPanel.detail[1] = -1;
    g_infoPanel.detail[2] = 202;
    g_infoPanel.detail[3] = -1;
    int cleared = InfoPanel_ResetDetailSlots(4);
    CHECK_EQ(cleared, 2);
    CHECK(logHas(h, "Destroy:200"));
    CHECK(logHas(h, "Destroy:202"));
    CHECK_EQ(g_infoPanel.detail[0], -1);
    CHECK_EQ(g_infoPanel.detail[2], -1);
    InfoPanel_SetHost(nullptr);
}

