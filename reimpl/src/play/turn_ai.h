#pragma once
// play::turn_ai — a REAL per-turn AI / NPC-schedule pass that mutates the live
// world (PLAYABLE_PLAN P4 "the AI heart").
//
// turn_driver.{h,cpp} (play::TurnDriver) drives the deterministic ECONOMY turn
// (price EMA + production integral + fire event). This module is the orthogonal
// AI side: it runs the reconstructed AI DIRECTOR + NPC-SCHEDULE passes — exactly
// the AI-relevant subset of the original per-turn driver
// VIBE_GameTick_BeginPlayerRound (0x533188) — over the LIVE shared entity arrays
// (sim::g_persons / sim::g_objects) for one player round.
//
// The original turn driver runs a fixed ordered cascade (already reconstructed
// 1:1 as the hookable sim::BeginPlayerRound / sim::TurnPass list). The AI-bearing
// passes of that cascade are:
//   - NpcTurnFlagSweep         (0x5331a2)  per-NPC turn-flag clear (+0x1C8 bits),
//                                          finds the last human (kind 6).
//   - TickRegisteredEvents     (0x5331e6)  VIBE_MeisterAi_TickRegisteredEvents.
//   - ExpireEventSlots/ApSlots (0x53326f/74)
//   - MeisterProcessPlayers    (0x533404)  VIBE_MeisterAi_ProcessPlayerTurn /faction
//                                          -> the per-worker mood/relation +
//                                          confrontation + mood-decay rule cores
//                                          (ai::MoodRelationDelta / ConfrontationDecision
//                                          / MoodDecayRoll), written back to the
//                                          live Person records.
//   - MeisterRunBuildingTasks  (0x533481)
//   - AiMethodBroadcastGroup   (0x5334ac)  VIBE_AiMethod_BroadcastGroupState — the
//                                          per-group-leader 4-bit state-mask roll.
//   - MeisterProcessBuildingNeeds (0x533510)
//   - TurnEndCoord27Broadcast  (0x533523)
//
// RunAiTurn binds those passes to the REAL reconstructed AI siblings (guild::ai)
// and lets the rest run as inert recorded hooks (the deferred giants — the full
// ProcessPlayerTurn entity-query/command-emit halves, the Amt economy passes —
// stay inert here, exactly as the build model prescribes). Every random draw is
// rooted at crt::Srand, so the same seed + same world drives the AI to a
// byte-identical end state across reruns (verified via play::HashWorldState).
#include <cstdint>
#include <vector>

#include "guild/common/types.h"

namespace guild::play {

// Per-turn AI effect tally — what the AI/NPC passes actually did to the live
// world this turn. Compared element-wise by the evolution/determinism asserts and
// printed by the e2e report.
struct AiTurnEffects {
    // sim::BeginPlayerRound pass accounting.
    int passesRun        = 0;   // total TurnPass steps invoked (the ordered list)
    int aiPassesRun      = 0;   // of those, the AI-bearing passes we wired real

    // NpcTurnFlagSweep (0x5331a2): live persons whose +0x1C8 turn-flags changed.
    int npcFlagsCleared  = 0;
    int humanPersonIndex = -1;  // word_63CC5C — last kind-6 person index

    // MeisterProcessPlayers (0x533404): the mood/relation/confront/decay rule core
    // run over every live worker of every processed player faction.
    int factionsProcessed   = 0;   // alive player factions whose AI turn ran
    int workersEvaluated    = 0;   // live Person records the mood rule scored
    int moodDeltasApplied   = 0;   // workers whose attitude/relation changed
    int confrontationsSpawned = 0; // ConfrontationDecision != 0
    int moodDecaysApplied   = 0;   // MoodDecayRoll != 0
    long long relationDeltaSum = 0;// sum of signed relation deltas written

    // AiMethodBroadcastGroup (0x5334ac): group-state mask broadcasts.
    int groupBroadcasts  = 0;      // group leaders that broadcast a mask
    long long groupMaskSum = 0;    // sum of rolled masks (RNG-driven; evolves)

    // TickRegisteredEvents (0x5331e6): registered AP/event handlers ticked.
    int eventsTicked     = 0;

    bool operator==(const AiTurnEffects& o) const;
    bool operator!=(const AiTurnEffects& o) const { return !(*this == o); }
};

// Run ONE real AI / NPC turn over the LIVE entity arrays (sim::g_persons et al.).
//
// `seed` re-seeds the shared CRT RNG (crt::Srand) so the AI's random decisions
// (mood-decay rolls, confrontation gates, group-state masks, tie-breaks) are
// reproducible. Pass the SAME seed each turn for a fully deterministic K-turn run
// rooted once, or vary it per turn — either way two runs with the same seed
// sequence over the same starting world produce identical results.
//
// The caller is responsible for having populated the live arrays (ResetEntityArrays
// + a seeded synthetic world, or a real city load). RunAiTurn does NOT reset them.
// Returns the per-turn effect tally. After the call, play::HashWorldState() will
// have advanced iff any AI pass mutated a live record.
AiTurnEffects RunAiTurn(std::uint32_t seed);

// Convenience: run `turns` AI turns from a single root seed (each turn re-seeds
// with seed + turnIndex so the per-turn RNG stream is reproducible but distinct,
// keeping the world genuinely EVOLVING rather than converging to a fixed point).
// Returns the per-turn effect tally (size == turns).
std::vector<AiTurnEffects> RunAiTurns(std::uint32_t seed, int turns);

} // namespace guild::play
