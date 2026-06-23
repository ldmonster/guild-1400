// ===========================================================================
// Unit tests for the three work-order pass DRIVERS (gilde.exe 1:1 reconstruction)
//
// Suite: AiMeisterPasses
//
// Functions under test:
//   MeisterCancelMatchingTasks  (0x45d4c4)
//   MeisterDispatchOrders       (0x45d618)
//   MeisterAssignIdleWorkers    (0x45379c)
//
// Coverage:
//  CancelMatchingTasks:
//   1. filterType 3  → flagMask 0x08; WO with bit8 clear and matching item type
//      triggers ChangePlayerAction on the matching person.
//   2. filterType 4  → flagMask 0x08; same mask as type 3 (decompile identical).
//   3. filterType 8  → flagMask 0x10; WO with bit0x10 clear triggers action.
//   4. filterType 7 (unknown) → flagMask 0 → no scanning, no action emitted.
//   5. Flag bit SET: WO with flagMask bit already set → TaskMatchesOrder false → no action.
//   6. Item type MISMATCH: handler type ≠ WO type → no action even if flag clear.
//   7. No handlers (null heFindFirst) → function returns without crash.
//   8. g_workOrderCount == 0 → no action emitted.
//
//  DispatchOrders (DispatchOrderCount pure core + mode-20 loop driver):
//   9. divisor=2, total=10, passIndex=0, already=0 → count = 10%2 + 10/2 + 0 = 5.
//  10. divisor=2, total=10, passIndex=1, already=0 → count = 10/2 + 0 = 5 (no remainder).
//  11. divisor=3, total=10, passIndex=0, already=2 → count = 10%3 + 10/3 + 2 = 1+3+2=6.
//  12. divisor=1, total=10, passIndex=0, already=0 → count = 10/1+0 = 10 (divisor≤1 path).
//  13. Mode-20: eligible workers dispatched; command pushed to sink with correct cmdType.
//  14. Mode-20: orderTypeWord==0 → no command pushed even with workers.
//  15. Mode-20: args[8] advances by number of dispatched workers.
//  16. args[7] (passIndex) incremented once before return on all code paths.
//
//  AssignIdleWorkers:
//  17. effStock != 0 (kWO_effStock) → IdleWorkerShouldQueue false → queueRequest20 NOT called.
//  18. flagBit 8 set → IdleWorkerShouldQueue false → queueRequest20 NOT called.
//  19. effStock==0 && bit8 clear → queueRequest20 CALLED with (wsNodeBldgId, itemHi).
//  20. g_workOrderCount==0 → queueRequest20 NOT called.
//  21. Multiple WO entries: only eligible ones fire queueRequest20.
//  22. wsNode==nullptr → queueRequest20 called with bldgId==0.
//
//  Constants pinned:
//  23. CancelTaskFlagMask(3)==0x8, CancelTaskFlagMask(4)==0x8, CancelTaskFlagMask(8)==0x10.
//  24. CancelTaskFlagMask(0)==0 (unknown → 0).
//  25. TaskMatchesOrder: matching hi-words + clear bit → true; set bit → false;
//      mismatched hi-words → false.
//  26. IdleWorkerShouldQueue: busy!=0 → false; flags&8 → false; both 0 → true.
//  27. DispatchOrderCount: exact values for all four formula branches.
// ===========================================================================

#include "tests/framework/test.h"

#include "sim/ai_meister.h"
#include "sim/ai_meister_internal.h"
#include "sim/meister_mgmt_recon.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "world/amt_slot_table.h"   // AmtSlot / kAmtSlotCount (mode-40 tests)

#include <cstring>
#include <vector>
#include <cstdint>

using namespace guild;
using namespace guild::sim;
using namespace guild::sim::aimei;

// g_aiIdleWorkstationNode is defined in ai_meister_passes.cpp (no header yet).
// Declare it here for test access.
namespace guild::sim {
    extern u8* g_aiIdleWorkstationNode;
}

// ---------------------------------------------------------------------------
// Helpers to build record stubs
// ---------------------------------------------------------------------------

// Build a 536-byte person record (zero-filled except specified fields).
static std::vector<u8> makePerson(i16 marker = 0, u8 profByte = 1,
                                   i32 busy = 0, i32 bldgHandle = 0) {
    std::vector<u8> p(kPersonStride, 0u);
    wr16(p.data(), kP_marker,   (u16)marker);
    wr8 (p.data(), kP_profByte, profByte);
    wr32(p.data(), kP_busy,     busy);
    wr32(p.data(), kP_employer, bldgHandle);
    return p;
}

// Build a minimal 536-byte meister record with a building record handle.
static std::vector<u8> makeMeister(i32 bldgHandle = 0) {
    std::vector<u8> m(kPersonStride, 0u);
    wr16(m.data(), kP_marker, 0);  // alive
    wr32(m.data(), kM_bldgRec, bldgHandle);
    return m;
}

// Build a minimal building record (at least 94 bytes) with:
//   [0] type byte, [1..4] building id (i32), [39..40] ownerWord, [93..96] sceneRoot.
static std::vector<u8> makeBldg(u8 typeByte = 1, i32 bldgId = 42,
                                  u16 ownerWord = 0, i32 sceneRoot = 0) {
    std::vector<u8> b(256, 0u);
    b[0] = typeByte;
    wr32(b.data(), kB_id1, bldgId);
    wr16(b.data(), kB_owner39, ownerWord);
    wr32(b.data(), kB_sceneRoot93, sceneRoot);
    return b;
}

// ---------------------------------------------------------------------------
// Test fixture helpers — reset all relevant globals.
// ---------------------------------------------------------------------------

// Access the file-local flag array in ai_meister_passes.cpp by injecting
// via the work-order table which is globally accessible.
// The flag array in passes is file-local (static u16 g_woBitsTable[]).
// We can set it only through the g_workOrderTable (kWO_effStock) or by
// directly exposing a setter. Since the test cannot access the static, we
// write into g_workOrderTable and the effStock field.
// The flag column in passes is the file-local g_woBitsTable[i]; there is no
// extern accessor. Tests that need specific flag values call the passes
// function after setting up the expected WO table state and injecting a
// controlled g_meisterLeaves table.
//
// For AssignIdleWorkers: we can set kWO_effStock via g_workOrderTable.
// For CancelMatchingTasks: the flag comparison is on g_woBitsTable[i] which
// is internal to the passes TU. We exercise the cancel path by setting up
// handlers and work-orders such that the flag is ALWAYS clear (zero-init) or
// by controlling the handler key vs WO key to test the mismatch path.

static void resetAll() {
    std::memset(g_persons,         0, sizeof(g_persons));
    std::memset(g_personIds,       0, sizeof(i32) * kPersonCapacity);
    std::memset(g_objects,         0, sizeof(g_objects));
    std::memset(g_workOrderTable,  0, sizeof(g_workOrderTable));
    std::memset(g_stockTable,      0, sizeof(g_stockTable));
    g_workOrderCount   = 0;
    g_stockRowCount    = 0;
    g_meisterCmdSink   = nullptr;
    g_meisterLeaves    = nullptr;
    g_aiIdleWorkstationNode = nullptr;
    std::memset(&g_meisterGameTime, 0, sizeof(g_meisterGameTime));
    g_meisterTimeExtra = 0;
    g_meisterTimeTail  = 0;
}

// Write work-order effective-stock (kWO_effStock = +0x40) for row i.
static void setWoEffStock(int i, i32 v) {
    wr32(g_workOrderTable + kWorkOrderStride * i, 0x40, v);
}

// Write the item type HIWORD into WO row i at offset +2 (matches woItemHi(i)).
// The formula: *(i32*)(WO+2) >> 16 == itemHi. We place the value as the upper
// 16 bits of the dword starting at WO+2.
static void setWoItemHi(int i, int itemHi) {
    // We need *(i32*)(WO + kWorkOrderStride*i + 2) = (u16)itemHi << 16.
    // Write bytes [2..5] of the row as: 0, 0, lo(itemHi), hi(itemHi).
    u8* woBase = g_workOrderTable + kWorkOrderStride * i;
    woBase[2] = 0;
    woBase[3] = 0;
    woBase[4] = (u8)(itemHi & 0xFF);
    woBase[5] = (u8)((itemHi >> 8) & 0xFF);
}

// ---------------------------------------------------------------------------
// Stub leaf tables
// ---------------------------------------------------------------------------

struct StubState {
    // He handler sequence (single handler, then null on next)
    std::vector<u8> handler;   // handler raw bytes (needs >= 174 bytes)
    int findFirstCalls  = 0;
    int findNextCalls   = 0;
    bool handlerReturned = false;

    // changePlayerAction tracking
    struct CpaCall { i32 bldgRecH; int z; u8* hdlr; u16 ownerW; };
    std::vector<CpaCall> cpaLog;

    // queueRequest20 tracking
    struct Q20Call { i32 bldgId; i16 itemHi; };
    std::vector<Q20Call> q20Log;
};

static StubState* gStub = nullptr;

static u8* stub_heFindFirst(int /*a*/, int /*b*/, int /*filterCode*/, int /*mode*/, i32 /*key*/) {
    if (!gStub) return nullptr;
    gStub->findFirstCalls++;
    gStub->handlerReturned = false;
    if (!gStub->handler.empty()) {
        gStub->handlerReturned = true;
        return gStub->handler.data();
    }
    return nullptr;
}

static u8* stub_heFindNext() {
    if (!gStub) return nullptr;
    gStub->findNextCalls++;
    // Return null after the first handler (single-handler stub).
    return nullptr;
}

static void stub_changePlayerAction(i32 bldgRecH, int z, u8* hdlr, u16 ownerW) {
    if (!gStub) return;
    gStub->cpaLog.push_back({bldgRecH, z, hdlr, ownerW});
}

static void stub_queueRequest20(i32 bldgId, i16 itemHi) {
    if (!gStub) return;
    gStub->q20Log.push_back({bldgId, itemHi});
}

// Build a minimal MeisterAiLeaves with He + changePlayerAction + queueRequest20.
static MeisterAiLeaves makeLeaves() {
    MeisterAiLeaves lv{};
    lv.heFindFirst        = stub_heFindFirst;
    lv.heFindNext         = stub_heFindNext;
    lv.changePlayerAction = stub_changePlayerAction;
    lv.queueRequest20     = stub_queueRequest20;
    return lv;
}

// ---------------------------------------------------------------------------
// Helper: encode a handler key dword (HIWORD = itemHi; LOWORD = anything).
// The cancel path reads *(i32*)(handler+170). We write this into our stub handler.
// ---------------------------------------------------------------------------
static std::vector<u8> makeHandler(int itemHi = 100) {
    std::vector<u8> h(256, 0u);
    // *(i32*)(h+170) with HIWORD = itemHi:
    i32 keyDw = (i32)((u32)itemHi << 16);
    std::memcpy(h.data() + 170, &keyDw, 4);
    return h;
}

// ---------------------------------------------------------------------------
// Build g_persons[0] as a person that matches employer=bldgHandle, alive, not busy.
// Also sets kP_busy to `busyVal` (to compare with handler address).
// ---------------------------------------------------------------------------
static void setupPerson0(i32 bldgHandle, i32 busyVal = 0, i16 marker = 0) {
    std::memset(&g_persons[0], 0, kPersonStride);
    u8* p = reinterpret_cast<u8*>(&g_persons[0]);
    wr16(p, kP_marker,   (u16)marker);
    wr8 (p, kP_profByte, 1);
    wr32(p, kP_busy,     busyVal);
    wr32(p, kP_employer, bldgHandle);
    g_personIds[0] = 999;  // person id
}

// ===========================================================================
// TESTS — pure-core constants (suite: AiMeisterPasses)
// ===========================================================================

// Test 23: CancelTaskFlagMask constants
TEST(AiMeisterPasses, CancelFlagMask_Types) {
    CHECK_EQ((int)CancelTaskFlagMask(3), 0x8);
    CHECK_EQ((int)CancelTaskFlagMask(4), 0x8);
    CHECK_EQ((int)CancelTaskFlagMask(8), 0x10);
    CHECK_EQ((int)CancelTaskFlagMask(0), 0);
    CHECK_EQ((int)CancelTaskFlagMask(7), 0);
    CHECK_EQ((int)CancelTaskFlagMask(99), 0);
}

// Test 24: TaskMatchesOrder — exact predicate
TEST(AiMeisterPasses, TaskMatchesOrder_Predicate) {
    // HIWORD of key = typeHi. Build keys with typeHi=77.
    i32 orderKey   = (i32)((u32)77 << 16);  // HIWORD=77
    i32 handlerKey = (i32)((u32)77 << 16);  // HIWORD=77
    // flag clear: match → true
    CHECK(TaskMatchesOrder((int)orderKey, (int)handlerKey, 0x00u, 0x08u));
    // flag bit set: mask&flags!=0 → false
    CHECK(!TaskMatchesOrder((int)orderKey, (int)handlerKey, 0x08u, 0x08u));
    // different HIWORD: false
    i32 wrongHandler = (i32)((u32)88 << 16);
    CHECK(!TaskMatchesOrder((int)orderKey, (int)wrongHandler, 0x00u, 0x08u));
    // zero mask → always false (mask==0 branch in TaskMatchesOrder)
    // (flagMask==0 means "don't scan" — but TaskMatchesOrder itself just checks (flag&mask)==0
    //  which is true when mask==0. So it returns (hi match && (0&0)==0) = hi match && true)
    CHECK(TaskMatchesOrder((int)orderKey, (int)handlerKey, 0xFFu, 0x00u));
}

// Test 25: IdleWorkerShouldQueue
TEST(AiMeisterPasses, IdleWorkerShouldQueue_Predicate) {
    CHECK(IdleWorkerShouldQueue(0, 0x00u));    // clear: true
    CHECK(!IdleWorkerShouldQueue(1, 0x00u));   // busy: false
    CHECK(!IdleWorkerShouldQueue(0, 0x08u));   // flag8 set: false
    CHECK(!IdleWorkerShouldQueue(1, 0x08u));   // both: false
    CHECK(IdleWorkerShouldQueue(0, 0x10u));    // bit8 clear (bit4 set, not bit3): true
    CHECK(!IdleWorkerShouldQueue(0, 0x09u));   // bit0 set AND bit3 set → bit3 check: false
}

// Test 26-27: DispatchOrderCount formula
TEST(AiMeisterPasses, DispatchOrderCount_Formula) {
    // divisor<=1: count = total/divisor + already
    CHECK_EQ(DispatchOrderCount(1, 10, 0, 0), 10);
    // NOTE: divisor==0 is never produced by the real callers; the original (and our
    // 1:1 core) would divide by zero, so we do NOT exercise it here (it is UB by
    // construction, matching gilde.exe). Callers always pass divisor >= 1.

    // passIndex!=0: count = total/divisor + already (no remainder)
    CHECK_EQ(DispatchOrderCount(2, 10, 1, 0), 5);   // 10/2 + 0 = 5
    CHECK_EQ(DispatchOrderCount(3, 10, 1, 2), 5);   // 10/3 + 2 = 3+2 = 5

    // passIndex==0, divisor>1: count = total%divisor + total/divisor + already
    CHECK_EQ(DispatchOrderCount(2, 10, 0, 0), 5);   // 10%2 + 10/2 + 0 = 0+5+0 = 5
    CHECK_EQ(DispatchOrderCount(3, 10, 0, 2), 6);   // 10%3 + 10/3 + 2 = 1+3+2 = 6
    CHECK_EQ(DispatchOrderCount(4,  7, 0, 1), 5);   // 7%4 + 7/4 + 1 = 3+1+1 = 5 (decompile 0x45d66c)
    CHECK_EQ(DispatchOrderCount(3,  9, 0, 0), 3);   // 9%3=0, 9/3=3, 0+3+0=3
}

// ===========================================================================
// TESTS — MeisterCancelMatchingTasks
// ===========================================================================

// Test 1: filterType 3 → flagMask 0x08; WO item matches handler item + bit8 clear
//         → ChangePlayerAction called for alive, matching-employer person
TEST(AiMeisterPasses, Cancel_Type3_Triggers_CPA) {
    resetAll();
    StubState stub;
    gStub = &stub;

    // Handler with item type 100 (HIWORD of *(i32*)(handler+170) = 100).
    stub.handler = makeHandler(100);

    // Person 0: alive (marker=0), employer matches bldgHandle=1, busy = address of handler.
    // kP_busy is compared against handlerAsI32 = (i32)(uintptr_t)handler.data().
    i32 handlerAsI32 = (i32)(std::uintptr_t)stub.handler.data();
    setupPerson0(makeObjHandle(0), handlerAsI32);  // employer = obj handle 0

    // Building record for the meister.
    std::vector<u8> bldg = makeBldg(1, 42, 0, 0);
    // Place bldg in g_objects[0].
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);

    // Meister record: building handle = makeObjHandle(0).
    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    // Work-order with item type 100 (same as handler), flag bit8 clear (zero-init).
    g_workOrderCount = 1;
    setWoItemHi(0, 100);
    // kWO_effStock = 0 (zero-init), flags = 0 (zero-init from g_woBitsTable default).

    MeisterAiLeaves lv = makeLeaves();
    g_meisterLeaves = &lv;

    MeisterCancelMatchingTasks(mr.data(), 3);

    // ChangePlayerAction must have been called for person 0.
    // (busy value matches handlerAsI32, marker != -1)
    CHECK(stub.cpaLog.size() >= 1u);

    gStub = nullptr;
}

// Test 2: filterType 4 same mask as 3 (identical WO scan logic)
TEST(AiMeisterPasses, Cancel_Type4_SameMaskAs3) {
    resetAll();
    // Just verify the mask is the same: CancelTaskFlagMask(4) == CancelTaskFlagMask(3).
    CHECK_EQ((int)CancelTaskFlagMask(4), (int)CancelTaskFlagMask(3));
    CHECK_EQ((int)CancelTaskFlagMask(4), 0x08);
}

// Test 3: filterType 8 → flagMask 0x10; WO with bit0x10 clear fires action
TEST(AiMeisterPasses, Cancel_Type8_Mask0x10) {
    CHECK_EQ((int)CancelTaskFlagMask(8), 0x10);
    // TaskMatchesOrder with mask=0x10, flags=0x10 (bit set) → false
    i32 key = (i32)((u32)55 << 16);
    CHECK(!TaskMatchesOrder((int)key, (int)key, 0x10u, 0x10u));
    // flags=0x08 (bit4 set but not bit0x10) → TaskMatchesOrder with mask=0x10 → true
    CHECK(TaskMatchesOrder((int)key, (int)key, 0x08u, 0x10u));
}

// Test 4: filterType 7 (unknown) → no action
TEST(AiMeisterPasses, Cancel_UnknownType_NoAction) {
    resetAll();
    StubState stub;
    gStub = &stub;
    stub.handler = makeHandler(100);

    std::vector<u8> bldg = makeBldg(1, 42);
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);
    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    g_workOrderCount = 1;
    setWoItemHi(0, 100);

    MeisterAiLeaves lv = makeLeaves();
    g_meisterLeaves = &lv;

    MeisterCancelMatchingTasks(mr.data(), 7);  // unknown filterType → mask=0

    // flagMask=0: TaskMatchesOrder(key, key, 0, 0) evaluates (hi match && (0&0)==0).
    // hi matches → returns true! But v5=1 → ChangePlayerAction IS called.
    // Actually when flagMask==0, the if(!flagMask && g_workOrderCount>0) branch is SKIPPED
    // (the condition in our impl is "if (flagMask != 0 && ...)")
    // So: no CPA calls.
    CHECK(stub.cpaLog.empty());

    gStub = nullptr;
}

// Test 5: Flag bit SET: WO has the flag mask bit set → TaskMatchesOrder returns false → no action
TEST(AiMeisterPasses, Cancel_FlagBitSet_NoAction) {
    // We cannot directly set g_woBitsTable in passes TU (it's static there).
    // However, CancelMatchingTasks reads g_woBitsTable via rdWoBits(i) which is file-local.
    // We test this path via TaskMatchesOrder directly (pure core test).
    // The pure core: TaskMatchesOrder(key, key, 0x08u, 0x08u) == false
    i32 key = (i32)((u32)77 << 16);
    CHECK(!TaskMatchesOrder((int)key, (int)key, 0x08u, 0x08u));  // bit set → false
    CHECK(!TaskMatchesOrder((int)key, (int)key, 0x10u, 0x10u));  // bit0x10 set → false
}

// Test 6: Item type MISMATCH → TaskMatchesOrder returns false → no action
TEST(AiMeisterPasses, Cancel_ItemTypeMismatch_NoAction) {
    i32 orderKey   = (i32)((u32)77 << 16);  // WO type = 77
    i32 handlerKey = (i32)((u32)88 << 16);  // handler type = 88 (different)
    CHECK(!TaskMatchesOrder((int)orderKey, (int)handlerKey, 0x00u, 0x08u));
}

// Test 7: No handlers (null heFindFirst) → no crash, no actions
TEST(AiMeisterPasses, Cancel_NullHeFindFirst_NoCrash) {
    resetAll();
    std::vector<u8> bldg = makeBldg(1, 42);
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);
    std::vector<u8> mr = makeMeister(makeObjHandle(0));
    g_workOrderCount = 1;
    setWoItemHi(0, 100);

    // g_meisterLeaves = nullptr → immediate return
    g_meisterLeaves = nullptr;
    // Must not crash:
    MeisterCancelMatchingTasks(mr.data(), 3);
    CHECK(true);  // reached here without crash
}

// Test 8: g_workOrderCount == 0 → no scan, no action
TEST(AiMeisterPasses, Cancel_ZeroWorkOrders_NoAction) {
    resetAll();
    StubState stub;
    gStub = &stub;
    stub.handler = makeHandler(100);

    std::vector<u8> bldg = makeBldg(1, 42);
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);
    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    g_workOrderCount = 0;  // no work orders

    MeisterAiLeaves lv = makeLeaves();
    g_meisterLeaves = &lv;

    MeisterCancelMatchingTasks(mr.data(), 3);

    // Handler IS returned (findFirst called), but no WO to scan → v5 stays 0 → no CPA
    CHECK(stub.cpaLog.empty());

    gStub = nullptr;
}

// ===========================================================================
// TESTS — MeisterDispatchOrders (mode-20 loop driver + count formula)
// ===========================================================================

// Test 9-12: DispatchOrderCount formula (already covered by pure-core tests above,
// but we also test the full pass via MeisterDispatchOrders mode-20).

// Helper: set up a minimal person for mode-20 dispatch (personQualifies check).
// The person must: alive, employer==bldgHandle, profByte!=0, busy==0,
//                  actionObj ptr != null, actionObj[44] == bldgId.
// We skip the actionObj check by using objects[1] as the action object and
// ensuring its building id (objectRec+44) matches the employer building id.
static void setupWorkerForDispatch(int personIdx, i32 bldgHandle, i32 bldgId) {
    std::memset(&g_persons[personIdx], 0, kPersonStride);
    u8* p = reinterpret_cast<u8*>(&g_persons[personIdx]);
    wr16(p, kP_marker,   0);          // alive
    wr8 (p, kP_profByte, 1);          // profession assigned
    wr32(p, kP_busy,     0);          // not busy
    wr32(p, kP_employer, bldgHandle); // employer handle
    // actionObj ptr: makeObjHandle(1) → g_objects[1]
    wrptr(p, kP_actionObj, makeObjHandle(1));
    g_personIds[personIdx] = 100 + personIdx;

    // g_objects[1]: building id at +44 must match bldgId.
    // ObjectRec: alive byte @+0, id @+1 (dword), pad. We need *(+44) == bldgId.
    std::memset(&g_objects[1], 0, kObjectStride);
    u8* obj1 = reinterpret_cast<u8*>(&g_objects[1]);
    obj1[0] = 1;  // alive
    wr32(obj1, 44, bldgId);  // the action-obj building id match

    // Employer record: bldgHandle -> g_objects[0] (the bldg).
    // *(employer+1) = bldgId.
    u8* emp = reinterpret_cast<u8*>(&g_objects[0]);
    wr32(emp, kB_id1, bldgId);  // *(g_objects[0]+1) = bldgId
}

// Test 13: Mode-20 with one eligible worker → command pushed, cmdType=20
TEST(AiMeisterPasses, Dispatch_Mode20_EligibleWorker_CmdPushed) {
    resetAll();

    // Building record at g_objects[0].
    std::vector<u8> bldg = makeBldg(1, 42, 0, 0);
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);

    // One eligible worker.
    setupWorkerForDispatch(0, makeObjHandle(0), 42);

    // Meister.
    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    // Work order with a non-zero order type word.
    u8  orderRowBuf[kWorkOrderStride] = {};
    i16 orderTypeWord = 55;
    std::memcpy(orderRowBuf, &orderTypeWord, 2);  // bytes [0..1] = 55

    // cmd sink.
    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;

    MeisterAiLeaves lv{};
    g_meisterLeaves = &lv;

    // args array: [0]=mode, [1]=mr, [2]=orderRow, [3..4]=nullptr, [5]=div, [6]=total, [7]=pass, [8]=already
    MeisterDispatchArgs args{};
    args.mode = 20; args.meisterRec = mr.data(); args.orderRow = orderRowBuf;
    args.divisor = 1; args.total = 2; args.passIndex = 0; args.already = 0;

    MeisterDispatchOrders(&args);

    // Command should have been pushed.
    CHECK(sink.emitted.size() >= 1u);
    if (!sink.emitted.empty()) {
        CHECK_EQ((int)sink.emitted[0].cmdType, 20);
    }

    // args[7] incremented.
    CHECK_EQ(args.passIndex, 1);

    g_meisterCmdSink = nullptr;
    g_meisterLeaves = nullptr;
}

// Test 14: Mode-20 with orderTypeWord==0 → no command pushed even if workers eligible
TEST(AiMeisterPasses, Dispatch_Mode20_ZeroOrderType_NoCmd) {
    resetAll();

    std::vector<u8> bldg = makeBldg(1, 42, 0, 0);
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);
    setupWorkerForDispatch(0, makeObjHandle(0), 42);
    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    u8  orderRowBuf[kWorkOrderStride] = {};
    // Leave orderTypeWord = 0 (default zero-init).

    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;

    MeisterAiLeaves lv{};
    g_meisterLeaves = &lv;

    MeisterDispatchArgs args{};
    args.mode = 20; args.meisterRec = mr.data(); args.orderRow = orderRowBuf;
    args.divisor = 1; args.total = 5;
    MeisterDispatchOrders(&args);

    // Workers may be counted but no command pushed because orderTypeWord==0.
    CHECK(sink.emitted.empty());
    // args[7] still incremented.
    CHECK_EQ(args.passIndex, 1);

    g_meisterCmdSink = nullptr;
    g_meisterLeaves = nullptr;
}

// Test 15: args[8] advances by the number of dispatched workers
TEST(AiMeisterPasses, Dispatch_Mode20_AlreadyCountAdvances) {
    resetAll();

    std::vector<u8> bldg = makeBldg(1, 42, 0, 0);
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);

    // Two eligible workers.
    setupWorkerForDispatch(0, makeObjHandle(0), 42);
    setupWorkerForDispatch(1, makeObjHandle(0), 42);
    // But: g_objects[1] is used as actionObj by both workers. They share the same obj.
    // That's fine for this test — we just want two workers to qualify.

    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    u8  orderRowBuf[kWorkOrderStride] = {};
    i16 orderTypeWord = 77;
    std::memcpy(orderRowBuf, &orderTypeWord, 2);

    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;
    MeisterAiLeaves lv{};
    g_meisterLeaves = &lv;

    // total=2, divisor=1, passIndex=0, already=0 → count = 2/1 + 0 = 2
    MeisterDispatchArgs args{};
    args.mode = 20; args.meisterRec = mr.data(); args.orderRow = orderRowBuf;
    args.divisor = 1; args.total = 2;
    MeisterDispatchOrders(&args);

    // args[8] should have advanced by the number of workers dispatched (≤ count).
    CHECK(args.already >= 1);

    g_meisterCmdSink = nullptr;
    g_meisterLeaves = nullptr;
}

// Test 16: args[7] (passIndex) incremented once per MeisterDispatchOrders call
TEST(AiMeisterPasses, Dispatch_PassIndex_IncrementedOnce) {
    resetAll();

    std::vector<u8> bldg = makeBldg(1, 42, 0, 0);
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);
    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    u8  orderRowBuf[kWorkOrderStride] = {};
    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;
    MeisterAiLeaves lv{};
    g_meisterLeaves = &lv;

    MeisterDispatchArgs args{};
    args.mode = 20; args.meisterRec = mr.data(); args.orderRow = orderRowBuf;
    args.divisor = 1; args.total = 0; args.passIndex = 5; args.already = 0;
    // passIndex starts at 5.
    MeisterDispatchOrders(&args);
    CHECK_EQ(args.passIndex, 6);  // incremented once

    g_meisterCmdSink = nullptr;
    g_meisterLeaves = nullptr;
}

// ===========================================================================
// TESTS — MeisterAssignIdleWorkers
// ===========================================================================

// Test 17: effStock != 0 → IdleWorkerShouldQueue false → queueRequest20 NOT called
TEST(AiMeisterPasses, AssignIdle_EffStockNonZero_NoQueue) {
    resetAll();
    StubState stub;
    gStub = &stub;

    g_workOrderCount = 1;
    setWoItemHi(0, 77);
    setWoEffStock(0, 5);  // effStock = 5 → IdleWorkerShouldQueue(5, 0) = false

    MeisterAiLeaves lv = makeLeaves();
    g_meisterLeaves = &lv;

    // Set the workstation node (esi in orig = the type-42 node ptr).
    u8 wsNodeBuf[8] = {};
    i32 bldgId = 99;
    std::memcpy(wsNodeBuf + 2, &bldgId, 4);
    g_aiIdleWorkstationNode = wsNodeBuf;

    MeisterAssignIdleWorkers();

    CHECK(stub.q20Log.empty());

    gStub = nullptr;
    g_meisterLeaves = nullptr;
}

// Test 18: flagBit 8 set → IdleWorkerShouldQueue false → queueRequest20 NOT called
// We CANNOT directly set g_woBitsTable[0] in the passes TU (it's static).
// Instead we verify via IdleWorkerShouldQueue pure core:
TEST(AiMeisterPasses, AssignIdle_FlagBit8Set_NoQueue) {
    // The pure core predicate: flags&8 != 0 → false
    CHECK(!IdleWorkerShouldQueue(0, 0x08u));
    // If IdleWorkerShouldQueue returns false, queueRequest20 is skipped.
    // We trust the driver correctly applies the core predicate (tested via core tests).
}

// Test 19: effStock==0 && bit8 clear (zero-init) → queueRequest20 CALLED
TEST(AiMeisterPasses, AssignIdle_Eligible_QueueCalled) {
    resetAll();
    StubState stub;
    gStub = &stub;

    g_workOrderCount = 1;
    setWoItemHi(0, 55);
    setWoEffStock(0, 0);  // effStock = 0 AND flags = 0 (zero-init) → eligible

    MeisterAiLeaves lv = makeLeaves();
    g_meisterLeaves = &lv;

    // Workstation node: building id at +2 = 88.
    u8 wsNodeBuf[8] = {};
    i32 bldgId = 88;
    std::memcpy(wsNodeBuf + 2, &bldgId, 4);
    g_aiIdleWorkstationNode = wsNodeBuf;

    MeisterAssignIdleWorkers();

    CHECK(stub.q20Log.size() == 1u);
    if (!stub.q20Log.empty()) {
        CHECK_EQ(stub.q20Log[0].bldgId, 88);   // *(wsNode+2) = 88
        CHECK_EQ((int)stub.q20Log[0].itemHi, 55);  // HIWORD of WO key = 55
    }

    gStub = nullptr;
    g_meisterLeaves = nullptr;
}

// Test 20: g_workOrderCount == 0 → queueRequest20 NOT called
TEST(AiMeisterPasses, AssignIdle_ZeroWoCount_NoQueue) {
    resetAll();
    StubState stub;
    gStub = &stub;

    g_workOrderCount = 0;

    MeisterAiLeaves lv = makeLeaves();
    g_meisterLeaves = &lv;

    u8 wsNodeBuf[8] = {};
    g_aiIdleWorkstationNode = wsNodeBuf;

    MeisterAssignIdleWorkers();

    CHECK(stub.q20Log.empty());

    gStub = nullptr;
    g_meisterLeaves = nullptr;
}

// Test 21: Multiple WO entries: only eligible (effStock==0, flag clear) fire
TEST(AiMeisterPasses, AssignIdle_MultipleWOs_OnlyEligibleQueue) {
    resetAll();
    StubState stub;
    gStub = &stub;

    // 3 work orders: [0] effStock=0,flag=0 (eligible); [1] effStock=3 (not); [2] eligible
    g_workOrderCount = 3;
    setWoItemHi(0, 10);   setWoEffStock(0, 0);
    setWoItemHi(1, 20);   setWoEffStock(1, 3);  // not eligible
    setWoItemHi(2, 30);   setWoEffStock(2, 0);

    MeisterAiLeaves lv = makeLeaves();
    g_meisterLeaves = &lv;

    u8 wsNodeBuf[8] = {};
    i32 bldgId = 77;
    std::memcpy(wsNodeBuf + 2, &bldgId, 4);
    g_aiIdleWorkstationNode = wsNodeBuf;

    MeisterAssignIdleWorkers();

    // Only WO[0] and WO[2] fire. WO[1] skipped.
    CHECK_EQ(stub.q20Log.size(), 2u);
    if (stub.q20Log.size() == 2u) {
        // Order must be [0] then [2] (linear scan).
        CHECK_EQ((int)stub.q20Log[0].itemHi, 10);
        CHECK_EQ((int)stub.q20Log[1].itemHi, 30);
    }

    gStub = nullptr;
    g_meisterLeaves = nullptr;
}

// Test 22: wsNode == nullptr → bldgId read as 0 → queueRequest20 called with bldgId=0
TEST(AiMeisterPasses, AssignIdle_NullWsNode_BldgId0) {
    resetAll();
    StubState stub;
    gStub = &stub;

    g_workOrderCount = 1;
    setWoItemHi(0, 44);
    setWoEffStock(0, 0);

    MeisterAiLeaves lv = makeLeaves();
    g_meisterLeaves = &lv;

    g_aiIdleWorkstationNode = nullptr;  // no node

    MeisterAssignIdleWorkers();

    // effStock=0, flags=0 → eligible; wsNode=null → bldgId=0.
    CHECK(stub.q20Log.size() == 1u);
    if (!stub.q20Log.empty()) {
        CHECK_EQ(stub.q20Log[0].bldgId, 0);
        CHECK_EQ((int)stub.q20Log[0].itemHi, 44);
    }

    gStub = nullptr;
    g_meisterLeaves = nullptr;
}

// ===========================================================================
// TESTS — MeisterDispatchOrders mode 21 / mode 40 (hardening, 1:1 fixes)
//
// Mode 21 (gilde.exe 0x45db8c): the command's product id (cmd.extra1) is read
//   from *(QueryFind-result + 2), NOT from orderRow4 (an earlier wave bug).
// Mode 40 (gilde.exe 0x45d8ab..0x45da41): a command is emitted ONLY when a free
//   Amt slot is found in *(bldgRec+113) (marker +0xD signed >1 && *(slot+0x10)==-1);
//   otherwise the function returns without emitting.
// ===========================================================================

// queryFind stub: returns a caller-injected node pointer.
static u8* g_qfResult = nullptr;
static int g_qfCalls  = 0;
static u8* stub_queryFind(i32 /*rootId*/, const int* /*filters*/, int /*nf*/) {
    ++g_qfCalls;
    return g_qfResult;
}

// Test 28: Mode-21 product id comes from the QueryFind result node (+2), not orderRow4.
TEST(AiMeisterPasses, Dispatch_Mode21_ProductIdFromQueryFind) {
    resetAll();

    std::vector<u8> bldg = makeBldg(1, 42, 0, 7);  // sceneRoot=7
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);
    setupWorkerForDispatch(0, makeObjHandle(0), 42);
    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    u8  orderRowBuf[kWorkOrderStride] = {};
    i16 orderTypeWord = 55;
    std::memcpy(orderRowBuf, &orderTypeWord, 2);

    // QueryFind result node: *(node+2) = 0x1234 (the product/bldg id).
    std::vector<u8> qfNode(64, 0u);
    i32 nodeProductId = 0x1234;
    std::memcpy(qfNode.data() + 2, &nodeProductId, 4);
    g_qfResult = qfNode.data();
    g_qfCalls = 0;

    // orderRow4 carries a DIFFERENT value at +2; must be IGNORED in mode 21.
    u8 orderRow4Buf[kWorkOrderStride] = {};
    i32 decoy = 0x9999;
    std::memcpy(orderRow4Buf + 2, &decoy, 4);

    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;
    MeisterAiLeaves lv{};
    lv.queryFind = stub_queryFind;
    g_meisterLeaves = &lv;

    MeisterDispatchArgs args{};
    args.mode = 21; args.meisterRec = mr.data(); args.orderRow = orderRowBuf;
    args.orderRow4 = orderRow4Buf;
    args.divisor = 1; args.total = 2;
    MeisterDispatchOrders(&args);

    CHECK(g_qfCalls == 1);
    CHECK(sink.emitted.size() >= 1u);
    if (!sink.emitted.empty()) {
        CHECK_EQ(sink.emitted[0].extra1, nodeProductId);  // from QueryFind, not decoy
        CHECK(sink.emitted[0].extra1 != decoy);
    }
    CHECK_EQ(args.passIndex, 1);

    g_meisterCmdSink = nullptr;
    g_meisterLeaves = nullptr;
    g_qfResult = nullptr;
}

// Test 29: Mode-21 with null QueryFind result → extra1 == -1 (FindWorkProductObject gap).
TEST(AiMeisterPasses, Dispatch_Mode21_NullQueryFind_ProductMinusOne) {
    resetAll();

    std::vector<u8> bldg = makeBldg(1, 42, 0, 7);
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);
    setupWorkerForDispatch(0, makeObjHandle(0), 42);
    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    u8  orderRowBuf[kWorkOrderStride] = {};
    i16 orderTypeWord = 55;
    std::memcpy(orderRowBuf, &orderTypeWord, 2);

    g_qfResult = nullptr;  // QueryFind finds nothing
    g_qfCalls = 0;

    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;
    MeisterAiLeaves lv{};
    lv.queryFind = stub_queryFind;
    g_meisterLeaves = &lv;

    MeisterDispatchArgs args{};
    args.mode = 21; args.meisterRec = mr.data(); args.orderRow = orderRowBuf;
    args.divisor = 1; args.total = 2;
    MeisterDispatchOrders(&args);

    CHECK(sink.emitted.size() >= 1u);
    if (!sink.emitted.empty()) {
        CHECK_EQ(sink.emitted[0].extra1, -1);
    }

    g_meisterCmdSink = nullptr;
    g_meisterLeaves = nullptr;
}

// Helper: build a 64-entry AmtSlot table (24 bytes each). `freeIdx` (>=0) gets an
// emittable free slot: marker(+0xD) = 2 (>1 signed), *(slot+0x10) = -1.
static std::vector<u8> makeAmtTable(int freeIdx, i32 slotKey0 = 0xABCD) {
    std::vector<u8> t(guild::world::kAmtSlotCount * 24, 0u);
    for (int i = 0; i < guild::world::kAmtSlotCount; ++i) t[i * 24 + 0x0D] = 0xFF;
    if (freeIdx >= 0) {
        u8* s = t.data() + freeIdx * 24;
        std::memcpy(s, &slotKey0, 4);          // *(slot+0) = key
        s[0x0D] = 2;                           // marker > 1 (signed)
        i32 neg1 = -1; std::memcpy(s + 0x10, &neg1, 4);  // *(slot+0x10) == -1
    }
    return t;
}

// Test 30: Mode-40 emits a command when a free Amt slot exists.
TEST(AiMeisterPasses, Dispatch_Mode40_FreeSlot_Emits) {
    resetAll();

    std::vector<u8> amt = makeAmtTable(3, 0x55AA);

    std::vector<u8> bldg = makeBldg(1, 42, 0, 0);
    u8* amtPtr = amt.data();
    std::memcpy(bldg.data() + 113, &amtPtr, sizeof(amtPtr));
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);

    setupWorkerForDispatch(0, makeObjHandle(0), 42);
    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    u8 orderRowBuf[kWorkOrderStride] = {};

    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;
    MeisterAiLeaves lv{};  // no He handlers → v86 not decremented
    g_meisterLeaves = &lv;

    MeisterDispatchArgs args{};
    args.mode = 40; args.meisterRec = mr.data(); args.orderRow = orderRowBuf;
    args.divisor = 1; args.total = 2;
    MeisterDispatchOrders(&args);

    CHECK(sink.emitted.size() >= 1u);
    if (!sink.emitted.empty()) {
        CHECK_EQ((int)sink.emitted[0].cmdType, 40);
        CHECK_EQ((int)sink.emitted[0].mode, 1);       // var_214 = 1
        CHECK_EQ(sink.emitted[0].srcId, 0x55AA);      // *(freeSlot+0)
        CHECK_EQ(sink.emitted[0].extra0, 100);        // g_personIds[0]
    }
    CHECK(args.already >= 1);

    g_meisterCmdSink = nullptr;
    g_meisterLeaves = nullptr;
}

// Test 31: Mode-40 with NO free slot → no command, early return (passIndex untouched).
TEST(AiMeisterPasses, Dispatch_Mode40_NoFreeSlot_NoEmit) {
    resetAll();

    std::vector<u8> amt = makeAmtTable(-1);  // no emittable free slot

    std::vector<u8> bldg = makeBldg(1, 42, 0, 0);
    u8* amtPtr = amt.data();
    std::memcpy(bldg.data() + 113, &amtPtr, sizeof(amtPtr));
    std::memcpy(&g_objects[0], bldg.data(),
                bldg.size() < kObjectStride ? bldg.size() : kObjectStride);

    setupWorkerForDispatch(0, makeObjHandle(0), 42);
    std::vector<u8> mr = makeMeister(makeObjHandle(0));

    u8 orderRowBuf[kWorkOrderStride] = {};

    MeisterCmdSink sink;
    g_meisterCmdSink = &sink;
    MeisterAiLeaves lv{};
    g_meisterLeaves = &lv;

    MeisterDispatchArgs args{};
    args.mode = 40; args.meisterRec = mr.data(); args.orderRow = orderRowBuf;
    args.divisor = 1; args.total = 2; args.passIndex = 0;
    MeisterDispatchOrders(&args);

    // No free slot → the original returns WITHOUT bumping passIndex and emits nothing.
    CHECK(sink.emitted.empty());
    CHECK_EQ(args.passIndex, 0);  // early return, no ++passIndex

    g_meisterCmdSink = nullptr;
    g_meisterLeaves = nullptr;
}
