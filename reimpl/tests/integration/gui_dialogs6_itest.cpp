// Integration test: drive gui_dialogs6's RunInventory against a REAL reconstructed
// sibling — gui::Panel_RunUseObject (VIBE_Panel_RunUseObject @0x54f3e8,
// src/gui/gui_dialogs5.cpp) — exactly as the live wiring does. RunInventory does
// NOT take a hook for the use-object dispatch; it calls the sibling directly, so
// this exercises the genuine cross-module call. We install gui_dialogs5's own
// hooks so the sibling's form-load is observable, click a populated inventory
// slot, and assert the sibling loaded its "panel\\useobj" form.
//
// We also forward gui_dialogs6's widget-creation-free leaves so the inventory
// path runs deterministically, and the force-quit latch (g_forceQuitLatch) is
// shared between both modules (defined once in gui_dialogs5.cpp) — we assert it
// is the same object across the modules.
#include "test.h"

#include "gui/gui_dialogs6.h"
#include "gui/gui_dialogs5.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

namespace {

// --- gui_dialogs5 (the REAL sibling) capture ------------------------------
std::vector<std::string>* g5_forms = nullptr;
int D5Form(i16, i16, const char* n) { if (g5_forms) g5_forms->push_back(n ? n : ""); return 9; }
// Make the sibling's slot lookup succeed so it loads its form.
int D5FindIdx(short, void*) { return 1; }
int D5FindSlot(short) { return 1; }
int D5Frame(int, int, const void*) { return 0; }   // exit the sibling's loop at once

// --- gui_dialogs6 (module under test) -------------------------------------
int g6_frameBudget = 0;
int D6Frame(int, int, const void*) { return g6_frameBudget-- > 0 ? 1 : 0; }
int D6Form(i16, i16, const char*) { return 8; }
int D6Destroy(int) { return 8; }

} // namespace

TEST(GuiDialogs6Itest, RunInventoryDispatchesRealUseObjectSibling) {
    ResetGuiDialogs6();
    ResetGuiDialogs5();

    std::vector<std::string> siblingForms;
    g5_forms = &siblingForms;

    // Install REAL gui_dialogs5 hooks: the sibling loads its form, its slot
    // lookups succeed, and its frame loop exits at once (deterministic).
    GuiDialogs5Hooks h5 = *GuiDialogs5Hooks_Default();
    h5.gameTickFinalize = &D5Form;
    h5.inventoryFindSlotIndexByItemId = &D5FindIdx;
    h5.inventoryFindSlotByItemId = &D5FindSlot;
    h5.gameLogicRunFrameLoop = &D5Frame;
    const GuiDialogs5Hooks* prev5 = SetGuiDialogs5Hooks(&h5);

    // gui_dialogs6 hooks: run exactly one frame, with one populated, usable slot.
    GuiDialogs6Hooks h6 = *GuiDialogs6Hooks_Default();
    h6.gameTickFinalize = &D6Form;
    h6.formDestroy = &D6Destroy;
    h6.inventoryFindSlotByItemId = [](short) { return 1; };  // item usable -> dispatch
    h6.readLastClickedId = []() { return 100; };             // != -1, drives the slot scan
    g6_frameBudget = 1;
    h6.gameLogicRunFrameLoop = &D6Frame;
    const GuiDialogs6Hooks* prev6 = SetGuiDialogs6Hooks(&h6);

    // Populate inventory slot 0: rec pointer + matching id, and make the hover
    // resolve to that slot id so the dispatch fires.
    static short itemRec[4] = {123, 0, 0, 0};
    g_invSlotRecs[0] = itemRec;
    g_invSlotIds[0]  = 555;
    // readHoverObject must equal g_invSlotIds[0]:
    h6.readHoverObject = []() { return 555; };
    SetGuiDialogs6Hooks(&h6);

    Panel_RunInventory(/*city*/2);

    // The REAL sibling ran and loaded its use-object form.
    bool sawUseObj = false;
    for (auto& f : siblingForms) if (f == "panel\\useobj") sawUseObj = true;
    CHECK(sawUseObj);
    CHECK(siblingForms.size() >= 1);

    g5_forms = nullptr;
    SetGuiDialogs6Hooks(prev6);
    SetGuiDialogs5Hooks(prev5);
}

// The force-quit latch is a single shared symbol across both modules (defined
// once in gui_dialogs5.cpp). Writing it through gui_dialogs6's force-quit path
// must be visible to the gui_dialogs5 view of the same global.
TEST(GuiDialogs6Itest, ForceQuitLatchSharedAcrossModules) {
    ResetGuiDialogs6();
    ResetGuiDialogs5();
    CHECK_EQ(g_forceQuitLatch, 0);

    // Drive RunThievesGuildTrain with a right-click and no dragged items -> sets
    // g_forceQuitLatch = 1 (the same global gui_dialogs5 owns).
    GuiDialogs6Hooks h = *GuiDialogs6Hooks_Default();
    h.gameTickFinalize = [](i16, i16, const char*) { return 3; };
    h.formDestroy = [](int) { return 3; };
    h.readMouseRelease = []() { return 1; };
    h.dragSlotCountUsed = []() { return 0; };
    static int budget = 1;
    budget = 1;
    h.gameLogicRunFrameLoop = [](int, int, const void*) { return budget-- > 0 ? 1 : 0; };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    Panel_RunThievesGuildTrain(0, 0, nullptr, 0);

    CHECK_EQ(g_forceQuitLatch, 1);   // set via gui_dialogs6, observed via the shared symbol
    SetGuiDialogs6Hooks(prev);
}
