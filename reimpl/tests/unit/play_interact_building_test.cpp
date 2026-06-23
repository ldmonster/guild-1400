// tests/unit/play_interact_building_test.cpp — UNIT: the building click->action
// classification golden. (building KIND code, menu item) -> the building Command
// (opcode + field + delta) and the contact-menu action GROUP the dialog opens.
// Mirrors VIBE_Building_EnterAndDispatch's kind-switch + the opcode-26
// QueueRequestArgs26 building-field delta. Pure; no entity/queue setup.
#include "test.h"

#include "play/interact_building.h"

using namespace guild;
using namespace guild::play;

// --- golden: pin the QueueRequestArgs26 (@0x494848) opcode-26 building-field-delta
// packet-staging byte offsets. Recovered constants traced to interact_building.h. ---
TEST(PlayInteractBuildingUnit, QueueRequestArgs26OffsetsGolden) {
    CHECK_EQ((int)kBuildingCmdOpcode, 26);  // 0x1A
    CHECK_EQ((int)kCmdBuildingIdOff, 0x10); // +16 building id
    CHECK_EQ((int)kCmdFieldOff,      0x14); // +20 field selector
    CHECK_EQ((int)kCmdValueOff,      0x18); // +24 delta value
}

// --- kind -> contact-menu action group (EnterAndDispatch's switch(*v21)). -------
TEST(PlayInteractBuildingUnit, KindToActionGroupGolden) {
    CHECK(BuildingDialogKindToActionGroup(19)  == BuildingActionGroup::kGuildMaster);
    CHECK(BuildingDialogKindToActionGroup(20)  == BuildingActionGroup::kSabotage);
    CHECK(BuildingDialogKindToActionGroup(21)  == BuildingActionGroup::kThreat);
    CHECK(BuildingDialogKindToActionGroup(22)  == BuildingActionGroup::kWineCellar);
    CHECK(BuildingDialogKindToActionGroup(24)  == BuildingActionGroup::kMistress);
    CHECK(BuildingDialogKindToActionGroup(116) == BuildingActionGroup::kSmith);
    CHECK(BuildingDialogKindToActionGroup(133) == BuildingActionGroup::kCarpenter);
    CHECK(BuildingDialogKindToActionGroup(155) == BuildingActionGroup::kStonemason);
    CHECK(BuildingDialogKindToActionGroup(247) == BuildingActionGroup::kProduction);
    CHECK(BuildingDialogKindToActionGroup(276) == BuildingActionGroup::kTreasury);
    // Unmapped codes fall to the default (plain frame-loop) arm.
    CHECK(BuildingDialogKindToActionGroup(0)    == BuildingActionGroup::kNone);
    CHECK(BuildingDialogKindToActionGroup(99)   == BuildingActionGroup::kNone);
    CHECK(BuildingDialogKindToActionGroup(23)   == BuildingActionGroup::kNone);
}

// --- (kind, menu item) -> building Command (the golden). -----------------------
TEST(PlayInteractBuildingUnit, ClassifyMutatingActionsOnProductionDialog) {
    // A production hub (carpenter, kind 133) exposes price/stock/upgrade verbs.
    BuildingCommand raise = ClassifyBuildingAction(133, BuildingMenuItem::kRaisePrice);
    CHECK(raise.issued);
    CHECK_EQ((int)raise.opcode, 26);
    CHECK_EQ(raise.field, (i32)kFieldPrice);   // 124
    CHECK_EQ(raise.delta, 1);
    CHECK(raise.group == BuildingActionGroup::kCarpenter);

    BuildingCommand lower = ClassifyBuildingAction(133, BuildingMenuItem::kLowerPrice);
    CHECK(lower.issued);
    CHECK_EQ(lower.field, (i32)kFieldPrice);
    CHECK_EQ(lower.delta, -1);

    BuildingCommand restock = ClassifyBuildingAction(116, BuildingMenuItem::kRestock);
    CHECK(restock.issued);
    CHECK_EQ(restock.field, (i32)kFieldStock); // 28
    CHECK_EQ(restock.delta, 1);
    CHECK(restock.group == BuildingActionGroup::kSmith);

    BuildingCommand sell = ClassifyBuildingAction(155, BuildingMenuItem::kSell);
    CHECK(sell.issued);
    CHECK_EQ(sell.field, (i32)kFieldStock);
    CHECK_EQ(sell.delta, -1);

    BuildingCommand upgrade = ClassifyBuildingAction(247, BuildingMenuItem::kUpgrade);
    CHECK(upgrade.issued);
    CHECK_EQ(upgrade.field, (i32)kFieldUpgrade); // 89
    CHECK_EQ(upgrade.delta, 1);
    CHECK(upgrade.group == BuildingActionGroup::kProduction);

    // Wine cellar + treasury also own building verbs.
    CHECK(ClassifyBuildingAction(22,  BuildingMenuItem::kRestock).issued);
    CHECK(ClassifyBuildingAction(276, BuildingMenuItem::kRaisePrice).issued);
}

TEST(PlayInteractBuildingUnit, ClassifyNonMutatingAndSocialDialogs) {
    // kNone item -> never issues.
    BuildingCommand none = ClassifyBuildingAction(133, BuildingMenuItem::kNone);
    CHECK(!none.issued);
    CHECK_EQ((int)none.opcode, 0);

    // Social dialogs (guild-master/sabotage/threat/mistress) dispatch non-building
    // verbs; a mutating item there issues NO building command.
    CHECK(!ClassifyBuildingAction(19, BuildingMenuItem::kRaisePrice).issued);
    CHECK(!ClassifyBuildingAction(20, BuildingMenuItem::kSell).issued);
    CHECK(!ClassifyBuildingAction(21, BuildingMenuItem::kUpgrade).issued);
    CHECK(!ClassifyBuildingAction(24, BuildingMenuItem::kRestock).issued);

    // An unmapped kind (default arm, no contact menu) also issues nothing.
    CHECK(!ClassifyBuildingAction(0, BuildingMenuItem::kRaisePrice).issued);

    // Even on a social dialog the GROUP is still classified.
    BuildingCommand gm = ClassifyBuildingAction(19, BuildingMenuItem::kRaisePrice);
    CHECK(gm.group == BuildingActionGroup::kGuildMaster);
}

// --- the dialog FSM lifecycle (Open gate -> ContactMenu -> Action). ------------
TEST(PlayInteractBuildingUnit, DialogFsmLifecycle) {
    SetBuildingDialogHooks(nullptr);   // inert defaults: entry allowed, inert interior

    BuildingDialogFsm fsm;
    CHECK(fsm.state() == BuildingDialogState::kClosed);

    // Open a carpenter (kind 133): entry allowed by default -> contact menu open.
    bool opened = fsm.Open(/*buildingId=*/7, /*kind=*/133);
    CHECK(opened);
    CHECK(fsm.state() == BuildingDialogState::kContactMenu);
    CHECK(fsm.group() == BuildingActionGroup::kCarpenter);
    CHECK_EQ(fsm.buildingId(), 7);

    // Choose raise-price -> Action state + the classified command.
    BuildingCommand cmd = fsm.ChooseMenuItem(BuildingMenuItem::kRaisePrice);
    CHECK(fsm.state() == BuildingDialogState::kAction);
    CHECK(cmd.issued);
    CHECK_EQ(cmd.field, (i32)kFieldPrice);

    fsm.Close();
    CHECK(fsm.state() == BuildingDialogState::kClosed);
}

TEST(PlayInteractBuildingUnit, DialogFsmEntryDenied) {
    BuildingDialogHooks hooks;
    hooks.checkEntryAllowed = [](i32, int) { return false; };  // gate shut
    SetBuildingDialogHooks(&hooks);

    BuildingDialogFsm fsm;
    bool opened = fsm.Open(/*buildingId=*/3, /*kind=*/133);
    CHECK(!opened);
    CHECK(fsm.state() == BuildingDialogState::kEntryDenied);
    // Choosing an item on a denied dialog issues nothing.
    BuildingCommand cmd = fsm.ChooseMenuItem(BuildingMenuItem::kRaisePrice);
    CHECK(!cmd.issued);

    SetBuildingDialogHooks(nullptr);
}
