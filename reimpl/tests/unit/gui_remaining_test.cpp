// Unit tests for the remaining deferred GUI builders / content helpers:
//   contact_actions : production trade menus + social/criminal action menus (entry set + wiring)
//   event_panel     : 16-slot event-bar table alloc / free / compact / visibility / selection
//   dialog_checks   : preflight checks + simple messagebox form + Book/Window helpers
#include "gui/contact_actions.h"
#include "gui/event_panel.h"
#include "gui/dialog_checks.h"
#include "gui/window.h"
#include "gui/object.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

// ===========================================================================
// contact_actions
// ===========================================================================
namespace {

// Records every dispatched verb so wiring can be asserted.
struct ActSink : ContactActionSink {
    int production=0, storage=0, transport=0, staff=0, master=0, searchExp=0, searchImp=0;
    int abductPick=0, freePick=0, talent=0, invite=0, abductDest=0, mistress=0;
    void reset() { *this = ActSink{}; }
    void OpenProductionPanel(int o) override { production = o; }
    void OpenStorage(int o) override { storage = o; }
    void OpenTransport(int o) override { transport = o; }
    void RunStaffBook(int o) override { staff = o; }
    void RunMasterCertificate(int o) override { master = o; }
    void RunSearchExport(int o) override { searchExp = o; }
    void RunSearchImport(int o) override { searchImp = o; }
    void AbductTargetPick(int o) override { abductPick = o; }
    void FreePrisonerPick(int o) override { freePick = o; }
    void ShowTalent(int w) override { talent = w; }
    void InviteGuests(int o) override { invite = o; }
    void AbductChooseDestination(int o) override { abductDest = o; }
    void ResidenceMistress(int o) override { mistress = o; }
};

// A gate that records which handler-flag masks were queried, returning a configurable mask.
struct FlagGate : ContactGate {
    int openMask = ~0;
    int lastQueried = 0;
    bool HandlerFlag(int mask) override { lastQueried |= mask; return (openMask & mask) != 0; }
};

} // namespace

TEST(GuiRemaining_Contact, SmithBothPagesAllEntries) {
    ResetContactMenu();
    ActSink sink; ContactActions_SetCommandSink(&sink);
    FlagGate gate; ContactActions_SetGate(&gate);

    ProdEntries e = ContactMenu_BuildSmith(kFormFlagPage200 | kFormFlagPage400);
    // All five entries registered, distinct nonzero ids.
    CHECK(e.production != 0);
    CHECK(e.masterCert != 0);
    CHECK(e.storage != 0);
    CHECK(e.transport != 0);
    CHECK(e.staffBook != 0);
    CHECK(e.gather == 0); // Smith has no gather entry
    // Each handler-flag mask was queried.
    CHECK((gate.lastQueried & 32) && (gate.lastQueried & 128) && (gate.lastQueried & 16) &&
          (gate.lastQueried & 256) && (gate.lastQueried & 64));

    // Wiring: clicking production opens the production panel.
    CHECK(ContactMenu_DispatchProductionEx(e.production, e));
    CHECK_EQ(sink.production, e.production);
    sink.reset();
    CHECK(ContactMenu_DispatchProductionEx(e.transport, e));
    CHECK_EQ(sink.transport, e.transport);
    sink.reset();
    CHECK(ContactMenu_DispatchProductionEx(e.staffBook, e));
    CHECK_EQ(sink.staff, e.staffBook);
    sink.reset();
    CHECK(ContactMenu_DispatchProductionEx(e.masterCert, e));
    CHECK_EQ(sink.master, e.masterCert);
    ContactActions_SetGate(nullptr);
}

TEST(GuiRemaining_Contact, SmithGatedOffEntriesSuppressed) {
    ResetContactMenu();
    ActSink sink; ContactActions_SetCommandSink(&sink);
    FlagGate gate; gate.openMask = 32; // only production allowed
    ContactActions_SetGate(&gate);

    ProdEntries e = ContactMenu_BuildSmith(kFormFlagPage200 | kFormFlagPage400);
    CHECK(e.production != 0);
    CHECK_EQ(e.masterCert, 0);
    CHECK_EQ(e.storage, 0);
    CHECK_EQ(e.transport, 0);
    CHECK_EQ(e.staffBook, 0);
    // A zero id never dispatches.
    CHECK(ContactMenu_DispatchProductionEx(0, e) == false);
    ContactActions_SetGate(nullptr);
}

TEST(GuiRemaining_Contact, PerfumeryGatherEntryAndWiring) {
    ResetContactMenu();
    ActSink sink; ContactActions_SetCommandSink(&sink);

    ProdEntries e = ContactMenu_BuildPerfumery(kFormFlagPage200 | kFormFlagPage400);
    CHECK(e.gather != 0);       // perfumery has the search/gather entry
    CHECK(e.production != 0);
    CHECK(ContactMenu_DispatchProductionEx(e.gather, e));
    CHECK_EQ(sink.searchExp, e.gather);
    sink.reset();
    CHECK(ContactMenu_DispatchProductionEx(e.storage, e));
    CHECK_EQ(sink.storage, e.storage);
}

TEST(GuiRemaining_Contact, PageGatingHidesEntries) {
    ResetContactMenu();
    // Only page 0x200 ready -> only production registered for Stonemason.
    ProdEntries e = ContactMenu_BuildStonemason(kFormFlagPage200);
    CHECK(e.production != 0);
    CHECK_EQ(e.storage, 0);
    CHECK_EQ(e.transport, 0);
    CHECK_EQ(e.masterCert, 0);
}

TEST(GuiRemaining_Contact, FlytrapOnlyPondWired) {
    ResetContactMenu();
    ActSink sink; ContactActions_SetCommandSink(&sink);
    FlytrapEntries e = ContactMenu_BuildFlytrap(kFormFlagPage200);
    CHECK(e.flytrap != 0 && e.terrarium != 0 && e.pond != 0);
    // Clicking flytrap/terrarium does nothing; pond runs import.
    CHECK(ContactMenu_DispatchFlytrap(e.flytrap, e) == false);
    CHECK(ContactMenu_DispatchFlytrap(e.pond, e));
    CHECK_EQ(sink.searchImp, e.pond);
}

TEST(GuiRemaining_Contact, ThreatFeastMistressWiring) {
    ResetContactMenu();
    ActSink sink; ContactActions_SetCommandSink(&sink);

    ThreatEntries t = ContactMenu_BuildThreat();
    CHECK(t.threat && t.pamphlet && t.rhetoric);
    CHECK(ContactMenu_DispatchThreat(t.threat, t));   CHECK_EQ(sink.abductPick, t.threat);
    CHECK(ContactMenu_DispatchThreat(t.pamphlet, t)); CHECK_EQ(sink.freePick, t.pamphlet);
    CHECK(ContactMenu_DispatchThreat(t.rhetoric, t)); CHECK_EQ(sink.talent, 4);

    ResetContactMenu(); sink.reset();
    FeastEntries f = ContactMenu_BuildFeast();
    CHECK(ContactMenu_DispatchFeast(f.feast, f));   CHECK_EQ(sink.invite, f.feast);
    CHECK(ContactMenu_DispatchFeast(f.fight, f));   CHECK_EQ(sink.talent, 3);
    CHECK(ContactMenu_DispatchFeast(f.slander, f)); CHECK_EQ(sink.abductDest, f.slander);

    ResetContactMenu(); sink.reset();
    int m = ContactMenu_BuildMistress();
    CHECK(m != 0);
    CHECK(ContactMenu_DispatchMistress(m, m));
    CHECK_EQ(sink.mistress, m);
    ContactActions_SetCommandSink(nullptr);
}

// ===========================================================================
// event_panel
// ===========================================================================
namespace {

struct EvtHost : EventPanelHost {
    int nextWidget = 100;
    int nextForm = 500;
    int destroyedWidgets = 0, destroyedForms = 0, raised = 0;
    int shown = 0, hidden = 0;
    // Per-widget "clicked" value (icon value).
    int iconValueFor = -1; // widget id whose IconValue() should return 1
    int LoadForm(const char*) override { return 9000; }
    int BuildEventForm(int) override { return nextForm++; }
    int AddIcon(int, int) override { return nextWidget++; }
    void DestroyWidget(int) override { ++destroyedWidgets; }
    void DestroyForm(int) override { ++destroyedForms; }
    void SetFormVisible(int, int v) override { if (v) ++shown; else ++hidden; }
    void RaiseForm(int) override { ++raised; }
    int IconValue(int w) override { return w == iconValueFor ? 1 : 0; }
};

} // namespace

TEST(GuiRemaining_Event, InitClearsSlots) {
    ResetEventPanel();
    EvtHost host; EventPanel_SetHost(&host);
    EventPanel_InitBar();
    CHECK_EQ(g_eventBarForm, 9000);
    for (int i = 0; i < kEventPanelSlots; ++i)
        CHECK_EQ(g_eventSlots[i].widgetId, -1);
    EventPanel_SetHost(nullptr);
}

TEST(GuiRemaining_Event, CreateSlotRejectsNonEvent) {
    ResetEventPanel();
    EvtHost host; EventPanel_SetHost(&host);
    int obj = EventPanel_RegisterObject(/*flags*/0u, /*time*/0); // flag 0x8 clear
    CHECK_EQ(EventPanel_CreateSlot(obj, 0, 0, "X", 5), 1);
    EventPanel_SetHost(nullptr);
}

TEST(GuiRemaining_Event, CreateSlotBindsTriple) {
    ResetEventPanel();
    EvtHost host; EventPanel_SetHost(&host);
    int obj = EventPanel_RegisterObject(/*flags*/8u, /*time*/10);
    CHECK_EQ(EventPanel_CreateSlot(obj, 7, 0, "Quest", 5), 0);
    CHECK_EQ(g_eventSlots[0].panelObj, obj);
    CHECK(g_eventSlots[0].widgetId != -1);
    CHECK(g_eventSlots[0].subForm != -1);
    CHECK_EQ(EventPanel_Object(obj)->slotIndex, 0);
    EventPanel_SetHost(nullptr);
}

TEST(GuiRemaining_Event, FillThenEvictOldest) {
    ResetEventPanel();
    EvtHost host; EventPanel_SetHost(&host);
    // Fill all 16 slots with increasing times (slot 0 is oldest).
    int handles[kEventPanelSlots];
    for (int i = 0; i < kEventPanelSlots; ++i) {
        handles[i] = EventPanel_RegisterObject(8u, /*time*/100 + i);
        CHECK_EQ(EventPanel_CreateSlot(handles[i], i, 0, "E", 5), 0);
    }
    // All 16 occupied.
    for (int i = 0; i < kEventPanelSlots; ++i)
        CHECK(g_eventSlots[i].widgetId != -1);
    // One more: the oldest (handle[0], smallest time) is evicted; new one lands at slot 15.
    int extra = EventPanel_RegisterObject(8u, /*time*/999);
    CHECK_EQ(EventPanel_CreateSlot(extra, 99, 0, "E2", 5), 0);
    CHECK_EQ(EventPanel_Object(handles[0])->slotIndex, -1); // evicted
    EventPanel_SetHost(nullptr);
}

TEST(GuiRemaining_Event, DestroyCompactsAndRecomputesHighWater) {
    ResetEventPanel();
    EvtHost host; EventPanel_SetHost(&host);
    int a = EventPanel_RegisterObject(8u, 1);
    int b = EventPanel_RegisterObject(8u, 2);
    int c = EventPanel_RegisterObject(8u, 3);
    EventPanel_CreateSlot(a, 0, 0, "A", 5);
    EventPanel_CreateSlot(b, 0, 0, "B", 5);
    EventPanel_CreateSlot(c, 0, 0, "C", 5);
    CHECK_EQ(g_eventHighWater, 2);
    // Destroy the middle one -> slot 2 (c) slides down to fill slot 1.
    EventPanel_DestroySlot(b, 0);
    CHECK_EQ(g_eventSlots[0].panelObj, a);
    CHECK_EQ(g_eventSlots[1].panelObj, c);       // c compacted down
    CHECK_EQ(g_eventSlots[2].widgetId, -1);      // slot 2 now free
    CHECK_EQ(g_eventHighWater, 1);
    CHECK_EQ(EventPanel_Object(c)->slotIndex, 1); // c's back-pointer fixed
    CHECK(host.destroyedWidgets >= 1 && host.destroyedForms >= 1);
    EventPanel_SetHost(nullptr);
}

TEST(GuiRemaining_Event, ToggleAndSetVisible) {
    ResetEventPanel();
    EvtHost host; EventPanel_SetHost(&host);
    EventPanel_InitBar();
    int a = EventPanel_RegisterObject(8u, 1);
    EventPanel_CreateSlot(a, 0, 0, "A", 5);
    // Show the bar.
    int shown = EventPanel_ToggleVisible(0);
    CHECK_EQ(shown, g_eventBarForm);
    CHECK_EQ(g_eventBarVisible, g_eventBarForm);
    // SetBarVisible forwards when visible.
    CHECK_EQ(EventPanel_SetBarVisible(1), 1);
    // Hide (0xFFFF).
    CHECK_EQ(EventPanel_ToggleVisible(0xFFFF), -1);
    CHECK_EQ(g_eventBarVisible, -1);
    // SetBarVisible is a no-op when hidden.
    CHECK_EQ(EventPanel_SetBarVisible(1), 1);
    EventPanel_SetHost(nullptr);
}

TEST(GuiRemaining_Event, SelectActiveSlotPicksClickedIcon) {
    ResetEventPanel();
    EvtHost host; EventPanel_SetHost(&host);
    EventPanel_InitBar();
    int a = EventPanel_RegisterObject(8u, 1);
    int b = EventPanel_RegisterObject(8u, 2);
    EventPanel_CreateSlot(a, 0, 0, "A", 5);
    EventPanel_CreateSlot(b, 0, 0, "B", 5);
    EventPanel_ToggleVisible(0); // make the bar visible
    // Mark slot 1's icon (widget id of b) as clicked.
    host.iconValueFor = g_eventSlots[1].widgetId;
    EventPanel_SelectActiveSlot();
    CHECK_EQ(g_eventSelected, 1);
    CHECK(host.raised >= 1);
    EventPanel_SetHost(nullptr);
}

// ===========================================================================
// dialog_checks
// ===========================================================================
namespace {
struct ChkSink : DialogCheckSink {
    int msg=-1, flags=-1; char kind=0; int count=0;
    void reset() { msg=-1; flags=-1; kind=0; count=0; }
    void ShowMessage(int m, int f, char k) override { msg=m; flags=f; kind=k; ++count; }
};
} // namespace

TEST(GuiRemaining_Dialog, SkillRequirement) {
    ChkSink s; DialogChecks_SetSink(&s);
    CHECK_EQ(Dialog_CheckSkillRequirement(/*skill*/10, /*req*/5, 1), 1); // pass
    CHECK_EQ(s.count, 0);
    CHECK_EQ(Dialog_CheckSkillRequirement(/*skill*/10, /*req*/0, 1), 1); // req==0 passes
    CHECK_EQ(s.count, 0);
    CHECK_EQ(Dialog_CheckSkillRequirement(/*skill*/2, /*req*/5, 7), 0); // fail
    CHECK_EQ(s.msg, kMsgSkillTooLow);
    CHECK_EQ(s.flags, 0);
    CHECK_EQ(s.kind, 7);
    DialogChecks_SetSink(nullptr);
}

TEST(GuiRemaining_Dialog, ActiveCharFlag) {
    ChkSink s; DialogChecks_SetSink(&s);
    CHECK_EQ(Dialog_CheckActiveCharFlag(/*busy*/false, 3, 1), 0);
    CHECK_EQ(s.count, 0);
    CHECK_EQ(Dialog_CheckActiveCharFlag(/*busy*/true, 3, 9), 1);
    CHECK_EQ(s.msg, kMsgCharBusy);
    CHECK_EQ(s.flags, 256);
    DialogChecks_SetSink(nullptr);
}

TEST(GuiRemaining_Dialog, ResourceAmountAndByItem) {
    ChkSink s; DialogChecks_SetSink(&s);
    CHECK_EQ(Dialog_CheckResourceAmount(/*needed*/5, /*owned*/10), 1);
    CHECK_EQ(Dialog_CheckResourceAmount(/*needed*/0, /*owned*/0), 1);
    CHECK_EQ(Dialog_CheckResourceAmount(/*needed*/5, /*owned*/2), 0);
    CHECK_EQ(s.msg, kMsgNotEnoughGeneric);
    // ByItem message selection.
    s.reset(); CHECK_EQ(Dialog_CheckResourceByItem(322, 5, 1, 2), 0); CHECK_EQ(s.msg, kMsgNotEnoughItem322); CHECK_EQ(s.flags, 4);
    s.reset(); CHECK_EQ(Dialog_CheckResourceByItem(277, 5, 1, 2), 0); CHECK_EQ(s.msg, kMsgNotEnoughItem277);
    s.reset(); CHECK_EQ(Dialog_CheckResourceByItem(99, 5, 1, 2), 0);  CHECK_EQ(s.msg, kMsgNotEnoughGeneric);
    s.reset(); CHECK_EQ(Dialog_CheckResourceByItem(322, 5, 9, 2), 1); CHECK_EQ(s.count, 0); // owned>=needed
    DialogChecks_SetSink(nullptr);
}

TEST(GuiRemaining_Dialog, SimpleFormSelection) {
    CHECK(std::strcmp(Dialog_SimpleFormForFlags(0x10), "misc\\Messagebox_BIG") == 0);
    CHECK(std::strcmp(Dialog_SimpleFormForFlags(0x20), "misc\\Messagebox_VERY_BIG") == 0);
    CHECK(std::strcmp(Dialog_SimpleFormForFlags(0x00), "misc\\Messagebox") == 0);
    // 0x10 takes priority over 0x20.
    CHECK(std::strcmp(Dialog_SimpleFormForFlags(0x30), "misc\\Messagebox_BIG") == 0);
}

TEST(GuiRemaining_Book, HandlePageButtonAndSetPageText) {
    CHECK_EQ(Book_HandlePageButton(kBookBtnForward), 2);
    CHECK_EQ(Book_HandlePageButton(kBookBtnBack), -2);
    CHECK_EQ(Book_HandlePageButton(1234), 0); // unrelated id

    unsigned char vis[8] = {0};
    // firstPage 2 of 6 pages -> pages 2,3 visible, others empty.
    CHECK_EQ(Book_SetPageText(/*firstPage*/2, /*pageCount*/6, vis), 1);
    CHECK_EQ(vis[0], 0); CHECK_EQ(vis[1], 0);
    CHECK_EQ(vis[2], 1); CHECK_EQ(vis[3], 1);
    CHECK_EQ(vis[4], 0); CHECK_EQ(vis[5], 0);
    // firstPage > pageCount -> 0.
    CHECK_EQ(Book_SetPageText(/*firstPage*/9, /*pageCount*/6, vis), 0);
}

TEST(GuiRemaining_Window, CreateScrollButtons) {
    ResetGuiState();
    // Build a real window to attach to.
    int win = Window_Create(10, 20, 200, 150, /*flags*/0);
    CHECK(win != -1);
    ScrollButtonIds ids{};
    int ret = Window_CreateScrollButtons(/*x*/-1, /*y*/-1, /*group*/3, win, /*gfx*/40, &ids);
    CHECK_EQ(ret, 896 * 3);
    CHECK(ids.down != -1 && ids.up != -1);
    CHECK(ids.down != ids.up);
    // The group/scroll flags were written to both buttons.
    CHECK_EQ(g_widgets[ids.down].at<i32>(476), 3);
    CHECK_EQ(g_widgets[ids.down].at<unsigned char>(444), 3);
    CHECK_EQ(g_widgets[ids.up].at<i32>(476), 3);
    CHECK_EQ(g_widgets[ids.up].at<unsigned char>(444), 3);
    // Stored into the window record (+940 down, +936 up).
    CHECK_EQ(g_windows[win].at<i32>(940), ids.down);
    CHECK_EQ(g_windows[win].at<i32>(936), ids.up);
}
