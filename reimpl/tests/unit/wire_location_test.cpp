// Verifies InstallRealLocationWiring() installs real command sinks into the live
// contact-menu / action-menu DI bridges (gui/contact_actions.*, gui/contact_loops.*),
// so the contact-loop "feast" verb routes into the 1:1-reconstructed
// guild::gui::Panel_RunGelage (0x54e940) instead of the inert library default.
#include "tests/framework/test.h"

#include "world/wire_location.h"
#include "gui/contact_loops.h"   // ContactLoopSink / Hud_BuildProductionMetal / Hud_DispatchProductionMetal
#include "gui/contact_actions.h" // ContactActions_SetCommandSink
#include "gui/gui_dialogs6.h"    // GuiDialogs6Hooks / SetGuiDialogs6Hooks / GuiDialogs6Hooks_Default

#include <cstring>
#include <string>

using namespace guild::gui;
using namespace guild::world;

namespace {

// Records the form path Panel_RunGelage opens, proving its reconstructed control
// flow ran when the wired RunFeast verb fired.
std::string g_lastForm;
int  WireLoc_Finalize(short, short, const char* formName) {
    g_lastForm = formName ? formName : "";
    return 7; // a non-zero form handle
}
int  WireLoc_Destroy(int) { return 0; }
// Frame loop never iterates -> the Gelage panel opens and immediately tears down.
int  WireLoc_FrameLoopNone(int, int, const void*) { return 0; }

} // namespace

// After install, dispatching the metal-production loop's "feast" entry routes the
// click through ContactLoopSink::RunFeast, which the real sink binds to
// Panel_RunGelage — observed by the form it opens ("special\\gelage").
TEST(WireLocation, FeastVerbRoutesIntoPanelRunGelage) {
    // Real, deterministic gui_dialogs6 hooks: open form, no frame iterations, destroy.
    GuiDialogs6Hooks h = *GuiDialogs6Hooks_Default();
    h.gameTickFinalize     = &WireLoc_Finalize;
    h.formDestroy          = &WireLoc_Destroy;
    h.gameLogicRunFrameLoop = &WireLoc_FrameLoopNone;
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    InstallRealLocationWiring();

    // Build the metal-production entry set (page 0x200 ready), then click "feast".
    MetalProdIds e = Hud_BuildProductionMetal(0x200);
    CHECK(e.feast != 0);            // the GELAGE entry registered

    g_lastForm.clear();
    bool handled = Hud_DispatchProductionMetal(e.feast, e);
    CHECK(handled);                 // the dispatcher consumed the click
    // The wired RunFeast -> Panel_RunGelage ran and opened the party form.
    CHECK(g_lastForm == "special\\gelage");

    // Restore for any later test in this TU.
    SetGuiDialogs6Hooks(prev);
    ContactLoops_SetCommandSink(nullptr);
    ContactActions_SetCommandSink(nullptr);
}

// Install is idempotent and leaves unbound verbs inert: a non-feast click (storage)
// dispatches through the sink without invoking Panel_RunGelage.
TEST(WireLocation, UnboundVerbStaysInert) {
    GuiDialogs6Hooks h = *GuiDialogs6Hooks_Default();
    h.gameTickFinalize     = &WireLoc_Finalize;
    h.formDestroy          = &WireLoc_Destroy;
    h.gameLogicRunFrameLoop = &WireLoc_FrameLoopNone;
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    InstallRealLocationWiring();
    InstallRealLocationWiring(); // idempotent

    MetalProdIds e = Hud_BuildProductionMetal(0x200);
    CHECK(e.storage != 0);

    g_lastForm.clear();
    bool handled = Hud_DispatchProductionMetal(e.storage, e);
    CHECK(handled);                       // storage click consumed (inert OpenStorage)
    CHECK(g_lastForm.empty());            // Panel_RunGelage did NOT run

    SetGuiDialogs6Hooks(prev);
    ContactLoops_SetCommandSink(nullptr);
    ContactActions_SetCommandSink(nullptr);
}
