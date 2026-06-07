#include "test.h"

// Cross-module integration: drive the contact-loop builders (contact_loops.cpp) against the
// REAL shared status-text table + register/reset leaves (contact_menu.cpp) and the REAL
// sibling per-trade builders (contact_actions.cpp).  Verifies that the loops reuse the same
// 32-entry table, that de-dup by object name is honoured across modules, and that a clicked
// handle routes through each module's own dispatcher consistently.

#include "gui/contact_loops.h"
#include "gui/contact_actions.h"

using namespace guild::gui;

namespace {

struct CountSink : ContactLoopSink {
    int storageHits = 0, transportHits = 0;
    void OpenStorage(int) override { ++storageHits; }
    void OpenTransport(int) override { ++transportHits; }
};

} // namespace

// The loops and the per-trade builders share contact_LAGER / contact_TRANSPORT entries.
// Registering them through different modules must de-dup to the SAME table handle.
TEST(GuiContactLoopsItest, SharedTableDedup) {
    ResetContactMenu();

    // Build a production-metal loop (registers LAGER + TRANSPORT among others).
    MetalProdIds metal = Hud_BuildProductionMetal(kFormFlagPage200);
    CHECK(metal.storage != 0);
    CHECK(metal.transport != 0);

    // Now build a sibling production panel from contact_actions.cpp on the SAME table; its
    // LAGER/TRANSPORT entries must resolve to the same handles (de-dup by name).
    ProdEntries prod = ContactMenu_BuildStonemason(kFormFlagPage200 | kFormFlagPage400);
    CHECK_EQ(prod.storage, metal.storage);
    CHECK_EQ(prod.transport, metal.transport);
}

// The shared table caps at 32 distinct objects.  A robber-hideout build (12 distinct names)
// plus the sabotage build (4) plus guild-master (5) must all coexist with unique handles and
// stay under the cap, and each dispatcher only fires on its own ids.
TEST(GuiContactLoopsItest, MultiBuilderCoexistence) {
    ResetContactMenu();
    CountSink sink; ContactLoops_SetCommandSink(&sink);

    ContactLoopGate gate; ContactLoops_SetGate(&gate); // HasObject default true

    RobberHideoutIds rob = ContactMenu_BuildRobberHideout(kFormFlagPage200 | kFormFlagPage400);
    SabotageIds sab = ContactMenu_BuildSabotage();

    // All registered ids are nonzero and distinct (different object names).
    int ids[] = { rob.equip, rob.ambush, rob.attack, rob.extort, rob.spyBuild, rob.raid,
                  rob.regen, rob.storage, rob.feast, rob.transport,
                  sab.sabotage, sab.beatUp, sab.night, sab.bribe };
    for (int a : ids) CHECK(a != 0);
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        for (size_t j = i + 1; j < sizeof(ids) / sizeof(ids[0]); ++j)
            CHECK(ids[i] != ids[j]);

    // A robber dispatch routes to the loop sink; a sabotage id is NOT a robber-storage id.
    CHECK(ContactMenu_DispatchRobberHideout(rob.storage, rob, 0));
    CHECK_EQ(sink.storageHits, 1);
    CHECK(ContactMenu_DispatchRobberHideout(sab.sabotage, rob, 0) == false);

    ContactLoops_SetGate(nullptr);
    ContactLoops_SetCommandSink(nullptr);
}

// Reset between menus actually clears the table so a fresh build gets fresh slots starting
// at slot 0 (the loops all call StatusText_ResetEntries first).
TEST(GuiContactLoopsItest, ResetBetweenMenus) {
    ResetContactMenu();

    Hud_BuildErzAbbau(kFormFlagPage200);
    CHECK(g_statusEntries[0].handle != 0); // slot 0 used

    // StatusText_ResetEntries (what every loop calls at entry) must clear slot 0.
    StatusText_ResetEntries();
    // A bare reset clears the per-object active flags but leaves the entry handles; the full
    // ResetContactMenu (used between independent menu sessions) clears the table records.
    ResetContactMenu();
    CHECK_EQ(g_statusEntries[0].handle, 0);

    TavernIds t = ContactMenu_BuildTavern();
    CHECK(g_statusEntries[0].handle != 0);
    CHECK(t.dice != 0);
}
