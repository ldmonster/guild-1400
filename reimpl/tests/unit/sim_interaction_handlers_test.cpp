// Unit tests for the interaction RULES cores (interaction_handlers.{h,cpp}).
// Each test drives a translated Eval/ContextAction/Perform handler with synthetic
// actor/event state and checks the accept/reject verdict, the eligibility/range/
// state gates, and the (mock) command/leaf emitted, against a hand-computed
// reference derived from the IDA decompilation.
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>

#include "sim/interaction_handlers.h"
#include "sim/entity.h"

using namespace guild;
using namespace guild::sim;

namespace {

ContextActor MakeActor() {
    ContextActor a;
    std::memset(&a, 0, sizeof(a));
    return a;
}
InteractionEventRec MakeEvent(u8 mode) {
    InteractionEventRec ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.mode = mode;
    return ev;
}

} // namespace

// --- layout / static checks -------------------------------------------------
TEST(SimIntHandlers, RecordStrides) {
    CHECK_EQ(sizeof(ContextActor), (size_t)kContextActorStride);
    // recovered byte offsets the binary addresses by raw index
    ContextActor a; (void)a;
    CHECK_EQ((size_t)((char*)&a.kind - (char*)&a), (size_t)2);
    CHECK_EQ((size_t)((char*)&a.gender - (char*)&a), (size_t)9);
    CHECK_EQ((size_t)((char*)&a.rank - (char*)&a), (size_t)13);
    CHECK_EQ((size_t)((char*)&a.assocPersonId - (char*)&a), (size_t)92);
    CHECK_EQ((size_t)((char*)&a.profession - (char*)&a), (size_t)358);
    CHECK_EQ((size_t)((char*)&a.profession2 - (char*)&a), (size_t)361);
    CHECK_EQ((size_t)((char*)&a.flag456 - (char*)&a), (size_t)456);
    CHECK_EQ((size_t)((char*)&a.flag457 - (char*)&a), (size_t)457);
    CHECK_EQ((size_t)((char*)&a.flag458 - (char*)&a), (size_t)458);
    // The InteractionEventRec is a host runtime model (NOT byte-packed: the
    // drag-source slot is a raw pointer in the binary that can't round-trip a
    // 64-bit pointer through the original 32-bit field). We only assert the fields
    // exist and are independently addressable; original offsets are in the header.
    InteractionEventRec ev; (void)ev;
    ev.targetId = 7; ev.param = 9;
    CHECK_EQ(ev.targetId, 7);
    CHECK_EQ(ev.param, 9);
}

// --- chain-style evaluators -------------------------------------------------
TEST(SimIntHandlers, ChainEvaluators) {
    // EvalReturnEight: result!=0 → 0; result==0 & !armed → 8; armed → keeps result(0).
    CHECK_EQ((int)EvalReturnEight(0, 0, 0), 8);
    CHECK_EQ((int)EvalReturnEight(0, 1, 0), 0);
    CHECK_EQ((int)EvalReturnEight(5, 0, 0), 0);
    // EvalReturnBoolNotArmed: result==0 → (armed==0)
    CHECK_EQ((int)EvalReturnBoolNotArmed(0, 0, 0), 1);
    CHECK_EQ((int)EvalReturnBoolNotArmed(0, 7, 0), 0);
    CHECK_EQ((int)EvalReturnBoolNotArmed(3, 0, 0), 0);
    CHECK_EQ((int)EvalReturnTwo(0, 0, 0), 2);
    CHECK_EQ((int)EvalReturnTwo(0, 1, 0), 0);
    CHECK_EQ((int)EvalReturnThree(0, 0, 0), 3);
    CHECK_EQ((int)EvalReturnThree(1, 0, 0), 0);
    CHECK_EQ((int)EvalRejectStub(), 0);
    CHECK_EQ((int)EvalRestStub(), 26);
}

TEST(SimIntHandlers, EvalActionCodes) {
    auto ev = MakeEvent(1);
    CHECK_EQ((int)EvalActionCode35(&ev, 3, 0), 35);
    CHECK_EQ((int)EvalActionCode35(&ev, 4, 0), 0);   // wrong armed
    CHECK_EQ((int)EvalActionCode36(&ev, 4, 0), 36);
    CHECK_EQ((int)EvalActionCode36(&ev, 3, 0), 0);
    ev.mode = 2;
    CHECK_EQ((int)EvalActionCode35(&ev, 3, 0), 0);   // wrong mode
}

// --- EvalUseDoor: range/state/flag/cash gates -------------------------------
static int s_doorCash = 0;
static int DoorCashHook(ContextActor*, u8) { return s_doorCash; }

TEST(SimIntHandlers, EvalUseDoorGates) {
    SetCurrencyHook(DoorCashHook);
    auto a = MakeActor();
    a.rank = 1;          // < 2 ok
    a.flag457 = 0x40;    // door-enabled bit set
    s_doorCash = 40000;  // 40000 * 0.44 = 17600 >= 16000 → pass

    auto ev = MakeEvent(0);
    char r = EvalUseDoor(&a, &ev, 0, 0);
    CHECK_EQ((int)r, 25);
    CHECK_EQ((int)ev.mode, 13);
    CHECK_EQ(ev.targetId, 2);
    CHECK_EQ(ev.param, 1);

    // armed > 1 → reject
    ev = MakeEvent(0);
    CHECK_EQ((int)EvalUseDoor(&a, &ev, 2, 0), 0);

    // rank >= 2 → reject
    ev = MakeEvent(0);
    a.rank = 2;
    CHECK_EQ((int)EvalUseDoor(&a, &ev, 0, 0), 0);
    a.rank = 1;

    // flag bit clear → reject
    ev = MakeEvent(0);
    a.flag457 = 0;
    CHECK_EQ((int)EvalUseDoor(&a, &ev, 0, 0), 0);
    a.flag457 = 0x40;

    // cash gate: 36363 * 0.44 = 15999.7 < 16000 → reject
    ev = MakeEvent(0);
    s_doorCash = 36363;
    CHECK_EQ((int)EvalUseDoor(&a, &ev, 0, 0), 0);
    // 36364 * 0.44 = 16000.16 >= 16000 → pass
    ev = MakeEvent(0);
    s_doorCash = 36364;
    CHECK_EQ((int)EvalUseDoor(&a, &ev, 0, 0), 25);

    // negative cash → reject
    ev = MakeEvent(0);
    s_doorCash = -1;
    CHECK_EQ((int)EvalUseDoor(&a, &ev, 0, 0), 0);

    // armed==1 requires the event to already be a door event (mode 13, target 2)
    ev = MakeEvent(0);  // not yet a door event
    s_doorCash = 40000;
    CHECK_EQ((int)EvalUseDoor(&a, &ev, 1, 0), 0);
    ev = MakeEvent(13); ev.targetId = 2;
    CHECK_EQ((int)EvalUseDoor(&a, &ev, 1, 0), 25);

    SetCurrencyHook(nullptr);
}

// --- ContextAction: single-target shape, mode gating -----------------------
TEST(SimIntHandlers, ContextModeGating) {
    auto a = MakeActor();
    auto ev = MakeEvent(0);   // mode 0 is neither 3 nor 1
    CHECK_EQ((int)ContextPromoteRank(&a, &ev), 1);
    CHECK_EQ((int)ContextOpenInventory(&a, &ev), 1);
    CHECK_EQ((int)ContextToggleFollow(&a, &ev), 1);
}

// --- ContextPromoteRank: rank<3 promotable; rank>=3 → 4 (next tier) ---------
TEST(SimIntHandlers, ContextPromoteRank) {
    ResetInteractionLeafTrace();
    auto a = MakeActor();
    a.kind = 6;
    a.rank = 1;
    auto ev = MakeEvent(kModeActivate);
    CHECK_EQ((int)ContextPromoteRank(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.privilegeLeaf, (int)kPrivShowDialog);

    a.rank = 3;
    ev = MakeEvent(kModeActivate);
    CHECK_EQ((int)ContextPromoteRank(&a, &ev), 4);  // rank too high → 4

    // tooltip path
    a.rank = 0;
    ev = MakeEvent(kModeTooltip);
    ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextPromoteRank(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.tooltipStringId, 0x1933);
}

// --- ContextOpenInventory: kind 5/6 gate ------------------------------------
TEST(SimIntHandlers, ContextOpenInventoryKind) {
    auto a = MakeActor();
    auto ev = MakeEvent(kModeActivate);
    a.kind = 4;
    CHECK_EQ((int)ContextOpenInventory(&a, &ev), 1);  // not 5/6
    a.kind = 5;
    ev = MakeEvent(kModeActivate);
    // kind 5 + mode 3: the (mode3 && kind==6) panel branch is skipped; falls to
    // (mode!=1||kind!=6) → return 2.
    CHECK_EQ((int)ContextOpenInventory(&a, &ev), 2);
    a.kind = 6;
    ev = MakeEvent(kModeActivate);
    ResetInteractionLeafTrace();
    CHECK_EQ((int)(ContextOpenInventory(&a, &ev) & 2), 2);
    CHECK_EQ(g_leafTrace.privilegeLeaf, (int)kPrivChangeProfession);
}

// --- ContextToggleFollow: flag/score gate -----------------------------------
TEST(SimIntHandlers, ContextToggleFollow) {
    auto a = MakeActor();
    auto ev = MakeEvent(kModeActivate);
    // default: flag456=0, score36=0 (>= -20.0), statusBits44=0 → blocked
    CHECK_EQ((int)ContextToggleFollow(&a, &ev), 1);
    // score below -20 unblocks (and statusBits high nibble zero is irrelevant then)
    a.score36 = -21;
    ev = MakeEvent(kModeActivate);
    ResetInteractionLeafTrace();
    ContextToggleFollow(&a, &ev);
    CHECK_EQ(g_leafTrace.privilegeLeaf, (int)kPrivMedicus);
    // high status nibble set unblocks too; on mode 3 the verdict is the leaf result
    // (default hook 0), and the medicus leaf fires.
    a.score36 = 0; a.statusBits44 = 0x10;
    ev = MakeEvent(kModeActivate);
    ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextToggleFollow(&a, &ev), 0);
    CHECK_EQ(g_leafTrace.privilegeLeaf, (int)kPrivMedicus);
    // mode 1 (tooltip) on an unblocked actor returns 2 and records the tooltip.
    ev = MakeEvent(kModeTooltip); a.kind = 6;
    ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextToggleFollow(&a, &ev), 2);
    CHECK_EQ(g_leafTrace.tooltipStringId, 0x193A);
    a.kind = 0;
    // flag456 0x40 always blocks
    a.flag456 = 0x40; a.score36 = -100; a.statusBits44 = 0xFF;
    ev = MakeEvent(kModeActivate);
    CHECK_EQ((int)ContextToggleFollow(&a, &ev), 1);
}

// --- Train tiers: exact vs >= rank gating ----------------------------------
TEST(SimIntHandlers, ContextTrainTiers) {
    auto a = MakeActor();
    a.kind = 6;
    // TrainRank3A: exact 3.  rank 2 → 1, rank 3 → 2, rank 4 → 4.
    a.rank = 2; { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextTrainRank3A(&a,&ev),1); }
    a.rank = 3; { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextTrainRank3A(&a,&ev),2); }
    a.rank = 4; { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextTrainRank3A(&a,&ev),4); }
    // TrainRank4A: exact 4.
    a.rank = 3; { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextTrainRank4A(&a,&ev),1); }
    a.rank = 4; { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextTrainRank4A(&a,&ev),2); }
    a.rank = 5; { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextTrainRank4A(&a,&ev),4); }
    // TrainRank5A: >= 5.
    a.rank = 4; { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextTrainRank5A(&a,&ev),1); }
    a.rank = 5; { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextTrainRank5A(&a,&ev),2); }
    a.rank = 9; { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextTrainRank5A(&a,&ev),2); }
}

// --- DemoteIfRank3 / EquipIfRank3 -------------------------------------------
TEST(SimIntHandlers, ContextDemoteEquip) {
    auto a = MakeActor();
    a.rank = 2;
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextDemoteIfRank3(&a,&ev),1); }
    a.rank = 3;
    { auto ev = MakeEvent(kModeActivate); ResetInteractionLeafTrace();
      CHECK_EQ((int)ContextDemoteIfRank3(&a,&ev),2);
      CHECK_EQ(g_leafTrace.privilegeLeaf,(int)kPrivSendBuildCmd); }
    // EquipIfRank3: rank>=3 && !(457&4)
    a.rank = 3; a.flag457 = 4;
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextEquipIfRank3(&a,&ev),1); }
    a.flag457 = 0;
    { auto ev = MakeEvent(kModeActivate); ResetInteractionLeafTrace();
      CHECK_EQ((int)ContextEquipIfRank3(&a,&ev),2);
      CHECK_EQ(g_leafTrace.privilegeLeaf,(int)kPrivSendSimpleCmd); }
}

// --- Profession-menu gates --------------------------------------------------
TEST(SimIntHandlers, ContextProfessionMenus) {
    auto a = MakeActor();
    a.kind = 6;
    a.profession = 11;
    { auto ev = MakeEvent(kModeActivate); ResetInteractionLeafTrace();
      CHECK_EQ((int)ContextProfessionMenu11(&a,&ev),2);
      CHECK_EQ(g_leafTrace.privilegeLeaf,(int)kPrivShowDialog); }
    a.profession = 12;
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextProfessionMenu11(&a,&ev),1); }
    // Guard accepts 27/15/21/13
    for (u8 p : {27,15,21,13}) {
        a.profession = p; auto ev = MakeEvent(kModeActivate);
        CHECK_EQ((int)ContextProfessionMenuGuard(&a,&ev),2);
    }
    a.profession = 14;
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextProfessionMenuGuard(&a,&ev),1); }
}

// --- Command-by-profession + drag-drop --------------------------------------
TEST(SimIntHandlers, ContextCommandByProfessionActivate) {
    auto a = MakeActor();
    a.kind = 6;
    a.profession = 22;   // AttackOrSteal accepts 22/25/18
    { auto ev = MakeEvent(kModeActivate); ResetInteractionLeafTrace();
      CHECK_EQ((int)ContextAttackOrSteal(&a,&ev),2);
      CHECK_EQ(g_leafTrace.privilegeLeaf,(int)kPrivCharmConfirm);
      CHECK_EQ(g_leafTrace.tooltipStringId,0x19AD); }   // kind6 always renders
    // busy bit → 9
    a.flag457 = 2;
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextAttackOrSteal(&a,&ev),9); }
    a.flag457 = 0;
    // wrong profession → 1
    a.profession = 5;
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextAttackOrSteal(&a,&ev),1); }
}

TEST(SimIntHandlers, ContextCommandByProfessionDrag) {
    auto a = MakeActor();
    a.flag457 = 1;   // selectable-as-drag-target
    ContextActor src = MakeActor();
    src.profession = 25;   // valid attack target prof
    auto ev = MakeEvent(kModeDragApply);  // mode 4
    ev.dragSource = &src;
    ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextAttackOrSteal(&a,&ev),10);
    CHECK_EQ(g_leafTrace.privilegeLeaf,(int)kPrivCharmConfirm);
    // flag457&1 clear → reject even on drag
    a.flag457 = 0;
    ev = MakeEvent(kModeDragApply); ev.dragSource = &src;
    CHECK_EQ((int)ContextAttackOrSteal(&a,&ev),1);
    // null drag source → reject
    a.flag457 = 1;
    ev = MakeEvent(kModeDragApply); ev.dragSource = 0;
    CHECK_EQ((int)ContextAttackOrSteal(&a,&ev),1);
    // src wrong profession → reject
    src.profession = 5;
    ev = MakeEvent(kModeDragApply); ev.dragSource = &src;
    CHECK_EQ((int)ContextAttackOrSteal(&a,&ev),1);
}

// --- StartWorkTask / StartGuildTask extra flag gates ------------------------
TEST(SimIntHandlers, ContextStartWorkTask) {
    auto a = MakeActor();
    a.kind = 6;
    a.profession = 7;
    // 458 & 0x20 blocks
    a.flag458 = 0x20;
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextStartWorkTask(&a,&ev),1); }
    a.flag458 = 0;
    // busy 457&2 → 9
    a.flag457 = 2;
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextStartWorkTask(&a,&ev),9); }
    a.flag457 = 0;
    // profession 0 → reject
    a.profession = 0;
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextStartWorkTask(&a,&ev),1); }
    // normal: result|2
    a.profession = 7;
    { auto ev = MakeEvent(kModeActivate); ResetInteractionLeafTrace();
      CHECK_EQ((int)(ContextStartWorkTask(&a,&ev) & 2),2);
      CHECK_EQ(g_leafTrace.privilegeLeaf,(int)kPrivRemoveFromOffice); }
}

TEST(SimIntHandlers, ContextStartGuildTask) {
    auto a = MakeActor();
    a.kind = 6;
    a.profession = 14;
    // 458 & 0x40 blocks
    a.flag458 = 0x40;
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextStartGuildTask(&a,&ev),1); }
    a.flag458 = 0;
    a.profession = 13;   // wrong profession
    { auto ev = MakeEvent(kModeActivate); CHECK_EQ((int)ContextStartGuildTask(&a,&ev),1); }
    a.profession = 14;
    { auto ev = MakeEvent(kModeActivate); ResetInteractionLeafTrace();
      CHECK_EQ((int)(ContextStartGuildTask(&a,&ev) & 2),2);
      CHECK_EQ(g_leafTrace.privilegeLeaf,(int)kPrivEmbezzlement); }
}

// --- AssignTask / AssignToSlot (entity-array dependent) ---------------------
TEST(SimIntHandlers, ContextAssignTask) {
    ResetEntityArrays();
    auto a = MakeActor();
    a.kind = 6;
    a.assocPersonId = -1;   // no associated person → skip the person check
    auto ev = MakeEvent(kModeActivate);
    ResetInteractionLeafTrace();
    CHECK_EQ((int)(ContextAssignTask(&a,&ev) & 2),2);

    // associated person that is a live actor of kind!=15 → reject
    g_persons[0].marker = 0;
    g_persons[0].id = 77; g_personIds[0] = 77;
    g_persons[0].isPlayer = 1; g_persons[0].kind = 6;
    a.assocPersonId = 77;
    ev = MakeEvent(kModeActivate);
    CHECK_EQ((int)ContextAssignTask(&a,&ev),1);

    // associated person kind==15 (allowed) → proceed
    g_persons[0].kind = 15;
    ev = MakeEvent(kModeActivate);
    CHECK_EQ((int)(ContextAssignTask(&a,&ev) & 2),2);
    ResetEntityArrays();
}

TEST(SimIntHandlers, ContextAssignToSlot) {
    ResetEntityArrays();
    auto a = MakeActor();
    a.rank = 5;
    a.assocPersonId = 88;
    // slot 0 free (marker -1 by reset); a valid live person record id 88
    g_persons[1].marker = 0; g_persons[1].id = 88; g_personIds[1] = 88;
    g_persons[1].isPlayer = 1;
    auto ev = MakeEvent(kModeActivate);
    ResetInteractionLeafTrace();
    CHECK_EQ((int)ContextAssignToSlot(&a,&ev),0);  // PanelDivorce hook returns 0
    CHECK_EQ(g_leafTrace.privilegeLeaf,(int)kPrivDivorce);

    // rank < 5 → reject
    a.rank = 4;
    ev = MakeEvent(kModeActivate);
    CHECK_EQ((int)ContextAssignToSlot(&a,&ev),1);
    a.rank = 5;

    // assoc -1 → reject
    a.assocPersonId = -1;
    ev = MakeEvent(kModeActivate);
    CHECK_EQ((int)ContextAssignToSlot(&a,&ev),1);
    ResetEntityArrays();
}

// --- Perform* command emitters ----------------------------------------------
TEST(SimIntHandlers, PerformHandlers) {
    ResetEntityArrays();
    ResetInteractionLeafTrace();
    ContextActor dummy = MakeActor();

    // PerformRenovate always emits "renovieren" / action 15.
    CHECK_EQ((int)PerformRenovate(&dummy,&dummy,&dummy),15);
    CHECK(g_leafTrace.commandTag && std::strcmp(g_leafTrace.commandTag,"renovieren")==0);
    CHECK_EQ(g_leafTrace.commandAction,15);

    // PerformSabotage: requires mode 4 + resolvable building.
    auto ev = MakeEvent(4);
    ev.targetId = 501;
    CHECK_EQ((int)PerformSabotage(&ev,&dummy),0);  // no building yet
    g_objects[0].alive = 1; g_objects[0].id = 501;
    ResetInteractionLeafTrace();
    CHECK_EQ((int)PerformSabotage(&ev,&dummy),23);
    CHECK(g_leafTrace.commandTag && std::strcmp(g_leafTrace.commandTag,"sabotage")==0);
    // wrong mode → 0
    ev.mode = 3;
    CHECK_EQ((int)PerformSabotage(&ev,&dummy),0);

    // PerformBeating: requires mode 7 + resolvable person.
    auto ev2 = MakeEvent(7);
    ev2.targetId = 901;
    CHECK_EQ((int)PerformBeating(&ev2,&dummy),0);
    g_persons[0].marker = 0; g_persons[0].id = 901; g_personIds[0] = 901;
    ResetInteractionLeafTrace();
    CHECK_EQ((int)PerformBeating(&ev2,&dummy),24);
    CHECK(g_leafTrace.commandTag && std::strcmp(g_leafTrace.commandTag,"pruegel")==0);
    ResetEntityArrays();
}
