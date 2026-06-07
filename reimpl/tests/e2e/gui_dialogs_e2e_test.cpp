// End-to-end test for the contact-menu dialog builders.
//
// Simulates opening the contact menu for a synthetic selected building across several
// frames of the modal loop: reset -> register the entry set -> a click on a wired entry
// dispatches the expected (mocked) command. Verifies the full entry tree (count, ids,
// gfx, labels) and that re-registering on the next frame de-dups to the same handles
// (the stable-id property the click wiring depends on).
#include "gui/contact_menu.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

namespace {
struct E2ESink : ContactCommandSink {
    int storageObj = 0;
    int prodWindowObj = 0; char prodWindowWhich = 0;
    int transportObj = 0; int transportMode = 0;
    void OpenStorage(int o) override { storageObj = o; }
    void OpenProductionWindow(int o, char w) override { prodWindowObj = o; prodWindowWhich = w; }
    void OpenTransport(int o, int m) override { transportObj = o; transportMode = m; }
};
} // namespace

// One full contact-menu session over a synthetic wood-storage building.
TEST(GuiDlgE2E, WoodStorageContactSession) {
    ResetContactMenu();
    E2ESink sink; ContactMenu_SetCommandSink(&sink);

    // ---- Frame 1: the builder opens. ResetEntries (the loop entry) then build. ----
    StatusText_ResetEntries();
    WoodIds f1 = ContactMenu_BuildWood();

    // Full entry tree: exactly 6 entries, all with nonzero distinct handles.
    CHECK(f1.storage && f1.production && f1.feast && f1.transport && f1.targetA && f1.targetB);
    int handles[6] = { f1.storage, f1.production, f1.feast, f1.transport, f1.targetA, f1.targetB };
    int registered = 0;
    for (int i = 0; i < kMaxStatusEntries; ++i)
        if (g_statusEntries[i].handle) ++registered;
    CHECK_EQ(registered, 6);
    // No duplicate handles.
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j)
            CHECK(handles[i] != handles[j]);

    // Verify a couple of widget records by content: production carries the wood label
    // name + gfx 19; storage carries contact_LAGER + gfx 14.
    bool prodOk = false, storeOk = false;
    for (int i = 0; i < kMaxStatusEntries; ++i) {
        StatusEntry& e = g_statusEntries[i];
        if (e.handle == f1.production && std::strcmp(e.name, "contact_PRODUKTION_HOLZ") == 0
            && e.gfxId == 19) prodOk = true;
        if (e.handle == f1.storage && std::strcmp(e.name, "contact_LAGER") == 0
            && e.gfxId == 14) storeOk = true;
    }
    CHECK(prodOk);
    CHECK(storeOk);

    // ---- Frame 2: nothing clicked; the loop re-registers (de-dup) -> same ids. ----
    WoodIds f2 = ContactMenu_BuildWood();
    CHECK_EQ(f2.storage, f1.storage);
    CHECK_EQ(f2.production, f1.production);
    CHECK_EQ(f2.transport, f1.transport);
    registered = 0;
    for (int i = 0; i < kMaxStatusEntries; ++i)
        if (g_statusEntries[i].handle) ++registered;
    CHECK_EQ(registered, 6); // still 6, no growth

    // ---- Frame 3: the player clicks the "production" entry. -----------------------
    int clicked = f2.production;     // dword_631720 == production handle
    CHECK(ContactMenu_DispatchWood(clicked, f2));
    CHECK_EQ(sink.prodWindowObj, clicked);
    CHECK_EQ(sink.prodWindowWhich, 'C');   // wood production -> production window 'C'
    CHECK_EQ(sink.storageObj, 0);          // nothing else fired
    CHECK_EQ(sink.transportObj, 0);

    // ---- Frame 4: the player clicks "transport". ----------------------------------
    clicked = f2.transport;
    CHECK(ContactMenu_DispatchWood(clicked, f2));
    CHECK_EQ(sink.transportObj, clicked);
    CHECK_EQ(sink.transportMode, 1);

    // ---- Closing the menu: ResetEntries clears the object-active flags. -----------
    int ret = StatusText_ResetEntries();
    CHECK_EQ(ret, 1600 * 4);

    ContactMenu_SetCommandSink(nullptr);
}

// A second building (mine) opened in the same process, with training targets present,
// confirms the gate-driven entry set + the 'A'/'B' production-window wiring end to end.
TEST(GuiDlgE2E, MineContactSessionWithTargets) {
    ResetContactMenu();
    struct G : ContactGate { bool HasObject(int k) override { return k == 52 || k == 53; } } gate;
    ContactMenu_SetGate(&gate);
    E2ESink sink; ContactMenu_SetCommandSink(&sink);

    StatusText_ResetEntries();
    MineIds ids = ContactMenu_BuildMine();
    int registered = 0;
    for (int i = 0; i < kMaxStatusEntries; ++i)
        if (g_statusEntries[i].handle) ++registered;
    CHECK_EQ(registered, 8);               // 6 base + 2 targets

    // Click "ABBAU" (mine) -> production window 'B'.
    CHECK(ContactMenu_DispatchMine(ids.mine, ids));
    CHECK_EQ(sink.prodWindowWhich, 'B');
    CHECK_EQ(sink.prodWindowObj, ids.mine);

    // Click "SUCHEN_STEIN" (search) -> production window 'A'.
    CHECK(ContactMenu_DispatchMine(ids.search, ids));
    CHECK_EQ(sink.prodWindowWhich, 'A');

    ContactMenu_SetCommandSink(nullptr);
    ContactMenu_SetGate(nullptr);
}
