// The per-turn ORCHESTRATION driver — see turn_driver.h.
// gilde.exe 0x533188 VIBE_GameTick_BeginPlayerRound.
//
// This is a 1:1 translation of the ordered pass sequence. The render/UI/voice/
// movie leaves and the network sync-wait loops are not simulation order and are
// elided here (DEFERRED — see report); every simulation pass is invoked through
// the ctx.pass hook in the original's exact order, so the recovered sequence is
// the contract.
#include "sim/turn_driver.h"

namespace guild::sim {

const char* TurnPassName(TurnPass p) {
    switch (p) {
        case TurnPass::ComputeWealthGrid:          return "ComputeWealthGrid";
        case TurnPass::NpcTurnFlagSweep:           return "NpcTurnFlagSweep";
        case TurnPass::TickRegisteredEvents:       return "TickRegisteredEvents";
        case TurnPass::ExpireEventSlots:           return "ExpireEventSlots";
        case TurnPass::ExpireApEventSlots:         return "ExpireApEventSlots";
        case TurnPass::PlantGrowth:                return "PlantGrowth";
        case TurnPass::RecalcAllProduction:        return "RecalcAllProduction";
        case TurnPass::CitySnapshotStats:          return "CitySnapshotStats";
        case TurnPass::StraftatSyncAll:            return "StraftatSyncAll";
        case TurnPass::MeisterProcessPlayers:      return "MeisterProcessPlayers";
        case TurnPass::AmtRunProductionPass:       return "AmtRunProductionPass";
        case TurnPass::AmtUpdateOfficeProsperity:  return "AmtUpdateOfficeProsperity";
        case TurnPass::AmtRunBuildingTaxPass:      return "AmtRunBuildingTaxPass";
        case TurnPass::AmtProcessLoanRepayments:   return "AmtProcessLoanRepayments";
        case TurnPass::AmtProcessOfficeWages:      return "AmtProcessOfficeWages";
        case TurnPass::AmtUpdateOffices:           return "AmtUpdateOffices";
        case TurnPass::MeisterRunBuildingTasks:    return "MeisterRunBuildingTasks";
        case TurnPass::HeProcessAllPlayerNews:     return "HeProcessAllPlayerNews";
        case TurnPass::AiMethodBroadcastGroup:     return "AiMethodBroadcastGroup";
        case TurnPass::CityTickStatsBroadcast:     return "CityTickStatsBroadcast";
        case TurnPass::AdvanceTurnTimer:           return "AdvanceTurnTimer";
        case TurnPass::MeisterProcessBuildingNeeds:return "MeisterProcessBuildingNeeds";
        case TurnPass::TurnEndCoord27Broadcast:    return "TurnEndCoord27Broadcast";
        case TurnPass::AmtBuildingTaxPassLight:    return "AmtBuildingTaxPassLight";
        case TurnPass::ResetPerTurnAccumulators:   return "ResetPerTurnAccumulators";
        case TurnPass::SyncAllTurnStates:          return "SyncAllTurnStates";
        case TurnPass::Count:                      return "Count";
    }
    return "?";
}

// Invoke one pass: record it in ctx.order, then call the bound hook.
static void Run(TurnDriverCtx& ctx, TurnPass p, int arg = 0) {
    ctx.order.push_back(p);
    if (ctx.pass)
        ctx.pass(p, arg, ctx.passCtx);
}

void BeginPlayerRound(const TurnState& state, TurnDriverCtx& ctx) {
    // 0x533191 — recompute the 8x8 district wealth grid.
    Run(ctx, TurnPass::ComputeWealthGrid);

    // 0x5331a2..0x5331df — walk all Persons, clear per-turn flags, find the last
    // human (kind 6) -> word_63CC5C.
    Run(ctx, TurnPass::NpcTurnFlagSweep);
    if (ctx.npcAliveMarker && ctx.npcKinds && ctx.npcTurnFlags) {
        ctx.humanPersonIndex = RunNpcTurnFlagSweep(
            ctx.npcAliveMarker, ctx.npcKinds, ctx.npcTurnFlags, ctx.npcCount);
    }

    // 0x5331e6 — MeisterAi event bookkeeping (then the RandomModulo(10) / sync
    // wait that depends on featureMask&4 is net plumbing, elided).
    Run(ctx, TurnPass::TickRegisteredEvents);
    // (0x533209) dword_11BC2D0 = 147591 round sync token — modeled in TurnState.

    // 0x53326f / 0x533274 — expire AI event + AP-event slots.
    Run(ctx, TurnPass::ExpireEventSlots);
    Run(ctx, TurnPass::ExpireApEventSlots);

    // (0x533280 round-begin scroll UI when isRoundOwner — DEFERRED.)

    // 0x533341..0x5333b6 — per farm (Person kind 30 player-6), grow its plants.
    Run(ctx, TurnPass::PlantGrowth);
    if (ctx.farms) {
        for (auto& farm : *ctx.farms)
            ctx.plantsAdvanced +=
                PlantAdvanceFarm(farm.data(), static_cast<int>(farm.size()));
    }

    // 0x5333be / 0x5333c3 — recompute all production, then snapshot city stats.
    Run(ctx, TurnPass::RecalcAllProduction);
    Run(ctx, TurnPass::CitySnapshotStats);

    // 0x53365d — branch: does THIS peer drive the heavy economic cascade?
    if (state.DrivesHeavyPasses()) {
        // 0x5333d5 — sync all crime records (then RefreshGuildState + Sleep, the
        // UI keep-alive between heavy passes, is plumbing).
        Run(ctx, TurnPass::StraftatSyncAll);

        // 0x5333e8..0x533416 — for each alive player faction, run its AI turn.
        if (ctx.factions) {
            for (int f = 0; f < ctx.factionCount && f < kMaxFactions; ++f) {
                const FactionSlot& s = ctx.factions[f];
                if (s.aliveMarker != 0xFFFF && s.isPlayer) {
                    Run(ctx, TurnPass::MeisterProcessPlayers, f);
                    ctx.processedFactions.push_back(f);
                }
            }
        } else {
            Run(ctx, TurnPass::MeisterProcessPlayers, -1);
        }

        // 0x533426 — Amt production pass.
        Run(ctx, TurnPass::AmtRunProductionPass);
        // 0x533439 — office prosperity.
        Run(ctx, TurnPass::AmtUpdateOfficeProsperity);
        // 0x533451 — building tax pass (flags = 3), then 0x533456 loan repayments.
        Run(ctx, TurnPass::AmtRunBuildingTaxPass, 3);
        Run(ctx, TurnPass::AmtProcessLoanRepayments);
        // 0x533469 — office wages, then 0x53346e update offices.
        Run(ctx, TurnPass::AmtProcessOfficeWages);
        Run(ctx, TurnPass::AmtUpdateOffices);
        // 0x533481 — MeisterAi per-building tasks (NullTick at 0x533486 is a stub).
        Run(ctx, TurnPass::MeisterRunBuildingTasks);
        // 0x533499 — process all players' news/heralds.
        Run(ctx, TurnPass::HeProcessAllPlayerNews);
        // 0x5334ac — broadcast AI group state.
        Run(ctx, TurnPass::AiMethodBroadcastGroup);
        // 0x5334bf — tick + broadcast city stats.
        Run(ctx, TurnPass::CityTickStatsBroadcast);

        // 0x5334c6..0x53350b — City_CopyStateStruct(v37); if v37[0] == -1 send a
        // splendor sync command, else advance the turn timer. We model "no city
        // state -1" (the common path) as: advance the timer.
        Run(ctx, TurnPass::AdvanceTurnTimer);

        // 0x533510 — resolve building needs (what to build next).
        Run(ctx, TurnPass::MeisterProcessBuildingNeeds);

        // 0x533523..0x53355a — broadcast turn-end Coord27 per Person, then a
        // final (-1,-1) sentinel.
        Run(ctx, TurnPass::TurnEndCoord27Broadcast);
        if (ctx.npcKinds && ctx.npcCount > 0) {
            for (int i = 0; i < ctx.npcCount; ++i)
                ctx.coord27Factions.push_back(i);   // QueueRequestCoord27(-1, id, 0)
        }
    } else {
        // 0x533668 — non-driving peer: only the light building-tax pass (flags=2).
        Run(ctx, TurnPass::AmtBuildingTaxPassLight, 2);
    }

    // 0x53355f..0x533561 — clear every Person's per-turn money accumulator.
    Run(ctx, TurnPass::ResetPerTurnAccumulators);
    if (ctx.perTurnAccum && ctx.npcCount > 0)
        ResetPerTurnAccumulators(ctx.perTurnAccum, ctx.npcCount);

    // (0x533598 win fanfare/outro scroll + 0x5336a3 news event scroll — UI,
    // DEFERRED.)

    // 0x5336c8 — when this peer drives, sync all characters' turn states.
    if (state.DrivesHeavyPasses())
        Run(ctx, TurnPass::SyncAllTurnStates);
}

} // namespace guild::sim
