// Unit tests for the person-record reconciliation (src/sim/person_record.{h,cpp}).
//
// These assert the DEFINITIVE finding: the "536" and "589" strides belong to
// THREE DISTINCT arrays (Person 536, Object 169, building-type 589), not one
// array with a wrong stride — and that the canonical strides/offsets match the
// decompiled arithmetic of:
//   VIBE_Person_FindRecordById  0x58bc6c  (steps 536, bound 411648)
//   VIBE_Person_IterNext        0x586a6c  (steps 169, bound 43264; 589*type table)
//   VIBE_Economy_ComputeGoodsDemand 0x578438 (need = typeDef+583; emp = object+39)
#include <cstring>

#include "sim/building.h"
#include "sim/entity.h"
#include "sim/person_record.h"
#include "sim/types.h"
#include "test.h"

using namespace guild::sim;
using namespace guild::sim::person_reconcile;

// ---------------------------------------------------------------------------
// Strides & scan bounds match the decompiled multipliers exactly.
// ---------------------------------------------------------------------------
TEST(PersonReconcile, CanonicalStrides) {
    // Person/NPC array: FindRecordById steps 536, bound == 536*768.
    CHECK_EQ(kPersonRecordStride, 536);
    CHECK_EQ(kPersonRecordCapacity, 768);
    CHECK_EQ(kPersonScanBound, 411648);
    CHECK_EQ(kPersonRecordStride * kPersonRecordCapacity, kPersonScanBound);
    CHECK_EQ((int)sizeof(Person), kPersonRecordStride);

    // Object/Building array: Person_IterNext steps 169, bound == 169*256.
    CHECK_EQ(kObjectRecordStride, 169);
    CHECK_EQ(kObjectRecordCapacity, 256);
    CHECK_EQ(kObjectScanBound, 43264);
    CHECK_EQ(kObjectRecordStride * kObjectRecordCapacity, kObjectScanBound);
    CHECK_EQ((int)sizeof(ObjectRec), kObjectRecordStride);

    // Type descriptor table: economy indexes 589 * typeByte + base.
    CHECK_EQ(kTypeDescriptorStride, 589);
    CHECK_EQ((int)sizeof(BuildingTypeDef), kTypeDescriptorStride);

    // The three strides are genuinely distinct.
    CHECK(kPersonRecordStride != kTypeDescriptorStride);
    CHECK(kPersonRecordStride != kObjectRecordStride);
    CHECK(kObjectRecordStride != kTypeDescriptorStride);
}

// FindRecordById's proven 589-vs-536 multiplier: replicate the index math and
// confirm the canonical constants reproduce it.
TEST(PersonReconcile, FindRecordByIdStepsBy536) {
    // Emulate the binary's byte cursor: v2 += 536 until v2 >= 411648.
    int slots = 0;
    for (int v2 = 0; v2 < kPersonScanBound; v2 += kPersonRecordStride)
        ++slots;
    CHECK_EQ(slots, kPersonRecordCapacity);  // exactly 768 steps of 536
}

// The 589 multiplier in Ai_EvaluateMeister: 589 = 32*(19*x) - 19*x per index.
TEST(PersonReconcile, TypeTableMultiplierIs589) {
    for (int x = 0; x < 32; ++x) {
        int a = 5 * x;        // lea [ecx*4]; add ecx
        a = (a << 2) - x;     // shl 2; sub ecx  -> 19*x
        int c = a;
        a = (a << 5) - c;     // shl 5; sub ecx  -> 589*x
        CHECK_EQ(a, 589 * x);
    }
}

// ---------------------------------------------------------------------------
// Offset contract: need byte @ typeDef+583, employment word @ object+39.
// ---------------------------------------------------------------------------
TEST(PersonReconcile, EconomyOffsets) {
    CHECK_EQ(kObjectEmploymentOff, 39);
    CHECK_EQ(kTypeNeedByteOff, 583);
    CHECK_EQ(kTypeNeedByteOff, (int)BuildingTypeField::kSecurity);
    CHECK_EQ((int)kEmploymentNone, 0xFFFF);
}

// ---------------------------------------------------------------------------
// The adapter joins the (object, type-table) pair exactly as the binary does.
// ---------------------------------------------------------------------------
TEST(PersonReconcile, AdapterJoinsObjectAndTypeTable) {
    // Build a type table where type 7's need byte (+583) is 42.
    static BuildingTypeDef table[256];
    std::memset(table, 0, sizeof(table));
    table[7].security = 42;   // typeDef[7] + 583
    table[9].security = 200;

    ObjectRec obj{};
    std::memset(&obj, 0, sizeof(obj));
    obj.alive = 7;            // object +0 type byte -> indexes type 7
    // employment word at object+39 = 100 (employed).
    guild::u16 emp = 100;
    std::memcpy(reinterpret_cast<guild::u8*>(&obj) + 39, &emp, sizeof(emp));

    PersonEcoInputs in = PersonReadEcoInputs(&obj, table, /*loaded*/true);
    CHECK_EQ((int)in.need, 42);
    CHECK(!in.unemployed);

    // Flip to unemployed (0xFFFF) and a different type.
    obj.alive = 9;
    guild::u16 none = 0xFFFF;
    std::memcpy(reinterpret_cast<guild::u8*>(&obj) + 39, &none, sizeof(none));
    in = PersonReadEcoInputs(&obj, table, true);
    CHECK_EQ((int)in.need, 200);
    CHECK(in.unemployed);

    // Table unloaded -> need byte reads 0 (mirrors dword_13CE294-null bail).
    in = PersonReadEcoInputs(&obj, table, /*loaded*/false);
    CHECK_EQ((int)in.need, 0);
    CHECK(in.unemployed);     // employment still read from the object record
}

// ---------------------------------------------------------------------------
// The two id-keyed arrays do NOT alias: a Person and an Object can share an id
// and resolve to different storage.
// ---------------------------------------------------------------------------
TEST(PersonReconcile, PersonAndObjectArraysDoNotAlias) {
    ResetEntityArrays();
    // Same id 500 in both arrays.
    g_persons[3].marker = 0; g_persons[3].id = 500; g_personIds[3] = 500;
    g_objects[4].alive = 1;  g_objects[4].id = 500;
    g_personArrayLoaded = true;
    g_sceneArrayLoaded = true;

    Person* p = PersonFindRecordById(500);
    ObjectRec* o = BuildingFindById(500);
    CHECK(p == &g_persons[3]);
    CHECK(o == &g_objects[4]);
    // Distinct storage regions.
    CHECK((const void*)p != (const void*)o);
    CHECK(p->id == 500);
    CHECK(o->id == 500);
}
