#include "sim/combat_orders.h"

#include "crt/rand.h"

#include <algorithm>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Order-tick non-attack state machine (the switch cases 1,3,4,5,6,7,8 of
// VIBE_Combat_UpdateUnitOrders @0x491688).
//
// In the original the in-flight-packet gate is:
//     v == 1 || VIBE_Command_GetPacketStatusById(v)
// where v == slot.packetId; v == 1 is the "ready" sentinel and a nonzero status
// means the previously-issued command has been applied. We model that as the
// helper PacketReady(): packetId == 1 (ready) OR packetId == -1 (none in flight,
// status query returns true for the absent/applied case in the original's gate).
// Tests drive this through OrderSlot::packetId directly.
// ---------------------------------------------------------------------------
static bool PacketReady(const OrderSlot& slot) {
    // The original: (packetId == 1) || GetPacketStatusById(packetId). A packetId
    // of -1 (none) or 1 (ready) lets the case proceed; any other id means a real
    // command is still in flight (its status is consulted — modelled as "done"
    // once the slot was advanced, i.e. only id 1 / -1 proceed here).
    return slot.packetId == 1 || slot.packetId == -1;
}

OrderCommand TickNonAttackOrder(OrderSlot& slot, const OrderWorldContext& ctx) {
    switch (slot.state) {
        // --- state 1: move to a tile -------------------------------------
        case kOrderMove: {
            if (!PacketReady(slot))
                return OrderCommand::None;
            u8 phase = slot.phase;
            slot.packetId = -1;          // *((_DWORD *)v4 + 2) = -1
            if (phase) {
                // arrived? TileToWorld(tileX,tileZ) within 30.0 -> LABEL_202
                if (ctx.unitOnTargetTile && ctx.unitOnTargetTile(slot, kOrderTolMove)) {
                    // LABEL_202: Op85(6) anim + Op80 sync — objective reached.
                    return OrderCommand::Captured;
                }
                return OrderCommand::None;
            }
            // not yet walking: find a free tile and issue the walk path.
            i32 ox = slot.tileX, oz = slot.tileZ;
            bool found = !ctx.findFreeTile || ctx.findFreeTile(slot.tileX, slot.tileZ, ox, oz);
            if (found) {
                slot.phase = 1;          // v4[12] = 1
                slot.tileX = ox;
                slot.tileZ = oz;
                // Op85 anim + Op78 path issued; packetId tracks the path command.
                slot.packetId = 1;
                return OrderCommand::PathToTile;
            }
            return OrderCommand::None;
        }

        // --- state 3: labelled march to a tile ---------------------------
        case kOrderMarch: {
            if (!PacketReady(slot))
                return OrderCommand::None;
            u8 phase = slot.phase;
            slot.packetId = -1;
            OrderCommand emitted = OrderCommand::None;
            // not yet walking AND not already on the tile (50.0) AND a free tile
            // exists -> issue the march path.
            bool onTile = ctx.unitOnTargetTile && ctx.unitOnTargetTile(slot, kOrderTolMarch);
            if (!phase && !onTile) {
                i32 ox = slot.tileX, oz = slot.tileZ;
                bool found = !ctx.findFreeTile ||
                             ctx.findFreeTile(slot.tileX, slot.tileZ, ox, oz);
                if (found) {
                    slot.phase = 1;
                    slot.tileX = ox;
                    slot.tileZ = oz;
                    slot.packetId = 1;
                    emitted = OrderCommand::PathToTile;
                }
            }
            // re-check arrival: if on the target tile -> Op85(1) + Op80 sync done.
            if (onTile) {
                slot.packetId = 1;
                emitted = OrderCommand::SyncDone;
            }
            return emitted;
        }

        // --- state 4: capture an object via the bone-chain probe ----------
        case kOrderCapture: {
            if (!PacketReady(slot))
                return OrderCommand::None;
            slot.packetId = -1;
            // The original probes a scene object (Object_FindByHandle on the slot's
            // tile fields) and a parent unit, then branches on faction / proximity.
            // The data-flow outcome the rule produces is one of:
            //   * objective complete (target gone / not alive) -> Op80 sync;
            //   * within 49.5 of the capture object -> Op85(3) + Op80 sync;
            //   * else a ware/move path (Op85(6) + Op78).
            // We model the arrival test only (the leaf supplies the probe via
            // unitOnTargetTile at tol 49.5); when arrived -> SyncDone, else path.
            bool arrived = ctx.unitOnTargetTile && ctx.unitOnTargetTile(slot, kOrderTolCapture);
            if (arrived) {
                slot.packetId = 1;
                return OrderCommand::SyncDone;
            }
            i32 ox = slot.tileX, oz = slot.tileZ;
            bool found = !ctx.findFreeTile || ctx.findFreeTile(slot.tileX, slot.tileZ, ox, oz);
            if (found) {
                slot.phase = 1;
                slot.tileX = ox;
                slot.tileZ = oz;
                slot.packetId = 1;
                return OrderCommand::PathToTile;
            }
            return OrderCommand::None;
        }

        // --- state 5: stand (target id is a Person) ----------------------
        case kOrderStand: {
            if (!PacketReady(slot))
                return OrderCommand::None;
            slot.packetId = 1;           // Op85(7) anim + Op80 sync issued
            return OrderCommand::SyncDone;
        }

        // --- state 6: ware-save collection phases ------------------------
        case kOrderWareCollect: {
            if (!PacketReady(slot))
                return OrderCommand::None;
            slot.packetId = -1;
            // The original first re-checks the ware object still exists; then
            // branches on the ware-phase byte (slot.WarePhase(), v4[34]) and the
            // unit-busy flag (*(actor+296)):
            //   phase 0 + busy:  drive toward the ware-save tile (Op85(4) + Op81)
            //   phase 0 + idle:  begin the walk to the save tile (Op85(6) + Op78)
            //   phase 1 + idle:  ware-save sync (Op81) at the resolved tile
            //   phase 2 + onTile(50): Op85(5) + Op80 sync (deposit complete)
            u8 warePhase = slot.WarePhase();
            bool busy = ctx.unitBusy && ctx.unitBusy();
            if (warePhase == 1) {
                if (!busy) {
                    slot.packetId = 1;
                    return OrderCommand::WareSave;   // Op81 + Op78 to the save tile
                }
                return OrderCommand::None;
            }
            if (warePhase == 2) {
                bool onTile = ctx.unitOnTargetTile &&
                              ctx.unitOnTargetTile(slot, kOrderTolMarch);
                if (onTile) {
                    slot.packetId = 1;
                    return OrderCommand::SyncDone;   // Op85(5) + Op80
                }
                return OrderCommand::None;
            }
            // warePhase == 0
            if (busy) {
                bool onTile = ctx.unitOnTargetTile &&
                              ctx.unitOnTargetTile(slot, kOrderTolMarch);
                if (onTile) {
                    slot.packetId = 1;
                    return OrderCommand::WareSave;   // Op85(4) + Op81
                }
                return OrderCommand::None;
            }
            slot.phase = 1;                          // v4[12] = 1
            i32 ox = slot.tileX, oz = slot.tileZ;
            bool found = !ctx.findFreeTile || ctx.findFreeTile(slot.tileX, slot.tileZ, ox, oz);
            if (found) {
                slot.tileX = ox;
                slot.tileZ = oz;
                slot.packetId = 1;
                return OrderCommand::PathToTile;     // Op85(6) + Op78
            }
            return OrderCommand::None;
        }

        // --- state 7: flee to the nearest free tile ----------------------
        case kOrderEscape: {
            if (!PacketReady(slot))
                return OrderCommand::None;
            u8 phase = slot.phase;
            slot.packetId = -1;
            if (phase) {
                if (ctx.unitOnTargetTile && ctx.unitOnTargetTile(slot, kOrderTolMove)) {
                    // LABEL_202: reached the escape tile -> Op85(6) + Op80 sync.
                    return OrderCommand::Captured;
                }
                return OrderCommand::None;
            }
            i32 ox = slot.tileX, oz = slot.tileZ;
            bool found = !ctx.findFreeTile || ctx.findFreeTile(slot.tileX, slot.tileZ, ox, oz);
            if (found) {
                slot.phase = 1;
                slot.tileX = ox;
                slot.tileZ = oz;
                slot.packetId = 1;
                return OrderCommand::PathToTile;     // Op85 anim + Op78 path
            }
            return OrderCommand::None;
        }

        // --- state 8: stand up -------------------------------------------
        case kOrderStandUp: {
            if (!PacketReady(slot))
                return OrderCommand::None;
            u8 phase = slot.phase;
            slot.packetId = -1;
            if (!phase) {
                slot.phase = 1;
                slot.packetId = 1;
                return OrderCommand::AnimMode;       // Op85(6) — play stand-up anim
            }
            return OrderCommand::None;
        }

        default:
            // state 0 (idle) / state 2 (attack, handled by TickOrderSlot) — no-op.
            return OrderCommand::None;
    }
}

// ===========================================================================
// Small per-unit action emitters.
// ===========================================================================
//
// The shared gate (all three): slot.state != 0 && slot.unitId != -1 && the unit
// resolves (FindUnitById != null). Only then is the gesture queued.
static bool GestureGate(const OrderSlot& slot, bool unitExists) {
    return slot.state != 0 && slot.unitId != -1 && unitExists;
}

// gilde.exe 0x490f18 — VIBE_Combat_StandUpUnitAction.
UnitGesture StandUpUnitAction(const OrderSlot& slot, bool unitExists) {
    if (!GestureGate(slot, unitExists))
        return UnitGesture::None;
    return UnitGesture::StandUp;          // Character_StandUp(unit->actorPtr)
}

// gilde.exe 0x490fa8 — VIBE_Combat_PickUpFromGroundAction.
UnitGesture PickUpFromGroundAction(const OrderSlot& slot, bool unitExists,
                                   bool groundObjectFound) {
    // The original gates only on slot.state and slot.unitId (NOT unitExists for
    // the StandUp part), but only queues the pick-up action if the ground object
    // is found (Object_FindByHandle != 0).
    if (slot.state == 0 || slot.unitId == -1)
        return UnitGesture::None;
    (void)unitExists;
    if (!groundObjectFound)
        return UnitGesture::None;         // StandUp ran, but no pick-up queued
    return UnitGesture::PickUpFromGround; // queue spezial/einhaendig...vom_boden
}

// gilde.exe 0x490f40 — VIBE_Combat_PlayCelebrateGesture.
UnitGesture PlayCelebrateGesture(const OrderSlot& slot, bool unitExists) {
    if (!GestureGate(slot, unitExists))
        return UnitGesture::None;
    return UnitGesture::Celebrate;        // queue "gestik/jubeln"
}

// ===========================================================================
// Capture-flag RULE.
// ===========================================================================

// gilde.exe 0x48cf30 — VIBE_Combat_CaptureUnitAction.
CaptureResult CaptureUnitAction(i32 attackerSideOwnerId, bool hasParentObject) {
    CaptureResult r;
    if (hasParentObject) {               // v4 = v3[128] != 0
        r.applied = true;
        r.newOwnerId = attackerSideOwnerId;  // *(obj+10) = *(word*)dword_6311E8
    }
    return r;
}

// ===========================================================================
// Objective-collector callbacks.
// ===========================================================================

// gilde.exe 0x48b488 — VIBE_Combat_WareObjectCallback.
bool WareObjectQualifies(bool namePrefixWare, i32 unitFaction, i32 objectOwner) {
    // The original: if (strncmp(name,"WARE_",5) || unitFaction==objectOwner) skip;
    // else append. So it qualifies when the prefix matches AND owners differ.
    return namePrefixWare && unitFaction != objectOwner;
}

// gilde.exe 0x48b5a4 — VIBE_Combat_ConquerObjectCallback.
bool ConquerObjectQualifies(bool namePrefixConquer) {
    return namePrefixConquer;            // strncmp(name,"sp_CONQUER",10) == 0
}

// ===========================================================================
// Tile / selection rule helpers.
// ===========================================================================

// gilde.exe 0x48d4d8 — VIBE_Combat_ClassifyTileType.
u8 ClassifyTileType(u8 personType) {
    switch (personType) {
        case 19: return 0;
        case 16: return 1;
        case 4:  return 2;
        default: return 0;
    }
}

// gilde.exe 0x486460 — VIBE_Combat_GetSelectionFlag.
i32 GetSelectionFlag(bool factionMatches, bool globalHighlight) {
    if (globalHighlight)
        return kSelectionHighlightFlag;
    return factionMatches ? kSelectionHighlightFlag : 0;
}

// gilde.exe 0x4897e0 — VIBE_Combat_CountActiveSlots.
int CountActiveSlots(const i32* roster16) {
    if (roster16[0] == -1)
        return 0;
    int count = 0;
    for (int i = 0; i < 16; ++i) {
        ++count;
        if (count >= 16)
            return -1;                   // all 16 populated -> "full" sentinel
        if (roster16[i + 1] == -1)
            return count;
    }
    return -1;
}

// gilde.exe 0x57e4c8 — VIBE_Combat_IsTargetUnderfull.
bool IsTargetUnderfull(u16 currentFill, float targetCapacity) {
    return static_cast<double>(currentFill) < static_cast<double>(targetCapacity);
}

// gilde.exe 0x57e714 — VIBE_Combat_AssignGuardTarget.
GuardAssign AssignGuardTarget(u16 capacityForType, u8 targetGuardCount,
                              bool hasOldTarget, u8 oldGuardCount) {
    GuardAssign r;
    if (targetGuardCount < static_cast<int>(capacityForType)) {
        if (hasOldTarget && oldGuardCount)
            r.oldTargetCount = static_cast<u8>(oldGuardCount - 1);
        else
            r.oldTargetCount = oldGuardCount;
        r.newTargetCount = static_cast<u8>(targetGuardCount + 1);
        r.assigned = true;
    } else {
        r.oldTargetCount = oldGuardCount;
        r.newTargetCount = targetGuardCount;
    }
    return r;
}

// ===========================================================================
// Street-brawl target picker.
// ===========================================================================

// gilde.exe 0x57829c — VIBE_Combat_SelectBeatingTarget.
int SelectBeatingTarget(const std::vector<i32>& seedIds,
                        const std::vector<u8>& filterTable25,
                        const std::function<std::vector<i32>(int attempt, u8 filterByte)>& queryFn) {
    i32 candidates[16];
    int count = 0;
    // Seed with the brawler's primary/secondary targets (already validated by the
    // caller: live actors). The original fills v14[0] / v14[1] before the loop.
    for (i32 id : seedIds) {
        if (count >= 16)
            break;
        candidates[count++] = id;
    }
    // Up to 32 query attempts (the v4 = 32 countdown), stopping at 16 candidates.
    int attempts = 32;
    while (count < 16 && attempts > 0) {
        --attempts;
        int attempt = 32 - attempts;     // 1-based attempt index (informational)
        // filterTable[5*RandomModulo(4) + RandomModulo(5)]
        int row = 5 * static_cast<int>(static_cast<u16>(Math_RandomModulo(4)));
        int col = static_cast<int>(static_cast<u16>(Math_RandomModulo(5)));
        u8 filterByte = 0;
        size_t idx = static_cast<size_t>(row + col);
        if (idx < filterTable25.size())
            filterByte = filterTable25[idx];
        std::vector<i32> ids = queryFn ? queryFn(attempt, filterByte)
                                       : std::vector<i32>{};
        // Append up to 3 SUCCESSFUL appends of the query results, each gated by
        // RandomModulo(4) < 3. Per disasm @0x57834B the "3" counter (ecx) is
        // incremented ONLY when the gate passes (inc ecx is inside the take block,
        // 0x578369); the iterator (Person_IterNext) advances every result, and the
        // RandomModulo(4) draw is made on every result that reaches the gate. So we
        // keep iterating/drawing until 3 successes (taken), 16 candidates, or the
        // result set is exhausted.
        int taken = 0;   // ecx — counts SUCCESSFUL appends, cap 3
        for (i32 id : ids) {
            if (taken >= 3 || count >= 16)
                break;
            if (static_cast<u16>(Math_RandomModulo(4)) < 3) {
                candidates[count++] = id;
                ++taken;   // inc ecx ONLY on gate-pass (0x578369)
            }
        }
    }
    if (count == 0)
        return -1;
    return candidates[static_cast<u16>(Math_RandomModulo(static_cast<u16>(count)))];
}

// ===========================================================================
// Dropped-bomb table allocator.
// ===========================================================================

// gilde.exe 0x486648 — VIBE_Object_SpawnBomb.
int SpawnDroppedBomb(std::vector<Bomb>& bombs, u32 now) {
    // The original scans dword_B5F810 in 2-dword (8-byte) steps over 32 entries
    // for the first free handle. We mirror that as the first inactive Bomb slot,
    // capacity 32. On success it records the spawn tick (the fuse base).
    if (bombs.size() < 32)
        bombs.resize(32);
    for (int i = 0; i < 32; ++i) {
        if (!bombs[i].active) {
            bombs[i] = Bomb{};
            bombs[i].active = true;
            bombs[i].spawnTick = now;    // dword_B5F814[v1] = dword_62EB38
            return i;
        }
    }
    return -1;                           // table full
}

} // namespace guild::sim
