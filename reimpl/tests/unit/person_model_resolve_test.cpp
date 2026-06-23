// Golden unit tests for the live-person -> character-model resolution
// (VIBE_Office_ResolveStaffModel @0x57c1e8, reconstructed 1:1 in
// play/person_render.*) and the asset-name mapping the spawn chain uses
// (mesh "_DYNAMIC/Character/<model>.bgf", anim "character/<base>/<clip>_<base>.baf").
//
// The expected names/codes are the byte-exact table contents recovered with
// get_bytes from 0x63DA78 / 0x63DAA0 / 0x63DAC8 / 0x63DF00 / 0x63E338 /
// 0x63EF18 / 0x6405E8 (see person_render.cpp).
#include "play/person_render.h"
#include "sim/types.h"
#include "tests/framework/test.h"

#include <cstring>
#include <string>

using namespace guild;
using namespace guild::play;

namespace {

// Build a synthetic person record by raw byte offset (the exact columns the
// resolver reads): gender +0x09, adult word +0x0A, threshold float +0x20,
// kind +0x02, office +0x164, profession +0x165, texSet +0x18C, name +0x1F0.
struct RawPerson {
    sim::Person p{};
    u8* bytes() { return reinterpret_cast<u8*>(&p); }
    RawPerson() {
        std::memset(&p, 0, sizeof p);
        i32 noTex = -1;
        std::memcpy(bytes() + 0x18C, &noTex, 4);   // texSet = -1 (none)
    }
    void gender(u8 g)        { bytes()[0x09] = g; }
    void kind(u8 k)          { bytes()[0x02] = k; }
    void office(u8 o)        { bytes()[0x164] = o; }
    void profession(u8 pr)   { bytes()[0x165] = pr; }
    void adultWord(u16 w)    { std::memcpy(bytes() + 0x0A, &w, 2); }
    void threshold(float t)  { std::memcpy(bytes() + 0x20, &t, 4); }
    void texSet(i32 t)       { std::memcpy(bytes() + 0x18C, &t, 4); }
    void storedName(const char* n) {
        std::strncpy(reinterpret_cast<char*>(bytes() + 0x1F0), n, 39);
    }
    const StaffModelRecord* resolve(StaffRandModulo rnd = nullptr) {
        return ResolveStaffModel(MakePersonModelView(&p), rnd);
    }
};

int FixedRand1(u16) { return 1; }

} // namespace

// --- 1. The child gate: !office && adultWord < threshold -> child records. ----
TEST(PersonModelResolve, ChildGatePicksChildModels) {
    RawPerson boy;
    boy.adultWord(5);
    boy.threshold(10.0f);
    const StaffModelRecord* r = boy.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("holzfaeller_SOLDAT")); // 0x63DA78
    CHECK_EQ((int)r->texVariants[0], 0x77);
    CHECK_EQ((int)r->texVariants[1], 0x78);

    RawPerson girl;
    girl.gender(1);
    girl.adultWord(5);
    girl.threshold(10.0f);
    r = girl.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("Magd_FRAU"));          // 0x63DAA0
    CHECK_EQ((int)r->texVariants[0], 0x79);

    // The office byte alone makes the person ADULT (the || in the gate).
    RawPerson officeChild;
    officeChild.adultWord(5);
    officeChild.threshold(10.0f);
    officeChild.office(0x34);
    r = officeChild.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("offizier_SOLDAT"));
}

// --- 2. Profession scan: code match in the gender's table. --------------------
TEST(PersonModelResolve, ProfessionMatch) {
    RawPerson m;                       // zeroed record: 0 >= 0.0 -> adult
    m.profession(0x04);
    const StaffModelRecord* r = m.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("priester_KUTTE"));
    CHECK_EQ(r->code, 0x04);
    CHECK_EQ((int)r->texVariants[0], 0x5d);

    RawPerson thief;
    thief.profession(0x02);
    r = thief.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("dieb_MANN2"));

    RawPerson f;
    f.gender(1);
    f.profession(0x05);
    r = f.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("handwerkerin_FRAU"));
}

// --- 3. Unmatched profession stops at the code-0 record (the fallback). -------
TEST(PersonModelResolve, ProfessionMissFallsBackToTerminatorRecord) {
    RawPerson m;
    m.profession(0x7f);                // no such profession code
    const StaffModelRecord* r = m.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("bettler3_MANN2"));     // idx 26
    CHECK_EQ(r->code, 0);

    RawPerson f;
    f.gender(1);
    f.profession(0x7f);
    r = f.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("minerin_FRAU"));       // idx 6
}

// --- 4. Office scan beats the profession scan and matches by code. ------------
TEST(PersonModelResolve, OfficeMatch) {
    RawPerson m;
    m.office(0x34);
    m.profession(0x02);                // office wins over profession
    const StaffModelRecord* r = m.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("offizier_SOLDAT"));
    CHECK_EQ(r->code, 0x34);

    RawPerson f;
    f.gender(1);
    f.office(0x46);
    r = f.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("magd_FRAU"));

    // An unmatched office code stops at the terminator record.
    RawPerson miss;
    miss.office(0x7f);
    r = miss.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("bettler_KUTTE"));      // idx 75
}

// --- 5. No office / no profession -> the gender table BASE record. ------------
TEST(PersonModelResolve, DefaultByGenderIsTableBase) {
    RawPerson m;                       // adult, office 0, profession 0
    const StaffModelRecord* r = m.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("bergmann3_MANN"));     // 0x63DAC8[0]
    CHECK(r == StaffMaleProfessionTable());

    RawPerson f;
    f.gender(1);
    r = f.resolve();
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("handwerkerin_FRAU"));  // 0x63DF00[0]
    CHECK(r == StaffFemaleProfessionTable());
}

// --- 6. Kind 17 -> the random spion record (injected rand). -------------------
TEST(PersonModelResolve, ReaperKindUsesRandomSpion) {
    RawPerson m;
    m.kind(17);
    const StaffModelRecord* r = m.resolve(&FixedRand1);
    CHECK(r != nullptr);
    CHECK_EQ(std::string(r->name), std::string("spion2_MEDIUM"));      // 0x6405E8[1]
    CHECK_EQ((int)r->texVariants[0], 0x7c);
    CHECK(r == &StaffReaperTable()[1]);
}

// --- 7. Stored-name path: rotating scratch + texVariant patch (+68). ----------
TEST(PersonModelResolve, StoredNamePathPatchesTexVariants) {
    ResetStaffModelScratch();
    RawPerson m;
    m.storedName("custom_MANN");
    m.texSet(2);
    const StaffModelRecord* r1 = m.resolve();
    CHECK(r1 != nullptr);
    CHECK_EQ(std::string(r1->name), std::string("custom_MANN"));
    // texVariants[0..3] = LOBYTE(texSet) + 68 = 70 ('F').
    for (int i = 0; i < 4; ++i) CHECK_EQ((int)r1->texVariants[i], 70);

    // The 8-slot rotation: a second resolve lands in a DIFFERENT scratch record.
    RawPerson m2;
    m2.storedName("other_FRAU");
    m2.texSet(0);
    const StaffModelRecord* r2 = m2.resolve();
    CHECK(r2 != nullptr);
    CHECK(r2 != r1);
    CHECK_EQ(std::string(r2->name), std::string("other_FRAU"));
    CHECK_EQ((int)r2->texVariants[0], 68);
    // r1's contents are still intact (its slot was not reused yet).
    CHECK_EQ(std::string(r1->name), std::string("custom_MANN"));

    // texSet == -1 disables the path -> falls through to the default tables.
    RawPerson m3;
    m3.storedName("custom_MANN");
    m3.texSet(-1);
    const StaffModelRecord* r3 = m3.resolve();
    CHECK(r3 == StaffMaleProfessionTable());
    ResetStaffModelScratch();
}

// --- 8. Asset-name mapping: mesh member + the PreloadAniSet sprintf. -----------
TEST(PersonModelResolve, AssetNameMapping) {
    CHECK_EQ(CharacterMeshMemberName("dieb_MANN2"),
             std::string("_DYNAMIC/Character/dieb_MANN2.bgf"));
    CHECK_EQ(CharacterMeshMemberName("Magd_FRAU"),
             std::string("_DYNAMIC/Character/Magd_FRAU.bgf"));
    // "character/%s/%s_%s.baf" (VIBE_Character_PreloadAniSet @0x403c34).
    CHECK_EQ(CharacterAnimMemberName("MANN2", "stehen/stehen_newnoise"),
             std::string("character/MANN2/stehen/stehen_newnoise_MANN2.baf"));
    CHECK_EQ(CharacterAnimMemberName("FRAU", "bewegung/gehen"),
             std::string("character/FRAU/bewegung/gehen_FRAU.baf"));

    // Case-insensitive member lookup returns the EXACT stored name.
    std::vector<io::ArchiveMember> members(2);
    members[0].name = "_DYNAMIC/Character/Bettler3_MANN2.bgf";
    members[1].name = "Character/MANN2/Bewegung/gehen_MANN2.baf";
    CHECK_EQ(FindMemberCaseInsensitive(members,
                                       "_DYNAMIC/Character/bettler3_MANN2.bgf"),
             std::string("_DYNAMIC/Character/Bettler3_MANN2.bgf"));
    CHECK_EQ(FindMemberCaseInsensitive(members,
                                       "character/MANN2/bewegung/gehen_MANN2.baf"),
             std::string("Character/MANN2/Bewegung/gehen_MANN2.baf"));
    CHECK_EQ(FindMemberCaseInsensitive(members, "absent.bgf"), std::string());
}
