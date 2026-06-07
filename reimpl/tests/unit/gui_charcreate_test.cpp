// Unit tests for the new-CHARACTER creation flow (gui/charcreate):
//   - RunChooseCharacter dynasty/actor classification (profession code + gender slot fill)
//   - ChooseCharacterTalent commit maps (parent code -> profession byte)
//   - ChooseProfession click->index resolution
//   - BuildCharacterPreviewScene portrait cycling (wrap, step, command tables)
//   - ChooseCharacterIntroVariant clicked-child -> variant byte
//
// All values are golden vectors recovered 1:1 from gilde.exe (switch tables, get_bytes
// command/name tables, the index-wrap arithmetic).
#include "gui/charcreate.h"
#include "test.h"

using namespace guild::gui;

// ===========================================================================
// Actor classification (0x52bcd4)
// ===========================================================================
TEST(GuiCharCreate, ActorProfessionCodes) {
    CHECK_EQ(ChooseCharacter_ActorProfessionCode(ChooseCharActor::kDiebMann), 2);
    CHECK_EQ(ChooseCharacter_ActorProfessionCode(ChooseCharActor::kZigeunerinFrau), 2);
    CHECK_EQ(ChooseCharacter_ActorProfessionCode(ChooseCharActor::kHandwerkerMann), 1);
    CHECK_EQ(ChooseCharacter_ActorProfessionCode(ChooseCharActor::kHandwerkerinFrau), 1);
    CHECK_EQ(ChooseCharacter_ActorProfessionCode(ChooseCharActor::kOffizierMann), 3);
    CHECK_EQ(ChooseCharacter_ActorProfessionCode(ChooseCharActor::kBuergerMann), 0);
    CHECK_EQ(ChooseCharacter_ActorProfessionCode(ChooseCharActor::kBuergerinFrau), 0);
    CHECK_EQ(ChooseCharacter_ActorProfessionCode(ChooseCharActor::kPriesterMann), 4);
    CHECK_EQ(ChooseCharacter_ActorProfessionCode(ChooseCharActor::kPriesterinFrau), 4);
    CHECK_EQ(ChooseCharacter_ActorProfessionCode(ChooseCharActor::kNone), -1);
}

TEST(GuiCharCreate, ActorGenderSets) {
    CHECK(ChooseCharacter_ActorIsMale(ChooseCharActor::kDiebMann));
    CHECK(ChooseCharacter_ActorIsMale(ChooseCharActor::kHandwerkerMann));
    CHECK(ChooseCharacter_ActorIsMale(ChooseCharActor::kOffizierMann));
    CHECK(ChooseCharacter_ActorIsMale(ChooseCharActor::kBuergerMann));
    CHECK(ChooseCharacter_ActorIsMale(ChooseCharActor::kPriesterMann));
    CHECK(!ChooseCharacter_ActorIsMale(ChooseCharActor::kZigeunerinFrau));
    CHECK(!ChooseCharacter_ActorIsMale(ChooseCharActor::kHandwerkerinFrau));
    CHECK(!ChooseCharacter_ActorIsMale(ChooseCharActor::kBuergerinFrau));
    CHECK(!ChooseCharacter_ActorIsMale(ChooseCharActor::kPriesterinFrau));
}

TEST(GuiCharCreate, AncestryObjectTable) {
    CHECK(std::string(kAncestryObjectNames[0]) == "dummy_GROSSVATER_VAETERLICH");
    CHECK(std::string(kAncestryObjectNames[1]) == "dummy_GROSSMUTTER_VAETERLICH");
    CHECK(std::string(kAncestryObjectNames[2]) == "dummy_GROSSVATER_MUETTERLICH");
    CHECK(std::string(kAncestryObjectNames[3]) == "dummy_GROSSMUTTER_MUETTERLICH");
    CHECK(std::string(kAncestryObjectNames[4]) == "dummy_VATER");
    CHECK(std::string(kAncestryObjectNames[5]) == "dummy_MUTTER");
}

TEST(GuiCharCreate, ApplyActorClickFillsParitySlots) {
    DynastyTable t;
    int fill = 0;
    // Even slots (0,2,4) take males; odd slots (1,3,5) take females.
    // A female click while the next free slot is even (0) is rejected.
    CHECK_EQ(ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kZigeunerinFrau, &fill), -1);
    CHECK_EQ(fill, 0);
    // Male fills slot 0 with its code.
    CHECK_EQ(ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kBuergerMann, &fill), 0);
    CHECK_EQ(t.slot[0], 0);    // Buerger code 0
    CHECK_EQ(fill, 1);
    // Female fills slot 1.
    CHECK_EQ(ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kHandwerkerinFrau, &fill), 1);
    CHECK_EQ(t.slot[1], 1);    // Handwerker code 1
    CHECK_EQ(fill, 2);
    // Not complete yet.
    CHECK(!ChooseCharacter_IsComplete(fill));
    // Fill the rest: slot2 male, slot3 female, slot4 male, slot5 female.
    CHECK_EQ(ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kOffizierMann, &fill), 2);
    CHECK_EQ(t.slot[2], 3);
    CHECK_EQ(ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kBuergerinFrau, &fill), 3);
    CHECK_EQ(t.slot[3], 0);
    CHECK_EQ(ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kPriesterMann, &fill), 4);
    CHECK_EQ(t.slot[4], 4);
    CHECK_EQ(ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kPriesterinFrau, &fill), 5);
    CHECK_EQ(t.slot[5], 4);
    CHECK_EQ(fill, 6);
    CHECK(ChooseCharacter_IsComplete(fill));
    // Further clicks rejected (scene complete).
    CHECK_EQ(ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kBuergerMann, &fill), -1);
}

// ===========================================================================
// Talent commit maps (0x52b088)
// ===========================================================================
TEST(GuiCharCreate, TalentPaternalMap) {
    CHECK_EQ(Talent_PaternalProfessionByte(0, 99), 28);
    CHECK_EQ(Talent_PaternalProfessionByte(1, 99), 49);
    CHECK_EQ(Talent_PaternalProfessionByte(2, 99), 4);
    CHECK_EQ(Talent_PaternalProfessionByte(3, 99), 55);
    CHECK_EQ(Talent_PaternalProfessionByte(4, 99), 16);
    CHECK_EQ(Talent_PaternalProfessionByte(5, 99), 99); // out of range -> prev
    CHECK_EQ(Talent_PaternalProfessionByte(-1, 7), 7);
}

TEST(GuiCharCreate, TalentMaternalMap) {
    CHECK_EQ(Talent_MaternalProfessionByte(0, 99), 28);
    CHECK_EQ(Talent_MaternalProfessionByte(1, 99), 37);
    CHECK_EQ(Talent_MaternalProfessionByte(2, 99), 4);
    CHECK_EQ(Talent_MaternalProfessionByte(3, 99), 0);
    CHECK_EQ(Talent_MaternalProfessionByte(4, 99), 73);
    CHECK_EQ(Talent_MaternalProfessionByte(5, 99), 99);
}

TEST(GuiCharCreate, TalentEnablePredicates) {
    // "+" enabled when value <= cap.
    CHECK(Talent_CanDecrease(3, 3));
    CHECK(Talent_CanDecrease(2, 3));
    CHECK(!Talent_CanDecrease(4, 3));
    // "-" enabled when budget>0 and value>=floor (default floor 0).
    CHECK(Talent_CanIncrease(1, 5));
    CHECK(!Talent_CanIncrease(1, 0));
    CHECK(!Talent_CanIncrease(-1, 5));
}

TEST(GuiCharCreate, CommitTalents) {
    CharCreateParams p;
    std::uint8_t talents[kTalentCount] = {3, 7, 1, 9, 2};
    CharCreate_CommitTalents(p, /*paternal*/1, /*maternal*/4, talents);
    CHECK_EQ(p.paternalProfessionByte, 49);
    CHECK_EQ(p.maternalProfessionByte, 73);
    for (int i = 0; i < kTalentCount; ++i)
        CHECK_EQ((int)p.talents[i], (int)talents[i]);
}

// ===========================================================================
// ChooseProfession click resolution (0x52c693)
// ===========================================================================
TEST(GuiCharCreate, ProfessionClickIndex) {
    // Build the 8 button ids the grid creates: beruf[i] + 1349.
    int beruf[kProfessionCount] = {10, 22, 4, 7, 99, 1, 55, 30};
    int ids[kProfessionCount];
    for (int i = 0; i < kProfessionCount; ++i)
        ids[i] = Profession_ButtonGfx(beruf[i]); // reused from newgame_setup
    CHECK_EQ(ids[0], 10 + kProfessionGfxBase);
    CHECK_EQ(ChooseProfession_IndexForClick(ids, ids[3]), 3);
    CHECK_EQ(ChooseProfession_IndexForClick(ids, ids[7]), 7);
    CHECK_EQ(ChooseProfession_IndexForClick(ids, 1), -1);
    CHECK_EQ(ChooseProfession_IndexForClick(nullptr, ids[0]), -1);
}

// ===========================================================================
// Portrait/model cycling (0x52b6b8)
// ===========================================================================
TEST(GuiCharCreate, PreviewTablesRecovered) {
    CHECK_EQ(kPreviewMaleCount, 8);
    CHECK_EQ(kPreviewFemaleCount, 7);
    CHECK(std::string(kPreviewMaleNames[0]) == "patrizier_MANN2");
    CHECK(std::string(kPreviewMaleNames[7]) == "handwerker2_MANN");
    CHECK(std::string(kPreviewFemaleNames[0]) == "bauerin_FRAU");
    CHECK(std::string(kPreviewFemaleNames[6]) == "zigeunerin_FRAU");
    // Command tables (raw bytes from dword_527378 / dword_52749C).
    CHECK_EQ(kPreviewMaleCommands[0], 0x57);
    CHECK_EQ(kPreviewMaleCommands[4], 0x00);
    CHECK_EQ(kPreviewMaleCommands[7], 0x3b);
    CHECK_EQ(kPreviewFemaleCommands[0], 0x10);
    CHECK_EQ(kPreviewFemaleCommands[6], 0x74);
}

TEST(GuiCharCreate, PreviewModelCount) {
    CHECK_EQ(Preview_ModelCount(0), 8); // male
    CHECK_EQ(Preview_ModelCount(1), 7); // female
}

TEST(GuiCharCreate, PreviewStepAndWrap) {
    CHECK_EQ(Preview_StepIndex(3, kPreviewKeyNext), 4);
    CHECK_EQ(Preview_StepIndex(3, kPreviewKeyPrev), 2);
    CHECK_EQ(Preview_StepIndex(3, kPreviewKeyConfirm), 3); // confirm doesn't step
    // Wrap: in-range stays, overflow modulos, underflow snaps to last (count-1).
    CHECK_EQ(Preview_WrapIndex(0, 8), 0);
    CHECK_EQ(Preview_WrapIndex(7, 8), 7);
    CHECK_EQ(Preview_WrapIndex(8, 8), 0);   // 8 % 8 == 0
    CHECK_EQ(Preview_WrapIndex(9, 8), 1);
    CHECK_EQ(Preview_WrapIndex(-1, 8), 7);  // underflow snaps to last
    CHECK_EQ(Preview_WrapIndex(-1, 7), 6);
}

TEST(GuiCharCreate, PreviewModelNameAndCommand) {
    CHECK(std::string(Preview_ModelName(0, 0)) == "patrizier_MANN2");
    CHECK(std::string(Preview_ModelName(1, 6)) == "zigeunerin_FRAU");
    // command = table + 1468
    CHECK_EQ(Preview_ModelCommand(0, 0), 0x57 + kPreviewCommandBase);
    CHECK_EQ(Preview_ModelCommand(0, 7), 0x3b + kPreviewCommandBase);
    CHECK_EQ(Preview_ModelCommand(1, 6), 0x74 + kPreviewCommandBase);
    // out of range guards
    CHECK(std::string(Preview_ModelName(0, 99)) == "");
    CHECK_EQ(Preview_ModelCommand(0, 99), 0);
}

TEST(GuiCharCreate, CommitPortrait) {
    CharCreateParams p;
    CharCreate_CommitPortrait(p, /*gender=female*/1, /*idx*/2);
    CHECK_EQ(p.gender, 1);
    CHECK_EQ(p.portraitIndex, 2);
    CHECK(p.portraitModel == "buergerin3_FRAU");
    CHECK_EQ(p.startCommand, 0x25 + kPreviewCommandBase);
    CHECK(p.committed);
}

// ===========================================================================
// IntroVariant selection (0x52e4e0)
// ===========================================================================
TEST(GuiCharCreate, IntroVariantClick) {
    int childIds[kIntroVariantChildCount] = {100, 101, 102, 103, 104, 105};
    CHECK_EQ(IntroVariant_ByteForClick(childIds, 100, 9), 0);
    CHECK_EQ(IntroVariant_ByteForClick(childIds, 104, 9), 4);
    CHECK_EQ(IntroVariant_ByteForClick(childIds, 105, 9), 9); // sixth child -> unchanged
    CHECK_EQ(IntroVariant_ByteForClick(childIds, 999, 3), 3); // miss -> prev
    CHECK_EQ(IntroVariant_ByteForClick(nullptr, 100, 2), 2);
}
