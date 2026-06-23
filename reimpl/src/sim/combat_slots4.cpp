#include "sim/combat_slots4.h"

#include "sim/combat.h"   // guild::sim::Math_RandomModulo (reused, defined in combat.cpp)

#include <cmath>
#include <cstdio>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hooks plumbing (inert default = all-null -> every side effect is a no-op).
// ---------------------------------------------------------------------------
namespace {
const CombatSlots4Hooks* g_hooks = nullptr;
CombatSlots4Hooks        g_inert{};
}

void SetCombatSlots4Hooks(const CombatSlots4Hooks* hooks) { g_hooks = hooks; }
const CombatSlots4Hooks& GetCombatSlots4Hooks() {
    return g_hooks ? *g_hooks : g_inert;
}

// ---------------------------------------------------------------------------
// Recovered blood-pool constants (see header; flt/dbl literals from .rdata).
//   flt_61B930 == flt_61CD60 == 3.1415927410125732f  (single-precision pi)
//   dbl_61B934 == dbl_61CD64 == 0.005555555555555555  (== 1/180)
// i.e. both blood-pool y terms are `(double)RandInt(360) * (float)pi * (1/180)` —
// an angle-in-degrees -> radians conversion of a random 0..359 roll.
// ---------------------------------------------------------------------------
const float  kDeathBloodScaleA = 3.1415927410125732f;
const double kDeathBloodScaleB = 0.005555555555555555;
const float  kBloodScaleA      = 3.1415927410125732f;
const double kBloodScaleB       = 0.005555555555555555;

// ===========================================================================
// DistanceToTargetXZ  (gilde.exe 0x4864a0)
// ===========================================================================
float DistanceToTargetXZ(float selfX, float selfZ, float targetX, float targetZ) {
    // v3 = target.x - self.x ; v4 = target.z - self.z  (each spilled to a float slot).
    // return sqrt(v3*v3 + 0.0*0.0 + v4*v4). The original keeps the y term as a
    // literal `0.0 * 0.0`; we reproduce the exact term order so the FPU accumulation
    // matches.
    float v3 = targetX - selfX;
    float v4 = targetZ - selfZ;
    double acc = static_cast<double>(v3) * static_cast<double>(v3)
               + 0.0 * 0.0
               + static_cast<double>(v4) * static_cast<double>(v4);
    return static_cast<float>(std::sqrt(acc));
}

// ===========================================================================
// TriggerEscapeAction  (gilde.exe 0x485c0c)
// ===========================================================================
bool TriggerEscapeAction(const EscapeInput& in, const void* unit) {
    const CombatSlots4Hooks& h = GetCombatSlots4Hooks();

    // if (!a1 || (a1[459] & 0x20) == 0) return 0;  — gated on "can flee".
    if (!in.canFlee)
        return false;

    // v4 = a1[453] (cowardice). if (RandomModulo(10) < v4) -> flee.
    int roll = static_cast<int>(static_cast<u16>(Math_RandomModulo(kEscapeRollMod)));
    if (roll < static_cast<int>(in.cowardice)) {
        // v11[0] = -cowardice ; ResetObjectHighlights ; queue the delta packet.
        if (h.resetObjectHighlights)
            h.resetObjectHighlights(unit);
        i8 fleeFlag = static_cast<i8>(-static_cast<int>(in.cowardice));
        if (h.queueEscapeDelta)
            h.queueEscapeDelta(unit, fleeFlag);
        // if at a class-6/7 building with a valid object def -> send the flee shout.
        if (in.atBuilding && (in.buildingClass == 6 || in.buildingClass == 7)
            && in.hasObjectDef) {
            if (h.sendFleeMessage)
                h.sendFleeMessage(unit);
        }
        return true;
    }

    // else: just queue the "stop" delta (flag = 1) and the follow-up request.
    if (h.queueEscapeDelta)
        h.queueEscapeDelta(unit, static_cast<i8>(1));
    return false;
}

// ===========================================================================
// Damage-number subsystem  (gilde.exe 0x487300 / 0x48736c)
// ===========================================================================
int SpawnDamageNumber(std::vector<DmgNumberRecord>& table, const void* owner,
                      int amount, int kind, float unitScale) {
    // v5 = 0; if (dword_B5F6D4[0]) { while(1){ v5+=5; if (v5>=80) break;
    //   if (!dword_B5F6D4[v5]) goto LABEL_4; } } else LABEL_4:
    // -> the first free slot is index 0 if slot0 is free, else the first later free
    //    slot (stride-5 in dwords == one record). If none free, return `result` (a2).
    int slot = -1;
    if (table.empty())
        return -1;
    if (table[0].value == 0) {
        slot = 0;
    } else {
        for (int i = 1; i < static_cast<int>(table.size()); ++i) {
            if (table[i].value == 0) { slot = i; break; }
        }
    }
    if (slot < 0)
        return -1;   // table full -> original returns the untouched a2 input.

    // v6 = (double)amount / (unitScale * 0.01f); store (int)v6 in the value cell.
    // dword_B5F6D0[v5] = 1115684864 (== 64.0f) seeds the float ttl cell first.
    // Original: v6 = (double)amount / (*(float*)(a1+28) * dbl_61B1C4). dbl_61B1C4 is
    // the EXACT double 0.01 (0x3f847ae147ae147b), NOT the float 0.01f promoted, so the
    // divisor must use the double literal (kDmgNumScale is now a double).
    double v6 = static_cast<double>(amount)
              / (static_cast<double>(unitScale) * kDmgNumScale);
    DmgNumberRecord& r = table[slot];
    r.owner  = owner;          // dword_B5F6DC[v5] = a1
    r.kind   = kind;           // dword_B5F6E0[v5] = a3
    r.ttl    = kDmgNumInitialTtl; // dword_B5F6D0[v5] = 64.0f
    r.value  = static_cast<int>(v6); // dword_B5F6D4[v5] = (int)v6  (also the live flag)
    r.widget = -1;
    return slot;
}

int UpdateDamageNumbers(std::vector<DmgNumberRecord>& table, double ttlStep) {
    const CombatSlots4Hooks& h = GetCombatSlots4Hooks();
    int expired = 0;

    // for (v0 = 0; v0 < 80; v0 += 5) { if (!dword_B5F6D4[v0]) continue; ... }
    for (int i = 0; i < static_cast<int>(table.size()); ++i) {
        DmgNumberRecord& r = table[i];
        if (r.value == 0)
            continue;   // !dword_B5F6D4[v0] -> LABEL_3 (skip)

        int x = 0, y = 0;
        bool onScreen = h.objectScreenBounds
                            ? h.objectScreenBounds(r.owner, &x, &y)
                            : false;
        if (!onScreen) {
            // if (widget != -1) SetVisible(widget, 0);  — hide, do NOT decay/expire.
            if (r.widget != -1 && h.objectSetVisible)
                h.objectSetVisible(r.widget, 0);
            continue;
        }

        // v3 = screenY + 16 (the label anchor). The original builds the text via
        // sprintf; the formatting is presentation, routed through createTextLabel.
        int anchorY = y + 16;
        if (r.widget == -1) {
            // create the label (the kind switch chooses the format string).
            char buf[64];
            FormatDamageText(buf, sizeof(buf), r.value, r.kind);
            if (h.createTextLabel)
                r.widget = h.createTextLabel(anchorY, /*y baked into label*/ y, buf);
        } else if (h.widgetLayoutBounds) {
            h.widgetLayoutBounds(anchorY, y, r.widget);
        }

        if (r.widget != -1 && h.objectSetVisible)
            h.objectSetVisible(r.widget, 1);

        // dword_B5F6D0[v0] += dbl_61B1DC; (ttl decays by a NEGATIVE step).
        r.ttl = static_cast<float>(static_cast<double>(r.ttl) + ttlStep);
        if (static_cast<double>(r.ttl) <= 0.0) {
            // destroy the widget + free the slot.
            if (r.widget != -1 && h.widgetDestroyByType)
                h.widgetDestroyByType(r.widget);
            r.widget = -1;
            r.value  = 0;
            r.owner  = nullptr;
            ++expired;
        }
    }
    return expired;
}

// Format the floating "-n" label per kind (the original's three sprintf branches:
//   kind 1 -> "[-n]"  (chars 92/93) ; kind 2 -> "(-n)" (chars 94/95) ; else "-n").
void FormatDamageText(char* out, unsigned long cap, int value, int kind) {
    if (!out || cap == 0) return;
    // value is the stored (positive) magnitude; the original prints "-value".
    if (kind == 1)
        std::snprintf(out, cap, "%c-%i%c", 92, value, 93);   // [-n]
    else if (kind == 2)
        std::snprintf(out, cap, "%c-%i%c", 94, value, 95);   // (-n)  (^ and _)
    else
        std::snprintf(out, cap, "-%i", value);
}

// ===========================================================================
// EvalUnitAttackMove  (gilde.exe 0x491324)
// ===========================================================================
bool IsNoWeaponDefType(i16 t) {
    switch (t) {
        case 0: case 340: case 342: case 344: case 366: case 370: case 374:
            return true;
        default:
            return false;
    }
}

AttackMoveDecision EvalUnitAttackMove(const AttackMoveInput& in) {
    // if ((weaponClass == 1 || 2) && active target && target.hp <= 0) return -1.
    if ((in.weaponClass == 1 || in.weaponClass == 2) && in.hasActiveTarget) {
        if (in.activeTargetHp <= 0)
            return AttackMoveDecision::kHoldNoTarget;
    }

    // if (!def || def.type in {0,340,342,344,366,370,374}) { ... }
    if (!in.hasObjectDef || IsNoWeaponDefType(in.objDefType)) {
        // if (!target || distance > range) -> close distance (move).
        if (!in.hasTarget || in.distanceToTarget > in.weaponRange)
            return AttackMoveDecision::kMoveToTarget;
        // else: fall through to the busy/attack gate below.
    }

    // if (self busy: self+388 -> +296 != 0) return -1.
    if (in.selfBusy)
        return AttackMoveDecision::kHoldBusy;

    return AttackMoveDecision::kAttack;
}

// ===========================================================================
// ProcessShotAndBomb  (gilde.exe 0x4bfb68)
// ===========================================================================
ShotBombOutcome ProcessShotAndBomb(const ShotBombInput& in) {
    ShotBombOutcome out;
    // result = UpdateBombExplosions();  (a side effect; not modelled here.)
    // if (!byte_671D96) return result;  — disarmed.
    if (!in.armed) {
        out.path = ShotBombPath::kDisarmed;
        return out;
    }
    out.statusWord = 33;   // word_62D310 = 33

    if (in.shotRequested) {
        // play the shot sound; apply pending melee hit if present; clear the slot.
        out.playedShotSound = true;
        if (in.hasPendingHit)
            out.appliedMeleeHit = true;
        // dword_11BC270 = 0  (clear pending-hit slot) — implicit.
        if (in.bombRequested) {
            // raycast -> tile -> SpawnBomb; ClearMouseButtonsByMask(16).
            out.placedBomb = true;
            out.clearedMouseMask = true;
            out.path = ShotBombPath::kShotThenBomb;
        } else {
            out.path = ShotBombPath::kShotOnly;
        }
        return out;
    }

    if (in.bombRequested) {
        out.placedBomb = true;
        out.clearedMouseMask = true;
        out.path = ShotBombPath::kBombOnly;
        return out;
    }

    out.path = ShotBombPath::kNone;   // armed but nothing requested.
    return out;
}

// ===========================================================================
// UpdatePursuitTargets  (gilde.exe 0x48c400)
// ===========================================================================
bool PursuitPressesAttack(int scaledOutputRatio, int roll100) {
    // if ((int)v11 >= 20 || RandomModulo(100) <= 30) attack; else flee.
    return scaledOutputRatio >= kPursuitAttackThreshold || roll100 <= kPursuitRollCutoff;
}

bool PursuitRecordShouldClear(int duelMode, u8 state, bool targetAlive) {
    // The clear loop runs only when dword_6315A4 > 1; the per-record condition that
    // resets a slot is:  dword_6315A4 <= 1
    //   || ( state != 3 && state != 4
    //        && (state != 2 || !targetUnitAlive) )
    // i.e. with duelMode > 1, a record is cleared unless it is in state 3/4, or in
    // state 2 with a live target.
    if (duelMode <= 1)
        return true;   // (the `||` short-circuit: the whole guard is true -> clear)
    if (state == 3 || state == 4)
        return false;
    if (state == 2 && targetAlive)
        return false;
    return true;
}

// ===========================================================================
// ResolveTargetEntityRef  (gilde.exe 0x57ea8c)
// ===========================================================================
ResolveTargetOutcome ResolveTargetEntityRef(const ResolveTargetInput& in) {
    ResolveTargetOutcome out;

    int id;
    if (in.flags & 1) {
        // LOWORD(v8) = PickRandomEventBuildings(seed).
        id = in.pickedEventId;   // -1 maps to 0xFFFF (the original's "no pick").
        // if (id != 0xFFFF && (flags & 2)) stamp owner bytes.
        if (id != -1 && (in.flags & 2)) {
            // byte_12CEA75[...] = a5 ; byte_12CEA74[...] = a2.
            out.stampedOwnerBytes = true;
        }
    } else {
        // v8 = CreateAndSpawn(a4, -1, a3, -1, 0, a2, a5, 2).
        id = in.spawnedPersonId;
    }

    // if (v8 == 0xFFFF) return 0;
    if (id == -1) {
        out.entityId = -1;
        return out;
    }
    out.entityId = id;   // &word_12CE910[268 * id]

    // if ((flags & 4) == 0) return entity;  if (!a6) return entity;
    if ((in.flags & 4) == 0 || !in.hasBuildingCtx)
        return out;

    // v12 = ResolveTargetObjekt(a6); if (!v12) return entity;
    if (!in.resolveObjektOk)
        return out;

    // SpawnAtBuildingEntrance(...) ; return entity.
    out.spawnedAtEntrance = true;
    return out;
}

// ===========================================================================
// Blood-pool spawn y-offset  (gilde.exe 0x48c708 / 0x4a4944)
// ===========================================================================
float BloodPoolYOffset(u32 randRoll, float scaleA, double scaleB) {
    // *(float*)&v7 = (double)RandInt(360) * flt_scaleA * dbl_scaleB;
    // The multiplication order is left-to-right in the x87; reproduce it: the
    // (double)roll is multiplied by the FLOAT scaleA (promoted to double) then by the
    // double scaleB, and the whole product is stored back through a 32-bit float slot.
    double prod = static_cast<double>(randRoll)
                * static_cast<double>(scaleA)
                * scaleB;
    return static_cast<float>(prod);
}

} // namespace guild::sim
