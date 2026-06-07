#include "sim/combat_slots3.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hooks plumbing (inert default = all-null -> every side effect is a no-op).
// ---------------------------------------------------------------------------
namespace {
const CombatSlots3Hooks* g_hooks = nullptr;
CombatSlots3Hooks        g_inert{};
}

void SetCombatSlots3Hooks(const CombatSlots3Hooks* hooks) { g_hooks = hooks; }
const CombatSlots3Hooks& GetCombatSlots3Hooks() {
    return g_hooks ? *g_hooks : g_inert;
}

namespace {

// Iterate the GameObject set: QueryFind for the first, IterNext for the rest. With
// the inert hook (null) the iteration is empty (returns nullptr), so callers fall
// straight through to their "no object" path — matching a battlefield with no
// matching objects.
const void* QueryFirst(const CombatSlots3Hooks& h, u32 objSet) {
    return h.gameObjectQueryFind ? h.gameObjectQueryFind(objSet, 1, 5) : nullptr;
}
const void* QueryNext(const CombatSlots3Hooks& h) {
    return h.gameObjectIterNext ? h.gameObjectIterNext() : nullptr;
}
i16 ObjId(const CombatSlots3Hooks& h, const void* obj) {
    return h.gameObjectId ? h.gameObjectId(obj) : 0;
}
i32 ObjHp(const CombatSlots3Hooks& h, const void* obj) {
    return h.gameObjectHp ? h.gameObjectHp(obj) : 0;
}

// Find the ring index whose idHigh matches `id` (the `while (entry>>16 != *v1)`
// scan, stepping 23 dwords, bailing at 230). Returns -1 == no match.
int RingIndexById(const std::vector<ObjDefEntry>& ring, i16 id) {
    for (int i = 0; i < static_cast<int>(ring.size()); ++i) {
        if (ring[i].idHigh == id)
            return i;
    }
    return -1;
}

// The first free ring entry (idHigh == 0), or -1 if the ring is full. (The original
// `for (j=0; j<230; j+=23) if (!(entry>>16)) return ...` fallback.)
int FirstFreeRingIndex(const std::vector<ObjDefEntry>& ring) {
    for (int i = 0; i < static_cast<int>(ring.size()); ++i) {
        if (ring[i].idHigh == 0)
            return i;
    }
    return -1;
}

} // namespace

// gilde.exe 0x485a54 — VIBE_Combat_FindObjectDef.
int FindObjectDef(std::vector<ObjDefEntry>& ring, u32 objSet) {
    const CombatSlots3Hooks& h = GetCombatSlots3Hooks();
    const void* obj = QueryFirst(h, objSet);
    if (obj) {
        // Walk objects until one is found in the ring (result != 0 / v3 == 0 break).
        for (; obj; obj = QueryNext(h)) {
            int idx = RingIndexById(ring, ObjId(h, obj));
            if (idx >= 0) {
                // Matched a ring entry. If it is a class-1/2 entry whose object hp
                // (obj+0x20) has dropped to <= 0, the original re-purposes the slot:
                // it returns the FIRST FREE entry instead. Otherwise return the match.
                i8 cls = ring[idx].classByte;
                if ((cls == 1 || cls == 2) && ObjHp(h, obj) <= 0)
                    return FirstFreeRingIndex(ring);
                return idx;
            }
            // No ring entry for this object -> keep iterating (LABEL_9).
        }
    }
    // No object iterated / nothing matched (LABEL_19): hand out the first free slot.
    return FirstFreeRingIndex(ring);
}

// gilde.exe 0x485b1c — VIBE_Combat_FindActiveTarget.
const void* FindActiveTarget(const std::vector<ObjDefEntry>& ring, u32 objSet) {
    const CombatSlots3Hooks& h = GetCombatSlots3Hooks();
    for (const void* obj = QueryFirst(h, objSet); obj; obj = QueryNext(h)) {
        int idx = RingIndexById(ring, ObjId(h, obj));
        if (idx < 0)
            continue;                       // LABEL_9: no entry, try next object
        i8 cls = ring[idx].classByte;       // v4 = v3[88]
        if (cls != 1 && cls != 2)
            return obj;                     // non-conquerable class: always valid
        // class 1/2: valid only while hp (obj+0x20, *((int*)v1+8)) > 0.
        if (ObjHp(h, obj) <= 0)
            return nullptr;                 // dead conquerable target -> give up
        return obj;
    }
    return nullptr;
}

// gilde.exe 0x485b94 — VIBE_Combat_ResetObjectHighlights.
int ResetObjectHighlights(const std::vector<ObjDefEntry>& ring, u32 objSet,
                          i32 team, i32 objField7) {
    const CombatSlots3Hooks& h = GetCombatSlots3Hooks();
    int queued = 0;
    for (const void* obj = QueryFirst(h, objSet); obj; obj = QueryNext(h)) {
        int idx = RingIndexById(ring, ObjId(h, obj));
        if (idx < 0)
            continue;                       // LABEL_6: skip non-ring objects
        // `if ((int*)(&entry+2)) Command_QueueRequest17(...)` — the entry pointer is
        // always non-null, so a request is queued for every matched object.
        if (h.queueClearRequest)
            h.queueClearRequest(team, objField7, ObjId(h, obj));
        ++queued;
    }
    return queued;
}

// gilde.exe 0x4870f0 — VIBE_Combat_CloseSelectionWindows.
int CloseSelectionWindows(std::vector<i32>& selWindows) {
    const CombatSlots3Hooks& h = GetCombatSlots3Hooks();
    int closed = 0;
    for (i32& slot : selWindows) {
        if (slot == -1)
            continue;
        if (h.windowRemoveIfActive)
            h.windowRemoveIfActive(slot);
        slot = -1;
        ++closed;
    }
    return closed;
}

// gilde.exe 0x48751c — VIBE_Combat_ResetDamageNumberTable.
// The loop body, per record: flag(+2)=0, handle(+1)=-1, age(+0)=0.
void ResetDamageNumberTable(std::vector<DamageNumberRecord>& table) {
    for (DamageNumberRecord& r : table) {
        r.flag   = 0;    // dword_B5F6C8[..] = 0
        r.handle = -1;   // dword_B5F6C4[..] = -1
        r.age    = 0;    // dword_B5F6C0[..] = 0
    }
}

// gilde.exe 0x487548 — VIBE_Combat_DestroyDamageNumbers.
int DestroyDamageNumbers(std::vector<DamageNumberRecord>& table) {
    const CombatSlots3Hooks& h = GetCombatSlots3Hooks();
    int destroyed = 0;
    for (DamageNumberRecord& r : table) {
        if (r.widget != -1) {               // dword_B5F6D8[i] != -1
            if (h.widgetDestroyByType)
                h.widgetDestroyByType(r.widget);
            ++destroyed;
        }
        r.flag   = 0;    // dword_B5F6C8[i] = 0
        r.handle = -1;   // dword_B5F6C4[i] = -1
        r.age    = 0;    // dword_B5F6C0[i] = 0
    }
    return destroyed;
}

// gilde.exe 0x488828 — VIBE_Combat_DestroyOrderSlotWindows.
int DestroyOrderSlotWindows(std::vector<OrderSlotWindow>& table) {
    const CombatSlots3Hooks& h = GetCombatSlots3Hooks();
    int torn = 0;
    for (OrderSlotWindow& r : table) {
        if (r.window == -1)                 // dword_B5A018[..] == -1 -> skip
            continue;
        if (h.windowRemoveIfActive)
            h.windowRemoveIfActive(r.window);
        r.window = -1;                      // dword_B5A018[..] = -1
        if (h.widgetDestroyByType)
            h.widgetDestroyByType(r.widget); // dword_B5A014[..]
        ++torn;
    }
    return torn;
}

// gilde.exe 0x489578 — VIBE_Combat_GatherPlayerUnits.
std::vector<const void*> GatherPlayerUnits(const std::vector<CombatUnitSlot>& slots) {
    std::vector<const void*> gathered;
    // `do { ... } while (v1 < 32 && v4 < 31)` — scan at most 32 slots, gather <= 31.
    int scanned = 0;
    for (const CombatUnitSlot& s : slots) {
        if (s.hasUnit && s.ownedByPlayer) {     // v5 && v3 == *(v5+136)
            gathered.push_back(s.handle);
            if (static_cast<int>(gathered.size()) >= kPlayerUnitCap)
                break;                          // v4 < 31 guard
        }
        if (++scanned >= kCombatUnitCount)      // v1 < 32 guard
            break;
    }
    return gathered;
}

// gilde.exe 0x48938c — VIBE_Combat_ResetSelectionState.
SelectionResetResult ResetSelectionState(std::vector<SelectionRecord>& records,
                                         std::vector<i32>& selWindows,
                                         i32& formHandle) {
    const CombatSlots3Hooks& h = GetCombatSlots3Hooks();
    SelectionResetResult res;

    // `for (i = 0; i != 4288; i += 134)` over the 32 selection records.
    for (SelectionRecord& r : records) {
        if (r.highlightSet) {               // byte_B5A4D8[..] != 0
            r.highlightSet = false;         //   -> = 0
            ++res.highlightsCleared;
        }
        if (r.unit && r.meshHighlighted) {  // unit && (mesh+530 & 2)
            if (h.meshClearHighlight)
                h.meshClearHighlight(r.unit); // Mesh_SetVertexColors(.,0,0,0)
            ++res.meshesCleared;
        }
    }

    if (h.dragSlotResetTable)
        h.dragSlotResetTable();             // VIBE_DragSlot_ResetTable()

    // Remove the 32 selection windows (dword_B5A1D0[j] != -1).
    for (i32& w : selWindows) {
        if (w != -1) {
            if (h.windowRemoveIfActive)
                h.windowRemoveIfActive(w);
            w = -1;
            ++res.windowsClosed;
        }
    }

    if (formHandle != -1) {                 // dword_63125C != -1
        if (h.formDestroy)
            h.formDestroy(formHandle);
        formHandle = -1;
        res.formDestroyed = true;
    }
    return res;
}

// gilde.exe 0x48c15c — VIBE_Combat_IssueOrdersForTeam (row resolution).
int FindSquadRowForTeam(const std::vector<SquadRow>& rows, int rosterCount, i32 team) {
    int limit = rosterCount < static_cast<int>(rows.size())
                    ? rosterCount : static_cast<int>(rows.size());
    for (int i = 0; i < limit; ++i) {
        if (rows[i].teamId == team)         // teamId[i] == *(a1+4)
            return i;                       // v2 = &dword_11AB000[215*i]; break
    }
    return -1;                              // v2 stays 0
}

// gilde.exe 0x48c15c — VIBE_Combat_IssueOrdersForTeam (slot selection).
std::vector<i32> CollectOrderSlotIds(const SquadRow& row, bool isPlayerSide) {
    // `v5 = (a1 == dword_6311E8) ? base+192 : base+128` then walk the 16-entry row,
    // building an order for each id != -1.
    const i32* ids = isPlayerSide ? row.playerSideIds : row.genericSideIds;
    std::vector<i32> out;
    for (int i = 0; i < kSquadOrderSlots; ++i) {
        if (ids[i] != -1)                   // `if (*v6 != -1)`
            out.push_back(ids[i]);
    }
    return out;
}

// gilde.exe 0x48c5e8 — VIBE_Combat_RunBattleFrameLoop.
int RunBattleFrameLoop(int frameArg, int stateArg, bool slowMoGate,
                       int tickCounter, int tickLimit, bool& forcedStep) {
    const CombatSlots3Hooks& h = GetCombatSlots3Hooks();
    forcedStep = false;
    int result = 0;
    while (true) {
        result = h.gameLogicRunFrameLoop ? h.gameLogicRunFrameLoop(frameArg, stateArg) : 0;
        if (!result)
            break;                          // RunFrameLoop returned 0 -> done
        if (h.cameraEdgeScroll)        h.cameraEdgeScroll();
        if (h.combatPickObjectUnderCursor) h.combatPickObjectUnderCursor();
        if (h.combatUpdateBombExplosions)  h.combatUpdateBombExplosions();
        if (h.combatUpdateThrownBombs)     h.combatUpdateThrownBombs();
        // `if (dword_672230 || v4 <= dword_6315A8) { dword_631614 = 1; UpdateProj(); }
        //  else UpdateProj();` — projectiles always update; the gate only sets the
        // "force-step" flag dword_631614.
        if (slowMoGate || tickCounter <= tickLimit)
            forcedStep = true;
        if (h.combatUpdateProjectiles) h.combatUpdateProjectiles();
    }
    return result;
}

// gilde.exe 0x48c648 (inner) — VIBE_Combat_RunOrderWaitLoop.
bool AnyOrderUnitMoving(const std::vector<OrderWaitSlot>& slots) {
    int scanned = 0;
    for (const OrderWaitSlot& s : slots) {
        // `if (record+4) { if (record+0 != -1) { unit = FindUnitById(...); if (unit)
        //  { v7 = unit+97; if (v7) if (*(v7+296)) break(=busy); } } }`
        if (s.active && s.hasId && s.unitMoving)
            return true;                    // a1 = 1 (busy) ; break
        if (++scanned >= kSquadOrderSlots)  // `if (v4 >= 16) goto LABEL_12`
            break;
    }
    return false;
}

// gilde.exe 0x492e6c — VIBE_Combat_BeginBattleOrCacheState.
BattleBeginAction BeginBattleOrCacheState(BattleCacheRecord& rec, i32 duelMode,
                                          i32 pendingFlag, i32 pendingValue) {
    if (duelMode) {                         // if (dword_6315A4) return RunBattleLoop(...)
        return BattleBeginAction::kRunBattleLoop;
    }
    if (pendingFlag) {                      // if (dword_6311F4)
        // `*(rec+852) = dword_6315A4; *(rec+856) = dword_6311F8;` — note +852 is set
        // to duelMode, which is 0 on this branch (we just tested it).
        rec.field852 = duelMode;            // == 0
        rec.field856 = pendingValue;
        return BattleBeginAction::kCachedPending;
    }
    rec.field852 = 1;                       // else *(rec+852) = 1
    return BattleBeginAction::kCachedDefault;
}

} // namespace guild::sim
