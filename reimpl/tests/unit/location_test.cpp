// Unit tests for the batch-2 location interaction FSMs (gilde.exe
// VIBE_Location_*ContactLoop / *Start). Each contact loop is driven through the
// generic register/dispatch FSM (world/location.cpp): build the concrete menu,
// register it, simulate a click on a slot, and assert it resolves to the right
// dialog target. The Form/GameLogic/Text/Command helpers the originals call live
// in other modules and are NOT exercised here (the menus are pure data).
#include "test.h"
#include "world/location2.h"

using namespace guild::world;

namespace {

// Mock frame-loop click source: in the original, dword_631720 holds the slot id
// of the item clicked this frame (0 == nothing). This resolves a "click on item
// index i" to the dialog the FSM would dispatch, mirroring the else-if chain.
int clickDialog(const std::vector<ContactMenuItem>& menu, std::size_t itemIndex) {
    ContactRegistration reg = ContactRegisterMenu(menu);
    if (itemIndex >= reg.slotIds.size()) return -1;
    int slot = reg.slotIds[itemIndex];   // dword_631720 for this click
    return ContactDispatch(menu, reg, slot);
}

int target(LocationDialog d) { return static_cast<int>(d); }

} // namespace

// --- Church family ----------------------------------------------------------

TEST(LocationFsmChurch, ShopOpenOffersSermonAndBaptism) {
    auto m = ChurchContactMenu(/*shopOpen=*/true, /*otherCity=*/false, /*confRoom=*/false);
    CHECK_EQ(m.size(), (std::size_t)2);
    CHECK_EQ(m[0].menuString, std::string("contact_PREDIGT"));
    CHECK_EQ(m[1].menuString, std::string("ob_WEIHWASSERBECKEN"));
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::ChurchSermon));
    CHECK_EQ(clickDialog(m, 1), target(LocationDialog::ChurchBaptism));
}

TEST(LocationFsmChurch, ForeignPriestWithConfessionRoom) {
    auto m = ChurchContactMenu(/*shopOpen=*/false, /*otherCity=*/true, /*confRoom=*/true);
    CHECK_EQ(m.size(), (std::size_t)3);
    CHECK_EQ(m[0].menuString, std::string("ob_KIRCHE_BEICHTSTUHLRAUM"));
    CHECK_EQ(m[1].menuString, std::string("ob_SPENDENKAESTCHEN"));
    CHECK_EQ(m[2].menuString, std::string("ob_WEIHWASSERBECKEN"));
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::ChurchConfession));
    CHECK_EQ(clickDialog(m, 1), target(LocationDialog::ChurchDonation));
    CHECK_EQ(clickDialog(m, 2), target(LocationDialog::ChurchBaptism));
}

TEST(LocationFsmChurch, ForeignPriestNoConfessionRoomDropsItem) {
    auto m = ChurchContactMenu(false, true, false);
    CHECK_EQ(m.size(), (std::size_t)2);
    CHECK_EQ(m[0].menuString, std::string("ob_SPENDENKAESTCHEN"));
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::ChurchDonation));
}

TEST(LocationFsmChurch, GuildEligibilityGate) {
    CHECK_EQ(ChurchGuildContactMenu(false).size(), (std::size_t)0);
    auto m = ChurchGuildContactMenu(true);
    CHECK_EQ(m.size(), (std::size_t)1);
    CHECK_EQ(m[0].menuString, std::string("contact_ZUNFTLEVEL1"));
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::ChurchGuildLevel1));
}

TEST(LocationFsmChurch, ConfessionOnlyWhenShopClosed) {
    CHECK_EQ(ChurchConfessionContactMenu(/*shopOpen=*/true).size(), (std::size_t)0);
    auto m = ChurchConfessionContactMenu(/*shopOpen=*/false);
    CHECK_EQ(m.size(), (std::size_t)1);
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::ChurchConfession));
}

TEST(LocationFsmChurch, Top5AndIndulgence) {
    auto m = ChurchTop5ContactMenu();
    CHECK_EQ(m.size(), (std::size_t)2);
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::ChurchTop5));
    CHECK_EQ(clickDialog(m, 1), target(LocationDialog::ChurchIndulgence));
}

// --- Idle / Dungeon ---------------------------------------------------------

TEST(LocationFsmIdle, NoItemsEver) {
    CHECK_EQ(IdleContactMenu().size(), (std::size_t)0);
    // A click resolves to nothing.
    CHECK_EQ(clickDialog(IdleContactMenu(), 0), -1);
}

TEST(LocationFsmDungeon, PlanAlwaysOffered) {
    auto m = DungeonPlanContactMenu();
    CHECK_EQ(m.size(), (std::size_t)1);
    CHECK_EQ(m[0].menuString, std::string("ob_KERKERPLAN"));
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::DungeonPlan));
}

TEST(LocationFsmDungeon, BribeGatedByCityFlag) {
    CHECK_EQ(DungeonBribeContactMenu(false).size(), (std::size_t)0);
    auto m = DungeonBribeContactMenu(true);
    CHECK_EQ(m.size(), (std::size_t)1);
    CHECK_EQ(m[0].menuString, std::string("contact_BESTECHEN"));
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::DungeonBribe));
}

// --- Equipment storage / training ------------------------------------------

TEST(LocationFsmEquip, ShopAndBackRoomGroups) {
    auto m = EquipStorageContactMenu(/*shop=*/true, /*back=*/true);
    CHECK_EQ(m.size(), (std::size_t)4);
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::ThievesGuildEquipment)); // AUSRUESTEN
    CHECK_EQ(clickDialog(m, 1), target(LocationDialog::TrainingSelect));        // REGENERATION
    CHECK_EQ(clickDialog(m, 2), target(LocationDialog::Storage));               // LAGER
    CHECK_EQ(clickDialog(m, 3), target(LocationDialog::Transport));             // TRANSPORT
}

TEST(LocationFsmEquip, ShopOnlyOmitsBackRoom) {
    auto m = EquipStorageContactMenu(true, false);
    CHECK_EQ(m.size(), (std::size_t)2);
    CHECK_EQ(m[0].menuString, std::string("contact_AUSRUESTEN"));
    CHECK_EQ(m[1].menuString, std::string("contact_REGENERATION"));
}

TEST(LocationFsmTraining, TargetNightBothOpenTrainingSelect) {
    CHECK_EQ(TargetNightContactMenu(false).size(), (std::size_t)0);
    auto m = TargetNightContactMenu(true);
    CHECK_EQ(m.size(), (std::size_t)2);
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::TrainingSelect));
    CHECK_EQ(clickDialog(m, 1), target(LocationDialog::TrainingSelect));
}

// --- Guard loops ------------------------------------------------------------

TEST(LocationFsmGuard, PlainCustomsBox) {
    auto m = GuardContactMenu(/*shop=*/true, /*customsBox=*/327);
    CHECK_EQ(m.size(), (std::size_t)5);
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::GuardPatrolStart));
    CHECK_EQ(clickDialog(m, 1), target(LocationDialog::GuardCustomsStart));
    CHECK_EQ(clickDialog(m, 2), target(LocationDialog::StaffBook));
    CHECK_EQ(clickDialog(m, 3), target(LocationDialog::MasterCertificate));
    CHECK_EQ(m[4].menuString, std::string("ob_ZOLLKASSE"));
    CHECK_EQ(clickDialog(m, 4), target(LocationDialog::GuardDetainStart));
}

TEST(LocationFsmGuard, SecretCustomsBox) {
    auto m = GuardContactMenu(true, 328);
    CHECK_EQ(m.size(), (std::size_t)5);
    CHECK_EQ(m[4].menuString, std::string("ob_ZOLLKASSE_MIT_GEHEIMFACH"));
    CHECK_EQ(clickDialog(m, 4), target(LocationDialog::GuardDetainStart));
}

TEST(LocationFsmGuard, NoCustomsBoxAndShopClosed) {
    CHECK_EQ(GuardContactMenu(true, 0).size(), (std::size_t)4);
    CHECK_EQ(GuardContactMenu(false, 327).size(), (std::size_t)0);
}

TEST(LocationFsmGuard, RaidMenuOrder) {
    auto m = GuardRaidContactMenu(true);
    CHECK_EQ(m.size(), (std::size_t)6);
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::Storage));
    CHECK_EQ(clickDialog(m, 1), target(LocationDialog::ThievesGuildEquipment));
    CHECK_EQ(clickDialog(m, 2), target(LocationDialog::GuardRaidStart));
    CHECK_EQ(clickDialog(m, 3), target(LocationDialog::SpyBuildingStart));
    CHECK_EQ(clickDialog(m, 4), target(LocationDialog::InformationDialog));
    CHECK_EQ(clickDialog(m, 5), target(LocationDialog::Transport));
}

TEST(LocationFsmGuard, TargetTrainingThreeItems) {
    auto m = GuardTargetContactMenu(true);
    CHECK_EQ(m.size(), (std::size_t)3);
    for (std::size_t i = 0; i < m.size(); ++i)
        CHECK_EQ(clickDialog(m, i), target(LocationDialog::TrainingSelect));
}

// --- Thief prison -----------------------------------------------------------

TEST(LocationFsmPrison, KidnapRansomAlwaysWhenShopOpen) {
    auto m = ThiefPrisonContactMenu(/*shop=*/true, /*ours=*/false, /*breakout=*/false);
    CHECK_EQ(m.size(), (std::size_t)2);
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::ThiefKidnapStart));
    CHECK_EQ(clickDialog(m, 1), target(LocationDialog::ThiefRansom));
}

TEST(LocationFsmPrison, BreakoutNeedsOwnPrisonerAndFlag) {
    // Both conditions required.
    CHECK_EQ(ThiefPrisonContactMenu(true, true, false).size(), (std::size_t)2);
    CHECK_EQ(ThiefPrisonContactMenu(true, false, true).size(), (std::size_t)2);
    auto m = ThiefPrisonContactMenu(true, true, true);
    CHECK_EQ(m.size(), (std::size_t)3);
    CHECK_EQ(m[2].menuString, std::string("contact_AUSBRECHEN"));
    CHECK_EQ(clickDialog(m, 2), target(LocationDialog::ThiefBreakout));
}

TEST(LocationFsmPrison, BreakoutOnlyWhenShopClosed) {
    // Breakout item is independent of shopOpen (gated by hostage state only).
    auto m = ThiefPrisonContactMenu(false, true, true);
    CHECK_EQ(m.size(), (std::size_t)1);
    CHECK_EQ(clickDialog(m, 0), target(LocationDialog::ThiefBreakout));
}

// --- Guard start gates ------------------------------------------------------

TEST(LocationFsmGuardStart, ActiveCharFlagSuppresses) {
    CHECK(!GuardPatrolStart(true).opened);
    CHECK(!GuardRaidStart(true).opened);
    CHECK(!GuardCustomsStart(true).opened);
    CHECK(!GuardDetainStart(true).opened);
}

TEST(LocationFsmGuardStart, PatrolConfig) {
    auto r = GuardPatrolStart(false);
    CHECK(r.opened);
    CHECK_EQ(r.action.dispatchMode, 1);
    CHECK(r.action.dialog == LocationDialog::GuardPatrolStart);
    CHECK(r.action.hasConfigBlob);
    CHECK_EQ(r.action.configFlag, 1024);
}

TEST(LocationFsmGuardStart, RaidConfig) {
    auto r = GuardRaidStart(false);
    CHECK(r.opened);
    CHECK_EQ(r.action.dispatchMode, 1);
    CHECK(r.action.dialog == LocationDialog::GuardRaidStart);
    CHECK_EQ(r.action.configFlag, 1024);
}

TEST(LocationFsmGuardStart, CustomsHasNoConfigBlobAndMode4) {
    auto r = GuardCustomsStart(false);
    CHECK(r.opened);
    CHECK_EQ(r.action.dispatchMode, 4);
    CHECK(r.action.dialog == LocationDialog::GuardCustomsStart);
    CHECK(!r.action.hasConfigBlob);
}

TEST(LocationFsmGuardStart, DetainConfig) {
    auto r = GuardDetainStart(false);
    CHECK(r.opened);
    CHECK_EQ(r.action.dispatchMode, 1);
    CHECK(r.action.dialog == LocationDialog::GuardDetainStart);
    CHECK(r.action.hasConfigBlob);
    CHECK_EQ(r.action.configFlag, 1);
}
