// Golden-vector unit tests for the script-purchase + network-menu reconstructions.
//   src/sim/script_recon_purchase.{h,cpp}   (0x43c690 / 6ac / 6d4, 0x50588c)
//   src/play/menu_recon_network_screens.{h,cpp} (0x529074/52dabc/52df18/530bcc/56a4d4/56abcc)
// Self-contained; drives the engine edges through the hook vtables.
#include "tests/framework/test.h"
#include "sim/script_recon_purchase.h"
#include "play/menu_recon_network_screens.h"
#include <cstdint>
#include <cstring>

using namespace guild;

// ===========================================================================
// Script load/run wrappers (0x43c690 / 0x43c6ac / 0x43c6d4)
// ===========================================================================
namespace {
sim::ScriptLoadRunHooks makeLoadRun(void* loaded, int mainRet, int argRet,
                                    const char** seenName, int* seenMode, int* seenArg) {
    sim::ScriptLoadRunHooks h;
    static void* s_loaded; static int s_main, s_arg;
    static const char** s_name; static int* s_seenMode; static int* s_seenArg;
    s_loaded = loaded; s_main = mainRet; s_arg = argRet;
    s_name = seenName; s_seenMode = seenMode; s_seenArg = seenArg;
    h.loadFromScriptDir = [](const char* n) -> void* { if (s_name) *s_name = n; return s_loaded; };
    h.runMain = [](void*) { return s_main; };
    h.runWithArgs = [](void*, int mode, int arg) {
        if (s_seenMode) *s_seenMode = mode; if (s_seenArg) *s_seenArg = arg; return s_arg; };
    return h;
}
} // namespace

TEST(ScriptReconPurchase, LoadAndRunMain_FailReturnsZero) {
    const char* name = nullptr;
    auto h = makeLoadRun(/*loaded*/nullptr, 99, 0, &name, nullptr, nullptr);
    auto* prev = sim::SetScriptLoadRunHooks(&h);
    CHECK_EQ(sim::Script_LoadAndRunMain("intro.esc"), 0);   // null load -> 0
    CHECK(name != nullptr && std::strcmp(name, "intro.esc") == 0);
    sim::SetScriptLoadRunHooks(prev);
}

TEST(ScriptReconPurchase, LoadAndRunMain_SuccessReturnsLoadedHandle) {
    // 0x43c6a7: the wrapper returns edx == the LOADED SCRIPT pointer (eax=edx),
    // NOT RunMain's result (1234 here is called-and-discarded).
    static int dummy = 7;
    auto h = makeLoadRun(&dummy, 1234, 0, nullptr, nullptr, nullptr);
    auto* prev = sim::SetScriptLoadRunHooks(&h);
    CHECK_EQ(sim::Script_LoadAndRunMain("x"),
             (int)(intptr_t)&dummy);   // loaded handle, not 1234
    sim::SetScriptLoadRunHooks(prev);
}

TEST(ScriptReconPurchase, LoadAndRunWithArg_PassesMode1AndArg) {
    // 0x43c6d0: returns edx == the loaded handle, NOT RunWithArgs's 555.
    static int dummy = 1;
    int mode = -1, arg = -1;
    auto h = makeLoadRun(&dummy, 0, 555, nullptr, &mode, &arg);
    auto* prev = sim::SetScriptLoadRunHooks(&h);
    CHECK_EQ(sim::Script_LoadAndRunWithArg("y", 42),
             (int)(intptr_t)&dummy);   // loaded handle, not 555
    CHECK_EQ(mode, 1);          // RunWithArgs(script, 1, arg) still invoked
    CHECK_EQ(arg, 42);
    sim::SetScriptLoadRunHooks(prev);
}

TEST(ScriptReconPurchase, LoadAndRunWithArg_FailReturnsZero) {
    auto h = makeLoadRun(nullptr, 0, 555, nullptr, nullptr, nullptr);
    auto* prev = sim::SetScriptLoadRunHooks(&h);
    CHECK_EQ(sim::Script_LoadAndRunWithArg("y", 42), 0);
    sim::SetScriptLoadRunHooks(prev);
}

// ===========================================================================
// 0x50588c — RunPurchaseLocationScript: key-index math + control flow
// ===========================================================================
namespace {
// shared scratch driven by the hooks below
struct PurchaseFix {
    int  resolvedTimes = 0;
    char lastPath[260] = {0};
    int  keyIndexSeen  = -1;
    bool isProduction  = false;
    i8   entityType    = 0;
    i16  locType       = 0;
    void* slotMatch    = nullptr;
    u8   stateByte     = 2;
    bool resolveOk     = true;
    bool loadOk        = true;
};
PurchaseFix* g_fix = nullptr;

sim::PurchaseScriptHooks makePurchaseHooks() {
    sim::PurchaseScriptHooks h;
    h.findActiveByEntity = [](void* e) { return e; };           // entity is its own person rec
    h.personSlot = [](int i) -> void* { return i == 3 ? g_fix->slotMatch : nullptr; };
    h.personStateByte = [](void*) -> u8 { return g_fix->stateByte; };
    h.personBuilding = [](void*) -> void* { return (void*)0x1; };
    h.locationKeyField = [](void*) -> u32 { return 7; };
    h.buildingKeyField = [](void*) -> u32 { return 7; };        // match -> proceed
    h.isProductionType = [](void*) { return g_fix->isProduction ? 1 : 0; };
    h.entityTypeByte = [](void*) { return g_fix->entityType; };
    h.locationTypeWord = [](void*) { return g_fix->locType; };
    h.keyString = [](int idx) -> const char* { g_fix->keyIndexSeen = idx; return "KEY"; };
    h.resolvePath = [](const char* p) -> void* {
        ++g_fix->resolvedTimes; std::snprintf(g_fix->lastPath, sizeof(g_fix->lastPath), "%s", p);
        return g_fix->resolveOk ? (void*)0x100 : nullptr; };
    h.loadScript = [](const char*) -> void* { return g_fix->loadOk ? (void*)0x200 : nullptr; };
    h.findByHandle = [](int) -> void* { return nullptr; };
    h.finish = [](void*) {};
    h.runWithArgs2 = [](void*, void*) { return 4242; };
    h.locationScriptHandle = [](void*) { return -1; };
    h.productionKeyBase = 1000;
    h.purchaseKeyBase   = 2000;
    return h;
}
} // namespace

TEST(ScriptReconPurchase, NoPartnerReturnsNull) {
    PurchaseFix fix; g_fix = &fix; fix.slotMatch = nullptr;     // no slot matches
    auto h = makePurchaseHooks();
    auto* prev = sim::SetPurchaseScriptHooks(&h);
    int entity = 9;
    CHECK(sim::Script_RunPurchaseLocationScript(&entity, nullptr) == nullptr);
    CHECK_EQ(fix.resolvedTimes, 0);
    sim::SetPurchaseScriptHooks(prev);
}

TEST(ScriptReconPurchase, ProductionKeyIndex589Stride) {
    PurchaseFix fix; g_fix = &fix;
    int entity = 0;
    fix.slotMatch = &entity;       // personSlot(3) == active == entity
    fix.isProduction = true; fix.entityType = 3;               // 589*3 + 1000 = 2767, +1
    auto h = makePurchaseHooks();
    auto* prev = sim::SetPurchaseScriptHooks(&h);
    void* r = sim::Script_RunPurchaseLocationScript(&entity, nullptr);
    // 0x5059f9: on the run-success path the original returns the PARTNER BUILDING
    // record (*(v6+0x184)), NOT the loaded script handle.  Here personBuilding
    // returns 0x1.
    CHECK(r == (void*)0x1);                                    // partner building record
    CHECK_EQ(fix.keyIndexSeen, 589 * 3 + 1000 + 1);            // 2768
    CHECK_EQ(sim::PurchaseScriptRunTick(), 4242);
    sim::SetPurchaseScriptHooks(prev);
}

TEST(ScriptReconPurchase, PurchaseKeyIndex65Stride) {
    PurchaseFix fix; g_fix = &fix;
    int entity = 0;
    fix.slotMatch = &entity;
    fix.isProduction = false; fix.locType = 4;                 // 65*4 + 2000 = 2260, +1
    int location = 0;
    auto h = makePurchaseHooks();
    auto* prev = sim::SetPurchaseScriptHooks(&h);
    void* r = sim::Script_RunPurchaseLocationScript(&entity, &location);
    // run-success returns the partner building record (0x1), not script 0x200.
    CHECK(r == (void*)0x1);
    CHECK_EQ(fix.keyIndexSeen, 65 * 4 + 2000 + 1);             // 2261
    sim::SetPurchaseScriptHooks(prev);
}

TEST(ScriptReconPurchase, ResolveFailNoLoad) {
    PurchaseFix fix; g_fix = &fix;
    int entity = 0; fix.slotMatch = &entity; fix.resolveOk = false;
    fix.isProduction = true; fix.entityType = 1;
    auto h = makePurchaseHooks();
    auto* prev = sim::SetPurchaseScriptHooks(&h);
    void* r = sim::Script_RunPurchaseLocationScript(&entity, nullptr);
    CHECK(r == nullptr);                                       // resolve null -> return
    sim::SetPurchaseScriptHooks(prev);
}

TEST(ScriptReconPurchase, LoadFailSetsTickMinusOne) {
    PurchaseFix fix; g_fix = &fix;
    int entity = 0; fix.slotMatch = &entity; fix.loadOk = false;
    fix.isProduction = true; fix.entityType = 1;
    auto h = makePurchaseHooks();
    auto* prev = sim::SetPurchaseScriptHooks(&h);
    sim::Script_RunPurchaseLocationScript(&entity, nullptr);
    CHECK_EQ(sim::PurchaseScriptRunTick(), -1);                // dword_634494 = -1
    sim::SetPurchaseScriptHooks(prev);
}

TEST(ScriptReconPurchase, PathFormatStringIsEinkauf) {
    PurchaseFix fix; g_fix = &fix;
    int entity = 0; fix.slotMatch = &entity;
    fix.isProduction = true; fix.entityType = 0;
    auto h = makePurchaseHooks();
    auto* prev = sim::SetPurchaseScriptHooks(&h);
    sim::Script_RunPurchaseLocationScript(&entity, nullptr);
    CHECK(std::strcmp(fix.lastPath,
        "x:\\engine\\gfx\\scripts\\locations\\KEY\\Einkauf_KEY.esc") == 0);
    sim::SetPurchaseScriptHooks(prev);
}

TEST(ScriptReconPurchase, StateByteTooLowRejected) {
    PurchaseFix fix; g_fix = &fix;
    int entity = 0; fix.slotMatch = &entity; fix.stateByte = 1;  // not > 1
    auto h = makePurchaseHooks();
    auto* prev = sim::SetPurchaseScriptHooks(&h);
    CHECK(sim::Script_RunPurchaseLocationScript(&entity, nullptr) == nullptr);
    CHECK_EQ(fix.resolvedTimes, 0);
    sim::SetPurchaseScriptHooks(prev);
}

TEST(ScriptReconPurchase, KeyMismatchRejected) {
    PurchaseFix fix; g_fix = &fix;
    int entity = 0; fix.slotMatch = &entity;
    auto h = makePurchaseHooks();
    h.buildingKeyField = [](void*) -> u32 { return 99; };       // != locationKey 7
    auto* prev = sim::SetPurchaseScriptHooks(&h);
    int location = 0;
    void* r = sim::Script_RunPurchaseLocationScript(&entity, &location);
    CHECK(r == (void*)0x1);                                     // returns partner building
    CHECK_EQ(fix.resolvedTimes, 0);
    sim::SetPurchaseScriptHooks(prev);
}

// ===========================================================================
// Menu pure-logic golden vectors
// ===========================================================================
TEST(ScriptReconMenu, RadioSelectionIndexConversion) {
    CHECK_EQ(play::RadioSelectionIndex(-1), 0);
    CHECK_EQ(play::RadioSelectionIndex(0), 1);
    CHECK_EQ(play::RadioSelectionIndex(5), 6);
    CHECK_EQ(play::RadioSelectionIndex(41), 42);
}

TEST(ScriptReconMenu, HistoryVariantForRow7) {
    CHECK_EQ(play::HistoryVariantForRow(0), -1);
    CHECK_EQ(play::HistoryVariantForRow(1), 0);
    CHECK_EQ(play::HistoryVariantForRow(2), 1);
    CHECK_EQ(play::HistoryVariantForRow(6), 5);
}

TEST(ScriptReconMenu, NetworkHistoryVariantForRow) {
    CHECK_EQ(play::NetworkHistoryVariantForRow(0), -1);
    CHECK_EQ(play::NetworkHistoryVariantForRow(1), 1);
    CHECK_EQ(play::NetworkHistoryVariantForRow(2), 2);
    CHECK_EQ(play::NetworkHistoryVariantForRow(3), 3);
    CHECK_EQ(play::NetworkHistoryVariantForRow(4), 5);
}

TEST(ScriptReconMenu, RoundEndSaleLineId) {
    CHECK_EQ(play::RoundEndSaleLineId(4), 0x1C1E);
    CHECK_EQ(play::RoundEndSaleLineId(16), 0x1C1E);
    CHECK_EQ(play::RoundEndSaleLineId(19), 0x1C1E);
    CHECK_EQ(play::RoundEndSaleLineId(0), 0x1C1F);
    CHECK_EQ(play::RoundEndSaleLineId(5), 0x1C1F);
}

TEST(ScriptReconMenu, RoundEndProfitLineId) {
    CHECK_EQ(play::RoundEndProfitLineId(10), 0x1C21);
    CHECK_EQ(play::RoundEndProfitLineId(-3), 0x1C22);
    CHECK_EQ(play::RoundEndProfitLineId(0), 0);
}

TEST(ScriptReconMenu, SlotStrideConstants) {
    CHECK_EQ(play::kSaveSlotStride, 544);
    CHECK_EQ(play::kSaveSlotScanEnd, 8704);
    CHECK_EQ(play::kSaveSlotScanEnd, play::kSaveSlotStride * 16);
    CHECK_EQ(play::kSaveSlotMax, 16);
    CHECK_EQ(play::kPlayerRecordStride, 238);
    CHECK_EQ(play::kWindowObjectStride, 952);
    CHECK_EQ(play::kBuildingRecordStride, 589);
}

TEST(ScriptReconMenu, CopyFatStringNormal) {
    // "Hi" as fat-string: 'H',attr 'i',attr 0
    u8 src[] = { 'H', 0x07, 'i', 0x07, 0x00, 0x00 };
    u8 dst[16] = {0};
    int n = play::CopyFatString(dst, src, sizeof(dst));
    CHECK_EQ(dst[0], (u8)'H'); CHECK_EQ(dst[1], (u8)0x07);
    CHECK_EQ(dst[2], (u8)'i'); CHECK_EQ(dst[3], (u8)0x07);
    CHECK_EQ(dst[4], (u8)0x00);
    CHECK_EQ(n, 5);             // H,attr,i,attr,NUL  = 5 bytes
}

TEST(ScriptReconMenu, CopyFatStringEmpty) {
    u8 src[] = { 0x00, 0x00 };
    u8 dst[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    int n = play::CopyFatString(dst, src, sizeof(dst));
    CHECK_EQ(dst[0], (u8)0x00);
    CHECK_EQ(n, 1);            // only the terminating low byte stored
    CHECK_EQ(dst[1], (u8)0xFF);// attr of terminator NOT written
}

TEST(ScriptReconMenu, CopyFatStringAttrZeroTerminates) {
    // low byte nonzero but attr == 0 on second pair: both bytes copied, then stop.
    u8 src[] = { 'A', 0x01, 'B', 0x00, 'C', 0x01 };
    u8 dst[16] = {0};
    int n = play::CopyFatString(dst, src, sizeof(dst));
    CHECK_EQ(dst[0], (u8)'A'); CHECK_EQ(dst[1], (u8)0x01);
    CHECK_EQ(dst[2], (u8)'B'); CHECK_EQ(dst[3], (u8)0x00);
    CHECK_EQ(n, 4);            // A,01,B,00 then while(attr==0) exits
    CHECK_EQ(dst[4], (u8)0x00);// 'C' not copied
}

TEST(ScriptReconMenu, EnterNetworkIpInertReturnsZero) {
    play::ResetMenuReconNetworkScreens();
    CHECK_EQ(play::Menu_EnterNetworkIp(), 0);   // inert hooks -> no commit
}

TEST(ScriptReconMenu, ChooseHistoryVariantClampsAndReturnsZero) {
    play::ResetMenuReconNetworkScreens();
    i32 sel = -5;
    CHECK_EQ(play::Menu_ChooseHistoryVariant(sel), 0);
    CHECK_EQ(sel, -1);          // historySel clamped to -1 when <= -1
}

TEST(ScriptReconMenu, LoadSaveBrowsersInertReturnZero) {
    play::ResetMenuReconNetworkScreens();
    CHECK_EQ(play::Menu_RunLoadNetworkGame(), 0);
    CHECK_EQ(play::Menu_RunSaveNetworkGame(), 0);
}
