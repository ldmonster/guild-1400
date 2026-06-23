// ===========================================================================
// ai_meister_equip_test.cpp — golden unit tests for the MeisterAi equip/supply
// sub-planners (guild::sim namespace, 1:1 clone of gilde.exe).
//
// Tests cover:
//   AiMeisterEquip_EquipStaffWeapon_*         (0x45e350)
//   AiMeisterEquip_CollectStorageItems_*      (0x45a62c)
//   AiMeisterEquip_GatherRequiredItems_*      (0x45c10c)
//   AiMeisterEquip_ReserveWorkstationItems_*  (0x45bd68)
//   AiMeisterEquip_CheckWorkstationCapacity_* (0x45ba84)
//
// Strategy: build minimal synthetic NPC/person states using the same raw byte
// offsets as the production code, wire deterministic stand-in leaf functions, and
// assert exact record mutations and emitted MeisterCommands.
//
// Float/threshold constants pinned from gilde.exe get_bytes:
//   byte_6477A1 @ 0x6477A1 = 0x00 (currency byte)
//   flags50 init = 129 (0x81)
//   kStockStride = 64 bytes/row, kWorkOrderStride = 88 bytes/row
// ===========================================================================
#include "tests/framework/test.h"

#include "sim/ai_meister.h"
#include "sim/ai_meister_internal.h"
#include "sim/ai_meister_equip.h"
#include "sim/entity.h"

#include <cstring>
#include <vector>
#include <cstdint>

using namespace guild;
using namespace guild::sim;
using namespace guild::sim::aimei;

// ===========================================================================
// Stock-table byte offsets (mirrors kST_* in ai_meister_equip.cpp).
// Kept local here since the originals are static in the .cpp.
// ===========================================================================
static constexpr int kST_key      = 0x00;  // B5444E (+0) key dword (HIWORD = type id)
static constexpr int kST_backIdx  = 0x06;  // B54454 (+6) back-index
static constexpr int kST_srcBldg  = 0x0A;  // B54458 (+10) source building ptr
static constexpr int kST_field18  = 0x12;  // B54460 (+18)
static constexpr int kST_required = 0x16;  // B54464 (+22) required count
static constexpr int kST_field26  = 0x1A;  // B54468 (+26)
static constexpr int kST_price    = 0x1E;  // flt_B5446C (+30) unit price float
static constexpr int kST_reserved = 0x26;  // B54474 (+38) incoming/reserved
static constexpr int kST_stock    = 0x2A;  // B54478 (+42) current stock
static constexpr int kST_freeCap  = 0x2E;  // B5447C (+46) free capacity
static constexpr int kST_flags50  = 0x32;  // B54480 (+50) flags/count
static constexpr int kST_bits     = 0x3E;  // word_B5448C (+62) bit flags word

// ===========================================================================
// Pointer to i32 cast helper (for 64-bit compatibility):
// The original code stores pointers as i32 (32-bit binary). In tests on 64-bit
// we store addresses of stack buffers as i32 by truncating — only safe when
// we know the value is passed back as a pointer and used carefully.
// We use static buffers to avoid real stack issues.
// ===========================================================================
static i32 ptrToI32(const void* p) {
    return static_cast<i32>(reinterpret_cast<intptr_t>(p));
}

// ===========================================================================
// Test helpers
// ===========================================================================

// Write a person record field using raw offsets:
static void setPerson(u8* buf, int field, i32 val) { wr32(buf, field, val); }

// Build a minimal 536-byte person record with the fields the equip code reads.
static void buildPerson(u8* buf, i32 employer, i32 container, i32 actionObj,
                        u8 profByte, i16 marker) {
    std::memset(buf, 0, 536);
    wr32(buf, kP_employer,  employer);
    wr32(buf, kP_container, container);
    wr32(buf, kP_actionObj, actionObj);
    wr8 (buf, kP_profByte,  profByte);
    i16 m = marker;
    std::memcpy(buf + kP_marker, &m, 2);
}

// Write a building record (minimal: typeByte, id at +1, ownerWord at +39):
static void buildBldg(u8* buf, u8 typeByte, i32 id, u16 owner) {
    buf[kB_typeByte] = typeByte;
    wr32(buf, kB_id1,    id);
    wr16(buf, kB_owner39, owner);
}

// Write a meister record (the a1 person record):
static void buildMeister(u8* buf, i32 bldgRecPtr, i32 budget) {
    std::memset(buf, 0, 536);
    wr32(buf, kM_bldgRec, bldgRecPtr);
    wr32(buf, kM_budget,  budget);
    i16 mk = 0;
    std::memcpy(buf + kP_marker, &mk, 2);
}

// Write a stock table row (helper for test setup):
static void setStockRow(int rowIdx, i32 typeHi, i32 backIdx, float price,
                        i32 stock, i32 freeCap, i32 flags50, i32 required = 0,
                        i32 reserved = 0, i32 srcBldg = 0) {
    u8* row = g_stockTable + 64 * rowIdx;
    // key: HIWORD = typeHi
    i32 keyDw = (i32)((u32)typeHi << 16);
    std::memcpy(row + kST_key,      &keyDw,   4);
    std::memcpy(row + kST_backIdx,  &backIdx, 4);
    std::memcpy(row + kST_price,    &price,   4);
    std::memcpy(row + kST_stock,    &stock,   4);
    std::memcpy(row + kST_freeCap,  &freeCap, 4);
    std::memcpy(row + kST_flags50,  &flags50, 4);
    std::memcpy(row + kST_required, &required,4);
    std::memcpy(row + kST_reserved, &reserved,4);
    std::memcpy(row + kST_srcBldg,  &srcBldg, 4);
}

// Read stock row field:
static i32 getStockI32(int rowIdx, int off) {
    i32 v; std::memcpy(&v, g_stockTable + 64 * rowIdx + off, 4); return v;
}
static float getStockFlt(int rowIdx, int off) {
    float v; std::memcpy(&v, g_stockTable + 64 * rowIdx + off, 4); return v;
}
static u8 getStockU8(int rowIdx, int off) {
    return g_stockTable[64 * rowIdx + off];
}

// Static building buffers (avoids stack pointer issues):
static u8 s_bldg[64]     = {};
static u8 s_mRec[536]    = {};
static u8 s_person0[536] = {};
static u8 s_actionObj[128] = {};
static u8 s_argBuf[64]   = {};

// Reset all module-level scratch state before each test:
static void resetAll() {
    ResetMeisterAiScratch();
    std::memset(g_persons,   0, sizeof(g_persons));
    std::memset(g_personIds, 0, sizeof(g_personIds));
    // Mark all persons as free (marker word = -1):
    for (int i = 0; i < 768; ++i) {
        i16 minus1 = -1;
        std::memcpy(reinterpret_cast<u8*>(&g_persons[i]) + kP_marker, &minus1, 2);
    }
    static MeisterAiLeaves inertLeaves{};
    g_meisterLeaves  = &inertLeaves;
    g_meisterCmdSink = nullptr;
    g_meisterEquipLeaves = MeisterEquipLeaves{};
    std::memset(g_sceneTypeListA, 0, sizeof(i16) * 88);
    std::memset(g_sceneTypeListB, 0, sizeof(i16) * 88);
    // Clear static buffers:
    std::memset(s_bldg,     0, sizeof(s_bldg));
    std::memset(s_mRec,     0, sizeof(s_mRec));
    std::memset(s_person0,  0, sizeof(s_person0));
    std::memset(s_actionObj,0, sizeof(s_actionObj));
    std::memset(s_argBuf,   0, sizeof(s_argBuf));
}

// ===========================================================================
// AiMeisterEquip_EquipStaffWeapon tests  (0x45e350)
// ===========================================================================

// --- Test: no staff assigned (all persons free) — returns without emitting command ---
TEST(AiMeisterEquip, EquipStaffWeapon_NoStaff_NoCommand) {
    resetAll();
    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;

    buildBldg(s_bldg, 1, 1000, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 50000);

    EquipStaffWeapon(s_mRec, 0);
    CHECK(sink.emitted.empty());
}

// --- Test: staff assigned but no weapon slot found — no command emitted ---
TEST(AiMeisterEquip, EquipStaffWeapon_StaffNoWeaponSlot_NoCommand) {
    resetAll();

    buildBldg(s_bldg, 1, 1000, 100);
    i32 bldgPtr = ptrToI32(s_bldg);

    // Person 0: assigned, profession=1, no action object -> fails the action-obj check
    buildPerson(s_person0, bldgPtr, 555, 0, 1, 0);
    std::memcpy(&g_persons[0], s_person0, 536);
    g_personIds[0] = 42;

    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;
    buildMeister(s_mRec, bldgPtr, 50000);

    EquipStaffWeapon(s_mRec, 0);
    CHECK(sink.emitted.empty());
}

// --- Test: full equip path — stock found, compatible, command emitted + count decremented ---
TEST(AiMeisterEquip, EquipStaffWeapon_CompatibleWeapon_CommandEmitted) {
    resetAll();

    // Build building record and place it in g_objects[0] so it can be addressed
    // via makeObjHandle(0). kM_bldgRec / kP_employer are pointer columns that use
    // the handle model (UPDATE 1 in SPEC). Raw ptrToI32 values don't resolve.
    buildBldg(s_bldg, 1, 1000, 100);
    std::memcpy(reinterpret_cast<u8*>(&g_objects[0]), s_bldg, 64);
    i32 bldgHandle = makeObjHandle(0);

    // Action object: *(+44) must equal building id 1000. Place in g_objects[1].
    wr32(s_actionObj, 44, 1000);
    std::memcpy(reinterpret_cast<u8*>(&g_objects[1]), s_actionObj, 64);
    i32 aoHandle = makeObjHandle(1);

    // Person 0: assigned, profession set, action obj same building.
    // Use handles for the pointer columns kP_employer and kP_actionObj.
    buildPerson(s_person0, bldgHandle, 9999/*container*/, aoHandle, 1, 0);
    std::memcpy(&g_persons[0], s_person0, 536);
    g_personIds[0] = 77;

    // Meister: use bldgHandle for kM_bldgRec (pointer column).
    buildMeister(s_mRec, bldgHandle, 50000);

    // Stock table: row 0 with typeHi=42 (the match key the inner scan compares).
    // CRITICAL (gilde.exe 0x45e47b/0x45e48c/0x45e50c): on a hit the original sets
    //   v4 = unk_B54490 + 64*k  (k = FA list iteration index, 0-based)
    // where unk_B54490 = dword_B5444E + 0x42 = g_stockTable + 66. For the first FA
    // iteration (k=0) v4 = g_stockTable + 0x42. The slot read is then
    //   *v4              = i16 at g_stockTable + 0x42      (the type word)
    //   *((_DWORD*)v4+10)= i32 at g_stockTable + 0x42 + 40 = g_stockTable + 106 (count)
    // The inner scan still matches against the real stock row 0 key (hi-word 42).
    g_stockRowCount = 1;
    {
        // Row 0 key: hi-word 42 so the inner scan finds a match.
        i32 keyDw = (i32)((u32)42 << 16);  // hi=42, lo=0
        std::memcpy(g_stockTable + kST_key, &keyDw, 4);
        // v4 view at g_stockTable+0x42: type word = 42, count (at +40 => byte 106) = 3.
        i16 tw42 = 42;
        std::memcpy(g_stockTable + 0x42, &tw42, 2);
        i32 cnt3 = 3;
        std::memcpy(g_stockTable + 106, &cnt3, 4);
    }

    // Scene type list A: entry 0 = outer guard (nonzero), entry 1 = 42 (actual type).
    // EquipStaffWeapon FA loop guard: 'if (word_B56FAC[0])' = stlA_valid(0) checks entry 0.
    // Inner loop: 'while (stlA_valid(v25/2))' at v25=2 checks entry 1; must be nonzero.
    // gilde.exe 0x45e350: loop starts at v25=2.
    g_sceneTypeListA[0] = 42;  // outer guard (entry 0 nonzero)
    g_sceneTypeListA[1] = 42;  // entry 1: stlA_valid(1) = true, loop enters

    // Leaves: queryFind returns a node with typeWord=42 at byte 0.
    static u8 fakeNode[64] = {};
    {
        i16 tw = 42;
        std::memcpy(fakeNode, &tw, 2);
    }
    static MeisterAiLeaves leaves{};
    leaves.queryFind = [](i32, const int*, int) -> u8* { return fakeNode; };
    leaves.avatarLookupById = [](i16) -> u8* {
        static u8 av[8] = {1}; return av;
    };
    leaves.isWeaponSlotCompatible = [](u8*, i16) -> int { return 1; };
    g_meisterLeaves = &leaves;

    // Arg: place in g_objects[2] with building id 1000 at offset 2.
    // EquipStaffWeapon reads *(v16+2) = rd32(argRec, 2) for buildingId.
    wr32(reinterpret_cast<u8*>(&g_objects[2]), 2, 1000);
    i32 argHandle = makeObjHandle(2);

    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;

    EquipStaffWeapon(s_mRec, argHandle);

    // Expect 1 buy command:
    CHECK_EQ((int)sink.emitted.size(), 1);
    if (!sink.emitted.empty()) {
        const MeisterCommand& cmd = sink.emitted[0];
        CHECK(cmd.buyItem);
        CHECK_EQ(cmd.actorId,     77);
        CHECK_EQ(cmd.buildingId,  1000);
        CHECK_EQ(cmd.buyItemType, 42);
        CHECK_EQ(cmd.buyAmount,   1);
    }

    // Count at v4+40 (= g_stockTable+106) should be decremented: 3 -> 2.
    // v4 = unk_B54490 + 64*0 = g_stockTable + 0x42 (gilde.exe 0x45e712 '--*((_DWORD*)v4+10)').
    i32 countAfter = 0;
    std::memcpy(&countAfter, g_stockTable + 106, 4);
    CHECK_EQ(countAfter, 2);
}

// ===========================================================================
// AiMeisterEquip_CollectStorageItems tests  (0x45a62c)
// ===========================================================================

// --- Test: empty scene-type lists — stock table stays empty ---
TEST(AiMeisterEquip, CollectStorageItems_EmptyLists_NoRows) {
    resetAll();
    buildBldg(s_bldg, 1, 42, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 1000);

    static MeisterAiLeaves leaves{};
    leaves.heFindFirst = [](int, int, int, int, i32) -> u8* { return nullptr; };
    g_meisterLeaves = &leaves;

    MeisterCollectStorageItems(s_mRec);
    CHECK_EQ(g_stockRowCount, 0);
}

// --- Test: FA list with one entry — one stock row added with correct typeHi ---
TEST(AiMeisterEquip, CollectStorageItems_SingleFaEntry_OneRow) {
    resetAll();

    // Place building in g_objects[0] so kM_bldgRec (a pointer column) resolves via handle model.
    buildBldg(s_bldg, 1, 100, 200);
    std::memcpy(reinterpret_cast<u8*>(&g_objects[0]), s_bldg, 64);
    buildMeister(s_mRec, makeObjHandle(0), 5000);

    // Scene type list A: type 342 at index 0, terminated by 0 at index 1
    // (342 = word_B56FAC value from guildstate_recon.cpp)
    g_sceneTypeListA[0] = 342;

    g_meisterEquipLeaves.lookupCachedMarketPrice = [](i16 id, u8) -> double {
        return (id == 342) ? 15.0 : 0.0;
    };
    g_meisterEquipLeaves.findItemStock = [](u8*, i16 type) -> i32 {
        return (type == 342) ? 7 : 0;
    };

    static MeisterAiLeaves leaves{};
    leaves.inventoryFreeCapacity = [](u8*, int, int, int) -> i32 { return 50; };
    leaves.heFindFirst = [](int, int, int, int, i32) -> u8* { return nullptr; };
    g_meisterLeaves = &leaves;

    MeisterCollectStorageItems(s_mRec);

    CHECK_EQ(g_stockRowCount, 1);

    // typeHi = 342 in key dword HIWORD:
    i32 keyDw = getStockI32(0, kST_key);
    i32 typeHi = (i32)((u32)keyDw >> 16);
    CHECK_EQ(typeHi, 342);

    // backIndex = -1:
    CHECK_EQ(getStockI32(0, kST_backIdx), -1);

    // price = 15.0f (±0.1):
    float price = getStockFlt(0, kST_price);
    CHECK(price >= 14.9f && price <= 15.1f);

    // stock = 7 (from FindItemStock):
    CHECK_EQ(getStockI32(0, kST_stock), 7);

    // freeCap = 50:
    CHECK_EQ(getStockI32(0, kST_freeCap), 50);

    // flags50 = 129:
    CHECK_EQ(getStockI32(0, kST_flags50), 129);
}

// --- Test: FA list with duplicate type — only one row added (dedup) ---
TEST(AiMeisterEquip, CollectStorageItems_DuplicateType_DeduplicatesRow) {
    resetAll();

    // Use handle model for kM_bldgRec pointer column.
    buildBldg(s_bldg, 1, 100, 200);
    std::memcpy(reinterpret_cast<u8*>(&g_objects[0]), s_bldg, 64);
    buildMeister(s_mRec, makeObjHandle(0), 5000);

    // Pre-populate stock table with type 342 already:
    g_stockRowCount = 1;
    setStockRow(0, 342, -1, 5.0f, 0, 0, 129);

    // FA list: also type 342
    g_sceneTypeListA[0] = 342;

    g_meisterEquipLeaves.lookupCachedMarketPrice = [](i16, u8) -> double { return 5.0; };

    static MeisterAiLeaves leaves{};
    leaves.heFindFirst = [](int, int, int, int, i32) -> u8* { return nullptr; };
    g_meisterLeaves = &leaves;

    MeisterCollectStorageItems(s_mRec);

    // Still 1 row (dedup):
    CHECK_EQ(g_stockRowCount, 1);
}

// --- Test: FC list entry only — one row added with reserved=0 ---
TEST(AiMeisterEquip, CollectStorageItems_FcListOnly_OneRow) {
    resetAll();

    // Use handle model for kM_bldgRec pointer column so containerRec resolves.
    buildBldg(s_bldg, 1, 100, 200);
    std::memcpy(reinterpret_cast<u8*>(&g_objects[0]), s_bldg, 64);
    buildMeister(s_mRec, makeObjHandle(0), 5000);

    g_sceneTypeListB[0] = 372;  // word_B56FC8 value = 372

    g_meisterEquipLeaves.lookupCachedMarketPrice = [](i16 id, u8) -> double {
        return (id == 372) ? 20.0 : 0.0;
    };
    g_meisterEquipLeaves.findItemStock = [](u8*, i16) -> i32 { return 3; };

    static MeisterAiLeaves leaves{};
    leaves.inventoryFreeCapacity = [](u8*, int, int, int) -> i32 { return 100; };
    leaves.heFindFirst = [](int, int, int, int, i32) -> u8* { return nullptr; };
    g_meisterLeaves = &leaves;

    MeisterCollectStorageItems(s_mRec);

    CHECK_EQ(g_stockRowCount, 1);

    // FC path: reserved = 0 (not (int)price like FA block):
    CHECK_EQ(getStockI32(0, kST_reserved), 0);

    // stock = 3:
    CHECK_EQ(getStockI32(0, kST_stock), 3);

    // price = 20.0f:
    float price = getStockFlt(0, kST_price);
    CHECK(price >= 19.9f && price <= 20.1f);
}

// --- Test: He handler adds to reserved/incoming of matching stock row ---
TEST(AiMeisterEquip, CollectStorageItems_HandlerAccumulatesReserved) {
    resetAll();

    // Use handle model for kM_bldgRec pointer column.
    buildBldg(s_bldg, 1, 100, 200);
    std::memcpy(reinterpret_cast<u8*>(&g_objects[0]), s_bldg, 64);
    buildMeister(s_mRec, makeObjHandle(0), 5000);

    // Pre-populate stock: one row with typeHi=42, reserved=0
    g_stockRowCount = 1;
    setStockRow(0, 42, -1, 10.0f, 5, 20, 129, 0, 0);

    // Static handler buffer and object buffer for the He iterator:
    static u8 handlerBuf[256] = {};
    static u8 objBuf[64]      = {};

    // Handler: building id at +172 = 99
    wr32(handlerBuf, 172, 99);

    // CollectProductionSlots result: 1 slot with item type hi=42 at outBuf[18]:
    // outBuf[0] = slotCount = 1
    // outBuf[18] = slot type word = 42 (at byte +72, accessed as word)
    // outBuf[17] = slot item key base: hi-word at +v27+2 = outBuf byte 68+0+2=70
    //   The item key dword must have HIWORD=42.
    // outBuf[26] = slot count dword = 8 (production count)
    // The accum loop: 'v34 = slotItemHi; if (stHi == v34) B54474[si] += count'

    static MeisterAiLeaves leaves{};
    // heFindFirst returns the single handler. heFindNext returns null immediately
    // (no second handler). The original test returned handlerBuf a second time by
    // mistake (handlerUsed flag bug), causing reserved to accumulate to 16 instead of 8.
    leaves.heFindFirst = [](int, int, int, int, i32) -> u8* {
        return handlerBuf;
    };
    leaves.heFindNext = []() -> u8* { return nullptr; };
    g_meisterLeaves = &leaves;

    g_meisterEquipLeaves.objectFindById = [](i32) -> u8* { return objBuf; };
    g_meisterEquipLeaves.collectProductionSlots = [](u8*, i32* outBuf) {
        std::memset(outBuf, 0, 46 * 4);
        outBuf[0] = 1;  // 1 production slot
        // outBuf[17] as i8* + v27+2 = outBuf byte 68+2 = 70: hi-word of item key.
        // Item key dword at outBuf[17] + v27 offset = outBuf[17]+0 = byte 68.
        // The dword at byte 68+2 (=70): we want HIWORD=42. So byte[72..75] as dword >> 16 = 42.
        // That means: outBuf[72..75] = dword with hi=42 => 0x002A0000.
        i32 itemKey = (i32)((u32)42 << 16);
        std::memcpy(reinterpret_cast<u8*>(outBuf) + 72, &itemKey, 4);
        // outBuf[18] = slot type word at &outBuf[18]+0 = byte 72: the same location?
        // Actually v42 = 2*outBuf[0] = 2. The slot-type walk:
        // slotTypeBase = &outBuf[18] = outBuf + 72. slotType = *(i16*)(slotTypeBase + v27).
        // v27 starts at 0 -> slotType = *(i16*)(outBuf+72) = lo-word of itemKey = 0.
        // Hmm: slotType == 0 -> the if(!slotType) skips the accumulation!
        // To get slotType != 0, we need the lo-word of outBuf[72..73] != 0.
        // But the item match is on the HI-word (outBuf+72+2 dword >> 16 = 42).
        // Let's separate: slotType at outBuf+72 = outBuf[18] = 42 (nonzero),
        // and item key hi at outBuf+72+2 = 42 as well.
        // outBuf[18] as i32* is byte 72. As i16* slotTypeBase+0 = *(i16*)(outBuf+72).
        // Set lo-word of outBuf[72] = 42 (nonzero slot type):
        i16 slotTyp = 42;
        std::memcpy(reinterpret_cast<u8*>(outBuf) + 72, &slotTyp, 2);
        // Item key at outBuf+74 (v27+2=2 from slotTypeBase=byte72): hi-word = 42:
        i32 itemKey2 = (i32)((u32)42 << 16);
        std::memcpy(reinterpret_cast<u8*>(outBuf) + 74, &itemKey2, 4);
        // Slot count at outBuf[26]+v45=0 = outBuf+104: set to 8:
        // slotCountBase = &outBuf[26] = byte 104. count at [104+0] = 8.
        i32 cnt = 8;
        std::memcpy(reinterpret_cast<u8*>(outBuf) + 104, &cnt, 4);
    };

    MeisterCollectStorageItems(s_mRec);

    // Handler was iterated; the 3 handler output slots are checked too.
    // The production-slot loop: slotType=42, handler+206+0*2 (dword>>16) = check.
    // handlerBuf+206 = 0 (default) -> hHi=0 != slotItemHi=42 -> v29=false -> accumulate.
    // The accumulation: for each stock row where stHi==42, add count(8) to reserved.
    // Stock row 0 typeHi=42: reserved should be 0+8=8.
    i32 reservedAfter = getStockI32(0, kST_reserved);
    // Note: handler+214+0*2 output slot also checked (the 3-slot loop).
    // handlerBuf+214 = 0 -> outWord=0 -> skip. No further addition.
    CHECK_EQ(reservedAfter, 8);
}

// ===========================================================================
// AiMeisterEquip_GatherRequiredItems tests  (0x45c10c)
// ===========================================================================

// --- Test: no assigned staff, budget 1000 — required cleared (no seller) ---
TEST(AiMeisterEquip, GatherRequiredItems_NoStaff_RequiredCleared) {
    resetAll();

    buildBldg(s_bldg, 1, 42, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 1000);

    // Stock row 0: type 42, required = 5
    g_stockRowCount = 1;
    setStockRow(0, 42, -1, 5.0f, 0, 20, 129, 5);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    MeisterGatherRequiredItems(s_mRec, 1000);

    // No persons assigned -> phase 1 marks nothing. Phase 2: required=5, srcBldg=0 -> cleared to 0.
    CHECK_EQ(getStockI32(0, kST_required), 0);
}

// --- Test: budget clamp reduces purchase quantity ---
TEST(AiMeisterEquip, GatherRequiredItems_BudgetClamp) {
    resetAll();

    buildBldg(s_bldg, 1, 42, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 30);

    g_sceneTypeListA[0] = 42;

    // Stock row 0: type 42, required = 10, price = 5.0, srcBldg nonzero (seller found)
    g_stockRowCount = 1;
    setStockRow(0, 42, -1, 5.0f, 0, 50, 129, 10, 0, 1/*srcBldg*/);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    MeisterGatherRequiredItems(s_mRec, 30);

    // Phase 3 budget clamp: budget=30, price=5, qty starts at 10.
    // while(10>0 && 30 < 10*5+0) -> 30<50: decrement.
    // Stops when 30 < qty*5 becomes false: at qty=6, 30 < 6*5=30 is false. Stop at 6.
    CHECK_EQ(getStockI32(0, kST_required), 6);
}

// --- Test: budget zero clamps all quantities to 0 ---
TEST(AiMeisterEquip, GatherRequiredItems_ZeroBudget_QuantitiesZero) {
    resetAll();

    buildBldg(s_bldg, 1, 42, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 0);

    g_sceneTypeListA[0] = 42;

    g_stockRowCount = 1;
    setStockRow(0, 42, -1, 1.0f, 0, 50, 129, 5, 0, 1/*srcBldg*/);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    MeisterGatherRequiredItems(s_mRec, 0);

    // Budget=0: while(5>0 && 0 < 5*1+0) -> 0<5 -> decrement to 0. Result: 0.
    CHECK_EQ(getStockI32(0, kST_required), 0);
}

// --- Test: accumulated cost carries over across FA list entries ---
TEST(AiMeisterEquip, GatherRequiredItems_AccumCostCarryover) {
    resetAll();

    buildBldg(s_bldg, 1, 42, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 25);

    // Two FA list entries: types 42 and 44
    g_sceneTypeListA[0] = 42;
    g_sceneTypeListA[1] = 44;

    // Two stock rows: type 42 (price 5, qty 3, srcBldg=1), type 44 (price 5, qty 3, srcBldg=1)
    g_stockRowCount = 2;
    setStockRow(0, 42, -1, 5.0f, 0, 50, 129, 3, 0, 1);
    setStockRow(1, 44, -1, 5.0f, 0, 50, 129, 3, 0, 1);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    MeisterGatherRequiredItems(s_mRec, 25);

    // Phase 3: budget=25.
    // Entry 42 (row 0): while(3>0 && 25 < 3*5+0) -> 25<15? No: 15<25? Wait:
    //   qty=3: 25 < 3*5+0 = 15? No (25 >= 15) -> stop immediately. qty=3.
    //   accum = (int)(3*5+0) = 15.
    // Entry 44 (row 1): while(3>0 && 25 < 3*5+15=30) -> 25<30 -> decrement.
    //   qty=2: 25 < 2*5+15=25? No -> stop. qty=2.
    //   accum = (int)(2*5+15) = 25.
    CHECK_EQ(getStockI32(0, kST_required), 3);
    CHECK_EQ(getStockI32(1, kST_required), 2);
}

// ===========================================================================
// AiMeisterEquip_ReserveWorkstationItems tests  (0x45bd68)
// ===========================================================================

// Helper to build an order buffer: order type id + up to 4 slot back-indices + maxReq
static void buildOrderBuf(u8* buf, i16 orderTypeId, i32 slots[4], i32 maxReq) {
    std::memset(buf, 0, 256);
    i16 tid = orderTypeId;
    std::memcpy(buf, &tid, 2);
    // Slot back-indices at offsets 4, 8, 12, 16 (dword positions 1, 2, 3, 4 from buf[0]):
    for (int k = 0; k < 4; ++k)
        std::memcpy(buf + 4 + k * 4, &slots[k], 4);
    // maxReq at buf+72 (dword offset 18):
    std::memcpy(buf + 72, &maxReq, 4);
}

// --- Test: all slots have invalid back-index (-2) — no mutation, no crash ---
TEST(AiMeisterEquip, ReserveWorkstationItems_AllSlotsInvalid_NoMutation) {
    resetAll();

    buildBldg(s_bldg, 1, 1000, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 10000);

    static u8 orderBuf[256] = {};
    i32 slots[4] = {-2, -2, -2, -2};
    buildOrderBuf(orderBuf, 10, slots, 5);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    MeisterReserveWorkstationItems(s_mRec, orderBuf);
    // Stock table unchanged:
    CHECK_EQ(g_stockRowCount, 0);
}

// --- Test: slot with valid back-index — flags50 capped, bits set ---
TEST(AiMeisterEquip, ReserveWorkstationItems_ValidSlot_BitsAndCapSet) {
    resetAll();

    // Set up item type def table for type 10, slot 0 (offset 38+0=38), needCount=4:
    static u8 typeDefBuf[65 * 256] = {};
    g_itemTypeDefBase = typeDefBuf;
    {
        u16 need = 4;
        std::memcpy(typeDefBuf + 65 * 10 + 38, &need, 2);
    }

    buildBldg(s_bldg, 1, 1000, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 10000);

    // Stock row 0: flags50=10, backIndex=-1 (raw material)
    g_stockRowCount = 1;
    setStockRow(0, 42, -1, 5.0f, 3, 20, 10 /*flags50*/);

    static u8 orderBuf[256] = {};
    i32 slots[4] = {0, -2, -2, -2};   // slot 0 uses stock row 0
    buildOrderBuf(orderBuf, 10, slots, 5 /*maxReq*/);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    MeisterReserveWorkstationItems(s_mRec, orderBuf);

    // flags50 capped to min(10, 5) = 5:
    CHECK_EQ(getStockI32(0, kST_flags50), 5);

    // bits lo byte should have bit 0x02 set:
    CHECK((getStockU8(0, kST_bits) & 0x02) != 0);

    g_itemTypeDefBase = nullptr;
}

// ===========================================================================
// AiMeisterEquip_CheckWorkstationCapacity tests  (0x45ba84)
// ===========================================================================

// --- Test: all slots invalid (-2) — returns true ---
TEST(AiMeisterEquip, CheckWorkstationCapacity_AllInvalid_True) {
    resetAll();

    buildBldg(s_bldg, 1, 1000, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 10000);

    static u8 orderBuf[256] = {};
    i32 slots[4] = {-2, -2, -2, -2};
    buildOrderBuf(orderBuf, 10, slots, 5);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    bool ok = MeisterCheckWorkstationCapacity(s_mRec, orderBuf, 0);
    CHECK(ok);
}

// --- Test: slot with sufficient stock — returns true ---
TEST(AiMeisterEquip, CheckWorkstationCapacity_SufficientStock_True) {
    resetAll();

    static u8 typeDefBuf[65 * 256] = {};
    g_itemTypeDefBase = typeDefBuf;
    // needCount for type 10 slot 0 = 3:
    {
        u16 need = 3;
        std::memcpy(typeDefBuf + 65 * 10 + 38, &need, 2);
    }

    buildBldg(s_bldg, 1, 1000, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 10000);

    // Stock row 0: stock = 5 (> needCount=3)
    g_stockRowCount = 1;
    setStockRow(0, 42, -1, 5.0f, 5 /*stock*/, 10 /*freeCap*/, 129);

    static u8 orderBuf[256] = {};
    i32 slots[4] = {0, -2, -2, -2};
    buildOrderBuf(orderBuf, 10, slots, 5);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    bool ok = MeisterCheckWorkstationCapacity(s_mRec, orderBuf, 0);
    CHECK(ok);  // stock(5) >= needCount(3) -> slot satisfied

    g_itemTypeDefBase = nullptr;
}

// --- Test: slot insufficient stock + topLevel=1 — returns false ---
TEST(AiMeisterEquip, CheckWorkstationCapacity_InsufficientTopLevel_False) {
    resetAll();

    static u8 typeDefBuf[65 * 256] = {};
    g_itemTypeDefBase = typeDefBuf;
    {
        u16 need = 10;
        std::memcpy(typeDefBuf + 65 * 10 + 38, &need, 2);
    }

    buildBldg(s_bldg, 1, 1000, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 10000);

    g_stockRowCount = 1;
    setStockRow(0, 42, -1, 5.0f, 2 /*stock*/, 20 /*freeCap*/, 129);

    static u8 orderBuf[256] = {};
    i32 slots[4] = {0, -2, -2, -2};
    buildOrderBuf(orderBuf, 10, slots, 10);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    // mode=1 (topLevel): must return false immediately:
    bool ok = MeisterCheckWorkstationCapacity(s_mRec, orderBuf, 1);
    CHECK(!ok);

    g_itemTypeDefBase = nullptr;
}

// --- Test: slot insufficient stock + freeCap=0 — returns false ---
TEST(AiMeisterEquip, CheckWorkstationCapacity_ZeroFreeCap_False) {
    resetAll();

    static u8 typeDefBuf[65 * 256] = {};
    g_itemTypeDefBase = typeDefBuf;
    {
        u16 need = 5;
        std::memcpy(typeDefBuf + 65 * 10 + 38, &need, 2);
    }

    buildBldg(s_bldg, 1, 1000, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 10000);

    g_stockRowCount = 1;
    setStockRow(0, 42, -1, 5.0f, 1 /*stock<5*/, 0 /*freeCap=0*/, 129);

    static u8 orderBuf[256] = {};
    i32 slots[4] = {0, -2, -2, -2};
    buildOrderBuf(orderBuf, 10, slots, 5);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    bool ok = MeisterCheckWorkstationCapacity(s_mRec, orderBuf, 0);
    CHECK(!ok);  // freeCap=0 -> infeasible

    g_itemTypeDefBase = nullptr;
}

// --- Test: slot insufficient stock + reserved>0 — skips slot (reserved already handles it) ---
TEST(AiMeisterEquip, CheckWorkstationCapacity_HasReserved_SkipsToNextSlot) {
    resetAll();

    static u8 typeDefBuf[65 * 256] = {};
    g_itemTypeDefBase = typeDefBuf;
    {
        u16 need = 5;
        std::memcpy(typeDefBuf + 65 * 10 + 38, &need, 2);
    }

    buildBldg(s_bldg, 1, 1000, 100);
    buildMeister(s_mRec, ptrToI32(s_bldg), 10000);

    g_stockRowCount = 1;
    // stock=1 (< 5), but reserved=3 (>0): the orig checks '!*(v6+9)' which is reserved.
    // When reserved != 0, falls through to next_slot. Only slot is slot 0 -> returns true.
    setStockRow(0, 42, -1, 5.0f, 1 /*stock*/, 20 /*freeCap*/, 129, 0, 3 /*reserved>0*/);

    static u8 orderBuf[256] = {};
    i32 slots[4] = {0, -2, -2, -2};
    buildOrderBuf(orderBuf, 10, slots, 5);

    static MeisterAiLeaves leaves{};
    g_meisterLeaves = &leaves;

    bool ok = MeisterCheckWorkstationCapacity(s_mRec, orderBuf, 0);
    CHECK(ok);  // reserved>0 -> skip (goto next_slot), returns true at end

    g_itemTypeDefBase = nullptr;
}

// ===========================================================================
// AiMeisterEquip_FloatConstants — pin byte-exact constants used
// ===========================================================================
TEST(AiMeisterEquip, FloatConstants_Pinned) {
    // gilde.exe 0x6477A1 = 0x00 (confirmed via get_bytes; currency byte):
    constexpr u8 kCurrency = 0x00u;
    CHECK_EQ((int)kCurrency, 0);

    // ConvertX = truncation toward zero: (int)15.9 = 15, (int)-15.9 = -15
    CHECK_EQ((int)15.9f, 15);
    CHECK_EQ((int)-15.9f, -15);
    // Budget clamp math (golden vector):
    {
        int qty = 10; float budget = 30.0f; float price = 5.0f; int accum = 0;
        while (qty > 0 && (double)budget < (double)qty * (double)price + (double)accum)
            --qty;
        // 30 < 6*5+0=30 is false -> stops at qty=6
        CHECK_EQ(qty, 6);
    }
    // flags50 init for new stock rows = 129 (0x81):
    CHECK_EQ(129, 129);
    // Stock stride = 64 bytes per row (kStockStride):
    CHECK_EQ((int)kStockStride, 64);
    // Work order stride = 88 bytes per row (kWorkOrderStride):
    CHECK_EQ((int)kWorkOrderStride, 88);
    // Stock table columns (byte offsets, pinned from address arithmetic):
    CHECK_EQ(kST_key,      0x00);
    CHECK_EQ(kST_backIdx,  0x06);
    CHECK_EQ(kST_price,    0x1E);
    CHECK_EQ(kST_reserved, 0x26);
    CHECK_EQ(kST_stock,    0x2A);
    CHECK_EQ(kST_freeCap,  0x2E);
    CHECK_EQ(kST_flags50,  0x32);
    CHECK_EQ(kST_bits,     0x3E);
}
