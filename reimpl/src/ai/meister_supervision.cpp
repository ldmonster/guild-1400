#include "ai/meister_supervision.h"

namespace guild::ai {

// ---------------------------------------------------------------------------
// Recovered double constants (raw bytes -> values; see header). Decoded
// byte-for-byte from gilde.exe at the listed VAs.
// ---------------------------------------------------------------------------
namespace {
constexpr double dbl_619970 = 168.0;  // FlagIdleStaff "is idle" gauge threshold
constexpr double dbl_619968 = 252.0;  // FlagIdleStaff "fresh staff" reprieve gate
constexpr double dbl_61E884 = 0.5;    // UpdateBuildingHealthState critical ratio

// --- inert default leaves ---------------------------------------------------
bool Def_handler_exists(int, int, int) { return false; }
void Def_queue_slot_reset28(int, u8, int, int) {}
void Def_request_build_op83(int) {}
void Def_request_build_op84(int, int) {}
u16  Def_rng_mod(u16) { return 0; }
void Def_change_player_action(int, int) {}
void Def_send_quickjump(int, int) {}
void Def_queue_state22(int) {}

SupervisionHooks MakeDefaultHooks() {
    SupervisionHooks h;
    h.handler_exists       = Def_handler_exists;
    h.queue_slot_reset28   = Def_queue_slot_reset28;
    h.request_build_op83   = Def_request_build_op83;
    h.request_build_op84   = Def_request_build_op84;
    h.rng_mod              = Def_rng_mod;
    h.change_player_action = Def_change_player_action;
    h.send_quickjump       = Def_send_quickjump;
    h.queue_state22        = Def_queue_state22;
    return h;
}

SupervisionHooks& HookSlot() {
    static SupervisionHooks h = MakeDefaultHooks();
    return h;
}

SupervisionHooks BindHooks(const SupervisionHooks& in) {
    SupervisionHooks d = MakeDefaultHooks();
    SupervisionHooks e = in;
    if (!e.handler_exists)       e.handler_exists = d.handler_exists;
    if (!e.queue_slot_reset28)   e.queue_slot_reset28 = d.queue_slot_reset28;
    if (!e.request_build_op83)   e.request_build_op83 = d.request_build_op83;
    if (!e.request_build_op84)   e.request_build_op84 = d.request_build_op84;
    if (!e.rng_mod)              e.rng_mod = d.rng_mod;
    if (!e.change_player_action) e.change_player_action = d.change_player_action;
    if (!e.send_quickjump)       e.send_quickjump = d.send_quickjump;
    if (!e.queue_state22)        e.queue_state22 = d.queue_state22;
    return e;
}

// Apply defaults to a caller-supplied hooks value (used by the passes that take
// a const SupervisionHooks& directly).
SupervisionHooks WithDefaults(const SupervisionHooks& in) { return BindHooks(in); }
} // namespace

SupervisionHooks SetSupervisionHooks(const SupervisionHooks& hooks) {
    SupervisionHooks prev = HookSlot();
    HookSlot() = BindHooks(hooks);
    return prev;
}
SupervisionHooks GetSupervisionHooks() { return HookSlot(); }

// ---------------------------------------------------------------------------
// 0x4c73b4 — VIBE_MeisterAi_RequestCmd134.
//   result = He_FindFirstHandlerByFilter(1, 0, 134);
//   if (!result) { ...pack type 0x86(=134) reset, master = *(dword_6498E4+4)...
//                  QueueRequestSlotReset28(); return; }
// We collapse the packet to (master build id, type 134); the game-time tail is
// dropped exactly as the sibling RequestSlotResetCmd helpers do.
// ---------------------------------------------------------------------------
bool RequestCmd134(int masterBuildId) {
    SupervisionHooks h = HookSlot();
    if (h.handler_exists(/*filterKind*/ 1, /*type*/ 134, /*owner*/ 0))
        return false;
    h.queue_slot_reset28(masterBuildId, /*type*/ 134, /*field7*/ -1, /*kind*/ 2);
    return true;
}

// ---------------------------------------------------------------------------
// 0x4c7308 — VIBE_MeisterAi_RequestBuildingCmd43.
//   for (p = Person_QueryBegin(master,1,5,7); p; p = Person_IterNext()) {
//     h = He_FindFirstHandlerByFilter(1,0,43);
//     if (h) { while (h->field4 != p->id) { h = He_FindNextMatchingHandler();
//                                           if (!h) goto emit; }
//              found = 1; }     // a pending 43 already targets this person
//     emit: if (!found) QueueRequestSlotReset28(type 43, master, p->id, kind 2);
//   }
// NOTE: in the original `found` (a1) is NOT reset between persons, so once any
// person has a matching handler every later person is skipped. Reproduced.
// ---------------------------------------------------------------------------
int RequestBuildingCmd43(int masterBuildId, const int* employeeIds, int employeeCount) {
    SupervisionHooks h = HookSlot();
    int emitted = 0;
    bool found = false;  // a1 — sticky across the person loop, as in the original
    for (int i = 0; i < employeeCount; ++i) {
        int pid = employeeIds[i];
        // handler_exists(kind=1,type=43,owner=pid): does a pending 43 target pid?
        if (h.handler_exists(/*filterKind*/ 1, /*type*/ 43, /*owner*/ pid))
            found = true;
        if (!found) {
            h.queue_slot_reset28(masterBuildId, /*type*/ 43, /*field7*/ pid, /*kind*/ 2);
            ++emitted;
        }
    }
    return emitted;
}

// ---------------------------------------------------------------------------
// 0x4c7590 — VIBE_MeisterAi_ClearDarkCorner.
//   for each tavern employee, resolve the dark-corner room (288 -> 300) and read
//   its last-use day (room+40). If currentDay - lastUseDay > 1, emit op83.
// lastUseDays[i] == INT_MIN sentinel means the room chain didn't resolve.
// ---------------------------------------------------------------------------
int ClearDarkCorner(int currentDay, const int* lastUseDays, int employeeCount) {
    SupervisionHooks h = HookSlot();
    int emitted = 0;
    for (int i = 0; i < employeeCount; ++i) {
        int last = lastUseDays[i];
        if (last == (-2147483647 - 1))  // INT_MIN: room unresolved
            continue;
        if (currentDay - last > 1) {
            h.request_build_op83(/*buildId*/ i);
            ++emitted;
        }
    }
    return emitted;
}

// ---------------------------------------------------------------------------
// 0x4c74c8 — VIBE_MeisterAi_SuperviseStammtisch.
//   for each seat: occ = seat.occupantId (word+14, stride 2 over 8 seats).
//   if (occ != -1) { rec = Person_FindRecordById(occ);
//                    if (!rec || !rec[8] || rec[2]==15) emit op84(seat); }
// ---------------------------------------------------------------------------
int SuperviseStammtisch(const StammtischSeat* seats, int seatCount) {
    SupervisionHooks h = HookSlot();
    int emitted = 0;
    for (int i = 0; i < seatCount; ++i) {
        const StammtischSeat& s = seats[i];
        if (s.occupantId == -1)
            continue;
        bool needs = !s.recordFound || !s.active || s.state == 15;
        if (needs) {
            h.request_build_op84(/*buildId*/ 0, s.occupantId);
            ++emitted;
        }
    }
    return emitted;
}

// ---------------------------------------------------------------------------
// 0x45df7c — VIBE_MeisterAi_FlagIdleStaff.
// ---------------------------------------------------------------------------
int FlagIdleStaff(BuildingFlag* building, int buildId, StaffSlot* slots,
                  int slotCount, const SupervisionHooks& hooksIn) {
    SupervisionHooks h = WithDefaults(hooksIn);
    if (!building)
        return -1;  // (not in the binary; defensive null guard for our view)
    // if (building+456 & 4) return 0;  — already supervised this pass (0x45e07a)
    if (building->supervisedBit & 4)
        return 0;
    building->supervisedBit |= 4;

    // Pass 1: find the first idle candidate (v2 set, v3 = scanned count).
    // owner gate (0x45e09b): dword_12CEA7C[slot] == *(building+364) == buildId.
    bool candidate = false;
    int firstSlot = 0;  // v3 — increments every iteration in the original
    for (int i = 0; i < slotCount; ++i) {
        const StaffSlot& s = slots[i];
        bool idle = s.live && s.ownerBuildId == buildId && s.employed
                    && !s.hasActionObj && (s.busyFlags & 4) == 0
                    && (static_cast<double>(s.gaugeA) < dbl_619970
                        || static_cast<double>(s.gaugeB) < dbl_619970);
        firstSlot = i + 1;  // ++v3 is unconditional in the original
        if (idle) { candidate = true; break; }
    }

    // Reprieve roll: a candidate exists but rng_mod(128) < 32 -> abort.
    if (candidate && h.rng_mod(0x80) < 0x20)
        return 0;
    // No candidate: the original reads byte_12CE993[536*v3] where v3 is the count
    // of slots scanned (the slot one past the last scanned). If that slot is
    // "fresh" (gauge < 252.0) and rng_mod(128) > 96 -> skip. The original relies
    // on the staff array being over-allocated (768 entries) so slot[v3] is always
    // valid; callers must size `slots` to at least slotCount+1 (or pass the full
    // capacity). We honor that read faithfully (no extra bounds clamp).
    if (!candidate) {
        const StaffSlot& s0 = slots[firstSlot];
        if ((static_cast<double>(s0.gaugeA) < dbl_619968
             || static_cast<double>(s0.gaugeB) < dbl_619968)
            && h.rng_mod(0x80) > 0x60) {
            return 0;
        }
    }

    // Pass 2: flag every idle-eligible slot with bit 0x10 (owner-gated, 0x45e052).
    for (int i = 0; i < slotCount; ++i) {
        StaffSlot& s = slots[i];
        if (s.live && s.ownerBuildId == buildId && s.employed && !s.hasActionObj
            && (s.busyFlags & 4) == 0)
            s.flags |= 0x10;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x45d3ec — VIBE_MeisterAi_CountStaffByType.
// The original is a recursive tree walk. We model it as: a head node (pre-checked
// once if not already counted), `head.childCount` direct members from `nodes`,
// and each member may root a sub-tree (childTree index into treeRoots) that is
// recursed into. Classification: trade 23 or 37 -> master, else other.
// ---------------------------------------------------------------------------
namespace {
inline bool IsMasterTrade(u8 t) { return t == 23 || t == 37; }

void WalkSubtree(int rootIdx, const StaffNode* nodes, int nodeCount,
                 const int* treeRoots, int treeRootCount, StaffCount& acc);

void WalkMembers(const StaffNode* members, int memberCount,
                 const StaffNode* nodes, int nodeCount,
                 const int* treeRoots, int treeRootCount, StaffCount& acc) {
    for (int i = 0; i < memberCount; ++i) {
        const StaffNode& m = members[i];
        if (!m.counted) {
            if (IsMasterTrade(m.trade)) ++acc.masters;
            else                        ++acc.others;
        }
        if (m.childTree >= 0)
            WalkSubtree(m.childTree, nodes, nodeCount, treeRoots, treeRootCount, acc);
    }
}

void WalkSubtree(int rootIdx, const StaffNode* nodes, int nodeCount,
                 const int* treeRoots, int treeRootCount, StaffCount& acc) {
    if (rootIdx < 0 || rootIdx >= treeRootCount) return;
    int base = treeRoots[rootIdx];
    if (base < 0 || base >= nodeCount) return;
    // Each subtree is a contiguous run of 8 member slots in `nodes` (the original
    // walks 8 entries of stride 2 -> head+8). We honor nodeCount as the bound.
    int count = 8;
    if (base + count > nodeCount) count = nodeCount - base;
    WalkMembers(nodes + base, count, nodes, nodeCount, treeRoots, treeRootCount, acc);
}
} // namespace

StaffCount CountStaffByType(const StaffNode* head, int headChildCount,
                            const StaffNode* nodes, int nodeCount,
                            const int* treeRoots, int treeRootCount) {
    StaffCount acc;
    // Head pre-check: counted only if its "already counted" bit (record+84&1) is
    // clear; then it is marked counted.
    if (head && !head->counted) {
        if (IsMasterTrade(head->trade)) ++acc.masters;
        else                            ++acc.others;
    }
    if (head) {
        // The head's direct members start right after the head record.
        WalkMembers(head + 1, headChildCount, nodes, nodeCount, treeRoots,
                    treeRootCount, acc);
    }
    return acc;
}

// ---------------------------------------------------------------------------
// 0x4c9104 — VIBE_MeisterAi_UpdateBuildingHealthState.
// ---------------------------------------------------------------------------
int UpdateBuildingHealthState(const HealthEmp* emps, int empCount,
                              const SupervisionHooks& hooksIn) {
    SupervisionHooks h = WithDefaults(hooksIn);
    int queued = 0;
    for (int i = 0; i < empCount; ++i) {
        const HealthEmp& e = emps[i];
        if (!e.inBuilding || e.noUpdate)
            continue;
        if ((e.trade == 6 || e.trade == 7) && e.maxHealth > 0) {
            double ratio = static_cast<double>(e.curHealth)
                           / static_cast<double>(e.maxHealth);
            if (ratio >= dbl_61E884)
                h.send_quickjump(/*msgId*/ 5297, e.ownerId);  // low health
            else
                h.send_quickjump(/*msgId*/ 5296, e.ownerId);  // critical
        }
        h.queue_state22(e.personId);
        ++queued;
    }
    return queued;
}

// ---------------------------------------------------------------------------
// 0x4c930c — VIBE_MeisterAi_RunBuildingTasks dispatcher.
// Invokes each reconstructed sub-task in the canonical order recorded by
// ai/building_needs.cpp::RunBuildingTasksOrder(). AssignWorkersToBuilding is the
// one still-deferred leaf in this slice (its body needs the live worker/room
// resolver + BuildingValue worth math), so it is an inert no-op here and noted.
// ---------------------------------------------------------------------------
int RunBuildingTasks(const SupervisionContext& ctx) {
    SupervisionHooks h = HookSlot();
    int emitted = 0;
    for (BuildingTask task : RunBuildingTasksOrder()) {
        switch (task) {
        case BuildingTask::RequestBuildingCmd43:
            emitted += RequestBuildingCmd43(ctx.masterBuildId, ctx.employeeIds,
                                            ctx.employeeCount);
            break;
        case BuildingTask::RequestCmd134:
            if (RequestCmd134(ctx.masterBuildId)) ++emitted;
            break;
        case BuildingTask::AssignWorkersToBuilding:
            // Deferred leaf (VIBE_MeisterAi_AssignWorkersToBuilding @0x4c8f14):
            // inert until the worker/room resolver is reconstructed.
            break;
        case BuildingTask::ClearDarkCorner:
            emitted += ClearDarkCorner(ctx.currentDay, ctx.darkCornerLastUse,
                                       ctx.darkCornerCount);
            break;
        case BuildingTask::SuperviseStammtisch:
            emitted += SuperviseStammtisch(ctx.stammtischSeats, ctx.stammtischCount);
            break;
        case BuildingTask::UpdateBuildingHealthState:
            emitted += UpdateBuildingHealthState(ctx.healthEmps, ctx.healthEmpCount, h);
            break;
        }
    }
    // The idle-staff flag pass is part of the daily sweep but runs outside the
    // six-task dispatch table; run it here if a building flag/staff view is given.
    if (ctx.buildingFlag && ctx.staffSlots) {
        if (FlagIdleStaff(ctx.buildingFlag, ctx.masterBuildId, ctx.staffSlots,
                          ctx.staffSlotCount, h) == 1) {
            // flagging emits no command packets (it mutates slot flag bytes only)
        }
    }
    return emitted;
}

} // namespace guild::ai
