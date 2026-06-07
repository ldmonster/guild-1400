#include "ai/meisterai.h"

#include "util/math_random.h"

// DEFERRED — the full VIBE_MeisterAi_ProcessPlayerTurn (0x5321ec) pass and its
// deeply-coupled sub-passes are NOT ported here (only the DATA/RULES cores are).
// Reasons (entity-array / scene-tree / value / coord / combat / command coupling):
//   * the object/building sweep (security, guard-target, banker tax) — needs
//     dword_13CE298 walk + GameObject_QueryFind + Building_GetSecurityLevel +
//     Command_QueueRequestQuad43/56/61/GuardTarget + EnqueueCmd14/15.
//   * the worker stocking pass — Building_CollectEntrySlots +
//     Building_ComputeItemBaseValue + Coord_ConvertX + Combat_ResolveTargetObjekt
//     + Character_SpawnAtBuildingEntrance + QueueRequest16/State22.
//   * the production-worth pass — BuildingValue_ComputeProductionWorth +
//     GameObject_SumValuesAtLocation + EnqueueCmd15.
// Also deferred (sibling director functions, same coupling): the whole MeisterAi_*
// economy family (PlanProduction 0x4596e4, AssignWorkstations 0x4599f0,
// DistributeWorkstationItems 0x45aa78, HireStaff/TrainStaff/RenovateBuilding,
// TradeManageStorage 0x45f1e4, TradeGeneral 0x4614d0, TradeRemotePurchaseA/B,
// CollectTransporters, ProcessBuildingNeeds 0x4c7774, RunBuildingTasks,
// and the RuleEval* decision family 0x464660..0x466259 — each carries 10-20
// per-rule float tuning constants and reads Economy/Gesetz/Office/He clusters),
// and the Amt_* per-turn passes (RunProductionPass, ProcessAllOfficeWages,
// ProcessLoanRepayments, RunBuildingTaxPass, UpdateOfficeProsperity,
// RunGoodsDistributionPass, PickRandomEventBuildings @0x57b304..0x57e2a4) which
// recon 05 assigns to guild::world (owned by the world agent).

namespace guild::ai {

// gilde.exe 0x532e89 (core) — security-heat sweep decrement.
u8 SecurityHeatDecrement(u8 heat, int securityLevel) {
    signed char d = static_cast<signed char>(
        static_cast<int>(heat) - 2 * securityLevel);
    if (d < 0)
        return 0;
    return static_cast<u8>(d);
}

// gilde.exe 0x53265f..0x5327f7 (core) — per-worker mood/relation delta.
i8 MoodRelationDelta(int attitudeA, int attitudeB, int relation) {
    int delta = 0; // HIBYTE(v124)

    // Part 1 (attitudeA / worker+61).
    if (attitudeA >= 100 || relation <= -120) {
        if (attitudeA <= 100 || relation >= 100) {
            // skip — delta unchanged.
        } else {
            int v31 = (attitudeA - 100) / 3;
            if (v31 < 1)
                v31 = 1;
            delta = static_cast<signed char>(v31);
        }
    } else {
        int v30 = (100 - attitudeA) / 3;
        if (v30 < 1)
            v30 = 1;
        delta = static_cast<signed char>(-v30);
    }

    // Part 2 (attitudeB / worker+65).
    if (attitudeB >= 100 || relation <= -120) {
        if (attitudeB > 100 && relation < 100) {
            int v70 = (100 - attitudeB) / 5;
            if (v70 < 1)
                v70 = 1;
            delta = static_cast<signed char>(delta - v70);
        }
    } else {
        int v32 = (attitudeB - 100) / 5;
        if (v32 < 1)
            v32 = 1; // matches LOBYTE(v32)=1 when the division underflows below 1
        delta = static_cast<signed char>(delta + v32);
    }

    return static_cast<i8>(delta);
}

// gilde.exe 0x532e73 (core) — banker tax payout.
i32 TaxPayout(u8 multiplier) {
    return 16000 * static_cast<i32>(multiplier);
}

// gilde.exe 0x532485 (core) — end-of-building mood-decay roll.
int MoodDecayRoll(u8 kind, bool flagBit0) {
    if (static_cast<u16>(guild::util::RandomModulo(0x64)) <= 0x1Eu)
        return 0; // roll <= 30 => no decay (the original requires > 0x1E)
    if (kind == 13 || kind == 12 || kind == 11 || kind == 16 || flagBit0)
        return 0;
    int mag = guild::util::RandomModulo(3);
    return -(mag + 2);
}

// gilde.exe 0x5328a3 (core) — confrontation spawn decision.
int ConfrontationDecision(int newRelation) {
    if (newRelation >= -26)
        return 0;
    // gate: -RandomModulo(0x4A) < newRelation  (both negative; fires when the
    // random magnitude is smaller than |newRelation|).
    int gate = -static_cast<int>(static_cast<u16>(guild::util::RandomModulo(0x4A)));
    if (gate >= newRelation)
        return 0;
    if (static_cast<u16>(guild::util::RandomModulo(0x64)) <= 0x32u)
        return 52; // challenge variant
    return 51;     // duel variant
}

// Driver exercising the worker mood/relation + decay rules.
int RunWorkerMoodPass(i32 playerId, const TurnWorker* workers, int count, TurnCmdFn emit) {
    int emitted = 0;
    for (int i = 0; i < count; ++i) {
        const TurnWorker& w = workers[i];

        i8 delta = MoodRelationDelta(w.attitudeA, w.attitudeB, w.relation);
        ++emitted; // the QueueRequest16 wage/stock command (always emitted)
        if (delta != 0) {
            if (emit) emit(kTurnMoodCoord, playerId, w.personId, delta);
            ++emitted;
        }
        ++emitted; // the State22 relation delta packet (always emitted)

        int newRelation = w.relation + delta;
        if (newRelation < -26) {
            int variant = ConfrontationDecision(newRelation);
            // ConfrontationDecision already rolled the gate; re-derive emission:
            // it returns 0 when the gate failed or the relation was >= -26.
            if (variant != 0) {
                if (emit) emit(kTurnConfront, playerId, w.personId, variant);
                ++emitted;
            }
        }

        int decay = MoodDecayRoll(w.kind, w.flagBit0);
        if (decay != 0) {
            if (emit) emit(kTurnDecay, playerId, w.personId, decay);
            ++emitted;
        }
    }
    return emitted;
}

} // namespace guild::ai
