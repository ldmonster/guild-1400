#include "world/location3.h"

#include <climits>

namespace guild::world {

// ===========================================================================
// Hooks plumbing (mirrors CutsceneMiscHooks / ObjLifeHooks in sibling modules):
// an inert all-null instance makes every dialog runnable headless.
// ===========================================================================
namespace {
const LocationDialogHooks* g_hooks = nullptr;
LocationDialogHooks        g_inert{};   // all-null: openForm()->0, frameStep ends loop
} // namespace

void SetLocationDialogHooks(const LocationDialogHooks* hooks) { g_hooks = hooks; }
const LocationDialogHooks& GetLocationDialogHooks() {
    return g_hooks ? *g_hooks : g_inert;
}

namespace {

std::int32_t H_openForm(const char* key) {
    const auto& h = GetLocationDialogHooks();
    return h.openForm ? h.openForm(key) : 0;
}
void H_destroyForm(std::int32_t form) {
    const auto& h = GetLocationDialogHooks();
    if (h.destroyForm) h.destroyForm(form);
}
std::int32_t H_frameStep(std::int32_t form) {
    const auto& h = GetLocationDialogHooks();
    return h.frameStep ? h.frameStep(form) : INT32_MIN;  // default: end loop
}
void H_queueBatch(int code, int count, const std::vector<std::int32_t>& ids) {
    const auto& h = GetLocationDialogHooks();
    if (h.queueBatch) h.queueBatch(code, count, ids.data(),
                                   static_cast<int>(ids.size()));
}
void H_showMessage(int textId) {
    const auto& h = GetLocationDialogHooks();
    if (h.showMessage) h.showMessage(textId);
}
void H_playFavorVoice() {
    const auto& h = GetLocationDialogHooks();
    if (h.playFavorVoice) h.playFavorVoice();
}
void H_changePlayerAction(std::int32_t target, std::int32_t cid) {
    const auto& h = GetLocationDialogHooks();
    if (h.changePlayerAction) h.changePlayerAction(target, cid);
}

constexpr int kMsgTooManyText = 5630;   // "too many participants" (text id 5630)

} // namespace

// ===========================================================================
// Pure table scanners.
// ===========================================================================

// gilde.exe 0x5126ff / 0x526363 / 0x5265ea — the leading occupied-slot probe:
//   v3=0; v4=0; if(!byte_12CEA98[0]) do { v4+=536; ++v3; }
//                while(v4<411648 && !byte_12CEA98[v4]);
// 411648 == 768*536; v4/536 == the slot index, v3 counts iterations. We mirror
// the exact "stop at first occupied OR after 768 slots" behavior. The original
// short-circuits on byte_12CEA98[0] (slot 0) before entering the loop.
int FirstOccupiedSlot(const SlotTableView& t) {
    const int n = static_cast<int>(t.occupied.size());
    if (n > 0 && t.occupied[0]) return 0;       // !byte_12CEA98[0] guard
    int slot = 0;
    do {
        ++slot;                                  // v4 += 536; ++v3;
        if (slot >= kMaxSlots) break;            // v4 < 411648 (== slot < 768)
    } while (slot >= n || !t.occupied[slot]);    // !byte_12CEA98[v4]
    return slot;
}

bool SlotTableFull(const SlotTableView& t) {
    return FirstOccupiedSlot(t) >= kMaxSlots;    // v3 >= 768
}

// gilde.exe 0x5121e2-type loop (RobberCampRaid 0x512fdf, ThiefBurglary 0x5241e2,
// GuardArrest 0x52646c, GuardRaid 0x526708, Spy 0x52462a):
//   v15=0; for(j=0;j!=102912;j+=134) if(byte_12CEA98[j*4]){ v15+=4;
//          collect dword_12CE914[j]; ++count; }   (some cap via v15<N / count<M)
// Here j strides 134 dwords per slot; byte_12CEA98[j*4] is the slot-occupied
// flag (occupied[slot]); dword_12CE914[j] is id[slot]. maxCount caps the ids.
int CollectOccupiedIds(const SlotTableView& t, int maxCount,
                       std::vector<std::int32_t>& out) {
    out.clear();
    int count = 0;                                     // == v13/v27
    const int slots = static_cast<int>(t.occupied.size());
    for (int slot = 0; slot < slots && slot < kMaxSlots; ++slot) {
        if (count >= maxCount) break;                  // while(... && v15 < cap)
        if (!t.occupied[slot]) continue;
        out.push_back(slot < static_cast<int>(t.id.size()) ? t.id[slot] : -1);
        ++count;                                       // ++v13 (inside the cap)
    }
    return count;                                      // capped count the cmd uses
}

// gilde.exe GuardRaid 0x526770 — the SEPARATE uncapped re-count used only for the
// over-capacity warning: for(j=0;j!=411648;j+=536) if(byte_12CEA98[j]) ++v18;
int CountOccupiedSlots(const SlotTableView& t) {
    int count = 0;
    const int slots = static_cast<int>(t.occupied.size());
    for (int slot = 0; slot < slots && slot < kMaxSlots; ++slot)
        if (t.occupied[slot]) ++count;
    return count;
}

// ===========================================================================
// Player-selection collectors (dword_11BB6A0[0..32)).
// ===========================================================================

// gilde.exe 0x5124f4-type loop (ThievesGuildBurglary 0x512262, Pickpocket
// 0x5124f4 / 0x524d02, Customs 0x526a35):
//   for(v11=0; v11<32 && v12<cap*4; ++v11)
//      if(rec=dword_11BB6A0[v11]; rec && rec[392]){ v12+=4; collect rec[+4]; ++v7; }
// maxCount is cap (the original's v12<24 => 6 ids, v12<32 => 8 ids).
int CollectSelectionIds(const SelectionTable& sel, int maxCount,
                        std::vector<std::int32_t>& out) {
    out.clear();
    int count = 0;
    for (int i = 0; i < kSelectionSlots && count < maxCount; ++i) {
        if (!sel[i].present || !sel[i].active) continue;  // rec && rec[392]
        out.push_back(sel[i].id);                          // collect rec[+4]
        ++count;
    }
    return count;
}

bool AnySelectionActive(const SelectionTable& sel) {
    for (const auto& e : sel)
        if (e.present && e.active) return true;            // rec[392]
    return false;
}

bool TooManyParticipants(int count) { return count > 6; }  // v14 > 6 / v18 > 6

// ===========================================================================
// Dialog runners. Each opens the form shell, drives the click loop via the
// frameStep hook, and on the "yes" click runs the recovered batch logic.
// The form shell semantics: frameStep returns the clicked object id; the runner
// treats the action as confirmed when it sees `kYesId`, and ends on INT32_MIN.
// (We use a fixed sentinel id for "yes" so a test can script confirmation.)
// ===========================================================================
namespace {
constexpr std::int32_t kYesId = 1;     // ChildObjectId (confirm)

// Drive the GUI loop until frameStep yields the confirm id (returns true) or the
// loop ends (returns false). Mirrors:
//   dword_75BF38=-1; do { ... if(clicked==yes) <body>; } while(RunFrameLoop(...));
bool RunUntilConfirm(std::int32_t form) {
    for (;;) {
        std::int32_t clicked = H_frameStep(form);
        if (clicked == INT32_MIN) return false;   // RunFrameLoop returned 0
        if (clicked == kYesId)    return true;     // dword_62D22C == ChildObjectId
        // any other id (e.g. the "no" button) just continues the loop
    }
}
} // namespace

// gilde.exe 0x5126cc — VIBE_Location_RobberCampStandard.
//   if(!a1) return 0;
//   find existing handler(98,target); if found -> msg 5790, return 0;
//   if FirstOccupiedSlot >= 768 -> msg(dword_8C90C8), return 0;
//   else if(!IsAnimalTargetBusy) -> msg 5782, return 0;
//   else { form loop; on confirm: build batch{code 98}, collect first active
//          selection id, QueueRequestSlotReset28, ClearAll; } return v33;
DialogOutcome RobberCampStandard(std::int32_t /*target*/, bool hasTarget,
                                 const SlotTableView& table,
                                 const SelectionTable& sel) {
    DialogOutcome o;
    o.action = static_cast<int>(DialogAction::RobberCampStandard);
    if (!hasTarget) return o;                        // if(!a1) return 0
    if (SlotTableFull(table)) { H_showMessage(0); return o; }  // v5>=768 abort
    o.opened = true;
    std::int32_t form = H_openForm("LOCATIONS\\RAEUBERLAGER\\STANDARD");
    if (RunUntilConfirm(form)) {
        H_playFavorVoice();                          // VIBE_Voice_PlayCraftFavorComment
        // original breaks on the FIRST active selection entry (the v9 loop) and
        // submits a single-id batch; count is "1 if any active".
        std::vector<std::int32_t> ids;
        for (const auto& e : sel)
            if (e.present && e.active) { ids.push_back(e.id); break; }
        o.count = static_cast<int>(ids.size());
        H_queueBatch(o.action, o.count, ids);
        o.committed = true;
    }
    H_destroyForm(form);
    return o;
}

// gilde.exe 0x512eb0 — VIBE_Location_RobberCampRaid.
//   if(a2 && NPC+39!=0xFFFF) { if(!IsAnimalTargetBusy) msg 5791,return;
//     form loop; on confirm: collect ALL occupied ids (code 117); if any ->
//       QueueRequestSlotReset28 + RequestBuildOp90 + favor voice;
//     else msg(dword_8C90C8); }
DialogOutcome RobberCampRaid(const SlotTableView& table) {
    DialogOutcome o;
    o.action = static_cast<int>(DialogAction::RobberCampRaid);
    o.opened = true;
    std::int32_t form = H_openForm("locations\\raeuberlager\\raubzug");
    if (RunUntilConfirm(form)) {
        std::vector<std::int32_t> ids;
        o.count = CollectOccupiedIds(table, kMaxSlots, ids);  // no cap (j!=102912)
        if (o.count) {
            H_queueBatch(o.action, o.count, ids);
            H_playFavorVoice();
            o.committed = true;
        } else {
            H_showMessage(0);                         // dword_8C90C8 "no camp"
        }
    }
    H_destroyForm(form);
    return o;
}

// gilde.exe 0x524074 — VIBE_Location_ThiefBurglaryDialog.
//   if(!a1) return 0;
//   if(!IsAnimalTargetBusy) msg 5791,return 0;
//   if(!CheckSecurityThreshold) msg(dword_8C8E80),return 0;
//   form loop; on confirm: collect ALL occupied ids (code 63); if any ->
//     QueueRequestSlotReset28 + favor voice; else msg(dword_8C8E78); ClearAll.
DialogOutcome ThiefBurglaryDialog(bool targetBusy, bool securityOk,
                                  const SlotTableView& table) {
    DialogOutcome o;
    o.action = static_cast<int>(DialogAction::ThiefBurglary);
    if (!targetBusy) { H_showMessage(5791); return o; }       // busy-elsewhere msg
    if (!securityOk) { H_showMessage(0);    return o; }       // dword_8C8E80
    o.opened = true;
    std::int32_t form = H_openForm("locations\\diebesgilde\\diebesgilde_einbruch");
    if (RunUntilConfirm(form)) {
        std::vector<std::int32_t> ids;
        o.count = CollectOccupiedIds(table, kMaxSlots, ids);
        if (o.count) {
            H_queueBatch(o.action, o.count, ids);
            H_playFavorVoice();
            o.committed = true;
        } else {
            H_showMessage(0);                          // dword_8C8E78
        }
    }
    H_destroyForm(form);
    return o;
}

// gilde.exe 0x526344 — VIBE_Location_GuardArrestDialog.
//   if(!a2) return 0;
//   if FirstOccupiedSlot>=768 -> msg(dword_8C905C), return 0;
//   form loop; on confirm: collect occupied ids cap 4 (code 67); if any ->
//     pad to 4 with -1, QueueRequestSlotReset28 + favor voice; ClearAll; return 1.
DialogOutcome GuardArrestDialog(const SlotTableView& table) {
    DialogOutcome o;
    o.action = static_cast<int>(DialogAction::GuardArrest);
    if (SlotTableFull(table)) { H_showMessage(0); return o; }
    o.opened = true;
    std::int32_t form = H_openForm("LOCATIONS\\STADT\\...");
    if (RunUntilConfirm(form)) {
        std::vector<std::int32_t> ids;
        o.count = CollectOccupiedIds(table, 4, ids);   // v16<4 cap
        if (o.count) {
            while (ids.size() < 4) ids.push_back(-1);   // pad to 4 (v26[++v18]=-1)
            H_queueBatch(o.action, o.count, ids);
            H_playFavorVoice();
            o.committed = true;
        }
    }
    H_destroyForm(form);
    return o;
}

// gilde.exe 0x5265c8 — VIBE_Location_GuardRaidDialog.
//   if(!a2) return 0;
//   if FirstOccupiedSlot>=768 -> msg(dword_8C905C), return 0;
//   form loop; on confirm: collect occupied ids cap 6 (code 68); if any ->
//     QueueRequestSlotReset28 + favor voice; then re-count ALL occupied buildings
//     and if >6 -> msg 5630. return 1.
DialogOutcome GuardRaidDialog(const SlotTableView& table) {
    DialogOutcome o;
    o.action = static_cast<int>(DialogAction::GuardRaid);
    if (SlotTableFull(table)) { H_showMessage(0); return o; }
    o.opened = true;
    std::int32_t form = H_openForm("LOCATIONS\\STADT\\...");
    if (RunUntilConfirm(form)) {
        std::vector<std::int32_t> ids;
        o.count = CollectOccupiedIds(table, 6, ids);    // v15<6 cap
        if (o.count) {
            H_queueBatch(o.action, o.count, ids);
            H_playFavorVoice();
            o.committed = true;
            // re-count ALL occupied (j+=536 loop) and warn when >6
            if (TooManyParticipants(CountOccupiedSlots(table)))
                H_showMessage(kMsgTooManyText);
        }
    }
    H_destroyForm(form);
    return o;
}

// gilde.exe 0x512110 / 0x512374 / 0x524b98 — the selection-batch thief actions.
//   find existing handler(code, targetId);
//   if found: re-issue ChangePlayerAction per active selection (committed=false);
//   else: collect active selection (cap maxIds), if any -> QueueRequestSlotReset28
//         + favor voice (committed=true); else msg(dword_8C9008).
DialogOutcome SelectionBatchDialog(DialogAction action, int maxIds,
                                   bool requestExists,
                                   const SelectionTable& sel) {
    DialogOutcome o;
    o.action = static_cast<int>(action);
    o.opened = true;
    if (requestExists) {
        // the `if(i)` branch: re-issue per-character actions, no new batch.
        for (const auto& e : sel)
            if (e.present && e.active) H_changePlayerAction(0, e.id);
        return o;                                       // committed stays false
    }
    std::vector<std::int32_t> ids;
    o.count = CollectSelectionIds(sel, maxIds, ids);
    if (o.count) {
        H_queueBatch(o.action, o.count, ids);
        H_playFavorVoice();
        o.committed = true;
    } else {
        H_showMessage(0);                               // dword_8C9008 "select someone"
    }
    return o;
}

} // namespace guild::world
