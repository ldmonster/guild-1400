// tests/unit/play_input_command_test.cpp — UNIT: the click->order CLASSIFICATION
// golden for guild::play::ClassifyOrder.
//
// Pins the (picked object CLASS + cursor MODE) -> order-KIND mapping 1:1 against
// VIBE_Command_IssueOnObject @0x488ff0's branch ladder:
//   disarmed                         -> none  (router returns 0)
//   no object under cursor           -> ground move (kind 1)
//   battle UNIT + attack allowed     -> attack       (kind 2)
//   battle UNIT, attack NOT allowed  -> none
//   object + "sp_ESCAPE" (kMove)     -> move          (kind 3)
//   object + "sp_CONQUER" (kLabeled) -> labelled move (kind 4)
//   object + "WARE" (kConquer)       -> conquer       (kind 6)
//   object + attack-move cursor      -> none (no label match on a non-unit)
#include "test.h"

#include "play/input_command.h"

using namespace guild;
using play::CursorMode;
using play::PickedClass;

// --- disarmed cursor never issues, regardless of pick ----------------------
TEST(PlayInputCommandUnit, DisarmedIssuesNothing) {
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kNone,   CursorMode::kDisarmed, false),
             (int)play::kKindNone);
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kUnit,   CursorMode::kDisarmed, true),
             (int)play::kKindNone);
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kObject, CursorMode::kDisarmed, false),
             (int)play::kKindNone);
}

// --- empty space -> cursor-raycast ground move (kind 1) --------------------
TEST(PlayInputCommandUnit, EmptySpaceGroundMove) {
    // Any armed mode with nothing under the cursor takes the ground-raycast branch.
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kNone, CursorMode::kAttackMove, false),
             (int)play::kKindGround);
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kNone, CursorMode::kMove, false),
             (int)play::kKindGround);
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kNone, CursorMode::kConquer, false),
             (int)play::kKindGround);
}

// --- battle UNIT under cursor -> attack iff allowed ------------------------
TEST(PlayInputCommandUnit, UnitAttackGate) {
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kUnit, CursorMode::kAttackMove, true),
             (int)play::kKindAttack);
    // Different mode but still a unit: attack still gates on attackAllowed.
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kUnit, CursorMode::kMove, true),
             (int)play::kKindAttack);
    // Not allowed (same team, no friendly-fire override) -> nothing issued.
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kUnit, CursorMode::kAttackMove, false),
             (int)play::kKindNone);
}

// --- non-unit object -> the selected label decides the order kind ----------
TEST(PlayInputCommandUnit, ObjectLabelDispatch) {
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kObject, CursorMode::kMove, false),
             (int)play::kKindMove);       // sp_ESCAPE
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kObject, CursorMode::kLabeled, false),
             (int)play::kKindLabeled);    // sp_CONQUER
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kObject, CursorMode::kConquer, false),
             (int)play::kKindConquer);    // WARE
    // Armed attack-move cursor on a non-unit object: no label match -> nothing.
    CHECK_EQ((int)play::ClassifyOrder(PickedClass::kObject, CursorMode::kAttackMove, false),
             (int)play::kKindNone);
}

// --- the kind byte VALUES are the recovered staging +4 kinds ---------------
TEST(PlayInputCommandUnit, KindByteValuesMatchStaging) {
    CHECK_EQ((int)play::kKindGround,  1);
    CHECK_EQ((int)play::kKindAttack,  2);
    CHECK_EQ((int)play::kKindMove,    3);
    CHECK_EQ((int)play::kKindLabeled, 4);
    CHECK_EQ((int)play::kKindConquer, 6);
}
