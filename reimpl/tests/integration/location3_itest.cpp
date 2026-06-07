// Integration test for src/world/location3 (the VIBE_Location_* dialog bodies).
//
// NOTE ON THE "REAL SIBLING" RULE: location3's installable hooks (LocationDialogHooks)
// are all GUI-shell / command-queue / handler-table primitives whose shapes are
// SYNTHETIC aggregates of the binary's callees — e.g. queueBatch(actionCode, count,
// ids, idCount) folds VIBE_Command_QueueRequestSlotReset28 + the per-id stores, and
// changePlayerAction(targetId, charId) is a 2-arg reduction of the multi-arg
// VIBE_Character_ChangePlayerAction. None of these map 1:1 onto a reconstructed src/
// free function, so this module genuinely has NO reconstructed sibling to forward a
// hook into. Per the brief, we therefore exercise the module's REAL inert
// default-hook path end to end: SetLocationDialogHooks(nullptr) leaves openForm()->0
// and frameStep()->INT32_MIN (the loop body never runs), so each dialog opens and
// closes WITHOUT committing — the genuine headless default flow the library ships.
// The pure table/selection scanners take no hooks at all and are asserted directly.
#include "test.h"

#include "world/location3.h"

#include <vector>

using namespace guild::world;

namespace {
// Build a SlotTableView whose occupied flags follow `occ` and ids are slot*10.
SlotTableView makeTable(const std::vector<std::uint8_t>& occ) {
    SlotTableView t;
    t.occupied = occ;
    t.id.resize(occ.size());
    for (std::size_t i = 0; i < occ.size(); ++i)
        t.id[i] = static_cast<std::int32_t>(i) * 10;
    return t;
}
}  // namespace

// --- Pure scanners (no hooks): the recovered table arithmetic -----------------
TEST(Location3Itest, OccupiedSlotScannersAreFaithful) {
    // Slots 0,3,4 occupied.
    SlotTableView t = makeTable({1, 0, 0, 1, 1, 0});
    CHECK_EQ(FirstOccupiedSlot(t), 0);          // slot 0 short-circuit
    CHECK_EQ(SlotTableFull(t), false);
    CHECK_EQ(CountOccupiedSlots(t), 3);

    std::vector<std::int32_t> ids;
    int n = CollectOccupiedIds(t, kMaxSlots, ids);   // uncapped
    CHECK_EQ(n, 3);
    if (ids.size() == 3) {
        CHECK_EQ(ids[0], 0);    // slot 0 id
        CHECK_EQ(ids[1], 30);   // slot 3 id
        CHECK_EQ(ids[2], 40);   // slot 4 id
    }

    // Capped collect (GuardArrest cap 4 behaviour, here cap 2).
    std::vector<std::int32_t> capped;
    int m = CollectOccupiedIds(t, 2, capped);
    CHECK_EQ(m, 2);
    CHECK_EQ(static_cast<int>(capped.size()), 2);

    // First-occupied when slot 0 empty: scan finds slot 3.
    SlotTableView t2 = makeTable({0, 0, 0, 1, 0});
    CHECK_EQ(FirstOccupiedSlot(t2), 3);

    // Empty table -> "full" gate fires (scan exhausts to kMaxSlots).
    SlotTableView empty = makeTable({0, 0, 0});
    CHECK_EQ(SlotTableFull(empty), true);
}

TEST(Location3Itest, SelectionScannersAreFaithful) {
    SelectionTable sel{};
    sel[2]  = {true, true, 111};    // present + active
    sel[5]  = {true, false, 222};   // present but inactive -> skipped
    sel[9]  = {true, true, 333};
    sel[31] = {true, true, 444};

    CHECK_EQ(AnySelectionActive(sel), true);

    std::vector<std::int32_t> ids;
    int n = CollectSelectionIds(sel, 6, ids);   // cap 6
    CHECK_EQ(n, 3);
    if (ids.size() == 3) {
        CHECK_EQ(ids[0], 111);
        CHECK_EQ(ids[1], 333);
        CHECK_EQ(ids[2], 444);
    }

    // Cap honoured: only 2 collected when maxCount==2.
    std::vector<std::int32_t> two;
    CHECK_EQ(CollectSelectionIds(sel, 2, two), 2);

    CHECK_EQ(TooManyParticipants(7), true);
    CHECK_EQ(TooManyParticipants(6), false);

    SelectionTable none{};
    CHECK_EQ(AnySelectionActive(none), false);
}

// --- End-to-end dialog flow over the REAL inert default-hook path -------------
// With SetLocationDialogHooks(nullptr) the form opens (handle 0) and frameStep
// immediately ends the loop, so no batch is committed for any confirm-gated dialog.
TEST(Location3Itest, DialogsRunHeadlessOverInertDefaults) {
    SetLocationDialogHooks(nullptr);   // explicit: use the shipped inert defaults

    SlotTableView table = makeTable({1, 0, 1, 0, 1});   // not full
    SelectionTable sel{};
    sel[0] = {true, true, 7};

    // RobberCampStandard: gates pass (hasTarget, not full), form opens, loop ends
    // immediately -> opened but NOT committed (the inert frameStep never confirms).
    DialogOutcome rc = RobberCampStandard(42, true, table, sel);
    CHECK_EQ(rc.opened, true);
    CHECK_EQ(rc.committed, false);
    CHECK_EQ(rc.count, 0);
    CHECK_EQ(rc.action, static_cast<int>(DialogAction::RobberCampStandard));

    // RobberCampRaid: form opens, no confirm -> no batch.
    DialogOutcome rr = RobberCampRaid(table);
    CHECK_EQ(rr.opened, true);
    CHECK_EQ(rr.committed, false);
    CHECK_EQ(rr.action, static_cast<int>(DialogAction::RobberCampRaid));

    // GuardRaidDialog: not full -> opens; inert loop ends -> not committed.
    DialogOutcome gr = GuardRaidDialog(table);
    CHECK_EQ(gr.opened, true);
    CHECK_EQ(gr.committed, false);

    // No-target gate: RobberCampStandard with hasTarget=false never opens.
    DialogOutcome no = RobberCampStandard(0, false, table, sel);
    CHECK_EQ(no.opened, false);
    CHECK_EQ(no.committed, false);

    // SelectionBatchDialog with an existing request takes the re-issue branch:
    // it opens, walks the active selection via the inert changePlayerAction
    // (a no-op), and never commits a fresh batch.
    DialogOutcome sb = SelectionBatchDialog(DialogAction::Pickpocket, 6,
                                            /*requestExists*/ true, sel);
    CHECK_EQ(sb.opened, true);
    CHECK_EQ(sb.committed, false);

    // ThiefBurglaryDialog gate: targetBusy=false -> aborts before opening
    // (inert showMessage swallows the popup).
    DialogOutcome tb = ThiefBurglaryDialog(/*targetBusy*/ false, /*securityOk*/ true,
                                           table);
    CHECK_EQ(tb.opened, false);
    CHECK_EQ(tb.committed, false);

    // Full-table gate: GuardArrestDialog on an all-empty table reports "full"
    // (FirstOccupiedSlot >= kMaxSlots) and never opens.
    SlotTableView fullGate = makeTable({0, 0, 0});
    DialogOutcome ga = GuardArrestDialog(fullGate);
    CHECK_EQ(ga.opened, false);
    CHECK_EQ(ga.committed, false);
}
