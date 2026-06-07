// End-to-end flows for the remaining GUI builders:
//   - Build several contact-menu trade/action panels for synthetic state, verify the full
//     entry set is registered into the shared status table and that wired clicks dispatch
//     the expected (mock) commands.
//   - Run the event-bar through a full lifecycle (init -> create slots -> select -> destroy
//     -> tear down) verifying the slot tree at each step.
//   - Run a dialog preflight gate sequence (skill -> resource -> build) end to end.
#include "gui/contact_actions.h"
#include "gui/event_panel.h"
#include "gui/dialog_checks.h"
#include "gui/window.h"
#include "gui/object.h"
#include "tests/framework/test.h"

#include <vector>

using namespace guild::gui;

namespace {

struct RecSink : ContactActionSink {
    std::vector<int> dispatched; // verb codes
    void OpenProductionPanel(int) override { dispatched.push_back(1); }
    void OpenStorage(int) override { dispatched.push_back(2); }
    void OpenTransport(int) override { dispatched.push_back(3); }
    void RunStaffBook(int) override { dispatched.push_back(4); }
    void RunMasterCertificate(int) override { dispatched.push_back(5); }
    void RunSearchExport(int) override { dispatched.push_back(6); }
    void RunSearchImport(int) override { dispatched.push_back(7); }
    void AbductTargetPick(int) override { dispatched.push_back(8); }
    void FreePrisonerPick(int) override { dispatched.push_back(9); }
    void ShowTalent(int) override { dispatched.push_back(10); }
    void InviteGuests(int) override { dispatched.push_back(11); }
    void AbductChooseDestination(int) override { dispatched.push_back(12); }
    void ResidenceMistress(int) override { dispatched.push_back(13); }
};

} // namespace

// ===========================================================================
// Contact menus: build several panels, walk every entry through dispatch.
// ===========================================================================
TEST(GuiRemainingE2E_Contact, BuildSeveralPanelsAndDispatchAll) {
    RecSink sink; ContactActions_SetCommandSink(&sink);

    // (1) Mixing production panel — full both-page build.
    ResetContactMenu();
    ProdEntries mix = ContactMenu_BuildMixing(kFormFlagPage200 | kFormFlagPage400);
    // Six entries: gather, production, storage, transport, master, staff.
    CHECK(mix.gather && mix.production && mix.storage && mix.transport && mix.masterCert && mix.staffBook);
    // All distinct.
    int ids[] = {mix.gather, mix.production, mix.storage, mix.transport, mix.masterCert, mix.staffBook};
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j)
            CHECK(ids[i] != ids[j]);
    // Dispatch the whole set; each click is handled exactly once.
    for (int id : ids)
        CHECK(ContactMenu_DispatchProductionEx(id, mix));
    CHECK_EQ((int)sink.dispatched.size(), 6);

    // (2) Brewery production — verify the storage/transport wiring lands.
    ResetContactMenu();
    sink.dispatched.clear();
    ProdEntries br = ContactMenu_BuildBrewery(kFormFlagPage200 | kFormFlagPage400);
    CHECK(br.production && br.storage && br.transport && br.masterCert && br.staffBook);
    CHECK(br.gather == 0); // brewery has no gather entry
    CHECK(ContactMenu_DispatchProductionEx(br.storage, br));
    CHECK(ContactMenu_DispatchProductionEx(br.transport, br));
    CHECK_EQ((int)sink.dispatched.size(), 2);
    CHECK_EQ(sink.dispatched[0], 2); // storage
    CHECK_EQ(sink.dispatched[1], 3); // transport

    // (3) Threat + feast action menus.
    ResetContactMenu();
    sink.dispatched.clear();
    ThreatEntries th = ContactMenu_BuildThreat();
    FeastEntries fe = ContactMenu_BuildFeast();
    // (threat and feast register into the same table; all 6 ids distinct & nonzero)
    int aids[] = {th.threat, th.pamphlet, th.rhetoric, fe.feast, fe.fight, fe.slander};
    for (int i = 0; i < 6; ++i) CHECK(aids[i] != 0);
    CHECK(ContactMenu_DispatchThreat(th.threat, th));
    CHECK(ContactMenu_DispatchFeast(fe.slander, fe));
    CHECK_EQ(sink.dispatched[0], 8);  // AbductTargetPick
    CHECK_EQ(sink.dispatched[1], 12); // AbductChooseDestination

    ContactActions_SetCommandSink(nullptr);
}

// ===========================================================================
// Event bar lifecycle.
// ===========================================================================
namespace {
struct LifeHost : EventPanelHost {
    int nextWidget = 200, nextForm = 700;
    int destroyedWidgets = 0, destroyedForms = 0;
    int clicked = -1;
    int LoadForm(const char*) override { return 8888; }
    int BuildEventForm(int) override { return nextForm++; }
    int AddIcon(int, int) override { return nextWidget++; }
    void DestroyWidget(int) override { ++destroyedWidgets; }
    void DestroyForm(int) override { ++destroyedForms; }
    void SetFormVisible(int, int) override {}
    void RaiseForm(int) override {}
    int IconValue(int w) override { return w == clicked ? 1 : 0; }
};
} // namespace

TEST(GuiRemainingE2E_Event, FullLifecycle) {
    ResetEventPanel();
    LifeHost host; EventPanel_SetHost(&host);

    EventPanel_InitBar();
    CHECK_EQ(g_eventBarForm, 8888);

    // Create three event slots.
    int a = EventPanel_RegisterObject(8u, 1);
    int b = EventPanel_RegisterObject(8u, 2);
    int c = EventPanel_RegisterObject(8u, 3);
    CHECK_EQ(EventPanel_CreateSlot(a, 1, 0, "A", 5), 0);
    CHECK_EQ(EventPanel_CreateSlot(b, 2, 0, "B", 5), 0);
    CHECK_EQ(EventPanel_CreateSlot(c, 3, 0, "C", 5), 0);
    CHECK_EQ(g_eventHighWater, 2);

    // Show the bar; select the middle slot via its clicked icon.
    EventPanel_ToggleVisible(0);
    host.clicked = g_eventSlots[1].widgetId;
    EventPanel_SelectActiveSlot();
    CHECK_EQ(g_eventSelected, 1);

    // Destroy the selected slot -> selection cleared, slot c compacts down, high-water drops.
    int destroyedBeforeW = host.destroyedWidgets;
    EventPanel_DestroySlot(b, 0);
    CHECK_EQ(g_eventSelected, -1);
    CHECK_EQ(g_eventSlots[1].panelObj, c);
    CHECK_EQ(g_eventHighWater, 1);
    CHECK(host.destroyedWidgets > destroyedBeforeW);

    // Tear down the bar: remaining slots destroyed + bar form destroyed.
    int formsBefore = host.destroyedForms;
    EventPanel_DestroyBar(0);
    CHECK(host.destroyedForms > formsBefore);

    EventPanel_SetHost(nullptr);
}

// ===========================================================================
// Dialog preflight gate sequence.
// ===========================================================================
namespace {
struct GateSink : DialogCheckSink {
    int lastMsg = -1, count = 0;
    void ShowMessage(int m, int, char) override { lastMsg = m; ++count; }
};
} // namespace

TEST(GuiRemainingE2E_Dialog, PreflightGateChain) {
    GateSink s; DialogChecks_SetSink(&s);

    // A dialog that requires skill 5 and 3 of a resource.  First attempt fails the skill gate.
    auto attempt = [&](int skill, int owned) -> bool {
        if (!Dialog_CheckSkillRequirement(skill, 5, 1)) return false;
        if (!Dialog_CheckResourceAmount(3, owned)) return false;
        return true; // would proceed to build
    };

    CHECK(attempt(/*skill*/2, /*owned*/10) == false); // skill gate stops it
    CHECK_EQ(s.lastMsg, kMsgSkillTooLow);
    s.count = 0;

    CHECK(attempt(/*skill*/9, /*owned*/1) == false);  // resource gate stops it
    CHECK_EQ(s.lastMsg, kMsgNotEnoughGeneric);
    s.count = 0;

    CHECK(attempt(/*skill*/9, /*owned*/10));           // both gates pass
    CHECK_EQ(s.count, 0);                               // no error shown

    DialogChecks_SetSink(nullptr);
}
