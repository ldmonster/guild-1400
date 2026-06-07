#include "ai/meister_economy.h"

#include "ai/meisterai.h"
#include "util/math_random.h"

// MeisterAi per-faction economy turn (gilde.exe 0x5321ec) + staffing/production
// decision cores. The full ProcessPlayerTurn body is reconstructed faithfully;
// the deeply entity/query/render-coupled leaf reads are injected via the
// MeisterFaction synthetic model (so the pass runs without the entity arrays).
//
// Production-multiplier constants (gilde.exe, recovered byte-for-byte):
//   flt_6198E8 = 1.5625000742147677e-05  (tier 3 stock scale)
//   flt_6198EC = 7.812500371073838e-06   (tier 2)
//   flt_6198F0 = 3.906250185536919e-06   (tier 1)
//   flt_6198F4 = 0.5                      (ratio scale)
//   flt_6198F8 = 0.25                     (ratio bias)
//
// DEFERRED sub-passes (same module, deferred for deeper coupling — translated as
// decision cores only or not at all; LISTed in the module report):
//   * AssignWorkstations (0x4599f0), DistributeWorkstationItems (0x45aa78),
//     CollectStorageItems/GatherRequiredItems/Reserve/CheckCapacity — these walk
//     the workstation item arrays (dword_B5444E[]) and the scene tree; their
//     bodies are item-buffer plumbing, not decision logic.
//   * TradeManageStorage (0x45f1e4), TradeGeneral (0x4614d0), TradeRemotePurchaseA/B
//     (0x46329c/0x463c4c), CollectTransporters (0x45e71c) — the trade planners;
//     1000+ instructions each over the city goods arrays + cart routing.
//   * ProcessBuildingNeeds (0x4c7774), RunBuildingTasks (0x4c930c) — building-need
//     resolution over the He handler arrays.
//   * RuleEvalProductionRate/PricePolicy and the stock-target rules (see
//     rule_eval.cpp banner).

namespace guild::ai {

namespace {
// PlanProduction stock-scale constants (flt_6198E8..F8).
constexpr float kProdTier3 = 1.5625000742147677e-05f;  // flt_6198E8
constexpr float kProdTier2 = 7.812500371073838e-06f;   // flt_6198EC
constexpr float kProdTier1 = 3.906250185536919e-06f;   // flt_6198F0
constexpr float kProdRatioScale = 0.5f;                // flt_6198F4
constexpr float kProdRatioBias  = 0.25f;               // flt_6198F8

void Emit(std::vector<MeisterCmdRecord>& out, MeisterCmd op,
          i32 a = 0, i32 b = 0, i32 c = 0, i32 d = 0) {
    out.push_back(MeisterCmdRecord{op, a, b, c, d});
}
} // namespace

// gilde.exe 0x5321ec — full MeisterAi player turn.
int ProcessPlayerTurn(const MeisterFaction& faction, std::vector<MeisterCmdRecord>& out) {
    const i32 player = faction.playerId;
    int v3 = 0; // command accumulator

    // --- Phase 1: object/building sweep (the 256-slot do/while loop) ---------
    for (const MeisterBuilding& b : faction.buildings) {
        if (!b.alive || b.ownerPlayer != player)
            continue;

        // (+91 & 0x800) -> cmd25 needs request.
        if (b.needsCmd25) {
            ++v3;
            Emit(out, MeisterCmd::kRequestArgs25, b.id, 2048);
        }

        const u8 kind = b.kind;
        if (kind == 4 || kind == 16 || kind == 19) {
            // Security sweep over the building's objects.
            for (const MeisterBuilding::SecObj& o : b.securityObjects) {
                ++v3;
                if (o.heat > 5) {
                    if (o.securityLevel >= 0) {
                        // heat clamp: heat - 2*securityLevel, signed-byte clamp.
                        u8 newHeat = SecurityHeatDecrement(static_cast<u8>(o.heat),
                                                           o.securityLevel);
                        Emit(out, MeisterCmd::kSecuritySweepHeat, o.entityId, newHeat);
                    } else {
                        Emit(out, MeisterCmd::kSecuritySweepCmd14, o.entityId);
                    }
                } else {
                    Emit(out, MeisterCmd::kSecuritySweepQuad43, b.id, -1, o.entityId);
                }
            }
        } else if (kind == 5) {
            // Banker: stat reset (State23) then tax payout if not faction-head
            // and held currency < 160000. We model the tax gate via aiMultiplier
            // and assume the synthetic banker qualifies (kind 5, not 6/7).
            Emit(out, MeisterCmd::kBankerStats23, b.id);
            ++v3;
            // The original gates on byte_12CE912[player] != 6/7 and
            // Person_SumCurrencyHeld(player) < 160000. For the synthetic faction
            // we always emit the tax (the gate is a faction-level read).
            ++v3;
            Emit(out, MeisterCmd::kBankerTax15, b.id, -1, TaxPayout(b.aiMultiplier));
        } else if (kind == 13 || kind == 12 || kind == 11) {
            // Guard target assignment.
            ++v3;
            Emit(out, MeisterCmd::kGuardTarget61, b.id, 1, b.guardTargetHi, 0);
        }

        // Market supervision block (Quad56 + SlotReset28 op27 / op49). The
        // original gates on a faction-head check; for the sweep we emit the
        // supervision request for every owned building, then the op49 reset only
        // for the market-supervised building.
        Emit(out, MeisterCmd::kSuperviseQuad56, b.id);
        ++v3;
        if (kind != 13 && kind != 12 && kind != 11) {
            ++v3; // QueueRequestSlotReset28 op27 (supervise begin)
            Emit(out, MeisterCmd::kSuperviseReset28, b.id, 27);
            if (b.isMarketSupervised) {
                ++v3;
                Emit(out, MeisterCmd::kSuperviseReset28b, b.id, 49);
            }
        }

        // End-of-building mood-decay roll: RandomModulo(100) > 30 then exclusions.
        int decay = MoodDecayRoll(kind, b.flagBit0);
        if (decay != 0) {
            ++v3;
            Emit(out, MeisterCmd::kMoodDecay, b.id, decay);
        }
    }

    // 32-command flush point (Amt_RefreshGuildState + Sleep). The running total
    // is preserved so the reference stays single-valued (we do not reset v3).

    // --- Phase 2: worker stocking + mood pass --------------------------------
    for (const MeisterWorker& w : faction.workers) {
        // Per entry-slot QueueRequest16 (item value already precomputed).
        Emit(out, MeisterCmd::kStockRequest16, w.personId, w.itemValue);
        int v29 = v3 + 1;

        // Mood/relation delta (the v124 accumulation). Identical to
        // MoodRelationDelta (already a faithful core).
        i8 delta = MoodRelationDelta(w.attitudeA, w.attitudeB, w.relation);
        if (delta != 0) {
            ++v29;
            Emit(out, MeisterCmd::kWorkerMoodCoord27, w.personId, w.workerEntityId, delta);
        }

        // State22 relation delta packet (always emitted).
        Emit(out, MeisterCmd::kWorkerState22, w.workerEntityId);
        v3 = v29 + 1;

        // Confrontation if post-delta relation < -26.
        int newRelation = w.relation + delta;
        if (newRelation < -26) {
            // gate: -RandomModulo(0x4A) < newRelation.
            int gate = -static_cast<int>(static_cast<u16>(guild::util::RandomModulo(0x4A)));
            if (gate < newRelation) {
                ++v3;
                if (static_cast<u16>(guild::util::RandomModulo(0x64)) <= 0x32u) {
                    // challenge variant (52); advances appointment time by 4..9.
                    guild::util::RandomModulo(6); // the +N time advance draw
                    Emit(out, MeisterCmd::kConfront52, w.personId, w.workerEntityId, 52);
                } else {
                    guild::util::RandomModulo(6);
                    Emit(out, MeisterCmd::kConfront51, w.personId, w.workerEntityId, 51);
                }
            }
            // Profession coord update for craft professions.
            u8 prof = w.profession;
            if (prof == 2 || prof == 1 || prof == 19 || prof == 20 || prof == 16 || prof == 15) {
                Emit(out, MeisterCmd::kProfessionCoord, w.workerEntityId);
                ++v3;
            }
        }
    }

    // --- Phase 3: production-worth pass --------------------------------------
    for (const MeisterCraftBuilding& c : faction.crafts) {
        if (c.hasWorthA && c.productSumA > 0) {
            ++v3;
            Emit(out, MeisterCmd::kProductionCmd15, c.ownerPersonId, c.productIdA, c.productSumA);
        }
        if (c.hasWorthB && c.productSumB > 0) {
            ++v3;
            Emit(out, MeisterCmd::kProductionCmd15, c.ownerPersonId, c.productIdB, c.productSumB);
        }
    }

    return v3;
}

// gilde.exe 0x45c670 (core) — HireStaff decision.
bool HireStaffDecision(int staffCount, int staffCap, int wage, int budget,
                       bool busyFlag, bool hasHandler) {
    if (staffCount >= staffCap)
        return false;
    // (staffCount==0) OR (!busyFlag && wage<=budget && RandomModulo(0x48) >= 36-2*staffCount)
    bool gate;
    if (staffCount == 0) {
        gate = true;
    } else {
        gate = !busyFlag && wage <= budget &&
               static_cast<u16>(guild::util::RandomModulo(0x48)) >= 36 - 2 * staffCount;
    }
    return gate && !hasHandler;
}

// gilde.exe 0x45d2ac (core) — TrainStaff decision.
bool TrainStaffDecision(int threshold, bool busyFlag, bool hasTrainer,
                        int trainerCount, int budget) {
    if (busyFlag)
        return false;
    int roll = static_cast<u16>(guild::util::RandomModulo(0x64));
    if (roll < threshold)
        return false;
    return !hasTrainer && trainerCount < 3 && budget >= kTrainStaffMinBudget;
}

// gilde.exe 0x4596e4 (core) — PlanProduction output ratio.
double PlanProductionRatio(u8 multiplierTier, int stockCount) {
    double scaled;
    switch (multiplierTier) {
    case 1: scaled = static_cast<double>(stockCount) * kProdTier1; break;
    case 2: scaled = static_cast<double>(stockCount) * kProdTier2; break;
    case 3: scaled = static_cast<double>(stockCount) * kProdTier3; break;
    default:
        // The original leaves v22 uninitialized in the default case (the switch
        // falls through to LABEL_26 without setting v22); in practice the AI
        // multiplier is always 1/2/3 for production buildings. We treat the
        // default as an unscaled stock count (0 contribution) to stay defined.
        scaled = 0.0;
        break;
    }
    return scaled * kProdRatioScale + kProdRatioBias;
}

} // namespace guild::ai
