// Integration test for guild::gui gui_dialogs8.
//
// REAL SIBLING WIRING: VIBE_Tooltip_BuildPerson computes its title-row rank argument by
// calling VIBE_BuildingType_ComputeRankWithinGroup(HIBYTE(*(record+353))) — a function
// that is ALREADY reconstructed (sim/building_type.cpp 0x58a560). Here we forward the
// buildingTypeRankWithinGroup hook into that REAL reconstructed sibling (NOT a mock),
// exactly as the live process wires it, drive Tooltip_BuildPerson with a person record
// whose +353 high byte is a real building-group code, and assert that the rank value the
// builder fed into the tooltip is the one the live sim::BuildingType_ComputeRankWithinGroup
// produced — i.e. the cross-module GUI/sim flow ran end to end.
//
// The builder also drives the already-reconstructed gui::Form_SelectWindow /
// Form_CenterChildWindows siblings directly in its production path; those run for real
// here too.
#include "test.h"

#include "gui/gui_dialogs8.h"
#include "sim/building_type.h"   // BuildingType_ComputeRankWithinGroup (REAL sibling)

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {

int g_capturedRank = -999;

// Forward the hook into the REAL reconstructed sim sibling, and capture what it returned
// so the test can assert the cross-module value flowed into the builder.
int RankViaRealSibling(u8 groupCode) {
    int rank = sim::BuildingType_ComputeRankWithinGroup(groupCode);
    g_capturedRank = rank;
    return rank;
}

int ItGameTick(i16, i16, const char*) { return 55; }
int ItText(unsigned, unsigned) { return 0; }
int ItFmt(char* out, const char*, const char*) { if (out) out[0] = '\0'; return 0; }

GuiDialogs8Hooks MakeWiredHooks() {
    GuiDialogs8Hooks h = *GuiDialogs8Hooks_Default();
    h.buildingTypeRankWithinGroup = RankViaRealSibling; // -> REAL sim sibling
    h.gameTickFinalize = ItGameTick;
    h.textRenderRichString = ItText;
    h.textRenderFormatted = ItFmt;
    return h;
}

} // namespace

TEST(GuiDialogs8It, BuildPersonRankViaRealBuildingTypeSibling) {
    ResetGuiDialogs8();
    g_capturedRank = -999;
    GuiDialogs8Hooks h = MakeWiredHooks();
    SetGuiDialogs8Hooks(&h);

    // Person record whose +353 high byte = building-group code 2.
    // sim::BuildingType_ComputeRankWithinGroup(2) == 6 - 2 + 1 == 5 (codes 1..6 group).
    u8 rec[600];
    std::memset(rec, 0, sizeof(rec));
    std::int32_t packed = 0x02000000; // high byte = 2
    std::memcpy(rec + 353, &packed, 4);

    int form = Tooltip_BuildPerson(reinterpret_cast<const i16*>(rec));
    CHECK_EQ(form, 55);

    // Independently compute what the REAL sibling produces for group code 2.
    int expected = sim::BuildingType_ComputeRankWithinGroup(2);
    CHECK_EQ(expected, 5);
    // And assert the builder fed the REAL sibling and used its result.
    CHECK_EQ(g_capturedRank, expected);

    SetGuiDialogs8Hooks(nullptr);
}

TEST(GuiDialogs8It, RankSiblishHighestCodeIsRankOne) {
    ResetGuiDialogs8();
    g_capturedRank = -999;
    GuiDialogs8Hooks h = MakeWiredHooks();
    SetGuiDialogs8Hooks(&h);

    // High byte = 6 -> highest code in the 1..6 group -> rank 1.
    u8 rec[600];
    std::memset(rec, 0, sizeof(rec));
    std::int32_t packed = 0x06000000;
    std::memcpy(rec + 353, &packed, 4);

    Tooltip_BuildPerson(reinterpret_cast<const i16*>(rec));
    CHECK_EQ(g_capturedRank, 1);
    CHECK_EQ(sim::BuildingType_ComputeRankWithinGroup(6), 1);

    SetGuiDialogs8Hooks(nullptr);
}
