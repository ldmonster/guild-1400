// Unit tests for the wave-20 UI-panel state logic (guild::sim / guild::gui).
// Golden vectors derived 1:1 from the recovered logic of:
//   VIBE_Item_UseObjectAction              @0x5671f4  (item-use state effect)
//   VIBE_Hotkey_OpenAssignWindow (table)   @0x4ff070  (validate/assign/clear @0x4fee18/0x4fef54)
//   VIBE_Menu_RunSaveNameInput             @0x56a700  (save-name confirm/cancel)
//   VIBE_TradePanel_RefreshSellColumns     @0x50bf08  (sell column table + guard)
#include "sim/item_use.h"
#include "gui/hotkey_assign.h"
#include "gui/save_name_input.h"
#include "gui/trade_panel.h"
#include "gui/trade_panel_windows.h"

#include "tests/framework/test.h"

#include <vector>

using namespace guild::sim;
using namespace guild::gui;

// ===========================================================================
// Item_UseObjectAction — validation result codes.
// ===========================================================================
TEST(ItemUse, ValidationCodes) {
    SetItemUseHooks(ItemUseHooks{});  // all defaults (find slot -> not found)

    ItemUseActor actor;
    // !args
    {
        ItemUseArgs a; a.present = false;
        CHECK_EQ(Item_UseObjectAction(actor, a), kItemUseBadArgs);
    }
    // !item id
    {
        ItemUseArgs a; a.itemId = 0;
        CHECK_EQ(Item_UseObjectAction(actor, a), kItemUseNoItemId);
        CHECK_EQ(a.suppressed, 0);  // a2[4] cleared on entry
    }
    // no slot
    {
        ItemUseArgs a; a.itemId = 100;
        CHECK_EQ(Item_UseObjectAction(actor, a), kItemUseNoSlot);
    }
}

TEST(ItemUse, PermissionBlocked) {
    ItemUseHooks h{};
    h.findSlotByItemId = [](i16) {
        ItemSlotRecord r; r.present = true; r.itemId = 5; r.permMask = 0x4; return r;
    };
    SetItemUseHooks(h);

    // 0x56724f: `test [slot+10h], permMask; jnz -> -5`. The use is BLOCKED when the
    // actor's mask OVERLAPS the slot mask (AND != 0); it PROCEEDS when AND == 0.
    ItemUseActor actor; actor.permMask = 0x2;  // no overlap with 0x4 (AND==0) -> proceeds
    ItemUseArgs a; a.itemId = 5;
    CHECK_EQ(Item_UseObjectAction(actor, a), 1);  // slot has no effect cb -> result 1

    actor.permMask = 0x6;  // overlaps 0x4 (AND==0x4 != 0) -> blocked
    a = ItemUseArgs{}; a.itemId = 5;
    CHECK_EQ(Item_UseObjectAction(actor, a), kItemUseBlocked);
}

// 373 success gate: fails only when RandomFloatScaled() < 0.66.
static double g_randVal = 0.0;
TEST(ItemUse, Item373SuccessGate) {
    ItemUseHooks h{};
    h.findSlotByItemId = [](i16) {
        ItemSlotRecord r; r.present = true; r.itemId = 373; r.permMask = 0x1; return r;
    };
    h.randomFloatScaled = []() { return g_randVal; };
    SetItemUseHooks(h);

    // actor.permMask must NOT overlap the slot mask (AND==0) for the use to proceed.
    ItemUseActor actor; actor.permMask = 0x0;

    g_randVal = 0.5;  // < 0.66 -> fail (spinResult 0)
    ItemUseArgs a1; a1.itemId = 373;
    Item_UseObjectAction(actor, a1);
    CHECK_EQ(a1.spinResult, 0);

    g_randVal = 0.66;  // >= 0.66 -> success
    ItemUseArgs a2; a2.itemId = 373;
    Item_UseObjectAction(actor, a2);
    CHECK_EQ(a2.spinResult, 1);

    // A non-373 item always succeeds regardless of the roll.
    g_randVal = 0.0;
    h.findSlotByItemId = [](i16) {
        ItemSlotRecord r; r.present = true; r.itemId = 99; r.permMask = 0x1; return r;
    };
    SetItemUseHooks(h);
    ItemUseArgs a3; a3.itemId = 99;
    Item_UseObjectAction(actor, a3);
    CHECK_EQ(a3.spinResult, 1);
}

// Effect callback returning 0 aborts before any command is queued.
static int g_objTrigCalls, g_useCmdCalls, g_violations, g_panelShows;
TEST(ItemUse, EffectCallbackAbort) {
    g_objTrigCalls = g_useCmdCalls = 0;
    ItemUseHooks h{};
    h.findSlotByItemId = [](i16) {
        ItemSlotRecord r; r.present = true; r.itemId = 7; r.permMask = 0x1;
        r.effectAndKind = 0x123;  // non-zero effect id
        r.objectTrigger = 50;
        return r;
    };
    h.effectCallback = [](u32, const ItemUseActor&, const ItemUseArgs&) { return 0; };
    h.queueObjectTrigger = [](const ItemUseArgs&, i32) { ++g_objTrigCalls; };
    h.queueUseCommand = [](i32, i16) { ++g_useCmdCalls; };
    SetItemUseHooks(h);

    ItemUseActor actor; actor.permMask = 0x0;  // no overlap with slot 0x1 -> proceeds
    ItemUseArgs a; a.itemId = 7;
    CHECK_EQ(Item_UseObjectAction(actor, a), 0);
    CHECK_EQ(g_objTrigCalls, 0);   // aborted before dispatching
    CHECK_EQ(g_useCmdCalls, 0);
}

// Object-trigger path vs plain-use path; law violation; player panel.
TEST(ItemUse, CommandPathsAndEffects) {
    g_objTrigCalls = g_useCmdCalls = g_violations = g_panelShows = 0;

    // (a) objectTrigger != 0 -> queueObjectTrigger, NOT queueUseCommand.
    ItemUseHooks h{};
    h.findSlotByItemId = [](i16) {
        ItemSlotRecord r; r.present = true; r.itemId = 8; r.permMask = 0x1;
        r.objectTrigger = 77; r.lawKind = -1; r.effectAndKind = 0; return r;
    };
    h.queueObjectTrigger = [](const ItemUseArgs&, i32 t) { ++g_objTrigCalls; CHECK_EQ(t, 77); };
    h.queueUseCommand = [](i32, i16) { ++g_useCmdCalls; };
    h.evaluateViolation = [](u8, i32, i32) { ++g_violations; };
    SetItemUseHooks(h);
    ItemUseActor actor; actor.permMask = 0x0; actor.typeByte = 0;  // AND==0 -> proceeds
    ItemUseArgs a; a.itemId = 8;
    CHECK_EQ(Item_UseObjectAction(actor, a), 1);
    CHECK_EQ(g_objTrigCalls, 1);
    CHECK_EQ(g_useCmdCalls, 0);
    CHECK_EQ(g_violations, 0);  // lawKind == -1

    // (b) objectTrigger == 0 -> plain use command; lawKind set -> violation; player panel.
    g_objTrigCalls = g_useCmdCalls = g_violations = g_panelShows = 0;
    h.findSlotByItemId = [](i16) {
        ItemSlotRecord r; r.present = true; r.itemId = 9; r.permMask = 0x1;
        r.objectTrigger = 0;
        // 0x56746f/0x567478: kind = HIBYTE(*(DWORD*)(slot+5)) == LOBYTE(lawKind at +0x08).
        r.lawKind = 5;        // != -1 -> evaluate; low byte (5) is the kind arg.
        r.effectAndKind = 0;  // no effect callback at +0x14.
        return r;
    };
    h.evaluateViolation = [](u8 kind, i32 actorId, i32 cityId) {
        ++g_violations; CHECK_EQ((int)kind, 5); CHECK_EQ(actorId, 42); CHECK_EQ(cityId, -1);
    };
    h.showUseObjectPanel = [](bool, i32, const ItemUseActor&, i16) { ++g_panelShows; };
    SetItemUseHooks(h);
    ItemUseActor pl; pl.permMask = 0x0; pl.id = 42; pl.typeByte = 6; pl.officeRecord = 0;
    ItemUseArgs a2; a2.itemId = 9;
    CHECK_EQ(Item_UseObjectAction(pl, a2), 1);
    CHECK_EQ(g_useCmdCalls, 1);   // plain use path
    CHECK_EQ(g_objTrigCalls, 0);
    CHECK_EQ(g_violations, 1);     // law kind set
    CHECK_EQ(g_panelShows, 1);     // typeByte == 6, not suppressed

    // (c) suppressed -> no panel even for the player.
    g_panelShows = 0;
    ItemUseArgs a3; a3.itemId = 9;
    // Effect callback sets suppression.
    h.effectCallback = [](u32, const ItemUseActor&, const ItemUseArgs&) { return 1; };
    // Pre-set suppression after entry: simulate by a hook that flips args — but entry
    // clears it. Instead drive via the panel-suppress path: typeByte != 6.
    SetItemUseHooks(h);
    ItemUseActor np = pl; np.typeByte = 4;  // not the local player
    CHECK_EQ(Item_UseObjectAction(np, a3), 1);
    CHECK_EQ(g_panelShows, 0);
}

// ===========================================================================
// Hotkey assign WINDOW (0x4ff070) — reuses the guild::play slot table.
//   build rows / enable logic / modal assign+clear+rebuild.
// ===========================================================================
static int g_selRow, g_action, g_winFrames;
TEST(Hotkey, BuildRowsAndEnable) {
    guild::play::Hotkey_ClearSlots();
    SetHotkeyWindowHooks(HotkeyWindowHooks{});  // defaults: building/object exist=true

    // Slot 0 has a building+object; slot 1 building only; rest empty.
    guild::play::g_hotkeySlots[0].buildingId = 1000;
    guild::play::g_hotkeySlots[0].objectId   = 2000;
    guild::play::g_hotkeySlots[1].buildingId = 1001;
    guild::play::g_hotkeySlots[1].objectId   = -1;

    auto rows = Hotkey_BuildRows();
    CHECK_EQ((int)rows.size(), guild::play::kHotkeySlotCount);
    CHECK(rows[0].hasBuilding);
    CHECK(rows[0].hasObject);
    CHECK_EQ(rows[0].buildingLabelColor, kHotkeyColorPresent);   // 67
    CHECK(rows[1].hasBuilding);
    CHECK(!rows[1].hasObject);
    CHECK(!rows[2].hasBuilding);
    CHECK_EQ(rows[2].buildingLabelColor, kHotkeyColorAbsent);    // 66 placeholder

    // Enable logic.
    CHECK(!Hotkey_WindowAssignEnabled(-1));   // no selection
    CHECK(Hotkey_WindowAssignEnabled(2));     // row selected -> assign enabled
    CHECK(Hotkey_WindowClearEnabled(0));      // slot 0 has a building -> clear enabled
    CHECK(!Hotkey_WindowClearEnabled(2));     // empty slot -> clear disabled
    CHECK(!Hotkey_WindowClearEnabled(-1));
}

TEST(Hotkey, ModalClearAndRebuild) {
    guild::play::Hotkey_ClearSlots();
    guild::play::g_hotkeySlots[0].buildingId = 500;
    guild::play::g_hotkeySlots[0].objectId   = 600;

    // Drive one frame: CLEAR slot 0, then the loop ends.
    g_winFrames = 1; g_selRow = 0; g_action = 2 /*clear*/;
    HotkeyWindowHooks h{};
    h.pumpFrame     = []() { return g_winFrames-- > 0; };
    h.selectedRow   = []() { return g_selRow; };
    h.pendingAction = []() { return g_action; };
    SetHotkeyWindowHooks(h);

    std::vector<HotkeyWindowRow> outRows;
    int frames = Hotkey_OpenAssignWindow(&outRows);
    CHECK_EQ(frames, 1);
    // Slot 0 cleared and rebuilt rows reflect the empty slot.
    CHECK_EQ(guild::play::g_hotkeySlots[0].buildingId, -1);
    CHECK_EQ(guild::play::g_hotkeySlots[0].objectId, -1);
    CHECK(!outRows[0].hasBuilding);
}

// Validate is delegated to the play-layer; confirm OpenAssignWindow runs it on entry.
static bool g_bExists2;
TEST(Hotkey, ValidateOnOpen) {
    guild::play::Hotkey_ClearSlots();
    guild::play::g_hotkeySlots[0].buildingId = 10;  // a building that "no longer exists"
    guild::play::g_hotkeySlots[0].objectId   = -1;

    // Wire the play-layer hooks so ValidateAssignments drops the stale slot.
    guild::play::HotkeyHooks ph{};
    ph.buildingFindById = [](i32) -> guild::u8* { return g_bExists2 ? (guild::u8*)1 : nullptr; };
    guild::play::Hotkey_SetHooks(ph);
    g_bExists2 = false;  // building gone

    g_winFrames = 0;  // no frames; just the entry validate
    HotkeyWindowHooks h{};
    h.pumpFrame = []() { return g_winFrames-- > 0; };
    SetHotkeyWindowHooks(h);

    Hotkey_OpenAssignWindow(nullptr);
    CHECK_EQ(guild::play::g_hotkeySlots[0].buildingId, -1);  // validated away
}

// ===========================================================================
// Save-name input — confirm vs cancel.
// ===========================================================================
static int g_frames, g_key;
static std::string g_text;
static bool g_cancel;
TEST(SaveName, ConfirmCopiesText) {
    g_frames = 1; g_key = kKeyEnter; g_text = "my city"; g_cancel = false;
    SaveNameInputHooks h{};
    h.pumpFrame  = []() { return g_frames-- > 0; };
    h.lastKey    = []() { return g_key; };
    h.fieldText  = []() { return g_text; };
    h.cancelRequested = []() { return g_cancel; };
    SetSaveNameInputHooks(h);

    std::string buf = "old";
    CHECK(Menu_RunSaveNameInput(buf));
    CHECK_EQ(buf, std::string("my city"));
}

TEST(SaveName, CancelLeavesBuf) {
    g_frames = 1; g_key = 0 /*not enter*/; g_text = "ignored"; g_cancel = true;
    SaveNameInputHooks h{};
    h.pumpFrame  = []() { return g_frames-- > 0; };
    h.lastKey    = []() { return g_key; };
    h.fieldText  = []() { return g_text; };
    h.cancelRequested = []() { return g_cancel; };
    SetSaveNameInputHooks(h);

    std::string buf = "keepme";
    CHECK(!Menu_RunSaveNameInput(buf));
    CHECK_EQ(buf, std::string("keepme"));
}

// ===========================================================================
// Trade-panel sell column refresh — golden-pin the column table + sell guard.
// (RefreshSellColumns @0x50bf08; column table dword_507D68.)
// ===========================================================================
TEST(TradeSell, ColumnTableBytes) {
    // dword_507D68 row 0..3 byte-verified against get_bytes: [2,0,0,0]x3, [3,0,0,0].
    CHECK_EQ(kColTableSell[0][0], 2);
    CHECK_EQ(kColTableSell[1][0], 2);
    CHECK_EQ(kColTableSell[2][0], 2);
    CHECK_EQ(kColTableSell[3][0], 3);
    // The select copies 4 selectors for the page (window +28 byte).
    i32 cols[kColTableCols];
    TradePanel_SelectColumns(kColTableSell, 3, cols);
    CHECK_EQ(cols[0], 3);
    CHECK_EQ(cols[1], 0);
}

TEST(TradeSell, SellGuardDropsIneligible) {
    for (int k = 0; k < kBuySlotCount; ++k) g_buySlots[k] = ItemSlot{};

    std::vector<RefreshObject> objs;
    RefreshObject a; a.protId = 100; a.stock = 5; a.capacity = 9; a.sellEligible = true;
    RefreshObject b; b.protId = 200; b.stock = 7; b.capacity = 9; b.sellEligible = false;
    objs.push_back(a); objs.push_back(b);

    int live = TradePanel_RefreshColumns(RefreshMode::kSell, objs);
    // Eligible item placed; ineligible dropped.
    bool has100 = false, has200 = false;
    for (int k = 0; k < kBuySlotCount; ++k) {
        if (g_buySlots[k].itemId == 100) { has100 = true; CHECK_EQ(g_buySlots[k].stock, 5); }
        if (g_buySlots[k].itemId == 200) has200 = true;
    }
    CHECK(has100);
    CHECK(!has200);
    CHECK_EQ(live, 1);  // a live slot exists
}
