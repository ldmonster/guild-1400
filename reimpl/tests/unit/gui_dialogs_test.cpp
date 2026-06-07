// Unit tests for the contact-menu / status-text dialog builders module.
//   - StatusText table: register (alloc + dedup), reset, capacity cap, object-active flag
//   - each builder registers the expected entry set (count, ids, gfx, labels)
//   - each builder's clicked-id -> action wiring resolves to the right command
#include "gui/contact_menu.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

namespace {

// A command sink that records the last action dispatched.
struct RecordingSink : ContactCommandSink {
    enum Kind { kNone, kStorage, kTransport, kProdPanel, kProdWindow, kFeast,
                kTraining, kStaff, kMaster, kThief, kSearch };
    Kind kind = kNone;
    int obj = 0; int arg = 0; char which = 0;
    void reset() { kind = kNone; obj = 0; arg = 0; which = 0; }
    void OpenStorage(int o) override { kind = kStorage; obj = o; }
    void OpenTransport(int o, int m) override { kind = kTransport; obj = o; arg = m; }
    void OpenProductionPanel(int o) override { kind = kProdPanel; obj = o; }
    void OpenProductionWindow(int o, char w) override { kind = kProdWindow; obj = o; which = w; }
    void RunFeast(int o) override { kind = kFeast; obj = o; }
    void RunTraining(int o) override { kind = kTraining; obj = o; }
    void RunStaffBook(int o) override { kind = kStaff; obj = o; }
    void RunMasterCertificate(int o) override { kind = kMaster; obj = o; }
    void RunThiefInfo(int o) override { kind = kThief; obj = o; }
    void RunTradeSearch(int o) override { kind = kSearch; obj = o; }
};

// A gate that opens any object the test asks for.
struct OpenGate : ContactGate {
    bool wantA = false, wantB = false; // kinds 53 / 52
    bool HasObject(int kind) override {
        if (kind == 53) return wantA;
        if (kind == 52) return wantB;
        return false;
    }
};

int CountRegistered() {
    int n = 0;
    for (int i = 0; i < kMaxStatusEntries; ++i)
        if (g_statusEntries[i].handle) ++n;
    return n;
}

} // namespace

// ===========================================================================
// StatusText table primitives.
// ===========================================================================
TEST(GuiDlgStatusText, RegisterAllocatesDistinctSlots) {
    ResetContactMenu();
    int a = StatusText_Register("contact_LAGER", 14, "Storage");
    int b = StatusText_Register("contact_TRANSPORT", 21, "Transport");
    CHECK(a != 0);
    CHECK(b != 0);
    CHECK(a != b);                       // distinct objects -> distinct handles
    CHECK_EQ(CountRegistered(), 2);
    // slot 0 holds LAGER with its gfx + label.
    CHECK_EQ(g_statusEntries[0].handle, a);
    CHECK_EQ(g_statusEntries[0].gfxId, 14);
    CHECK(std::strcmp(g_statusEntries[0].name, "contact_LAGER") == 0);
    CHECK(std::strcmp(g_statusEntries[0].label, "Storage") == 0);
}

TEST(GuiDlgStatusText, RegisterDedupsByName) {
    ResetContactMenu();
    int a = StatusText_Register("contact_LAGER", 14, "Storage");
    int again = StatusText_Register("contact_LAGER", 99, "Other"); // same name
    CHECK_EQ(a, again);                  // returns the existing handle
    CHECK_EQ(CountRegistered(), 1);      // no new slot
    CHECK_EQ(g_statusEntries[0].gfxId, 14); // original gfx kept (dedup doesn't rewrite)
}

TEST(GuiDlgStatusText, ResetClearsTable) {
    ResetContactMenu();
    StatusText_Register("contact_LAGER", 14, "Storage");
    StatusText_Register("contact_TRANSPORT", 21, "Transport");
    int ret = StatusText_ResetEntries();
    CHECK_EQ(ret, 1600 * 4);             // original returns final index * 4
    // The handles remain in the slots (reset clears the object-active flag + the
    // selection array, not the slot handle) — matching the decompile.
    CHECK_EQ(g_statusEntries[0].handle != 0, true);
}

TEST(GuiDlgStatusText, CapacityCapAt32) {
    ResetContactMenu();
    char name[32];
    for (int i = 0; i < kMaxStatusEntries; ++i) {
        std::snprintf(name, sizeof(name), "obj_%02d", i);
        CHECK(StatusText_Register(name, 1, "x") != 0);
    }
    CHECK_EQ(CountRegistered(), kMaxStatusEntries);
    // The 33rd distinct object cannot be registered.
    int overflow = StatusText_Register("obj_overflow", 1, "x");
    CHECK_EQ(overflow, 0);
}

// ===========================================================================
// RemoteTrade builder: entry set + wiring.
// ===========================================================================
TEST(GuiDlgRemoteTrade, EntrySetAndWiring) {
    ResetContactMenu();
    RecordingSink sink; ContactMenu_SetCommandSink(&sink);
    RemoteTradeIds ids = ContactMenu_BuildRemoteTrade();
    CHECK_EQ(CountRegistered(), 2);
    CHECK(ids.remoteBuy != 0 && ids.sell != 0 && ids.remoteBuy != ids.sell);

    sink.reset();
    CHECK(ContactMenu_DispatchRemoteTrade(ids.remoteBuy, ids));
    CHECK_EQ(sink.kind, RecordingSink::kTransport);
    CHECK_EQ(sink.arg, 4);               // remote buy -> transport mode 4

    sink.reset();
    CHECK(ContactMenu_DispatchRemoteTrade(ids.sell, ids));
    CHECK_EQ(sink.kind, RecordingSink::kTransport);
    CHECK_EQ(sink.arg, 2);               // sell -> transport mode 2

    sink.reset();
    CHECK_EQ(ContactMenu_DispatchRemoteTrade(0, ids), false); // nothing clicked
    CHECK_EQ(sink.kind, RecordingSink::kNone);
    ContactMenu_SetCommandSink(nullptr);
}

// ===========================================================================
// InfoBooks builder.
// ===========================================================================
TEST(GuiDlgInfoBooks, EntrySetAndWiring) {
    ResetContactMenu();
    RecordingSink sink; ContactMenu_SetCommandSink(&sink);
    InfoBooksIds ids = ContactMenu_BuildInfoBooks();
    CHECK_EQ(CountRegistered(), 3);

    sink.reset(); CHECK(ContactMenu_DispatchInfoBooks(ids.masterCert, ids));
    CHECK_EQ(sink.kind, RecordingSink::kMaster);
    sink.reset(); CHECK(ContactMenu_DispatchInfoBooks(ids.staffBook, ids));
    CHECK_EQ(sink.kind, RecordingSink::kStaff);
    sink.reset(); CHECK(ContactMenu_DispatchInfoBooks(ids.info, ids));
    CHECK_EQ(sink.kind, RecordingSink::kThief);
    ContactMenu_SetCommandSink(nullptr);
}

// ===========================================================================
// Production family (Carpenter etc.) — shared wiring, name-parameterized layout.
// ===========================================================================
TEST(GuiDlgProduction, CarpenterEntrySet) {
    ResetContactMenu();
    ProductionIds ids = ContactMenu_BuildProduction(kProdCarpenter, /*hasGather=*/false);
    CHECK_EQ(CountRegistered(), 5);      // production + storage + transport + master + staff
    CHECK_EQ(ids.gather, 0);             // no gather entry
    // production entry carries the carpenter label name + gfx 19.
    bool found = false;
    for (int i = 0; i < kMaxStatusEntries; ++i)
        if (g_statusEntries[i].handle == ids.production) {
            CHECK_EQ(g_statusEntries[i].gfxId, 19);
            CHECK(std::strcmp(g_statusEntries[i].name, kProdCarpenter) == 0);
            found = true;
        }
    CHECK(found);
}

TEST(GuiDlgProduction, PerfumeryHasGather) {
    ResetContactMenu();
    ProductionIds ids = ContactMenu_BuildProduction(kProdPerfumery, /*hasGather=*/true);
    CHECK_EQ(CountRegistered(), 6);      // + gather (contact_SAMMELN)
    CHECK(ids.gather != 0);
}

TEST(GuiDlgProduction, Wiring) {
    ResetContactMenu();
    RecordingSink sink; ContactMenu_SetCommandSink(&sink);
    ProductionIds ids = ContactMenu_BuildProduction(kProdSmith, true);

    sink.reset(); CHECK(ContactMenu_DispatchProduction(ids.production, ids));
    CHECK_EQ(sink.kind, RecordingSink::kProdPanel);
    sink.reset(); CHECK(ContactMenu_DispatchProduction(ids.transport, ids));
    CHECK_EQ(sink.kind, RecordingSink::kTransport); CHECK_EQ(sink.arg, 1);
    sink.reset(); CHECK(ContactMenu_DispatchProduction(ids.storage, ids));
    CHECK_EQ(sink.kind, RecordingSink::kStorage);
    sink.reset(); CHECK(ContactMenu_DispatchProduction(ids.masterCert, ids));
    CHECK_EQ(sink.kind, RecordingSink::kMaster);
    sink.reset(); CHECK(ContactMenu_DispatchProduction(ids.staffBook, ids));
    CHECK_EQ(sink.kind, RecordingSink::kStaff);
    sink.reset(); CHECK(ContactMenu_DispatchProduction(ids.gather, ids));
    CHECK_EQ(sink.kind, RecordingSink::kSearch);
    ContactMenu_SetCommandSink(nullptr);
}

// ===========================================================================
// Mine builder — gated training targets + production-window 'A'/'B'/'C' wiring.
// ===========================================================================
TEST(GuiDlgMine, EntrySetWithoutTargets) {
    ResetContactMenu();
    OpenGate gate; gate.wantA = false; gate.wantB = false;
    ContactMenu_SetGate(&gate);
    MineIds ids = ContactMenu_BuildMine();
    CHECK_EQ(CountRegistered(), 6);      // 6 base entries, no targets
    CHECK_EQ(ids.targetA, 0);
    CHECK_EQ(ids.targetB, 0);
    ContactMenu_SetGate(nullptr);
}

TEST(GuiDlgMine, EntrySetWithTargetsAndWiring) {
    ResetContactMenu();
    OpenGate gate; gate.wantA = true; gate.wantB = true;
    ContactMenu_SetGate(&gate);
    RecordingSink sink; ContactMenu_SetCommandSink(&sink);
    MineIds ids = ContactMenu_BuildMine();
    CHECK_EQ(CountRegistered(), 8);      // 6 base + 2 training targets
    CHECK(ids.targetA != 0 && ids.targetB != 0);

    sink.reset(); CHECK(ContactMenu_DispatchMine(ids.mine, ids));
    CHECK_EQ(sink.kind, RecordingSink::kProdWindow); CHECK_EQ(sink.which, 'B');
    sink.reset(); CHECK(ContactMenu_DispatchMine(ids.search, ids));
    CHECK_EQ(sink.kind, RecordingSink::kProdWindow); CHECK_EQ(sink.which, 'A');
    sink.reset(); CHECK(ContactMenu_DispatchMine(ids.production, ids));
    CHECK_EQ(sink.kind, RecordingSink::kProdWindow); CHECK_EQ(sink.which, 'C');
    sink.reset(); CHECK(ContactMenu_DispatchMine(ids.feast, ids));
    CHECK_EQ(sink.kind, RecordingSink::kFeast);
    sink.reset(); CHECK(ContactMenu_DispatchMine(ids.targetA, ids));
    CHECK_EQ(sink.kind, RecordingSink::kTraining);
    sink.reset(); CHECK(ContactMenu_DispatchMine(ids.targetB, ids));
    CHECK_EQ(sink.kind, RecordingSink::kTraining);
    ContactMenu_SetCommandSink(nullptr);
    ContactMenu_SetGate(nullptr);
}

// ===========================================================================
// Wood storage hub builder.
// ===========================================================================
TEST(GuiDlgWood, EntrySetAndWiring) {
    ResetContactMenu();
    RecordingSink sink; ContactMenu_SetCommandSink(&sink);
    WoodIds ids = ContactMenu_BuildWood();
    CHECK_EQ(CountRegistered(), 6);

    sink.reset(); CHECK(ContactMenu_DispatchWood(ids.production, ids));
    CHECK_EQ(sink.kind, RecordingSink::kProdWindow); CHECK_EQ(sink.which, 'C');
    sink.reset(); CHECK(ContactMenu_DispatchWood(ids.transport, ids));
    CHECK_EQ(sink.kind, RecordingSink::kTransport); CHECK_EQ(sink.arg, 1);
    sink.reset(); CHECK(ContactMenu_DispatchWood(ids.targetA, ids));
    CHECK_EQ(sink.kind, RecordingSink::kTraining);
    ContactMenu_SetCommandSink(nullptr);
}
