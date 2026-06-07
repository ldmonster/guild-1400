// Per-turn tick state + leaf rule cores — see gametick.h.
// gilde.exe 0x533188 VIBE_GameTick_BeginPlayerRound (state + cascade leaves)
//           0x56eba4 VIBE_Plant_AdvanceGrowthStage  (growth rule)
#include "sim/gametick.h"

namespace guild::sim {

TurnState& GameTurnState() {
    static TurnState s;
    return s;
}

// gilde.exe 0x5331a2..0x5331df — the 768-slot Person turn-flag sweep.
// for (i=0; i<768; ++i) {
//   DumpNpcRecord(...);                       // statistics (DEFERRED)
//   v5 = dword_12CEAD8[i] & 0xE0874703;
//   kind = byte_12CE912[i];
//   dword_12CEAD8[i] = v5;
//   if (kind == 6) v0 = i;                    // last human
// }
// word_63CC5C = v0;
int RunNpcTurnFlagSweep(const u16* aliveMarker, const u8* kinds,
                        u32* turnFlags, int count) {
    int humanIndex = -1;  // v0 = -1
    for (int i = 0; i < count; ++i) {
        // The flag clear is unconditional in the original (it masks every row,
        // alive or not). aliveMarker is read for parity / future filters.
        (void)aliveMarker;
        turnFlags[i] = ClearTurnFlags(turnFlags[i]);
        if (kinds[i] == 6)
            humanIndex = i;
    }
    GameTurnState().humanPersonIndex = static_cast<i16>(humanIndex);
    return humanIndex;
}

// gilde.exe 0x56ebf0..0x56ebfc — the per-node growth increment.
u8 PlantAdvanceStage(u8 stage, u8 cap) {
    if (stage < cap)
        return static_cast<u8>(stage + 1);
    return stage;
}

// gilde.exe 0x56ebaa..0x56ec04 — the 384-node growth loop (growth rule only).
int PlantAdvanceFarm(PlantNode* nodes, int count) {
    int advanced = 0;
    for (int i = 0; i < count; ++i) {
        PlantNode& n = nodes[i];
        if (n.typeByte == 0xFF)          // node[10]>>24 == -1 (empty)
            continue;
        u8 next = PlantAdvanceStage(n.stage, n.cap);
        if (next != n.stage) {
            n.stage = next;
            ++advanced;
        }
    }
    return advanced;
}

// gilde.exe 0x53355f..0x533561 — the per-turn accumulator clear.
void ResetPerTurnAccumulators(i32* accum, int personCount) {
    for (int i = 0; i < personCount; ++i)
        accum[i * kPerTurnAccumStride] = 0;
}

} // namespace guild::sim
