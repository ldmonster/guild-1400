// ===========================================================================
// Golden-vector unit tests for the recon4 hotkey / scancode / drag-grid cluster.
//   src/play/input_recon4_hotkey.{h,cpp}
// Headless: all engine leaves stubbed via hooks; no main() (shared test_main).
// ===========================================================================
#include "play/input_recon4_hotkey.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// Reset all module globals + hooks to a clean state before each test body.
void ResetModule() {
    Hotkey_SetHooks(HotkeyHooks{});
    Input_SetScancodeTable(nullptr);
    Hotkey_ClearSlots();
    g_lastHotkeyChar   = 0;
    g_hotkeySuppress   = 0;
    g_hotkeySelWord    = 0;
    g_hotkeyActiveChar = 0;
    g_hotkeySelection  = HotkeySelection{};
}

// Pack a 4-byte little-endian id at offset `off` into a record buffer.
void put4(u8* p, int off, i32 v) {
    for (int b = 0; b < 4; ++b)
        p[off + b] = static_cast<u8>((v >> (8 * b)) & 0xFF);
}

} // namespace

// ---------------------------------------------------------------------------
// CharToScancode — the 12 hard-coded arrow/keypad cases (table-independent).
// ---------------------------------------------------------------------------
TEST(InputRecon4, CharToScancode_HardCodedCases) {
    ResetModule();
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'R', 0), 48);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'O', 0), 49);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'P', 0), 50);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'Q', 0), 51);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'K', 0), 52);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'L', 0), 53);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'M', 0), 54);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'G', 0), 55);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'H', 0), 56);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'I', 0), 57);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'J', 0), 45);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'N', 0), 43);
    // Hard-coded cases ignore modifiers (the switch returns before the table scan).
    CHECK_EQ((int)Input_CharToScancode(1, 1, 'R', 1), 48);
}

// ---------------------------------------------------------------------------
// CharToScancode — table scan + modifier high-byte composition.
// Build a synthetic table where index 30 holds 'a' (no mods), index 31 holds
// 'a' with shift (high byte 1), index 32 holds 'a' with ctrl+alt (high byte 6).
// ---------------------------------------------------------------------------
TEST(InputRecon4, CharToScancode_TableScan) {
    ResetModule();
    std::vector<u16> table(256, 0xFFFF); // -1 sentinels everywhere else
    table[30] = static_cast<u16>('a');             // key 'a', no mods
    table[31] = static_cast<u16>('a' | (1 << 8));  // 'a' + shift
    table[32] = static_cast<u16>('a' | (6 << 8));  // 'a' + ctrl|alt
    Input_SetScancodeTable([table](int i) { return table[i]; });

    CHECK_EQ((int)Input_CharToScancode(0, 0, 'a', 0), 30); // matches index 30
    CHECK_EQ((int)Input_CharToScancode(1, 0, 'a', 0), 31); // shift -> high byte 1
    CHECK_EQ((int)Input_CharToScancode(0, 1, 'a', 1), 32); // ctrl|alt -> high byte 6

    // shift assigns (=1), then ctrl/alt OR in: shift+ctrl => high byte 3 (no entry).
    CHECK_EQ((int)Input_CharToScancode(1, 1, 'a', 0), 0);
    // A char with no table entry returns 0.
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'z', 0), 0);
}

// CharToScancode with no table installed: only hard-coded cases resolve.
TEST(InputRecon4, CharToScancode_NoTable) {
    ResetModule();
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'a', 0), 0);
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'R', 0), 48);
    // key==0 still scans the all-zero table; index 0 holds 0 -> matches -> 0.
    CHECK_EQ((int)Input_CharToScancode(0, 0, 0, 0), 0);
}

// First-match-wins: lowest index matching the key is returned.
TEST(InputRecon4, CharToScancode_FirstMatchWins) {
    ResetModule();
    std::vector<u16> table(256, 0xFFFF);
    table[10] = static_cast<u16>('x');
    table[200] = static_cast<u16>('x');
    Input_SetScancodeTable([table](int i) { return table[i]; });
    CHECK_EQ((int)Input_CharToScancode(0, 0, 'x', 0), 10);
}

// ---------------------------------------------------------------------------
// ValidateAssignments — drops slots whose building/object went away or whose
// selection flags lost bit 0; keeps valid ones.
// ---------------------------------------------------------------------------
TEST(InputRecon4, ValidateAssignments_DropsAndKeeps) {
    ResetModule();
    static u8 bldRec[8];  // a dummy building record
    static u8 objRec[8];  // a dummy object record

    // Slot 0: building exists, object exists, flags bit0 set -> KEEP.
    g_hotkeySlots[0] = HotkeySlot{0, 100, 200};
    // Slot 1: building missing -> DROP.
    g_hotkeySlots[1] = HotkeySlot{0, 101, 201};
    // Slot 2: building exists, object id set but missing -> DROP.
    g_hotkeySlots[2] = HotkeySlot{0, 102, 202};
    // Slot 3: building exists, flags bit0 clear -> DROP.
    g_hotkeySlots[3] = HotkeySlot{0, 103, -1};
    // Slot 4: empty (buildingId -1) -> untouched.
    g_hotkeySlots[4] = HotkeySlot{0, -1, 999};

    HotkeyHooks h;
    h.buildingFindById = [&](i32 id) -> u8* {
        if (id == 100 || id == 102 || id == 103) return bldRec;
        return nullptr; // 101 missing
    };
    h.objectFindById = [&](i32 id) -> u8* {
        if (id == 200) return objRec;
        return nullptr; // 202 missing (-> drop slot 2)
    };
    h.buildingComputeSelectionFlags = [&](u16, u8* bld, int, u8*) -> int {
        // bit0 set for slot 0's building, clear for slot 3's.
        return (g_hotkeySlots[0].buildingId == 100 && bld == bldRec) ? 1 : 0;
    };
    Hotkey_SetHooks(h);

    // Make the flags depend on the slot precisely: re-stub to set bit0 only for
    // building id 100.
    h.buildingComputeSelectionFlags = [&](u16, u8*, int, u8*) -> int { return 0; };
    Hotkey_SetHooks(h);
    // With flags always 0, slot 0 (valid building+object) is also dropped.
    Hotkey_ValidateAssignments();
    CHECK_EQ(g_hotkeySlots[0].buildingId, -1);
    CHECK_EQ(g_hotkeySlots[1].buildingId, -1);
    CHECK_EQ(g_hotkeySlots[2].buildingId, -1);
    CHECK_EQ(g_hotkeySlots[3].buildingId, -1);
    CHECK_EQ(g_hotkeySlots[4].buildingId, -1); // was already empty
    CHECK_EQ(g_hotkeySlots[4].objectId, 999);  // empty slot untouched
}

TEST(InputRecon4, ValidateAssignments_KeepsValidSlot) {
    ResetModule();
    static u8 bldRec[8];
    static u8 objRec[8];
    g_hotkeySlots[0] = HotkeySlot{0, 100, 200};
    HotkeyHooks h;
    h.buildingFindById = [&](i32 id) -> u8* { return id == 100 ? bldRec : nullptr; };
    h.objectFindById   = [&](i32 id) -> u8* { return id == 200 ? objRec : nullptr; };
    h.buildingComputeSelectionFlags = [&](u16, u8*, int, u8*) -> int { return 1; };
    Hotkey_SetHooks(h);
    Hotkey_ValidateAssignments();
    CHECK_EQ(g_hotkeySlots[0].buildingId, 100);
    CHECK_EQ(g_hotkeySlots[0].objectId, 200);
}

// Object id == -1 is permitted (the "set-but-missing" drop only triggers when
// objectId != -1).
TEST(InputRecon4, ValidateAssignments_NoObjectIsValid) {
    ResetModule();
    static u8 bldRec[8];
    g_hotkeySlots[0] = HotkeySlot{0, 100, -1};
    HotkeyHooks h;
    h.buildingFindById = [&](i32 id) -> u8* { return id == 100 ? bldRec : nullptr; };
    h.objectFindById   = [&](i32) -> u8* { return nullptr; };
    h.buildingComputeSelectionFlags = [&](u16, u8*, int, u8*) -> int { return 1; };
    Hotkey_SetHooks(h);
    Hotkey_ValidateAssignments();
    CHECK_EQ(g_hotkeySlots[0].buildingId, 100);
}

// ---------------------------------------------------------------------------
// ActivateBuilding — finds the slot matching the latched char and opens it.
// ---------------------------------------------------------------------------
TEST(InputRecon4, ActivateBuilding_OpensMatchingSlot) {
    ResetModule();
    static u8 bldRec[8];
    static u8 objRec[8];
    put4(objRec, 2, 0x12345678); // object record's id field at +2

    g_hotkeySlots[3] = HotkeySlot{'F', 300, 400};
    g_lastHotkeyChar   = 'F';
    g_hotkeyActiveChar = 7;

    int openCalls = 0;
    i32 openedField = 0; u8 openedChar = 0;
    HotkeyHooks h;
    h.buildingFindById = [&](i32 id) -> u8* { return id == 300 ? bldRec : nullptr; };
    h.objectFindById   = [&](i32 id) -> u8* { return id == 400 ? objRec : nullptr; };
    h.buildingComputeSelectionFlags = [&](u16, u8*, int, u8*) -> int { return 1; };
    h.dialogOpenBuilding = [&](i32 f, i32, u8 c) { ++openCalls; openedField = f; openedChar = c; };
    Hotkey_SetHooks(h);

    Hotkey_ActivateBuilding();
    CHECK_EQ(openCalls, 1);
    CHECK_EQ(openedField, (i32)0x12345678);
    CHECK_EQ((int)openedChar, 7);
}

TEST(InputRecon4, ActivateBuilding_SuppressedOrNoChar) {
    ResetModule();
    int openCalls = 0;
    HotkeyHooks h;
    h.dialogOpenBuilding = [&](i32, i32, u8) { ++openCalls; };
    Hotkey_SetHooks(h);

    g_lastHotkeyChar = 0; // no char
    Hotkey_ActivateBuilding();
    CHECK_EQ(openCalls, 0);

    g_lastHotkeyChar = 'F';
    g_hotkeySuppress = 1; // suppressed
    Hotkey_ActivateBuilding();
    CHECK_EQ(openCalls, 0);
}

TEST(InputRecon4, ActivateBuilding_NoObjectOpensMinusOne) {
    ResetModule();
    static u8 bldRec[8];
    g_hotkeySlots[0] = HotkeySlot{'A', 300, -1};
    g_lastHotkeyChar = 'A';
    int openCalls = 0; i32 openedField = 0;
    HotkeyHooks h;
    h.buildingFindById = [&](i32 id) -> u8* { return id == 300 ? bldRec : nullptr; };
    h.objectFindById   = [&](i32) -> u8* { return nullptr; };
    h.buildingComputeSelectionFlags = [&](u16, u8*, int, u8*) -> int { return 1; };
    h.dialogOpenBuilding = [&](i32 f, i32, u8) { ++openCalls; openedField = f; };
    Hotkey_SetHooks(h);
    Hotkey_ActivateBuilding();
    CHECK_EQ(openCalls, 1);
    CHECK_EQ(openedField, -1);
}

// ---------------------------------------------------------------------------
// AssignFromSelection — writes the selection ids into the target slot.
// ---------------------------------------------------------------------------
TEST(InputRecon4, AssignFromSelection_BuildingAndObject) {
    ResetModule();
    g_hotkeySelection.hasBuilding = true;
    g_hotkeySelection.buildingId  = 555;
    g_hotkeySelection.hasObject   = true;
    g_hotkeySelection.objectId    = 666;
    int banner = -1;
    HotkeyHooks h;
    h.hudStatusBanner = [&](int slot) { banner = slot; };
    Hotkey_SetHooks(h);

    int r = Hotkey_AssignFromSelection(5, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_hotkeySlots[5].buildingId, 555);
    CHECK_EQ(g_hotkeySlots[5].objectId, 666);
    CHECK_EQ(banner, 5);
}

TEST(InputRecon4, AssignFromSelection_BuildingNoObject) {
    ResetModule();
    g_hotkeySelection.hasBuilding = true;
    g_hotkeySelection.buildingId  = 777;
    g_hotkeySelection.hasObject   = false;
    Hotkey_AssignFromSelection(2, 0);
    CHECK_EQ(g_hotkeySlots[2].buildingId, 777);
    CHECK_EQ(g_hotkeySlots[2].objectId, -1);
}

TEST(InputRecon4, AssignFromSelection_FallbackPath) {
    ResetModule();
    g_hotkeySelection.hasBuilding = false; // no current selection
    g_hotkeySelection.fallbackBuildingId = 888;
    Hotkey_AssignFromSelection(1, 0);
    CHECK_EQ(g_hotkeySlots[1].buildingId, 888);
    CHECK_EQ(g_hotkeySlots[1].objectId, -1);
}

// ---------------------------------------------------------------------------
// StoreDefaultEntry — building type != 29 resolves work-product/storable object.
// ---------------------------------------------------------------------------
TEST(InputRecon4, StoreDefaultEntry_WorkProduct) {
    ResetModule();
    static u8 bld[8];  bld[0] = 5;       // type byte != 29
    put4(bld, 1, 1234);                  // building id @+1
    static u8 obj[8];  put4(obj, 1, 5678); // object id @+1 (StoreDefaultEntry uses +1)

    HotkeyHooks h;
    h.buildingFindWorkProduct = [&](u8* b) -> u8* { return b == bld ? obj : nullptr; };
    h.buildingFindStorable    = [&](u8*) -> u8* { return nullptr; };
    Hotkey_SetHooks(h);

    int r = Hotkey_StoreDefaultEntry(4, bld);
    CHECK_EQ(r, 12 * 4);
    CHECK_EQ(g_hotkeySlots[4].buildingId, 1234);
    CHECK_EQ(g_hotkeySlots[4].objectId, 5678);
}

TEST(InputRecon4, StoreDefaultEntry_StorableFallback) {
    ResetModule();
    static u8 bld[8];  bld[0] = 1;  put4(bld, 1, 11);
    static u8 obj[8];  put4(obj, 1, 22);
    HotkeyHooks h;
    h.buildingFindWorkProduct = [&](u8*) -> u8* { return nullptr; };  // no work product
    h.buildingFindStorable    = [&](u8* b) -> u8* { return b == bld ? obj : nullptr; };
    Hotkey_SetHooks(h);
    Hotkey_StoreDefaultEntry(0, bld);
    CHECK_EQ(g_hotkeySlots[0].buildingId, 11);
    CHECK_EQ(g_hotkeySlots[0].objectId, 22);
}

TEST(InputRecon4, StoreDefaultEntry_TypeBlocked) {
    ResetModule();
    static u8 bld[8];  bld[0] = 29;  put4(bld, 1, 99); // type 29 -> no object lookup
    int wpCalls = 0;
    HotkeyHooks h;
    h.buildingFindWorkProduct = [&](u8*) -> u8* { ++wpCalls; return nullptr; };
    Hotkey_SetHooks(h);
    Hotkey_StoreDefaultEntry(3, bld);
    CHECK_EQ(wpCalls, 0);             // lookups skipped for type 29
    CHECK_EQ(g_hotkeySlots[3].buildingId, 99);
    CHECK_EQ(g_hotkeySlots[3].objectId, -1);
}

// ---------------------------------------------------------------------------
// DragSlot_ResetGridTable — verifies the 32-row grid layout + return value.
// ---------------------------------------------------------------------------
TEST(InputRecon4, DragGridReset_Layout) {
    ResetModule();
    // Dirty the table first to prove it is reset.
    for (int d = 0; d < kDragGridDwords; ++d)
        g_dragGridTable[d] = 0x7777;

    int r = DragSlot_ResetGridTable();
    CHECK_EQ(r, 64 * 31 + 48); // 2032

    for (int i = 0; i < kDragGridRows; ++i) {
        const i32* row = &g_dragGridTable[i * kDragGridRowDwords];
        // Row header { 0, -1, -1, -1 }.
        CHECK_EQ(row[0], 0);
        CHECK_EQ(row[1], -1);
        CHECK_EQ(row[2], -1);
        CHECK_EQ(row[3], -1);
        // Six sub-slots: id dword at row dword index 4,6,8,10,12,14 == -1;
        // qty dword (index 5,7,...,15) == 0.
        for (int k = 0; k < 6; ++k) {
            CHECK_EQ(row[4 + 2 * k], -1); // id
            CHECK_EQ(row[5 + 2 * k], 0);  // qty word (high padding) == 0
        }
    }
}
// (no main: shared test_main.cpp provides it)
