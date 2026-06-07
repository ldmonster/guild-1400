#include "world/amt_goods.h"

// Faithful port of the recoverable core of VIBE_Amt_RunGoodsDistributionPass
// (gilde.exe 0x57dd84) and VIBE_Amt_RefreshGuildState (0x4becdc).
//
// The original walks word_12CE910 (stride 536 bytes) three times: once to count
// active production buildings (v0), once to drain over-stocked type-3 businesses,
// once to handle type-8 firms (drain / spawn / tally). It then fires a rand-gated
// "special firm" spawn and, when the active count is at/below dbl_62598C (652.8),
// spawns replacement businesses toward a target of 40. Each spawn spins on the
// command queue (VIBE_Amt_RefreshGuildState) until the enqueued build resolves.
// We keep the exact branch structure, constants and RNG order; the Person/Building
// joins and the command commits are routed through GoodsDistribHooks.

namespace guild::world {

// ---------------------------------------------------------------------------
// RefreshGuildState pump.
// ---------------------------------------------------------------------------
namespace {
RefreshGuildStateHook g_refreshHook = nullptr;
void*                 g_refreshCtx = nullptr;
} // namespace

void AmtSetRefreshGuildStateHook(RefreshGuildStateHook hook, void* ctx) {
    g_refreshHook = hook;
    g_refreshCtx = ctx;
}

// gilde.exe 0x4becdc — FlushSendQueue + ReceiveAndQueue + ExecCommands.
int AmtRefreshGuildState() {
    if (g_refreshHook)
        return g_refreshHook(g_refreshCtx);
    return 0;
}

// ---------------------------------------------------------------------------
// Leading active-count walk (v0).
//   if ( *(_WORD*)v1 != 0xFFFF && v1[8] ) ++v0;   for every stride-536 slot.
// ---------------------------------------------------------------------------
int GoodsCountActive(const GoodsBuilding* b, int count) {
    int v0 = 0;
    for (int i = 0; i < count; ++i) {
        if (b[i].present && b[i].occupied != 0)
            ++v0;
    }
    return v0;
}

// ---------------------------------------------------------------------------
// Type-3 drain predicate (first drain walk).
//   present && occupied && typeByte==3
//     && ( worthWord >= RandomModulo(4)+44 || (double)activeCount >= 652.8 )
//     && guardsClear (+358/+360/+361 all 0)
//     && FindMatchingSupplier()
// ---------------------------------------------------------------------------
bool GoodsType3ShouldDrain(const GoodsBuilding& b, int activeCount, int rand4) {
    if (!b.present || b.occupied == 0)
        return false;
    if (b.typeByte != 3)
        return false;
    bool worthGate = (static_cast<unsigned>(b.worthWord)
                          >= static_cast<unsigned>(rand4) + 44u)
                  || (static_cast<double>(activeCount) >= kGoodsThreshold);
    if (!worthGate)
        return false;
    if (!b.guardsClear)
        return false;
    return b.hasSupplier;
}

// ---------------------------------------------------------------------------
// Replenish spawn-count selector.
//   v32 >= 40            -> 0
//   v32 >= 30  (< 40)    -> RandomModulo(2)+1
//   v32 <  30            -> (40 - v32) / 2
// ---------------------------------------------------------------------------
int GoodsSpawnCount(int firms, int rand2) {
    if (firms >= 30) {
        if (firms >= 40)
            return 0;
        return rand2 + 1;          // (u16)RandomModulo(2) + 1
    }
    return (40 - firms) / 2;
}

// ---------------------------------------------------------------------------
// Full pass.
// ---------------------------------------------------------------------------
GoodsDistribResult GoodsRunDistributionPass(const GoodsBuilding* b, int count,
                                            GoodsDistribHooks& hooks) {
    GoodsDistribResult r;

    // Walk 1: count active production buildings (v0).
    int v0 = GoodsCountActive(b, count);

    int v32 = 0;   // firm tally
    int v33 = 0;   // sub-tally (field123)

    // Walk 2: drain over-stocked type-3 businesses, decrementing the active count.
    for (int i = 0; i < count; ++i) {
        if (!b[i].present || b[i].occupied == 0 || b[i].typeByte != 3)
            continue;
        // The worth gate consumes a RandomModulo(4) draw whenever the type test
        // passed (the original evaluates it lazily, but the worth comparison is
        // the first short-circuit term; reproduce the draw order by drawing here).
        int rand4 = hooks.RandomModulo(4);
        if (GoodsType3ShouldDrain(b[i], v0, rand4)) {
            --v0;                      // --v0 before the stock adjust
            hooks.DrainStock(i);       // VIBE_Building_AdjustStockAndNotify
            ++r.drained;
        }
    }

    // Walk 3: type-8 firms — drain / spawn-block / tally.
    for (int i = 0; i < count; ++i) {
        if (!b[i].present || b[i].occupied <= 1)   // *v4 != -1 && +8 > 1
            continue;
        const GoodsBuilding& bi = b[i];

        if (static_cast<double>(v0) > kGoodsThreshold
            && bi.typeByte == 8 && bi.ownersClear) {
            // Over-supplied: drain this firm if it has a supplier.
            if (bi.hasSupplier) {
                --v0;
                hooks.DrainStock(i);
                ++r.drained;
            }
        } else if (bi.typeByte == 8 && bi.overStocked && bi.ownersClear) {
            // Under-supplied but this firm is over-stocked: convert it (the delta
            // packet sets +2 = 3 / +13 = 2 and queues a payout) — counted as a firm.
            ++v32;
        } else if (bi.activeType) {
            // An active character/firm: tally, and bump the sub-count when field123.
            ++v32;
            if (bi.hasField123)
                ++v33;
        }
    }

    r.activeCount = v0;

    // Rand-gated "special firm" spawn: when v33 < 6 and RandomModulo(4) == 0.
    if (v33 < 6 && hooks.RandomModulo(4) == 0) {
        if (hooks.SpawnSpecialFirm()) {   // spins on RefreshGuildState until status 1
            ++v32;
            r.spawnedSpecial = true;
        }
    }

    // Replenish step: only when the active count is at/below the threshold.
    if (static_cast<double>(v0) <= kGoodsThreshold) {
        int rand2 = (v32 >= 30 && v32 < 40) ? hooks.RandomModulo(2) : 0;
        int n = GoodsSpawnCount(v32, rand2);
        r.spawnCount = n;
        for (int k = 0; k < n; ++k) {
            // The original computes a building variant from two RandomModulo draws
            // (RandomModulo(2)+1 wing, RandomModulo(0xC)+1 kind) before each spawn.
            int wing = hooks.RandomModulo(2) + 1;
            int kind = hooks.RandomModulo(0xC) + 1;
            int variant = kind * 16 + wing;   // VIBE_BuildingType_ComputeVariantIndex stand-in
            if (hooks.SpawnBusiness(variant)) {
                ++v32;
                ++r.spawned;
            }
        }
    }

    r.firms = v32;
    r.field123 = v33;
    return r;
}

} // namespace guild::world
