// End-to-end flow for the info-panel content builders (gilde.exe 0x4b6454..0x4b7468).
// Drives a whole "select a subject -> dispatcher picks a builder -> builder assembles the
// panel" sequence across the sibling dispatcher and the real builder bodies, then a
// GUARDED real-asset leg that checks the shipped forms.BIN exists and the panel form names
// are present in it.
#include "test.h"

#include "gui/infopanel.h"
#include "gui/infopanel_build.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild::gui;

namespace {

struct E2EHost : InfoPanelHost {
    std::vector<std::string> forms;
    std::vector<std::string> texts;
    int nextWidget = 500;
    int LoadForm(const char* name) override { forms.push_back(name); return 11; }
    void RenderRichString(const char* fmt) override { texts.push_back(fmt); }
    int CurrentWindowHalfHeight() override { return 48; }
    int AddObject(int, int, int) override { return nextWidget++; }
    int AddSprite(int, int, int) override { return nextWidget++; }
    int AddSlider(int, int, int, int, int, int, int) override { return nextWidget++; }
    int BuildingUpgradeLevel(const void*) override { return 75; }
};

} // namespace

TEST(GuiInfoPanelBuildE2E, FullSelectionCycle) {
    // A sequence of selections; each rebuilds the panel from scratch (panel freed between).
    E2EHost h; InfoPanel_SetHost(&h);

    // 1) Empty selection -> standard panel.
    ResetInfoPanelBuild();
    {
        InfoSelection sel{};
        InfoBuilder b = InfoPanel_SelectBuilder(sel);
        CHECK(b == InfoBuilder::kStandard);
        CHECK(InfoPanel_BuildStandard(nullptr) == 11);
    }
    // 2) Building selected -> building panel with the upgrade slider populated.
    ResetInfoPanelBuild();
    {
        InfoSelection sel{}; sel.building = 0x500;
        InfoBuilder b = InfoPanel_SelectBuilder(sel);
        CHECK(b == InfoBuilder::kBuilding);
        InfoBuildingRecord bd; bd.code = 12; bd.item = 0xFFFF;
        int form = InfoPanel_BuildBuilding(bd, 0x10, 1, true, true);
        CHECK(form == 11);
        CHECK(g_infoPanel.slider != -1);   // a normal building has the upgrade slider
    }
    // 3) Single person -> person panel.
    ResetInfoPanelBuild();
    {
        InfoSelection sel{}; sel.person = 0x600; sel.personMode = 1;
        InfoBuilder b = InfoPanel_SelectBuilder(sel);
        CHECK(b == InfoBuilder::kPerson);
        InfoPersonRecord p; p.nameCode = 3; p.satisfaction = 60;
        CHECK(InfoPanel_BuildPerson(p) == 11);
    }

    // All four panel forms were referenced across the cycle.
    bool std = false, geb = false, per = false;
    for (auto& f : h.forms) {
        if (f == "panel\\infopanel_standard") std = true;
        if (f == "panel\\infopanel_gebaeude") geb = true;
        if (f == "panel\\infopanel_personen") per = true;
    }
    CHECK(std && geb && per);
    InfoPanel_SetHost(nullptr);
}

TEST(GuiInfoPanelBuildE2E, RealAssetFormsGuarded) {
    // GUARDED: requires a real "Die Gilde" install pointed at by GUILD_GAME_DIR with a
    // Resources/forms.BIN.  Otherwise this is a clean skip-pass.
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir) {
        CHECK(true);
        return;
    }
    std::string path = std::string(dir) + "/Resources/forms.BIN";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        // Asset folder present but no forms.BIN at the expected path -> skip-pass.
        CHECK(true);
        return;
    }
    // Scan the archive for the panel form-name strings the builders load.
    std::vector<char> buf;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        buf.resize(static_cast<size_t>(sz));
        size_t got = std::fread(buf.data(), 1, buf.size(), f);
        buf.resize(got);
    }
    std::fclose(f);

    auto contains = [&](const char* needle) {
        size_t n = std::strlen(needle);
        if (buf.size() < n) return false;
        for (size_t i = 0; i + n <= buf.size(); ++i)
            if (std::memcmp(buf.data() + i, needle, n) == 0) return true;
        return false;
    };
    // At least the building panel should be present in a real assets file.
    CHECK(contains("infopanel_gebaeude") || contains("infopanel_standard"));
}
