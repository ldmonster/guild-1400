#include "sim/combat_orders2.h"

#include <cstdio>
#include <cstring>

namespace guild::sim {

// ===========================================================================
// Hooks plumbing (inert defaults) — mirrors CombatDriversHooks.
// ===========================================================================
namespace {
OrderDriverHooks       g_defaultHooks;   // all-null -> inert
const OrderDriverHooks* g_hooks = &g_defaultHooks;

// --- inert-aware leaf wrappers (the original's leaf calls, defaulted) ---------
int HPacketStatus(i32 id) {
    return g_hooks->packetStatus ? g_hooks->packetStatus(id) : 0;
}
OrderDriverHooks::UnitInfo HResolveUnit(i32 id, u8 state) {
    if (g_hooks->resolveUnit) return g_hooks->resolveUnit(id, state);
    return OrderDriverHooks::UnitInfo{};  // alive, no def
}
bool HOnTile(const OrderSlot& s, float tol) {
    return g_hooks->onTargetTile ? g_hooks->onTargetTile(s, tol) : false;
}
bool HFindFree(i32 inX, i32 inZ, i32& outX, i32& outZ) {
    return g_hooks->findFreeTile ? g_hooks->findFreeTile(inX, inZ, outX, outZ) : false;
}
bool HBusy(i32 id) {
    return g_hooks->unitBusy ? g_hooks->unitBusy(id) : false;
}
int HRoll(u16 n) {
    return g_hooks->randomModulo ? g_hooks->randomModulo(n) : 0;
}
void HEmit(int slotIdx, OrderEmit op) {
    if (g_hooks->emit) g_hooks->emit(slotIdx, op);
}

// The ready sentinel: a slot advances when its in-flight packet is the ready
// sentinel 1, OR GetPacketStatusById reports it applied.
//   `*((_DWORD *)v4 + 2) == 1 || VIBE_Command_GetPacketStatusById(...)`
bool PacketReady(const OrderSlot& slot) {
    return slot.packetId == 1 || HPacketStatus(slot.packetId) != 0;
}
} // namespace

void SetOrderDriverHooks(const OrderDriverHooks* hooks) {
    g_hooks = hooks ? hooks : &g_defaultHooks;
}
const OrderDriverHooks& GetOrderDriverHooks() { return *g_hooks; }

// ---------------------------------------------------------------------------
// LABEL_202 — the shared "objective reached" terminal emission.
//   v73[0]=*(v7+4); LOBYTE(v73[1])=0;
//   RequestBuildOp85Unit(v7, 6, v73);
//   *((_DWORD*)v4+2) = RequestBuildOp80(v88[4], v73);
// We reset packetId to -1 first (every case does so right after the gate) is NOT
// done here — the cases that jump to LABEL_202 already reset packetId; LABEL_202
// only re-assigns it via the Op80 result. With the abstracted sink we leave
// packetId as the post-gate -1 (Op80's ring slot is the leaf result we don't
// model), preserving the "packet now pending" 1:1 shape via the emit reporting.
// ---------------------------------------------------------------------------
void EmitObjectiveReached(OrderSlot& slot, int slotIndex) {
    HEmit(slotIndex, OrderEmit::Op85Anim);   // RequestBuildOp85Unit(v7, 6, ...)
    HEmit(slotIndex, OrderEmit::Op80Sync);   // RequestBuildOp80(...)
    (void)slot;
}

// ---------------------------------------------------------------------------
// One slot, one tick. Returns true if the slot did work (entered its case body).
// Faithfully follows the decompile's per-case shape; scene/path/command leaves
// route through the hooks. Tile fields are mutated exactly as the original writes
// them (v4+4/+5/+6 == tileX/tileZ/tileAux; v4[12] == phase; packetId == v4+2).
// ---------------------------------------------------------------------------
static bool TickSlot(OrderSlot& slot, int idx, bool unitAlive, i16 defClass) {
    (void)defClass;   // the weapon-class refinement lives in EvaluateAttack
    switch (slot.state) {
    // --- case 1: move -------------------------------------------------------
    case 1: {
        if (!PacketReady(slot)) return false;
        const u8 phase = slot.phase;            // v8 = v4[12]
        slot.packetId = -1;                     // *((_DWORD*)v4+2) = -1
        if (phase) {
            // TileToWorld(tileX,tileZ); within 30 -> objective reached.
            if (HOnTile(slot, kTolMove)) {
                EmitObjectiveReached(slot, idx); // LABEL_202
            }
        } else {
            i32 outX = slot.tileX, outZ = slot.tileZ;
            if (HFindFree(slot.tileX, slot.tileZ, outX, outZ)) {
                // sprintf("cmb_%i"); phase=1; Op85 anim; Op78 path.
                slot.phase = 1;
                HEmit(idx, OrderEmit::Op85Anim);
                HEmit(idx, OrderEmit::Op78Path);
                slot.packetId = 1;  // RequestBuildOp78DualStr result (pending)
            }
        }
        return true;
    }

    // --- case 2: attack -----------------------------------------------------
    // The range/hit/damage RULE proper lives in combat_battle.cpp EvaluateAttack;
    // here we drive its OUTER bookkeeping 1:1: the gate, the firing-flag write
    // (v4[29]), the hit roll (RandomModulo(255)) and, on the completion edge, the
    // Op85 anim2 + Op80 sync emission. We surface the firing decision and damage.
    case 2: {
        // a2 = *(v4+2); gate: !=1 && !status -> bail.
        if (slot.packetId != 1 && HPacketStatus(slot.packetId) == 0) return false;
        slot.packetId = -1;
        if (!unitAlive) { EmitObjectiveReached(slot, idx); return true; }
        // The hit-chance roll (the decompile's VIBE_Math_RandomModulo(0xFFu)).
        // Firing decision is computed at the EvaluateAttack altitude; the driver
        // only consumes the roll + writes the firing flag and emits on completion.
        const int roll = HRoll(0xFF);
        const bool fires = roll != 0;          // abstracted; EvaluateAttack refines
        slot.firing = static_cast<u8>(fires ? 1 : 0);  // v4[29] = v85
        // On the firing/completion edge the original queues anim2 + sync (the
        // LABEL_94/95 path): Op85 anim mode 2 then Op80.
        HEmit(idx, OrderEmit::Op85Anim);
        HEmit(idx, OrderEmit::Op80Sync);
        return true;
    }

    // --- case 3: march ------------------------------------------------------
    case 3: {
        if (!PacketReady(slot)) return false;
        const u8 phase = slot.phase;            // v49
        slot.packetId = -1;
        if (!phase && !HOnTile(slot, kTolMarch)) {
            i32 outX = slot.tileX, outZ = slot.tileZ;
            if (HFindFree(slot.tileX, slot.tileZ, outX, outZ)) {
                slot.phase = 1;
                HEmit(idx, OrderEmit::Op85Anim);
                HEmit(idx, OrderEmit::Op78Path);
                slot.packetId = 1;
            }
        }
        // TileToWorld(tileX,tileZ); within 50 -> anim1 + sync.
        if (HOnTile(slot, kTolMarch)) {
            HEmit(idx, OrderEmit::Op85Anim);    // RequestBuildOp85Unit(v7, 1, ...)
            HEmit(idx, OrderEmit::Op80Sync);
            slot.packetId = 1;
        }
        return true;
    }

    // --- case 4: capture ----------------------------------------------------
    // Bone-probe + tolerance branches. The object-probe (FindByHandle/bone chain)
    // is a leaf; we drive the two emission outcomes the decompile reaches:
    //   * reached/standing on the captured object (within 49.5) -> objective Op80;
    //   * else a free-tile walk found -> phase=1, anim6 + Op78.
    case 4: {
        if (!PacketReady(slot)) return false;
        slot.packetId = -1;
        if (HOnTile(slot, kTolCapture)) {
            // !*(v83+8) / completion -> LABEL_190 (Op80 objective sync).
            EmitObjectiveReached(slot, idx);
        } else {
            // The original walks via the +32/+36 dwords (`*((_DWORD*)v4+8)` /
            // `*((_DWORD*)v4+9)`), which are the hitFlag (+32) and predictedDamage
            // (+36) fields reused as the capture-walk tile pair — NOT tileX/tileAux.
            i32 outX = slot.hitFlag, outZ = slot.predictedDamage; // *(v4+8),*(v4+9)
            if (!slot.phase && HFindFree(slot.hitFlag, slot.predictedDamage, outX, outZ)) {
                slot.phase = 1;
                slot.hitFlag         = outX;    // *((_DWORD*)v4+8)=v82[0]
                slot.predictedDamage = outZ;    // *((_DWORD*)v4+9)=v76
                HEmit(idx, OrderEmit::Op85Anim);    // anim mode 6
                HEmit(idx, OrderEmit::Op78Path);
                slot.packetId = 1;
            }
        }
        return true;
    }

    // --- case 5: stand ------------------------------------------------------
    case 5: {
        if (!PacketReady(slot)) return false;
        slot.packetId = -1;
        HEmit(idx, OrderEmit::Op85Anim);        // RequestBuildOp85Unit(v7, 7, ...)
        HEmit(idx, OrderEmit::Op80Sync);
        slot.packetId = 1;
        return true;
    }

    // --- case 6: ware-collect ----------------------------------------------
    // The original (decompile @0x492505) first re-checks the ware object exists
    // (FindByHandle -> if gone, Op85(6)+Op80 objective sync). Then it branches on
    // warePhase (v4[34], the hitFlag byte-2 alias) AND the unit-busy flag
    // (v53 = *(actor+296)). The free-tile / TileToWorld calls use the BYTE tile
    // fields (u8)v4[32]/(u8)v4[33] — the low two bytes of hitFlag — NOT tileX/tileZ:
    //   warePhase 1 & !busy: probe ware tile + free-tile -> Op81 + Op78;
    //   warePhase 2:         within 50 -> Op85(5) + Op80;
    //   warePhase 0 & busy:  within 50 -> Op85(4) + Op81;
    //   warePhase 0 & !busy: phase=1, Op85(6), free-tile -> Op78.
    case 6: {
        if (!PacketReady(slot)) return false;
        slot.packetId = -1;
        const u8 warePhase = slot.WarePhase();      // v4[34]
        const bool busy = HBusy(slot.unitId);       // v53 = *(actor+296)
        const i32 wx = static_cast<i32>(static_cast<u8>(slot.hitFlag));         // (u8)v4[32]
        const i32 wz = static_cast<i32>(static_cast<u8>(slot.hitFlag >> 8));    // (u8)v4[33]
        if (warePhase) {
            if (warePhase == 1) {
                if (!busy) {
                    i32 outX = wx, outZ = wz;
                    if (HFindFree(wx, wz, outX, outZ)) {
                        HEmit(idx, OrderEmit::Op81Ware); // RequestBuildOp81 ware-save
                        HEmit(idx, OrderEmit::Op78Path);
                        slot.packetId = 1;
                    }
                }
            } else if (warePhase == 2) {
                if (HOnTile(slot, kTolMarch)) {          // within 50
                    HEmit(idx, OrderEmit::Op85Anim);     // anim mode 5
                    HEmit(idx, OrderEmit::Op80Sync);
                    slot.packetId = 1;
                }
            }
        } else if (busy) {                               // warePhase 0 + busy
            if (HOnTile(slot, kTolMarch)) {              // within 50
                HEmit(idx, OrderEmit::Op85Anim);         // anim mode 4
                HEmit(idx, OrderEmit::Op81Ware);
                slot.packetId = 1;
            }
        } else {                                         // warePhase 0 + idle
            slot.phase = 1;                              // v4[12] = 1
            HEmit(idx, OrderEmit::Op85Anim);             // anim mode 6
            i32 outX = wx, outZ = wz;
            if (HFindFree(wx, wz, outX, outZ)) {
                HEmit(idx, OrderEmit::Op78Path);
                slot.packetId = 1;
            }
        }
        return true;
    }

    // --- case 7: escape -----------------------------------------------------
    case 7: {
        if (!PacketReady(slot)) return false;
        const u8 phase = slot.phase;            // v65
        slot.packetId = -1;
        if (phase) {
            if (HOnTile(slot, kTolMove)) {      // within 30
                EmitObjectiveReached(slot, idx); // LABEL_202
            }
        } else {
            i32 outX = slot.tileX, outZ = slot.tileZ;
            if (HFindFree(slot.tileX, slot.tileZ, outX, outZ)) {
                slot.phase = 1;
                HEmit(idx, OrderEmit::Op85Anim);
                HEmit(idx, OrderEmit::Op78Path);
                slot.packetId = 1;
            }
        }
        return true;
    }

    // --- case 8: standup ----------------------------------------------------
    case 8: {
        if (!PacketReady(slot)) return false;
        const u8 phase = slot.phase;            // v68
        slot.packetId = -1;
        if (!phase) {
            slot.phase = 1;
            HEmit(idx, OrderEmit::Op85Anim);    // anim mode 6
            slot.packetId = 1;
        }
        return true;
    }

    default:
        // case 0 (idle) and anything else -> LABEL_14 (no work).
        return false;
    }
}

int UpdateUnitOrders(std::vector<OrderSlot>& slots, bool globalHalt) {
    // Prelude: `if (!dword_6311F0)` — when the global halt latch is set the driver
    // does nothing this tick.
    if (globalHalt) return 0;

    int worked = 0;
    const int n = static_cast<int>(slots.size());
    // The original walks exactly 16 slots (v79 < 16). We honour the supplied count
    // but never exceed 16 (the squad's fixed roster).
    const int limit = n < kOrderSlotsPerSquad ? n : kOrderSlotsPerSquad;

    for (int i = 0; i < limit; ++i) {
        OrderSlot& slot = slots[i];

        // `if (!v81[4]) goto LABEL_15;` — state 0 (idle) advances past.
        if (slot.state == 0) continue;
        // `if (*(_DWORD *)v81 == -1) goto LABEL_15;` — empty slot.
        if (slot.unitId == -1) continue;

        // Resolve the unit: state 5 -> Person id, else CombatUnit id. The leaf
        // returns alive (*(unit+8)) + the weapon def class word.
        OrderDriverHooks::UnitInfo info = HResolveUnit(slot.unitId, slot.state);

        // The dead-unit PRUNE:
        //   if ((!v7 || !*(_BYTE*)(v7+8)) && v87) { *v4=-1; v4[4]=0; return; }
        // i.e. when the unit is dead/missing AND a def exists, reset the slot and
        // BAIL the whole driver (the original returns). We mirror the reset + bail.
        if (!info.alive && info.hasDef) {
            slot.unitId = -1;
            slot.state  = 0;
            break;   // the original returns here
        }

        // The unit-busy flag (*(actor+296)) is consulted inside the per-case
        // bodies in the original (only the ware-collect case 6 reads it), so it is
        // queried there (HBusy), not here.

        if (TickSlot(slot, i, info.alive, info.defClass))
            ++worked;
    }
    return worked;
}

// ===========================================================================
// BuildDummyTargetUnits — FERN/NAHK naming rule.
// ===========================================================================
bool IsFernTargetClass(i16 defClass) {
    return defClass == 372 || defClass == 374 || defClass == 350 || defClass == 352;
}

std::vector<DummyTargetName> BuildDummyTargetNames(const std::vector<i32>& roster,
                                                   const char* tag,
                                                   const std::vector<i16>& defClassOf) {
    std::vector<DummyTargetName> out;
    int fernCounter = 1;   // v26 — FERN 1-based counter
    int nahkCounter = 1;   // v23 — NAHK 1-based counter
    const char* t = tag ? tag : "";

    const int n = static_cast<int>(roster.size());
    const int limit = n < 16 ? n : 16;          // the original's `v25 < 16`
    for (int i = 0; i < limit; ++i) {
        if (roster[i] == -1) continue;          // `if (*v2 == -1) goto LABEL_3`
        // A roster id that resolves to no unit (negative class sentinel) is skipped
        // (FindUnitById/FindObjectDef null -> LABEL_3 in the original).
        if (i >= static_cast<int>(defClassOf.size())) continue;
        const i16 cls = defClassOf[i];
        if (cls < 0) continue;

        DummyTargetName d;
        if (IsFernTargetClass(cls)) {
            d.isFern = true;
            d.index  = fernCounter++;            // v10 = v26++
            std::snprintf(d.name, sizeof(d.name), "dummy_%s_FERN_%02i", t, d.index);
        } else {
            d.isFern = false;
            d.index  = nahkCounter++;            // v11 = v23++
            std::snprintf(d.name, sizeof(d.name), "dummy_%s_NAHK_%02i", t, d.index);
        }
        out.push_back(d);
    }
    return out;
}

// ===========================================================================
// CreateOrderSlotWindows — slot-table reset + highlight-eligibility rule.
// ===========================================================================
void ResetOrderSlotTables(i32* tables[6]) {
    // The original: v0 starts 0; do { v0+=8; tbl[v0]=-1; ... } while (v0 != 48).
    // So it writes indices 8,16,24,32,40,48 (6 entries) of each of the 6 tables.
    for (int t = 0; t < 6; ++t) {
        if (!tables[t]) continue;
        for (int k = 1; k <= kOrderSlotTableEntries; ++k)   // 8,16,..,48
            tables[t][k * 8] = -1;
    }
}

int CountOrderSlotRows(const std::vector<bool>& populated,
                       const std::vector<bool>& factionMatch,
                       bool globalHighlight) {
    int rows = 0;     // v21
    int col  = 0;     // v2 (advances by 8 per row)
    const int n = static_cast<int>(populated.size());
    // `while (v22 < 31 && v2 < 48)` — roster bound 31, slot-column bound 48.
    for (int i = 0; i < n && i < 31 && col < 48; ++i) {
        if (!populated[i]) continue;            // `if (v3)` gate (dword_11BB6A0[i])
        // The highlight gate: v4 = 8 when faction match OR global highlight.
        bool match = (i < static_cast<int>(factionMatch.size())) && factionMatch[i];
        bool highlight = match || globalHighlight;
        if (!highlight) continue;               // `if ((v4 & 8) != 0)`
        ++rows;
        col += 8;                               // v2 += 8
    }
    return rows;
}

// ===========================================================================
// BuildObjectiveIcons — grid layout + price total.
// ===========================================================================
std::vector<ObjectiveIcon> BuildObjectiveIcons(const std::vector<ObjectiveEntry>& entries,
                                               int mode, i32 playerFaction,
                                               int baseX, int baseY, i32& outTotal) {
    std::vector<ObjectiveIcon> out;
    int idx = 0;                  // v3 — placed-icon index (drives the grid pos)
    long long total = 0;          // v22 accumulator (truncated to int each step)

    for (const ObjectiveEntry& e : entries) {
        // Filter (v25): 0 all; 1 only owner==player; 2 only owner!=player.
        //   `if (!v25 || v25==1 && playerFaction==owner) goto place;`
        //   `if (v25 != 2) skip;  if (owner != player) place; else skip;`
        bool place;
        if (mode == 0) place = true;
        else if (mode == 1) place = (e.factionOwner == playerFaction);
        else if (mode == 2) place = (e.factionOwner != playerFaction);
        else place = false;
        if (!place) continue;

        ObjectiveIcon ic;
        ic.x      = 120 * (idx % 5) + baseX;    // 120*(v3%5)+v24
        ic.y      = 96 * (idx / 5) + baseY;     // 96*(v3/5)+v21
        ic.labelX = ic.x - 16;                  // *(&v20[2]) - 16
        ic.labelY = ic.y - 15;                  // WORD2(v22) - 15
        ic.count  = e.count;                    // *(v7+7)
        ic.value  = e.marketPrice * static_cast<double>(e.count);  // price*count
        out.push_back(ic);

        // total += marketPrice*count (the v18 = v17 + (double)(int)v22 step,
        // re-truncated to int via LODWORD(v22) = (int)v18).
        total = static_cast<long long>(static_cast<double>(static_cast<int>(total)) + ic.value);
        ++idx;                                  // ++v3
    }
    outTotal = static_cast<i32>(total);
    return out;
}

// ===========================================================================
// BuildUnitRosterPanel — column layout + health ratio.
// ===========================================================================
std::vector<RosterCell> BuildUnitRosterPanel(const std::vector<i32>& roster,
                                             int cols, int screenW,
                                             int baseX, int baseY,
                                             const std::vector<bool>& alive,
                                             int& outColW) {
    std::vector<RosterCell> out;

    // Count populated slots (the prelude do/while over a1[0..15]).
    int count = 0;                              // v5
    const int n = static_cast<int>(roster.size());
    const int rosterLimit = n < 16 ? n : 16;
    for (int i = 0; i < rosterLimit; ++i)
        if (roster[i] != -1) ++count;           // `if (*a1 != -1) ++v5`

    // colW = ((screenW) - 48) / (count + 1).   (v6 = (screenW - 48)/(v5+1))
    const int colW = (screenW - 48) / (count + 1);
    outColW = colW;

    // The grid width `v10` (cols) is the recovered modulus; guard against 0.
    const int gridCols = cols > 0 ? cols : 1;

    for (int i = 0; i < rosterLimit; ++i) {     // `v7 < 16`
        if (roster[i] == -1) continue;          // `if (*v25 != -1)`
        RosterCell c;
        // x = colW + colW*(i % cols) + baseX  (v22 = v23 + v23*(v7%v10) + v21)
        c.x = colW + colW * (i % gridCols) + baseX;
        // y = 100*(i / cols) + baseY          (HIDWORD = 100*(v7/v10) + v26)
        c.y = 100 * (i / gridCols) + baseY;
        c.healthBarX = c.x;                     // BuildTiledRow(v22, y+60, ...)
        c.healthBarY = c.y + 60;
        c.labelX = c.x - 36;                    // AddTextLabel(v22-36, y-17, ...)
        c.labelY = c.y - 17;
        // dead branch: `if (!*(_BYTE*)(v24+8))` -> a tilt sprite. alive[i] mirrors
        // *(unit+8); dead == !alive.
        c.dead = !((i < static_cast<int>(alive.size())) ? alive[i] : true);
        out.push_back(c);
    }
    return out;
}

// ===========================================================================
// BuildHudWindow — slider-centering rule.
// ===========================================================================
int HudSliderCenterX(int screenW) {
    // `((*(int*)(dword_62D298+6) >> 16) - 400) / 2`  — screenW is the already-
    // shifted width; we centre a 400-wide slider.
    return (screenW - kHudSliderWidth) / 2;
}

} // namespace guild::sim
