#include "world/location4.h"

#include <climits>

namespace guild::world {

// ===========================================================================
// Hooks plumbing — same pattern as location3.cpp's LocationDialogHooks: an inert
// all-null instance so every dialog is runnable headless. A test installs its own.
// ===========================================================================
namespace {
const LocationDialog4Hooks* g_hooks4 = nullptr;
LocationDialog4Hooks        g_inert4{};   // all-null -> inert defaults below
} // namespace

void SetLocationDialog4Hooks(const LocationDialog4Hooks* hooks) { g_hooks4 = hooks; }
const LocationDialog4Hooks& GetLocationDialog4Hooks() {
    return g_hooks4 ? *g_hooks4 : g_inert4;
}

namespace {

// ---- location4-specific hook wrappers (inert defaults match the orig gates) ----
bool D4_checkSkill(int level) {
    const auto& h = GetLocationDialog4Hooks();
    return h.checkSkillRequirement ? h.checkSkillRequirement(level) : true;
}
void D4_requestBuildOp(std::int32_t target, int op) {
    const auto& h = GetLocationDialog4Hooks();
    if (h.requestBuildOp) h.requestBuildOp(target, op);
}
void D4_evaluateViolation(int kind, int sev, std::int32_t perp,
                          std::int32_t target, int extra) {
    const auto& h = GetLocationDialog4Hooks();
    if (h.evaluateViolation) h.evaluateViolation(kind, sev, perp, target, extra);
}
int D4_countExistingHandlers() {
    const auto& h = GetLocationDialog4Hooks();
    return h.countExistingHandlers ? h.countExistingHandlers() : 0;
}
void D4_enqueueBribe(std::int32_t jailer, std::int32_t city, int amount) {
    const auto& h = GetLocationDialog4Hooks();
    if (h.enqueueBribe) h.enqueueBribe(jailer, city, amount);
}
std::int32_t D4_selectedBuilding() {
    const auto& h = GetLocationDialog4Hooks();
    return h.selectedBuilding ? h.selectedBuilding() : -1;
}
void D4_queueInfoRequest(std::int32_t perp, std::int32_t building) {
    const auto& h = GetLocationDialog4Hooks();
    if (h.queueInfoRequest) h.queueInfoRequest(perp, building);
}
int D4_dragSlotCount() {
    const auto& h = GetLocationDialog4Hooks();
    return h.dragSlotCount ? h.dragSlotCount() : 0;
}
void D4_commitTrainingItem(int slot) {
    const auto& h = GetLocationDialog4Hooks();
    if (h.commitTrainingItem) h.commitTrainingItem(slot);
}

// ---- shared GUI-shell wrappers (re-using location3's PUBLIC hooks API) ----------
// location3's H_openForm/H_frameStep/... live in an anonymous namespace, so we
// re-derive identical thin wrappers from the same public LocationDialogHooks.
std::int32_t G_openForm(const char* key) {
    const auto& h = GetLocationDialogHooks();
    return h.openForm ? h.openForm(key) : 0;
}
void G_destroyForm(std::int32_t form) {
    const auto& h = GetLocationDialogHooks();
    if (h.destroyForm) h.destroyForm(form);
}
std::int32_t G_frameStep(std::int32_t form) {
    const auto& h = GetLocationDialogHooks();
    return h.frameStep ? h.frameStep(form) : INT32_MIN;  // default: end loop
}
void G_queueBatch(int code, int count, const std::vector<std::int32_t>& ids) {
    const auto& h = GetLocationDialogHooks();
    if (h.queueBatch) h.queueBatch(code, count, ids.data(),
                                   static_cast<int>(ids.size()));
}
void G_showMessage(int textId) {
    const auto& h = GetLocationDialogHooks();
    if (h.showMessage) h.showMessage(textId);
}
void G_playFavorVoice() {
    const auto& h = GetLocationDialogHooks();
    if (h.playFavorVoice) h.playFavorVoice();
}

constexpr std::int32_t kYesId = 1;   // ChildObjectId (confirm), as in location3

// dword_75BF38=-1; do { ... if(clicked==yes) <body>; } while(RunFrameLoop(...));
// Returns true when the confirm id is observed, false when the loop ends.
bool RunUntilConfirm4(std::int32_t form) {
    for (;;) {
        std::int32_t clicked = G_frameStep(form);
        if (clicked == INT32_MIN) return false;   // RunFrameLoop returned 0
        if (clicked == kYesId)    return true;     // dword_62D22C == ChildObjectId
    }
}

constexpr int kMsgCapacityFull = 0;   // dword_8C905C "no more room"
constexpr int kMsgSpyTooMany   = 5641; // Spy: existing+occupied >= 2*capacity
constexpr int kMsgBusyElsewhere= 5791; // Spy: !IsAnimalTargetBusy

} // namespace

// ===========================================================================
// gilde.exe 0x52554c — VIBE_Location_ThiefKidnapDialog.
// ===========================================================================
DialogOutcome ThiefKidnapDialog(bool hasTarget, bool targetCaptive, bool blockedFlag,
                                std::int32_t perpId, std::int32_t cityFirstId,
                                const SlotTableView& table) {
    DialogOutcome o;
    o.action = static_cast<int>(DialogAction4::ThiefKidnap);
    if (!hasTarget) return o;                          // if(!result) return result
    if (!D4_checkSkill(3)) return o;                   // CheckSkillRequirement(...,3)
    if (targetCaptive) return o;                       // v4 && NPC+39==target -> 0
    if (blockedFlag) { G_showMessage(kMsgKidnapBlocked); return o; }  // +433 -> msg 5577
    o.opened = true;
    std::int32_t form = G_openForm("LOCATIONS\\DIEBESGILDE\\DIEBESGILDE_ENTFUEHRUNG");
    if (RunUntilConfirm4(form)) {
        std::vector<std::int32_t> ids;
        o.count = CollectOccupiedIds(table, kMaxSlots, ids);  // for(j) j!=102912, no cap
        if (o.count) {
            G_queueBatch(o.action, o.count, ids);             // QueueRequestSlotReset28
            D4_requestBuildOp(cityFirstId, -3);               // RequestBuildOp90(id,-3)
            D4_evaluateViolation(25, 1, perpId, cityFirstId, -1);  // Gesetz(25,1,...)
            G_playFavorVoice();
            o.committed = true;                               // v30 = 1
        } else {
            G_showMessage(0);                                 // dword_8C8E78
        }
    }
    G_destroyForm(form);
    return o;
}

// ===========================================================================
// gilde.exe 0x5268a8 — VIBE_Location_GuardCustomsDialog.
// ===========================================================================
DialogOutcome GuardCustomsDialog(bool hasTarget, const SlotTableView& table,
                                 const SelectionTable& sel) {
    DialogOutcome o;
    o.action = static_cast<int>(DialogAction4::GuardCustoms);
    if (!hasTarget) return o;                                  // if(!a1) return 0
    if (SlotTableFull(table)) { G_showMessage(kMsgCapacityFull); return o; }  // v4>=768
    o.opened = true;
    std::int32_t form = G_openForm("LOCATIONS\\STADT\\ZOLL");
    if (RunUntilConfirm4(form)) {
        std::vector<std::int32_t> ids;
        o.count = CollectSelectionIds(sel, 6, ids);            // v19<24 cap -> 6 ids
        if (o.count) {
            G_queueBatch(o.action, o.count, ids);              // QueueRequestSlotReset28
            G_playFavorVoice();
            // NOTE: the original does NOT set a commit flag here (v35 stays 0); the
            // batch is queued but the function still returns 0. committed reflects
            // "a batch was actually queued" for test observability.
            o.committed = true;
        }
    }
    G_destroyForm(form);
    return o;
}

// ===========================================================================
// gilde.exe 0x526b34 — VIBE_Location_GuardDetainDialog.
// ===========================================================================
DialogOutcome GuardDetainDialog(bool hasTarget, bool alreadyDetainedFlag,
                                const SlotTableView& table,
                                const SelectionTable& sel) {
    DialogOutcome o;
    o.action = static_cast<int>(DialogAction4::GuardDetain);
    if (!hasTarget) return o;                                  // if(!a1) return 0
    if (SlotTableFull(table)) { G_showMessage(kMsgCapacityFull); return o; }  // v4>=768
    if (alreadyDetainedFlag) { G_showMessage(0); return o; }   // (a1+91 & 8) -> 8CA974
    o.opened = true;
    std::int32_t form = G_openForm("LOCATIONS\\STADT\\GEWAHRSAM");
    if (RunUntilConfirm4(form)) {
        // The original BREAKS on the FIRST active selection entry (the while(1)
        // loop with break on rec[+392]) and submits a single-id batch.
        std::vector<std::int32_t> ids;
        for (const auto& e : sel)
            if (e.present && e.active) { ids.push_back(e.id); break; }
        o.count = static_cast<int>(ids.size());
        if (o.count) {
            G_queueBatch(o.action, o.count, ids);              // QueueRequestSlotReset28
            G_playFavorVoice();
            o.committed = true;
        }
        // re-count ALL active and warn when >1 (the for(i!=32) v12 recount).
        int active = 0;
        for (const auto& e : sel) if (e.present && e.active) ++active;
        if (active > 1) G_showMessage(kMsgDetainMultiple);     // dword_8C90B4 (mode 4)
    }
    G_destroyForm(form);
    return o;
}

// ===========================================================================
// gilde.exe 0x524380 — VIBE_Location_ThiefSpyBuildingDialog.
// ===========================================================================
DialogOutcome ThiefSpyBuildingDialog(bool hasTarget, bool targetBusy,
                                     int occupiedForTarget, int buildingCapacity,
                                     const SlotTableView& table) {
    DialogOutcome o;
    o.action = static_cast<int>(DialogAction4::ThiefSpyBuilding);
    if (!hasTarget) return o;                                  // !a1 || a1+39==0xFFFF
    if (!targetBusy) { G_showMessage(kMsgBusyElsewhere); return o; }  // !IsAnimalTargetBusy
    // existing pending spy handlers + this-target occupants, capacity gate:
    // v13 = v40 + v6; if(v13 < 2*buildingCapacity) open; else msg 5641.
    int v13 = D4_countExistingHandlers() + occupiedForTarget;
    if (v13 >= 2 * buildingCapacity) { G_showMessage(kMsgSpyTooMany); return o; }
    o.opened = true;
    std::int32_t form = G_openForm("locations\\diebesgilde\\diebesgilde_spionage");
    if (RunUntilConfirm4(form)) {
        std::vector<std::int32_t> ids;
        o.count = CollectOccupiedIds(table, 8, ids);           // v28<8 cap -> 8 ids
        if (o.count) {
            G_queueBatch(o.action, o.count, ids);              // QueueRequestSlotReset28
            G_playFavorVoice();
            o.committed = true;                                // v39 = count != 0
        } else {
            G_showMessage(0);                                  // dword_8C905C
        }
    }
    G_destroyForm(form);
    return o;
}

// ===========================================================================
// gilde.exe 0x523d1c — VIBE_Location_DungeonBribeDialog.
// ===========================================================================
DialogOutcome DungeonBribeDialog(bool hasJailer, bool jailerRecordExists,
                                 bool bribePending,
                                 std::int32_t jailerId, std::int32_t cityId,
                                 int bribeAmount) {
    DialogOutcome o;
    o.action = static_cast<int>(DialogAction4::DungeonBribe);
    if (!hasJailer) return o;                                  // !Office_GetEntryByHolder(17)
    if (!jailerRecordExists) { G_showMessage(kMsgDungeonNoHolder); return o; }  // 8C8DC0
    if (bribePending) { G_showMessage(kMsgDungeonExists); return o; }  // msg 5573
    o.opened = true;
    std::int32_t form = G_openForm("locations\\kerker\\bestechung");
    if (RunUntilConfirm4(form)) {
        // v22=57; EnqueueCmd15(jailer,city,amount,...); QueueRequestSlotReset28.
        D4_enqueueBribe(jailerId, cityId, bribeAmount);
        std::vector<std::int32_t> ids{ jailerId };
        o.count = 1;
        G_queueBatch(o.action, o.count, ids);
        o.committed = true;                                    // v9 = 1
    }
    G_destroyForm(form);
    return o;
}

// ===========================================================================
// gilde.exe 0x52481c — VIBE_Location_ThiefInformationDialog.
// ===========================================================================
DialogOutcome ThiefInformationDialog(std::int32_t perpId) {
    DialogOutcome o;
    o.action = 0;   // not a slot-reset batch; uses QueueRequestQuad43
    o.opened = true;
    std::int32_t form = G_openForm("locations\\diebesgilde\\information");
    if (RunUntilConfirm4(form)) {
        // on confirm with a radio selection (v10 != -1): submit quad43 for the
        // selected building id; spin until packet status (modeled as immediate).
        std::int32_t building = D4_selectedBuilding();
        if (building != -1) {                                  // v10 != -1
            D4_queueInfoRequest(perpId, building);             // QueueRequestQuad43
            o.count = 1;
            o.committed = true;
        }
    }
    G_destroyForm(form);
    return o;
}

// ===========================================================================
// gilde.exe 0x5253c0 — VIBE_Location_ThiefTrainingDialog.
// ===========================================================================
DialogOutcome ThiefTrainingDialog(bool activeCharFlag) {
    DialogOutcome o;
    o.action = 0;
    if (activeCharFlag) return o;                              // if(CheckActiveCharFlag()) return
    o.opened = true;
    std::int32_t form = G_openForm("locations\\diebesgilde\\ausbildung");
    if (RunUntilConfirm4(form)) {
        // commit every used grid slot: for(i=0;i!=512;i+=16) if(slot!=-1 && dataptr)
        //   Object_SetValueOrText(...). We model the count via dragSlotCount().
        int used = D4_dragSlotCount();
        for (int i = 0; i < used; ++i) D4_commitTrainingItem(i);
        o.count = used;
        o.committed = (used > 0);
    }
    G_destroyForm(form);
    return o;
}

} // namespace guild::world
