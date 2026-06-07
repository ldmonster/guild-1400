// Integration test: the info-panel content builders driven by the REAL sibling
// dispatcher (gui/infopanel.cpp — VIBE_InfoPanel_Update / InfoPanel_SelectBuilder).
// The dispatcher classifies the live selection into an InfoBuilder; we route that into
// the actual builder bodies (gui/infopanel_build.cpp) and check end-to-end that the
// right form is loaded for the selection.
#include "test.h"

#include "gui/infopanel.h"
#include "gui/infopanel_build.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

struct RecHost : InfoPanelHost {
    std::vector<std::string> forms;
    int formId = 9;
    int LoadForm(const char* name) override { forms.push_back(name); return formId; }
    int CurrentWindowHalfHeight() override { return 40; }
    int AddObject(int, int, int) override { return 300; }
    int AddSprite(int, int, int) override { return 301; }
    int AddSlider(int, int, int, int, int, int, int) override { return 302; }
    int BuildingUpgradeLevel(const void*) override { return 1; }
};

// Run the builder the real dispatcher selected.
int RunSelected(InfoBuilder b, const InfoSelection& sel) {
    switch (b) {
        case InfoBuilder::kTransporter: {
            InfoObjectRecord t; t.code = 8;
            return InfoPanel_BuildTransporter(t, true, 50, 60);
        }
        case InfoBuilder::kPerson: {
            InfoPersonRecord p; p.aggregate = (sel.personMode > 1);
            p.nameCode = 1; p.satisfaction = 50;
            return InfoPanel_BuildPerson(p);
        }
        case InfoBuilder::kObject: {
            InfoObjectRecord o; o.code = 50;
            return InfoPanel_BuildObject(o, nullptr, nullptr, false, false);
        }
        case InfoBuilder::kBuilding: {
            InfoBuildingRecord bd; bd.code = 10;
            return InfoPanel_BuildBuilding(bd, 0, 1, true, true);
        }
        case InfoBuilder::kStandard:
            return InfoPanel_BuildStandard(nullptr);
        default:
            return -1;
    }
}

} // namespace

TEST(GuiInfoPanelBuildIT, DispatcherSelectsTransporterAndLoadsForm) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);

    InfoSelection sel{};
    sel.transporter = 0x1000;
    sel.transporterCategory = kTransporterPanelCategory; // 29

    // Real dispatcher (sibling module) picks the builder.
    InfoBuilder b = InfoPanel_SelectBuilder(sel);
    CHECK(b == InfoBuilder::kTransporter);

    int form = RunSelected(b, sel);
    CHECK(form == 9);
    CHECK(h.forms.size() == 1);
    CHECK_EQ(h.forms[0], std::string("panel\\infopanel_transporter"));
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuildIT, DispatcherSelectsBuildingAndLoadsForm) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);

    InfoSelection sel{};
    sel.building = 0x2000;       // a building is selected (no transporter/subobject)
    sel.sceneObject = 0;

    InfoBuilder b = InfoPanel_SelectBuilder(sel);
    CHECK(b == InfoBuilder::kBuilding);

    int form = RunSelected(b, sel);
    CHECK(form == 9);
    CHECK_EQ(h.forms[0], std::string("panel\\infopanel_gebaeude"));
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuildIT, DispatcherSelectsStandardWhenEmpty) {
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);

    InfoSelection sel{}; // nothing selected

    InfoBuilder b = InfoPanel_SelectBuilder(sel);
    CHECK(b == InfoBuilder::kStandard);

    int form = RunSelected(b, sel);
    CHECK(form == 9);
    CHECK_EQ(h.forms[0], std::string("panel\\infopanel_standard"));
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuildIT, UpdateRunsBuilderThroughCommandSink) {
    // The full Update flow: InfoPanel_Update detects the change, the command sink runs the
    // selected builder, which loads the form via the build host.
    ResetInfoPanelBuild();
    RecHost h; InfoPanel_SetHost(&h);

    struct Bridge : InfoPanelCommandSink {
        InfoSelection sel;
        int lastForm = -2;
        void Build(InfoBuilder b) override { lastForm = RunSelected(b, sel); }
    } bridge;

    InfoSelection sel{};
    sel.person = 0x3000;
    sel.personMode = 1;          // single person -> kPerson
    bridge.sel = sel;
    InfoPanel_SetCommandSink(&bridge);

    InfoSnapshot snap{}; // invalid -> change detected
    InfoBuilder ran = InfoPanel_Update(sel, snap);
    CHECK(ran == InfoBuilder::kPerson);
    CHECK(bridge.lastForm == 9);
    CHECK_EQ(h.forms[0], std::string("panel\\infopanel_personen"));
    CHECK(snap.valid);

    // A second Update with the same selection: no change -> no rebuild.
    ResetInfoPanelBuild();
    h.forms.clear();
    InfoBuilder again = InfoPanel_Update(sel, snap);
    CHECK(again == InfoBuilder::kNone);
    CHECK(h.forms.empty());

    InfoPanel_SetCommandSink(nullptr);
    InfoPanel_SetHost(nullptr);
}
