#include "play/turn_ai.h"

#include <vector>

#include "crt/rand.h"             // Srand, RandNext, RandStatePtr

#include "sim/entity.h"           // g_persons, kPersonCapacity, ResetEntityArrays
#include "sim/person.h"           // PersonGetByte / PersonSetByte (byte-faithful)
#include "sim/types.h"            // Person, PersonField
#include "sim/turn_driver.h"      // BeginPlayerRound, TurnDriverCtx, TurnPass
#include "sim/gametick.h"         // TurnState, ClearTurnFlags

#include "ai/meisterai.h"         // MoodRelationDelta / ConfrontationDecision / MoodDecayRoll / TurnWorker
#include "ai/meisterai3.h"        // TickRegisteredEvents, BroadcastGroupState, ApHandler, MethodEnv
#include "ai/aimethod2.h"         // MethodEnv

namespace guild::play {

bool AiTurnEffects::operator==(const AiTurnEffects& o) const {
    return passesRun == o.passesRun && aiPassesRun == o.aiPassesRun &&
           npcFlagsCleared == o.npcFlagsCleared &&
           humanPersonIndex == o.humanPersonIndex &&
           factionsProcessed == o.factionsProcessed &&
           workersEvaluated == o.workersEvaluated &&
           moodDeltasApplied == o.moodDeltasApplied &&
           confrontationsSpawned == o.confrontationsSpawned &&
           moodDecaysApplied == o.moodDecaysApplied &&
           relationDeltaSum == o.relationDeltaSum &&
           groupBroadcasts == o.groupBroadcasts &&
           groupMaskSum == o.groupMaskSum &&
           eventsTicked == o.eventsTicked;
}

namespace {

// Live-Person field offsets the original AI rule cores read by raw byte offset
// (the MeisterAi_ProcessPlayerTurn worker math addresses worker+61 / worker+65).
// These sit inside the Person record's not-yet-named AI block (+0x29..+0x80).
constexpr int kPfAttitudeA = 61;   // worker+61 — MoodRelationDelta attitudeA
constexpr int kPfAttitudeB = 65;   // worker+65 — MoodRelationDelta attitudeB
// kPfReputation (+0x80) is the documented "reputation/heat scalar byte"; the AI
// turn director's mood pass adjusts the player<->worker relation, which we map to
// this live relation/heat column (signed, stored as a byte).
constexpr int kPfRelation  = guild::sim::kPfReputation;   // +0x80
// kPfGroupState: a spare live byte the BroadcastGroupState mask is written into so
// the broadcast genuinely mutates the world (the original emits a delta-field
// command; here it lands in the live record's AI block, evolving the hash).
constexpr int kPfGroupState = 0x2A;  // inside the AI/turn-fields block (+0x29..)

// Read a signed-byte field from a live Person record.
int GetSByte(const guild::sim::Person* rec, int off) {
    return static_cast<signed char>(guild::sim::PersonGetByte(rec, off));
}

// ---------------------------------------------------------------------------
// The driver context shared between the bound pass hooks and RunAiTurn. The pass
// hook (PassHook) dispatches each TurnPass to the REAL reconstructed AI sibling,
// mutating the live g_persons array; the inert passes just count.
// ---------------------------------------------------------------------------
struct AiTurnRunner {
    AiTurnEffects fx;

    // MethodEnv for the AiMethod group-state roll: default leaves (rng routed
    // through the shared LCG ai::RandomModulo). Deterministic given the seed.
    guild::ai::MethodEnv env{};

    // --- MeisterProcessPlayers (0x533404) ---------------------------------
    // The per-faction Guild-Master AI turn: walk this faction's live workers and
    // run the mood/relation + confrontation + mood-decay rule cores, writing the
    // results back into the live Person records. This is the faithful DATA/RULES
    // core of VIBE_MeisterAi_ProcessPlayerTurn (the deferred entity-query/command
    // halves stay inert — see ai/meisterai.cpp banner).
    void ProcessFactionAiTurn(int faction) {
        using namespace guild::sim;
        ++fx.factionsProcessed;
        for (int i = 0; i < kPersonCapacity; ++i) {
            Person& w = g_persons[i];
            if (w.marker == -1)        // free slot
                continue;
            if (w.isPlayer == 0)       // not a live actor / worker
                continue;
            // A faction's workers are its owned persons (ownerPlayer == faction).
            // (Single-faction worlds: ownerPlayer 0 == default faction.)
            if (static_cast<int>(w.ownerPlayer) != faction)
                continue;

            ++fx.workersEvaluated;

            int attA = GetSByte(&w, kPfAttitudeA);
            int attB = GetSByte(&w, kPfAttitudeB);
            int rel  = GetSByte(&w, kPfRelation);

            // (1) mood/relation delta — VIBE_MeisterAi mood core (0x53265f).
            int delta = guild::ai::MoodRelationDelta(attA, attB, rel);
            int newRel = rel;
            if (delta != 0) {
                newRel = rel + delta;
                // clamp to the signed-byte range the live column holds.
                if (newRel > 127) newRel = 127;
                if (newRel < -128) newRel = -128;
                guild::sim::PersonSetByte(&w, kPfRelation,
                                          static_cast<guild::u8>(newRel));
                ++fx.moodDeltasApplied;
                fx.relationDeltaSum += (newRel - rel);
            }

            // (2) confrontation decision — VIBE_MeisterAi confront core (0x5328a3).
            // Consumes RNG exactly as the original (gate + variant rolls).
            if (newRel < -26) {
                int variant = guild::ai::ConfrontationDecision(newRel);
                if (variant != 0)
                    ++fx.confrontationsSpawned;
            }

            // (3) end-of-worker mood-decay roll — VIBE_MeisterAi (0x532485).
            // flagBit0 from the live status flag; consumes RNG as the original.
            bool flagBit0 =
                (guild::sim::PersonGetDword(&w, guild::sim::kPfStatusFlag) & 1) != 0;
            int decay = guild::ai::MoodDecayRoll(w.kind, flagBit0);
            if (decay != 0) {
                int newA = attA + decay;     // decay lands on the mood/attitude
                if (newA > 127) newA = 127;
                if (newA < -128) newA = -128;
                guild::sim::PersonSetByte(&w, kPfAttitudeA,
                                          static_cast<guild::u8>(newA));
                ++fx.moodDecaysApplied;
            }
        }
    }

    // --- AiMethodBroadcastGroup (0x5334ac) --------------------------------
    // VIBE_AiMethod_BroadcastGroupState walks the person table; every "group
    // leader" rolls a 4-bit state mask and broadcasts it. We use the REAL
    // ai::RollGroupStateMask core and write the mask into the live record (the
    // original's command emission), so the world genuinely evolves.
    void BroadcastGroupStates() {
        using namespace guild::sim;
        for (int i = 0; i < kPersonCapacity; ++i) {
            Person& p = g_persons[i];
            if (p.marker == -1 || p.isPlayer == 0)
                continue;
            // The original treats byte+2 == 3 as "group leader"; we mirror that
            // on the live kind byte so only some persons broadcast.
            if (p.kind != 3)
                continue;
            int mask = guild::ai::RollGroupStateMask(env);   // REAL RNG mask roll
            guild::sim::PersonSetByte(&p, kPfGroupState,
                                      static_cast<guild::u8>(mask));
            ++fx.groupBroadcasts;
            fx.groupMaskSum += mask;
        }
    }

    // --- TickRegisteredEvents (0x5331e6) ----------------------------------
    // VIBE_MeisterAi_TickRegisteredEvents over the registered-handler table. We
    // materialize a small synthetic handler list (the live game's byte_11D6040
    // table is a separate global not modeled in g_persons); the per-type dispatch
    // is inert (Meister3Hooks default), so this is a deterministic count pass.
    void TickEvents() {
        using namespace guild::sim;
        std::vector<guild::ai::ApHandler> handlers;
        // One live handler per live player faction-person of kind 6/30 (a meister
        // / building actor), bounded so it stays cheap and deterministic.
        for (int i = 0; i < kPersonCapacity; ++i) {
            const Person& p = g_persons[i];
            if (p.marker == -1 || p.isPlayer == 0)
                continue;
            if (p.kind == 6 || p.kind == 30) {
                guild::ai::ApHandler h{};
                h.active = 1;
                h.flags  = 0x10;            // bit 0x10 set -> ticks
                h.type   = static_cast<guild::u8>(p.kind);   // < 0x88
                handlers.push_back(h);
            }
        }
        fx.eventsTicked =
            guild::ai::TickRegisteredEvents(handlers.data(),
                                            static_cast<int>(handlers.size()));
    }
};

// The bound pass hook: BeginPlayerRound calls this for every TurnPass in the
// recovered order. We dispatch the AI-bearing passes to the runner's real-sibling
// drivers and count the rest as inert recorded steps.
void PassHook(guild::sim::TurnPass pass, int arg, void* ctx) {
    auto* r = static_cast<AiTurnRunner*>(ctx);
    using P = guild::sim::TurnPass;
    ++r->fx.passesRun;
    switch (pass) {
        case P::TickRegisteredEvents:
            r->TickEvents();
            ++r->fx.aiPassesRun;
            break;
        case P::MeisterProcessPlayers:
            r->ProcessFactionAiTurn(arg);
            ++r->fx.aiPassesRun;
            break;
        case P::AiMethodBroadcastGroup:
            r->BroadcastGroupStates();
            ++r->fx.aiPassesRun;
            break;
        case P::NpcTurnFlagSweep:
        case P::ExpireEventSlots:
        case P::ExpireApEventSlots:
        case P::MeisterRunBuildingTasks:
        case P::MeisterProcessBuildingNeeds:
        case P::TurnEndCoord27Broadcast:
            ++r->fx.aiPassesRun;   // AI-bearing but inert/deferred this turn
            break;
        default:
            break;                 // economy/UI passes — not our concern here
    }
}

} // namespace

AiTurnEffects RunAiTurn(std::uint32_t seed) {
    using namespace guild::sim;

    // Root all AI randomness at this seed so the turn is reproducible.
    crt::Srand(seed);

    // Build the live-array views the orchestration's leaf cores read directly.
    // NpcTurnFlagSweep walks these columns and clears each live slot's transient
    // turn-flag bits (the +0x1C8 dword), recording the last human (kind 6).
    static std::vector<guild::u16> aliveMarkers;
    static std::vector<guild::u8>  kinds;
    static std::vector<guild::u32> turnFlags;
    aliveMarkers.assign(kPersonCapacity, 0);
    kinds.assign(kPersonCapacity, 0);
    turnFlags.assign(kPersonCapacity, 0);
    for (int i = 0; i < kPersonCapacity; ++i) {
        aliveMarkers[i] = static_cast<guild::u16>(g_persons[i].marker);
        kinds[i]        = g_persons[i].kind;
        turnFlags[i]    = static_cast<guild::u32>(
            PersonGetDword(&g_persons[i], kPfTurnBits));
    }

    AiTurnRunner runner;

    // Drive the AI cascade through the reconstructed ordered pass list. Force the
    // heavy (host) path so the AI passes actually run, and supply ONE alive player
    // faction so MeisterProcessPlayers fires (faction 0 == the default owner).
    TurnState state{};
    state.runFlags = 0x08;          // (runFlags & 8) -> DrivesHeavyPasses()

    static std::vector<FactionSlot> factions;
    factions.assign(1, FactionSlot{});
    factions[0].aliveMarker = 0;    // != 0xFFFF -> alive
    factions[0].isPlayer    = true; // player faction -> AI turn runs

    TurnDriverCtx tctx{};
    tctx.pass    = &PassHook;
    tctx.passCtx = &runner;
    tctx.npcAliveMarker = aliveMarkers.data();
    tctx.npcKinds       = kinds.data();
    tctx.npcTurnFlags   = turnFlags.data();
    tctx.npcCount       = kPersonCapacity;
    tctx.factions       = factions.data();
    tctx.factionCount   = static_cast<int>(factions.size());

    BeginPlayerRound(state, tctx);

    // Write the swept turn-flags back into the live records (the sweep is the
    // original's in-place dword_12CEAD8[i] &= 0xE0874703) and tally what changed.
    for (int i = 0; i < kPersonCapacity; ++i) {
        guild::u32 before = static_cast<guild::u32>(
            PersonGetDword(&g_persons[i], kPfTurnBits));
        guild::u32 after = turnFlags[i];
        if (after != before) {
            PersonSetDword(&g_persons[i], kPfTurnBits,
                           static_cast<guild::i32>(after));
            ++runner.fx.npcFlagsCleared;
        }
    }
    runner.fx.humanPersonIndex = tctx.humanPersonIndex;
    runner.fx.factionsProcessed = static_cast<int>(tctx.processedFactions.size());

    return runner.fx;
}

std::vector<AiTurnEffects> RunAiTurns(std::uint32_t seed, int turns) {
    std::vector<AiTurnEffects> out;
    out.reserve(turns > 0 ? turns : 0);
    for (int t = 0; t < turns; ++t)
        out.push_back(RunAiTurn(seed + static_cast<std::uint32_t>(t)));
    return out;
}

} // namespace guild::play
