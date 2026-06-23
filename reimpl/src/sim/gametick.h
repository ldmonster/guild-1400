#pragma once
// Per-turn tick state + leaf rule cores for the Guild turn driver (gilde.exe).
//
// MODULE: the per-turn tick driver VIBE_GameTick_BeginPlayerRound (0x533188) and
// its day/turn cascade. This file recovers the byte-exact TURN-STATE and
// PLAYER-ITERATION globals the orchestration reads/writes, plus the two
// self-contained rule cores the cascade runs before the heavy passes:
//   * the per-NPC turn-flag clear  (dword_12CEAD8[i] &= 0xE0874703)
//   * plant/farm growth-stage advance (VIBE_Plant_AdvanceGrowthStage 0x56eba4)
//   * the per-turn accumulator reset (dword_12CE8E0[134*i] = 0)
// The ordered ORCHESTRATION (which pass runs when) lives in turn_driver.{h,cpp};
// this file is the data substrate + leaf math it drives.
//
// All globals below are recovered from VIBE_GameTick_BeginPlayerRound's
// decompilation and verified with get_bytes; the offset comments give the
// original symbol + absolute address (imagebase 0x400000).
#include <cstddef>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Turn-state globals (the control bits the orchestration branches on).
// Recovered byte-for-byte; the cold IDB shows the static-init values below.
// ===========================================================================
struct TurnState {
    // word_63C740 (0x63C740): feature/run mask. Bit 0x04 => "networked / sync"
    // path (RunSyncWaitLoop, broadcast Coord27 per faction); bit 0x80 (read by
    // production) => "pause" (production windows ignored). Static image: 0.
    u16 featureMask = 0;          // word_63C740
    // dword_63C79C (0x63C79C): "is the local player the round owner / show UI".
    // Gates the round-begin scroll, the news scroll, and SyncAllTurnStates.
    // Static image: 1.
    i32 isRoundOwner = 1;         // dword_63C79C
    // byte_63CC28 (0x63CC28): run flags. Bit 0x08 => "this peer drives the
    // heavy passes" (the host). Static image: 0.
    u8  runFlags = 0;             // byte_63CC28
    // dword_764CE0 (0x764CE0): net standalone flag. -1 == single-player/host
    // (so the host runs the heavy passes). Static image: 0 (no session loaded).
    i32 netStandalone = 0;        // dword_764CE0
    // word_63CC5C (0x63CC5C): index of the last player-6 (human) Person scanned
    // this turn; set in the NPC sweep, consumed by the news scroll. Init -1.
    i16 humanPersonIndex = -1;    // word_63CC5C
    // dword_63C744 (0x63C744): difficulty level (0..4). Used by the city
    // sync-command splendor formula. Static image: 0.
    i32 difficulty = 0;           // dword_63C744
    // dword_11BC2D0 (0x11BC2D0): the round's sync token (147591 == 0x24087),
    // written before the optional RunSyncWaitLoop. Static image: 0.
    i32 syncToken = 0;            // dword_11BC2D0
    // dword_63CC30 (0x63CC30): "game-over outro shown" latch; set after the
    // win/outro path so the news scroll is not also shown. Static image: 0.
    i32 outroShown = 0;           // dword_63CC30
    // byte_63CC41 (0x63CC41): "victory condition met this turn" flag, gates the
    // win fanfare + outro. Static image: 0.
    u8  victoryFlag = 0;          // byte_63CC41

    // Returns true when this peer is the authoritative round driver and so runs
    // the heavy pass cascade: (runFlags & 8) != 0 || netStandalone == -1.
    // (The exact predicate is repeated three times in the original.)
    bool DrivesHeavyPasses() const {
        return (runFlags & 8) != 0 || netStandalone == -1;
    }
};

// The single global turn-state instance (mirrors the scattered globals).
TurnState& GameTurnState();

// gilde.exe 0x53319b — the round sync token constant written to dword_11BC2D0.
constexpr i32 kRoundSyncToken = 147591;        // 0x24087

// ===========================================================================
// Per-NPC turn-flag clear (the first cascade step, inside the 768-Person scan).
//   v5 = dword_12CEAD8[i] & 0xE0874703;  dword_12CEAD8[i] = v5;
// Clears the per-turn transient bits while preserving the persistent mask.
// ===========================================================================
constexpr u32 kTurnFlagPersistMask = 0xE0874703u;  // word @0x5331b7

// Clears the transient turn flags of `flags`, keeping the persistent bits.
inline u32 ClearTurnFlags(u32 flags) { return flags & kTurnFlagPersistMask; }

// ===========================================================================
// Person NPC-sweep result. The sweep walks all 768 Person slots, clears each
// live slot's turn flags, and records the index of the last human (kind 6).
// `kinds[i]` is the kind byte column (byte_12CE912), `aliveMarker[i]` the +0
// word (-1 == free). `turnFlags[i]` is dword_12CEAD8[i] (cleared in place).
// Returns the human-person index (word_63CC5C; -1 if none).
// ===========================================================================
int RunNpcTurnFlagSweep(const u16* aliveMarker, const u8* kinds,
                        u32* turnFlags, int count /* == 768 */);

// ===========================================================================
// Plant/farm growth (VIBE_Plant_AdvanceGrowthStage 0x56eba4, growth-rule core).
// The original walks a 64-node sub-array of one farm building's plant nodes:
//   v2 = base; v3 = base + 0x600 (1536 bytes); do { ... v2 += 0x18; } while v2!=v3
// i.e. stride 24 bytes (6 dwords) over a 0x600-byte span == 64 nodes
// (lea ebx,[eax+600h] @0x56ebac; add edx,18h @0x56ebff — disasm-verified, the
//  Hex-Rays "a1 + 384" is 384 *dwords* = the span, NOT the node count).
// For each node whose empty-marker byte (the top byte of node[+0x0A], i.e. the
// signed-extended byte at offset 13: `sar [edx+0Ah],18h == -1`) is not 0xFF and
// whose growth-stage byte ([edx+0Dh], the SAME byte at offset 13) is below the
// per-type cap (OfficeTypeRecord[+0x44=68], unsigned compare `jnb`), it
// increments the stage by 1. The model-detach and reload
// (Object_DetachAndRelease / EnsureModelsLoaded) is render plumbing (DEFERRED).
// This recovers the integer growth rule.
//   if stage < cap: stage += 1.
// ===========================================================================
constexpr int kPlantNodeCount  = 64;  // 0x600 / 0x18 (disasm @0x56ebac/0x56ebff)
constexpr int kPlantNodeStride = 6;   // dwords (24 bytes / 0x18)

// Advance one plant node's growth stage toward its cap. Returns the new stage.
u8 PlantAdvanceStage(u8 stage, u8 cap);

// A synthetic plant node (the fields the growth rule reads/writes). NOTE: in the
// binary the empty-marker and the growth stage are the SAME byte (offset 13);
// `typeByte` here mirrors that marker (0xFF == empty) and `stage` the value it
// increments — for a real node the two always coincide, but the split keeps the
// "skip empty / else grow" rule legible. cap is OfficeTypeRecord[+68].
struct PlantNode {
    u8 typeByte = 0;   // offset 13 as marker; 0xFF == empty slot (skipped)
    u8 stage    = 0;   // offset 13 as growth stage (mutated; unsigned < cap)
    u8 cap      = 0;   // OfficeTypeRecord[+68] for this node's type
};

// Advance every non-empty node in `nodes` one stage toward its cap (mirrors the
// 384-node loop). Returns the count of nodes actually advanced.
int PlantAdvanceFarm(PlantNode* nodes, int count);

// ===========================================================================
// Per-turn accumulator reset (the cascade's final per-Person clear).
//   for (m=0; m != 102912; m += 134) dword_12CE8E0[m] = 0;
// 102912 == 134 * 768 dwords; zeroes each Person's per-turn money accumulator.
// ===========================================================================
constexpr int kPerTurnAccumStride = 134;  // dwords per Person row
void ResetPerTurnAccumulators(i32* accum /* dword_12CE8E0, 134*768 dwords */,
                              int personCount /* == 768 */);

} // namespace guild::sim
