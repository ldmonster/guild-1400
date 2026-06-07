#pragma once
// The per-turn ORCHESTRATION driver (gilde.exe VIBE_GameTick_BeginPlayerRound
// 0x533188) — the exact ordered sequence of passes invoked each player round.
//
// VIBE_GameTick_BeginPlayerRound is the turn-based economic master tick. It runs
// the day/turn cascade in a FIXED order: city-wealth recompute -> per-NPC turn
// flag clear -> cutscene/round-begin UI -> plant growth -> production recalc +
// city stats snapshot -> (if this peer drives) the heavy cascade: MeisterAi per
// faction, then the Amt economic passes (production, prosperity, building-tax,
// loans, wages, offices), then building-needs/news/group-state/city-stats, then
// the turn-end Coord27 broadcast -> per-turn accumulator reset -> news/outro UI
// -> SyncAllTurnStates.
//
// This module reconstructs that ORCHESTRATION 1:1: the ordered pass sequence and
// the branch conditions (TurnState::DrivesHeavyPasses, isRoundOwner, featureMask
// bit 4), wiring the already-translated pass functions in. Every pass is invoked
// through a settable PASS HOOK table (default: a recording mock) so the exact
// sequence is verifiable and the heavy world/ai passes can be plugged in (the
// host wires the real world::Amt* / ai::ProcessPlayerTurn; tests wire mocks or
// the real rule cores). Mutations all route through the command hook (mock).
//
// The render/UI/cutscene/voice/movie leaves (Surface_ColorFill, Scroll_Open,
// Voice_PlayQueuedSample, Movie_PlayOutro, GameLogic_RunFrameLoop, the
// History/Cutscene display) are NOT part of the simulation order and are
// DEFERRED (listed in the report); the orchestration records them as inert
// "ui-marker" steps so the simulation-relevant order stays intact.
#include <vector>

#include "guild/common/types.h"
#include "sim/gametick.h"

namespace guild::sim {

// ===========================================================================
// The ordered passes of a player round (the enum value IS the invocation
// order). Names map 1:1 to the calls in VIBE_GameTick_BeginPlayerRound; the
// address comment gives the original call site.
// ===========================================================================
enum class TurnPass : int {
    ComputeWealthGrid = 0,   // 0x533191 VIBE_City_ComputeWealthGrid
    NpcTurnFlagSweep,        // 0x5331a2 768-Person flag clear + human-index
    TickRegisteredEvents,    // 0x5331e6 VIBE_MeisterAi_TickRegisteredEvents
    ExpireEventSlots,        // 0x53326f VIBE_MeisterAi_ExpireEventSlots
    ExpireApEventSlots,      // 0x533274 VIBE_MeisterAi_ExpireApEventSlots
    PlantGrowth,             // 0x533398 VIBE_Plant_AdvanceGrowthStage (per farm)
    RecalcAllProduction,     // 0x5333be VIBE_Building_RecalcAllProduction
    CitySnapshotStats,       // 0x5333c3 VIBE_City_SnapshotStats
    // ---- heavy cascade (only when DrivesHeavyPasses()) ----
    StraftatSyncAll,         // 0x5333d5 VIBE_Straftat_SyncAllToNetwork
    MeisterProcessPlayers,   // 0x533404 VIBE_MeisterAi_ProcessPlayerTurn /faction
    AmtRunProductionPass,    // 0x533426 VIBE_Amt_RunProductionPass
    AmtUpdateOfficeProsperity,// 0x533439 VIBE_Amt_UpdateOfficeProsperity
    AmtRunBuildingTaxPass,   // 0x533451 VIBE_Amt_RunBuildingTaxPass(3)
    AmtProcessLoanRepayments,// 0x533456 VIBE_Amt_ProcessLoanRepayments
    AmtProcessOfficeWages,   // 0x533469 VIBE_Amt_ProcessAllOfficeWages
    AmtUpdateOffices,        // 0x53346e VIBE_Amt_UpdateOffices
    MeisterRunBuildingTasks, // 0x533481 VIBE_MeisterAi_RunBuildingTasks
    HeProcessAllPlayerNews,  // 0x533499 VIBE_He_ProcessAllPlayerNews
    AiMethodBroadcastGroup,  // 0x5334ac VIBE_AiMethod_BroadcastGroupState
    CityTickStatsBroadcast,  // 0x5334bf VIBE_City_TickStatsAndBroadcast
    AdvanceTurnTimer,        // 0x533672 VIBE_GameTick_AdvanceTurnTimer (or city sync)
    MeisterProcessBuildingNeeds,// 0x533510 VIBE_MeisterAi_ProcessBuildingNeeds
    TurnEndCoord27Broadcast, // 0x53353f VIBE_Command_QueueRequestCoord27 /faction
    // ---- non-driving peer path ----
    AmtBuildingTaxPassLight, // 0x533668 VIBE_Amt_RunBuildingTaxPass(2)
    // ---- always (after the branch) ----
    ResetPerTurnAccumulators,// 0x53355f dword_12CE8E0 clear
    SyncAllTurnStates,       // 0x5336e1 VIBE_Character_SyncAllTurnStates
    Count
};

const char* TurnPassName(TurnPass p);

// ===========================================================================
// Pass hook: every orchestration step invokes the bound function for its
// TurnPass (default: a recording no-op). `arg` carries the per-pass operand the
// original passes (e.g. RunBuildingTaxPass's flags = 3 vs 2; the faction id for
// the per-player MeisterAi pass; the faction id for the Coord27 broadcast).
// ===========================================================================
using TurnPassFn = void (*)(TurnPass pass, int arg, void* ctx);

// ===========================================================================
// The faction/player iteration the heavy cascade uses (0x5333e8..0x533416):
//   v21 = faction id 0..767;  v22 = 268 * faction (WORD index into word_12CE910)
//   if (word_12CE910[v22] != -1 && byte_12CE918[v22*2])   // alive && is-player
//       MeisterAi_ProcessPlayerTurn(faction);
// The per-Person stride is 536 bytes == 268 words; byte_12CE918 is the is-player
// column at byte offset 8 within the record (so 2*v22 bytes == 536*faction).
// ===========================================================================
constexpr int kPlayerIterWordStride = 268;   // word_12CE910 step per faction
constexpr int kMaxFactions          = 768;

// One faction slot the iteration reads (the two columns it gates on).
struct FactionSlot {
    u16  aliveMarker = 0xFFFF; // word_12CE910[268*f] ; -1 == free
    bool isPlayer    = false;  // byte_12CE918[536*f]  ; nonzero == player faction
};

// ===========================================================================
// Driver context: the synthetic state + bound hooks the orchestration drives.
// The driver fills `order` with the TurnPass sequence actually invoked (for the
// sequence-verification tests) and routes every pass through `pass`.
// ===========================================================================
struct TurnDriverCtx {
    // --- bound passes (default: record into `order`) ---
    TurnPassFn pass = nullptr;
    void*      passCtx = nullptr;

    // --- synthetic state the leaf cores need ---
    // Person columns for the NPC sweep (length npcCount).
    const u16* npcAliveMarker = nullptr;
    const u8*  npcKinds = nullptr;
    u32*       npcTurnFlags = nullptr;
    int        npcCount = 0;
    // Per-turn accumulator column (length 134*npcCount dwords).
    i32*       perTurnAccum = nullptr;
    // Faction slots for the player iteration (length factionCount).
    const FactionSlot* factions = nullptr;
    int        factionCount = 0;
    // Plant farms grown this turn (each a node list); optional.
    std::vector<std::vector<PlantNode>>* farms = nullptr;

    // --- recorded results ---
    std::vector<TurnPass> order;   // every pass invoked, in order
    int humanPersonIndex = -1;     // from the NPC sweep (word_63CC5C)
    std::vector<int> processedFactions;   // factions ProcessPlayerTurn ran for
    std::vector<int> coord27Factions;     // faction ids broadcast at turn end
    int plantsAdvanced = 0;        // total plant nodes advanced
};

// ===========================================================================
// gilde.exe 0x533188 — VIBE_GameTick_BeginPlayerRound.
// Runs one player round on the supplied synthetic state, invoking each pass in
// the recovered order through ctx.pass and recording the sequence in ctx.order.
// The branch on TurnState::DrivesHeavyPasses() selects the heavy cascade vs the
// light (non-driving) building-tax-only path, exactly as the original.
// ===========================================================================
void BeginPlayerRound(const TurnState& state, TurnDriverCtx& ctx);

} // namespace guild::sim
