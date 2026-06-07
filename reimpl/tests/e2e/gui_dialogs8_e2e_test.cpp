// End-to-end test for guild::gui gui_dialogs8.
//
// Exercises a whole tooltip flow across the three recovered builders with a single set of
// installed hooks acting as the form/text/person runtime: a person hovers (BuildPerson),
// then a detailed person tooltip opens modally and is dismissed (BuildPersonDetailed),
// then an object is hovered (BuildObject). We assert the cross-builder render sequence the
// live game would emit, and that the modal loop ran and tore the form down.
#include "test.h"

#include "gui/gui_dialogs8.h"
#include "gui/gui_dialogs5.h" // g_forceQuitLatch

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::gui;

namespace {

struct Trace {
    std::vector<unsigned> richIds;
    int forms = 0;
    int frames = 0;
    int drags = 0;
    int cards = 0;
    bool resolve = true;
    int budget = 2;
};
Trace* g_t = nullptr;

int EText(unsigned id, unsigned) { if (g_t) g_t->richIds.push_back(id); return 0; }
int EFmt(char* out, const char*, const char*) { if (out) out[0] = '\0'; return 0; }
int ETick(i16, i16, const char*) { if (g_t) ++g_t->forms; return g_t ? g_t->forms : 1; } // valid in-range form ids 1..3
int EResolve(void*) { return g_t && g_t->resolve ? 1 : 0; }
int ECard(int, int, void*) { if (g_t) ++g_t->cards; return 0; }
int ERank(u8) { return 3; }
int EFrame(int, int, const void*) { if (g_t && g_t->frames < g_t->budget) { ++g_t->frames; return 1; } return 0; }
void EDrag(void*) { if (g_t) ++g_t->drags; }
int EMouseEdge() { return 1; } // cancel edge always set -> drives the force-quit latch

GuiDialogs8Hooks MakeHooks() {
    GuiDialogs8Hooks h = *GuiDialogs8Hooks_Default();
    h.textRenderRichString = EText;
    h.textRenderFormatted = EFmt;
    h.gameTickFinalize = ETick;
    h.personResolveStatusFlags = EResolve;
    h.hudBuildPersonCardSimple = ECard;
    h.buildingTypeRankWithinGroup = ERank;
    h.gameLogicRunFrameLoop = EFrame;
    h.dragSlotBeginDragText = EDrag;
    h.readMouseRelease = EMouseEdge;
    return h;
}

bool saw(const Trace& t, unsigned id) {
    for (unsigned x : t.richIds) if (x == id) return true;
    return false;
}

} // namespace

TEST(GuiDialogs8E2E, TooltipFlowAcrossBuilders) {
    ResetGuiDialogs8();
    Trace t;
    g_t = &t;
    GuiDialogs8Hooks h = MakeHooks();
    SetGuiDialogs8Hooks(&h);

    // --- 1) hover a person -> BuildPerson ---
    u8 person[600];
    std::memset(person, 0, sizeof(person));
    person[2] = 4;                  // detail gate value (< 10)
    person[9] = 1;                  // gender -> male text bases
    person[13] = 5;                 // talent level
    std::int32_t packed = 0x03000000;
    std::memcpy(person + 353, &packed, 4);

    int formA = Tooltip_BuildPerson(reinterpret_cast<const i16*>(person));
    CHECK(formA > 0);
    CHECK_EQ(t.cards, 1);           // status resolved -> card built
    CHECK(saw(t, 0x2B));            // cash row
    CHECK(saw(t, 0x33));            // guild row
    size_t afterPerson = t.richIds.size();
    CHECK(afterPerson > 0);

    // --- 2) open the detailed (modal) tooltip -> BuildPersonDetailed ---
    g_forceQuitLatch = 0;
    int formB = Tooltip_BuildPersonDetailed(reinterpret_cast<const i16*>(person));
    CHECK(formB > 0);
    CHECK_EQ(t.frames, t.budget);   // modal idle loop spun the full budget
    CHECK_EQ(t.drags, 1);           // drag-begin fired on exit
    CHECK_EQ(g_forceQuitLatch, 1);  // cancel edge forced the latch

    // --- 3) hover an object -> BuildObject ---
    int formC = Tooltip_BuildObject(17);
    CHECK(formC > 0);
    CHECK(saw(t, 0x1F));            // price-pair row
    CHECK(saw(t, 0x25));            // object section header

    // Three distinct forms were finalized across the flow.
    CHECK_EQ(t.forms, 3);

    SetGuiDialogs8Hooks(nullptr);
    g_t = nullptr;
}
