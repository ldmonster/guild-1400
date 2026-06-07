// Integration tests: the char-creation flow wired against its REAL sibling modules
// (gui/newgame_setup geometry + start-command constant, gui/main_menu session flags).
// Verifies the ChooseProfession click path lands on the SAME grid the newgame_setup
// geometry builds, and that a full Talent->Profession->Portrait commit threads through
// the shared NewGameParams/CharCreateParams structs consistently.
#include "gui/charcreate.h"
#include "gui/newgame_setup.h"
#include "gui/main_menu.h"
#include "test.h"

using namespace guild::gui;

// The CHOOSEPROFESSION grid built by the REAL newgame_setup geometry; a click resolved by
// charcreate's id scan must map back to the same button index and beruf byte.
TEST(GuiCharCreateItest, ProfessionGridRoundTrip) {
    const int beruf[kProfessionCount] = {0, 5, 11, 23, 41, 59, 85, 107};
    int ids[kProfessionCount];
    for (int i = 0; i < kProfessionCount; ++i) {
        // Geometry from the real newgame_setup module.
        int x = Profession_ButtonX(i);
        int y = Profession_ButtonY(i);
        ids[i] = Profession_ButtonGfx(beruf[i]);
        CHECK_EQ(x, kProfessionCellW * (i % kProfessionColumns) + kProfessionOriginX);
        CHECK_EQ(y, kProfessionCellH * (i / kProfessionColumns) + kProfessionOriginY);
        CHECK_EQ(ids[i], beruf[i] + kProfessionGfxBase);
    }
    // A click on each button id resolves to that button's index, and id-1349 recovers the
    // beruf byte (matches newgame_setup's ChooseProfession_ByteForWidgetId).
    for (int i = 0; i < kProfessionCount; ++i) {
        CHECK_EQ(ChooseProfession_IndexForClick(ids, ids[i]), i);
        CHECK_EQ(ChooseProfession_ByteForWidgetId(ids[i], beruf), beruf[i]);
    }
}

// A full dynasty fill produces a complete 6-slot table that the downstream Talent commit
// can consume; the two PARENT slots (4 paternal male, 5 maternal female) feed the
// profession-byte maps.
TEST(GuiCharCreateItest, DynastyToTalentToParams) {
    DynastyTable t;
    int fill = 0;
    // Drive the canonical click order: male,female alternating to fill 0..5.
    ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kBuergerMann, &fill);      // slot0 code0
    ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kZigeunerinFrau, &fill);   // slot1 code2
    ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kHandwerkerMann, &fill);   // slot2 code1
    ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kBuergerinFrau, &fill);    // slot3 code0
    ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kOffizierMann, &fill);     // slot4 code3 (father)
    ChooseCharacter_ApplyActorClick(t, ChooseCharActor::kPriesterinFrau, &fill);   // slot5 code4 (mother)
    CHECK(ChooseCharacter_IsComplete(fill));

    CharCreateParams p;
    p.dynasty = t;
    std::uint8_t talents[kTalentCount] = {1, 2, 3, 4, 5};
    // Parent profession codes come from slots 4/5.
    CharCreate_CommitTalents(p, p.dynasty.slot[4], p.dynasty.slot[5], talents);
    CHECK_EQ(p.paternalProfessionByte, Talent_PaternalProfessionByte(3, 0)); // 55
    CHECK_EQ(p.maternalProfessionByte, Talent_MaternalProfessionByte(4, 0)); // 73
    CHECK_EQ(p.paternalProfessionByte, 55);
    CHECK_EQ(p.maternalProfessionByte, 73);
}

// The portrait commit arms a start command in the same dword_122F528 register the
// newgame_setup profession commit (kStartCommandValue) writes to.  Both are well-formed
// command ids derived from the recovered tables.  (Note: male model 0's command,
// 0x57+1468, happens to equal the 1555 profession sentinel — a real coincidence in the
// shipped data, so we don't assert they differ.)
TEST(GuiCharCreateItest, PortraitCommandWellFormed) {
    CharCreateParams p;
    CharCreate_CommitPortrait(p, /*male*/0, /*idx*/1);
    CHECK_EQ(p.startCommand, kPreviewMaleCommands[1] + kPreviewCommandBase);
    CHECK(p.startCommand >= kPreviewCommandBase);
    CHECK_EQ(kStartCommandValue, 1555);
    // And male model 0 indeed coincides with the profession sentinel (documents the quirk).
    CharCreateParams p0;
    CharCreate_CommitPortrait(p0, /*male*/0, /*idx*/0);
    CHECK_EQ(p0.startCommand, kStartCommandValue);
    CHECK(p.committed);
}

// IntroVariant feeds the same dynasty/session pipeline; a valid pick advances the variant,
// the unmapped sixth child and the session-flag constants stay consistent.
TEST(GuiCharCreateItest, IntroVariantWithSessionFlags) {
    int childIds[kIntroVariantChildCount] = {10, 11, 12, 13, 14, 15};
    int variant = 0;
    variant = IntroVariant_ByteForClick(childIds, 13, variant);
    CHECK_EQ(variant, 3);
    variant = IntroVariant_ByteForClick(childIds, 15, variant); // sixth child no-op
    CHECK_EQ(variant, 3);
    // The new-game session this character feeds carries the history+newgame flags.
    CHECK_EQ(kSessionNewGame | kSessionHistory, 0x0009);
}
