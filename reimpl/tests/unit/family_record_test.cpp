// WAVE-18 — the FAMILY-RECORD table (word_13C3110) + VIBE_Person_GetFamilyRecord
// @0x58c408 + the inline allocator @0x58ecf3.
//
// Golden-pins:
//   (a) the record layout (164 bytes / 82 words, 16 records) and reset image
//       (word[0] = -1 sentinel, dword_647720 = 0);
//   (b) the accessor gate: kind in {5,6,7} AND (signed char)+81 < 0 (the 0x8000
//       bit at +0x50), index = +0x50 & 0xF;
//   (c) the allocator: +0x50 = count|0x8000, record +128 = -1.0f, record[0] =
//       family word, counter increment, table-full => nullptr;
//   (d) the gate FIRING end-to-end: a kind-6 CreateAndSpawn now allocates a real
//       family record (the wave-17 deferral is closed), and ExCreatePersonB
//       stamps the dynasty name through it.
#include "tests/framework/test.h"

#include "sim/family_record.h"
#include "sim/person_create.h"
#include "sim/entity.h"
#include "sim/command_apply5.h"
#include "sim/command.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

float SeedF(const u8* fam) {
    float f; std::memcpy(&f, fam + kFamSeedOff, 4); return f;
}
i16 Word0(const u8* fam) {
    i16 w; std::memcpy(&w, fam + kFamWordOff, 2); return w;
}

void ResetWorld() {
    std::memset(g_persons, 0, sizeof(Person) * kPersonCapacity);
    ResetEntityArrays();
    ResetPersonCreate();      // also calls FamilyRecord_ResetAll
    crt::Srand(0);
}

// Build a kind-6 (family/player) spawn args bundle.
PersonSpawnArgs FamilyArgs(u8 kind) {
    PersonSpawnArgs a{};
    a.kind = kind;
    a.parentAId = 0;          // no parents -> the no-parents path
    a.parentBId = 0;
    a.ownerWord = 16;
    a.a6 = 0; a.a7 = 0; a.a8 = 0;
    return a;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(family_record, layout_and_reset) {
    CHECK_EQ(kFamilyStride, 164);
    CHECK_EQ(kFamilyWords, 82);
    CHECK_EQ(kFamilyCapacity, 16);
    CHECK_EQ(sizeof(g_familyTable), static_cast<size_t>(164 * 16));

    // Scribble, then reset, and confirm the 0x5896fc image: each record cleared
    // with word[0] = -1; the counter zeroed.
    std::memset(g_familyTable, 0xAB, sizeof(g_familyTable));
    g_familyCount = 9;
    FamilyRecord_ResetAll();
    CHECK_EQ(g_familyCount, 0);
    for (int i = 0; i < kFamilyCapacity; ++i) {
        const u8* rec = &g_familyTable[164 * i];
        CHECK_EQ(Word0(rec), static_cast<i16>(-1));
        // bytes after word[0] are zero-filled.
        bool zero = true;
        for (int b = 2; b < 164; ++b) if (rec[b]) zero = false;
        CHECK(zero);
    }
}

// ---------------------------------------------------------------------------
TEST(family_record, accessor_gate) {
    FamilyRecord_ResetAll();
    u8 person[kPersonStride];
    std::memset(person, 0, sizeof(person));

    // kind not in {5,6,7} -> always null even with the bit set.
    person[2] = 9;
    i16 famWord = static_cast<i16>(3 | 0x8000);
    std::memcpy(person + 0x50, &famWord, 2);
    CHECK(Person_GetFamilyRecord(person) == nullptr);

    // kind 6 but +81 >= 0 (no 0x8000 bit) -> null.
    person[2] = 6;
    i16 noBit = 3;            // high byte 0 -> +81 == 0 (>= 0)
    std::memcpy(person + 0x50, &noBit, 2);
    CHECK(static_cast<i8>(person[81]) >= 0);
    CHECK(Person_GetFamilyRecord(person) == nullptr);

    // kind 6 with the bit set -> record at index (word & 0xF).
    for (int kind : {5, 6, 7}) {
        for (int idx = 0; idx < 16; ++idx) {
            person[2] = static_cast<u8>(kind);
            i16 w = static_cast<i16>(idx | 0x8000);
            std::memcpy(person + 0x50, &w, 2);
            CHECK(static_cast<i8>(person[81]) < 0);
            u8* got = Person_GetFamilyRecord(person);
            CHECK(got == &g_familyTable[164 * idx]);
        }
    }
}

// ---------------------------------------------------------------------------
TEST(family_record, allocator) {
    FamilyRecord_ResetAll();
    u8 person[kPersonStride];

    // Allocate all 16 slots; each stamps +0x50, the record seed, word[0].
    for (int i = 0; i < 16; ++i) {
        std::memset(person, 0, sizeof(person));
        person[2] = 6;
        u8* fam = FamilyRecord_AllocForPerson(person, 6);
        CHECK(fam == &g_familyTable[164 * i]);
        i16 pw; std::memcpy(&pw, person + 0x50, 2);
        CHECK_EQ(pw, static_cast<i16>(i | 0x8000));
        CHECK_EQ(Word0(fam), static_cast<i16>(i | 0x8000));
        CHECK_EQ(SeedF(fam), -1.0f);
        CHECK_EQ(g_familyCount, i + 1);
    }
    // 17th -> table full -> nullptr, +0x50 untouched.
    std::memset(person, 0, sizeof(person));
    person[2] = 6;
    person[0x50] = 0x11; person[0x51] = 0x22;
    CHECK(FamilyRecord_AllocForPerson(person, 6) == nullptr);
    CHECK_EQ(person[0x50], 0x11);
    CHECK_EQ(person[0x51], 0x22);
    CHECK_EQ(g_familyCount, 16);
}

// ---------------------------------------------------------------------------
// The gate FIRES through the real person factory now (wave-17 deferral closed).
TEST(family_record, create_and_spawn_fires_gate) {
    ResetWorld();

    u16 idx = Person_CreateAndSpawn(FamilyArgs(6)); // player/head kind
    CHECK(idx != 0xFFFF);

    u8* rec = reinterpret_cast<u8*>(&g_persons[idx]);
    // The family slot word is set with the 0x8000 bit -> +81 < 0.
    i16 fw; std::memcpy(&fw, rec + 0x50, 2);
    CHECK((static_cast<u16>(fw) & 0x8000) != 0);
    CHECK(static_cast<i8>(rec[81]) < 0);

    // The accessor resolves the real record and it carries the alloc stamps.
    u8* fam = Person_GetFamilyRecord(rec);
    CHECK(fam == &g_familyTable[164 * (static_cast<u16>(fw) & 0xF)]);
    CHECK_EQ(SeedF(fam), -1.0f);
    CHECK_EQ(Word0(fam), fw);
    CHECK_EQ(g_familyCount, 1);

    // A non-family kind (3) does NOT touch the family table.
    u16 idx2 = Person_CreateAndSpawn(FamilyArgs(3));
    CHECK(idx2 != 0xFFFF);
    u8* rec2 = reinterpret_cast<u8*>(&g_persons[idx2]);
    CHECK(Person_GetFamilyRecord(rec2) == nullptr);
    CHECK_EQ(g_familyCount, 1); // unchanged
}

// ---------------------------------------------------------------------------
// ExCreatePersonB (opcode 0x0C) now stamps the dynasty name through the record.
TEST(family_record, ex_create_person_b_stamps_name) {
    ResetWorld();
    Apply5_SetStandalone(true);

    CommandPacket pkt{};
    std::memset(&pkt, 0, sizeof(pkt));
    // packet +37 = person name, +53 = family/dynasty name (StrNCopyPad 16).
    std::memcpy(pkt.bytes + 37, "Otto", 5);
    std::memcpy(pkt.bytes + 53, "Fugger", 7);
    // +31 wappen dword, +36 faith byte, +28 owner word.
    u16 owner = 16; std::memcpy(pkt.bytes + 28, &owner, 2);

    AckEntry ack{};
    int r = ExCreatePersonB(pkt, &ack);
    CHECK_EQ(r, 0);

    // ack carries the new record pointer; find the family record from it.
    u8* rec = reinterpret_cast<u8*>(&g_persons[0]); // first allocated slot
    // The created person is a family kind (a2/ack != null -> kind 6).
    CHECK_EQ(rec[2], 6);
    u8* fam = Person_GetFamilyRecord(rec);
    CHECK(fam != nullptr);
    // The dynasty name landed at familyRec+2 and at the person's own +64 field.
    CHECK(std::strncmp(reinterpret_cast<char*>(fam + kFamNameOff), "Fugger", 6) == 0);
    CHECK(std::strncmp(reinterpret_cast<char*>(rec + 64), "Fugger", 6) == 0);
    CHECK_EQ(SeedF(fam), -1.0f);
}
