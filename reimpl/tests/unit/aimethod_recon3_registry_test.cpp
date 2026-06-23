// Golden tests for the AI-method registry loaders (recon3).
//   gilde.exe 0x4794e4 VIBE_AiNeeds_LookupAttributeIndex
//   gilde.exe 0x468f6c VIBE_AiMethod_RegisterFromIni
//   gilde.exe 0x468a40 VIBE_AiMethod_LoadDataFile (field schedule + prev mirror)
#include "tests/framework/test.h"
#include "sim/aimethod_recon3_registry.h"

#include <cstring>

using namespace guild;       // u8, i8, u32, f32 typedefs
using namespace guild::sim;

// --------------------------------------------------------------------------
// LookupAttributeIndex: exact ordinals, case-insensitivity, unknown -> -1.
// --------------------------------------------------------------------------
TEST(AiMethodRecon3, AttributeIndexExactOrdinals) {
    CHECK_EQ(AiNeeds_LookupAttributeIndex("APS"), (i8)0);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("UNVERSEHRTHEIT"), (i8)1);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("WOHNUNG"), (i8)2);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("GELD"), (i8)3);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("BERUF"), (i8)4);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("VERGNUEGEN"), (i8)5);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("ANSEHEN"), (i8)6);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("AMT"), (i8)7);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("BILDUNG"), (i8)8);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("RECHTSCHAFFENHEIT"), (i8)9);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("GEMEINHEIT"), (i8)10);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("SICHERHEIT"), (i8)11);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("FORTPFLANZUNG"), (i8)12);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("TRAEGHEIT"), (i8)13);
}

TEST(AiMethodRecon3, AttributeIndexCaseInsensitive) {
    CHECK_EQ(AiNeeds_LookupAttributeIndex("geld"), (i8)3);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("Beruf"), (i8)4);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("aMt"), (i8)7);
}

TEST(AiMethodRecon3, AttributeIndexUnknownIsMinusOne) {
    CHECK_EQ(AiNeeds_LookupAttributeIndex("NOTATHING"), (i8)-1);
    CHECK_EQ(AiNeeds_LookupAttributeIndex(""), (i8)-1);
    CHECK_EQ(AiNeeds_LookupAttributeIndex(nullptr), (i8)-1);
    // Prefix must not partial-match (full-string compare).
    CHECK_EQ(AiNeeds_LookupAttributeIndex("GEL"), (i8)-1);
    CHECK_EQ(AiNeeds_LookupAttributeIndex("GELDX"), (i8)-1);
}

// --------------------------------------------------------------------------
// ChangeIsNonZero: sign-masked nonzero test (+/-0.0 -> false).
// --------------------------------------------------------------------------
TEST(AiMethodRecon3, ChangeNonZeroSignMask) {
    CHECK(!AiMethod_ChangeIsNonZero(0.0f));
    CHECK(!AiMethod_ChangeIsNonZero(-0.0f));
    CHECK(AiMethod_ChangeIsNonZero(1.0f));
    CHECK(AiMethod_ChangeIsNonZero(-1.0f));
    CHECK(AiMethod_ChangeIsNonZero(0.0001f));
}

// --------------------------------------------------------------------------
// HasAnyEffect validation loop.
// --------------------------------------------------------------------------
TEST(AiMethodRecon3, HasEffectNoneWhenAllUnused) {
    AiMethodRecord rec;          // all slots attr=-1, change=0
    CHECK(!AiMethod_HasAnyEffect(rec));
}

TEST(AiMethodRecon3, HasEffectAttrSetButZeroChangeStillNone) {
    AiMethodRecord rec;
    rec.shortSlots[1].attrIndex = 3; // GELD
    rec.shortSlots[1].change    = 0.0f;
    CHECK(!AiMethod_HasAnyEffect(rec)); // change zero -> not effective
    rec.shortSlots[1].change    = -0.0f;
    CHECK(!AiMethod_HasAnyEffect(rec));
}

TEST(AiMethodRecon3, HasEffectChangeSetButAttrMinusOneStillNone) {
    AiMethodRecord rec;
    rec.longSlots[2].attrIndex = -1;
    rec.longSlots[2].change    = 5.0f;
    CHECK(!AiMethod_HasAnyEffect(rec)); // attr -1 -> not effective
}

TEST(AiMethodRecon3, HasEffectShortSideQualifies) {
    AiMethodRecord rec;
    rec.shortSlots[3].attrIndex = 5;   // VERGNUEGEN
    rec.shortSlots[3].change    = 2.0f;
    CHECK(AiMethod_HasAnyEffect(rec));
}

TEST(AiMethodRecon3, HasEffectLongSideQualifies) {
    AiMethodRecord rec;
    rec.longSlots[0].attrIndex = 0;    // APS (index 0 is valid, != -1)
    rec.longSlots[0].change    = -1.0f;
    CHECK(AiMethod_HasAnyEffect(rec));
}

// --------------------------------------------------------------------------
// RegisterFromIni: id gate.
// --------------------------------------------------------------------------
TEST(AiMethodRecon3, RegisterRejectsIdZeroAndOutOfRange) {
    IniSource ini;
    AiMethodRecord rec;
    rec.id = 0;
    CHECK_EQ(AiMethod_RegisterFromIni(rec, true, ini), 0);
    rec.id = 61;
    CHECK_EQ(AiMethod_RegisterFromIni(rec, true, ini), 0);
    rec.id = 200; // signed-byte: -56 <= 0 -> reject
    CHECK_EQ(AiMethod_RegisterFromIni(rec, true, ini), 0);
}

TEST(AiMethodRecon3, RegisterAcceptsValidIdRange) {
    IniSource ini; // all-null callbacks -> every key absent
    AiMethodRecord rec;
    rec.id = 1;
    CHECK_EQ(AiMethod_RegisterFromIni(rec, true, ini), 1);
    rec.id = 60;
    CHECK_EQ(AiMethod_RegisterFromIni(rec, true, ini), 1);
}

// --------------------------------------------------------------------------
// RegisterFromIni: per-slot fill + effect + prev-mirror via a stub IniSource.
// --------------------------------------------------------------------------
namespace {
struct FakeIni {
    // slot 0 short: GELD / +3.0 ; everything else absent.
    static const char* shortDesire(int slot, void*) {
        return slot == 0 ? "GELD" : nullptr;
    }
    static const char* longDesire(int, void*) { return nullptr; }
    static f32 shortChange(int slot, void*) { return slot == 0 ? 3.0f : 0.0f; }
    static f32 longChange(int, void*) { return 0.0f; }
};
} // namespace

TEST(AiMethodRecon3, RegisterFillsSlotsAndReportsEffect) {
    IniSource ini;
    ini.getShortDesire = &FakeIni::shortDesire;
    ini.getLongDesire  = &FakeIni::longDesire;
    ini.getShortChange = &FakeIni::shortChange;
    ini.getLongChange  = &FakeIni::longChange;

    AiMethodRecord rec;
    rec.id = 5;
    bool hasEffect = false;
    CHECK_EQ(AiMethod_RegisterFromIni(rec, true, ini, &hasEffect), 1);

    // slot 0 short filled from GELD / 3.0
    CHECK_EQ(rec.shortSlots[0].attrIndex, (i8)3);
    CHECK_EQ(rec.shortSlots[0].change, 3.0f);
    // absent keys -> attr -1, change 0
    CHECK_EQ(rec.shortSlots[1].attrIndex, (i8)-1);
    CHECK_EQ(rec.shortSlots[1].change, 0.0f);
    CHECK_EQ(rec.longSlots[0].attrIndex, (i8)-1);
    // effective (GELD with +3.0)
    CHECK(hasEffect);
    // prev mirror == short slots
    CHECK_EQ(rec.prevSlots[0].attrIndex, (i8)3);
    CHECK_EQ(rec.prevSlots[0].change, 3.0f);
    CHECK_EQ(rec.prevSlots[1].attrIndex, (i8)-1);
}

namespace {
// Desire present but its change is zero -> "keinerlei Auswirkungen".
const char* noEffShort(int slot, void*) { return slot == 0 ? "BERUF" : nullptr; }
f32 zeroChange(int, void*) { return 0.0f; }
} // namespace

TEST(AiMethodRecon3, RegisterReportsNoEffectWhenChangeZero) {
    IniSource ini;
    ini.getShortDesire = &noEffShort;
    ini.getShortChange = &zeroChange;
    AiMethodRecord rec;
    rec.id = 7;
    bool hasEffect = true;
    CHECK_EQ(AiMethod_RegisterFromIni(rec, true, ini, &hasEffect), 1);
    CHECK_EQ(rec.shortSlots[0].attrIndex, (i8)4); // BERUF still mapped
    CHECK(!hasEffect);                            // but change 0 -> no effect
}

// --------------------------------------------------------------------------
// LoadDataFile field schedule + StreamRecord behavior.
// --------------------------------------------------------------------------
TEST(AiMethodRecon3, FieldScheduleExactOffsets) {
    const AiMethodFieldSpec expect[18] = {
        {0,1},{1,32},{48,1},{52,4},{56,1},{60,4},{64,1},{68,4},{72,1},{76,4},
        {112,1},{116,4},{120,1},{124,4},{128,1},{132,4},{136,1},{140,4},
    };
    for (int i = 0; i < kAiMethodFieldCount; ++i) {
        CHECK_EQ(kAiMethodFieldSchedule[i].offset, expect[i].offset);
        CHECK_EQ(kAiMethodFieldSchedule[i].size, expect[i].size);
    }
    // Total serialized payload bytes (sum of sizes).
    int total = 0;
    for (int i = 0; i < kAiMethodFieldCount; ++i) total += kAiMethodFieldSchedule[i].size;
    CHECK_EQ(total, 1 + 32 + 8 * 1 + 8 * 4); // 1+32+8+32 = 73
}

namespace {
struct StreamCtx { int calls = 0; int failAt = -1; };
bool xferOk(u8*, i32, i32, void* c) {
    StreamCtx* s = static_cast<StreamCtx*>(c);
    int k = s->calls++;
    return k != s->failAt;
}
} // namespace

TEST(AiMethodRecon3, StreamRecordAllFieldsThenMirror) {
    u8 buf[148] = {};
    // seed short-slot region (offset 48..79) with a recognizable pattern; prev
    // region (80..111) starts zeroed and must become a copy after mirror.
    for (int i = 48; i < 80; ++i) buf[i] = static_cast<u8>(i);
    StreamCtx ctx;
    ByteStream s; s.xfer = &xferOk; s.ctx = &ctx;
    CHECK(AiMethod_StreamRecord(buf, s, /*mirrorPrevOnSuccess=*/true));
    CHECK_EQ(ctx.calls, 18); // all 18 fields attempted
    // mirror copied 48..79 -> 80..111
    CHECK_EQ(std::memcmp(buf + 80, buf + 48, 32), 0);
}

TEST(AiMethodRecon3, StreamRecordStopsAtFirstFailure) {
    u8 buf[148] = {};
    StreamCtx ctx; ctx.failAt = 4; // fail the 5th field
    ByteStream s; s.xfer = &xferOk; s.ctx = &ctx;
    CHECK(!AiMethod_StreamRecord(buf, s, true));
    CHECK_EQ(ctx.calls, 5); // attempted up to and including the failing one
    // mirror NOT applied on failure
    CHECK_EQ(buf[80], (u8)0);
}

TEST(AiMethodRecon3, StreamRecordNoMirrorOnWritePath) {
    u8 buf[148] = {};
    for (int i = 48; i < 80; ++i) buf[i] = 0xAB;
    StreamCtx ctx;
    ByteStream s; s.xfer = &xferOk; s.ctx = &ctx;
    CHECK(AiMethod_StreamRecord(buf, s, /*mirrorPrevOnSuccess=*/false));
    // write path leaves prev region untouched
    CHECK_EQ(buf[80], (u8)0);
}
