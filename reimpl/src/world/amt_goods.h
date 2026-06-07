#pragma once
// Goods-distribution pass (full) + the guild-state refresh pump — the deferred
// remainder of the Amt heartbeat.
//
// Translated functions:
//   VIBE_Amt_RunGoodsDistributionPass  0x57dd84   (full pass)
//   VIBE_Amt_RefreshGuildState         0x4becdc   (command-queue pump)
//
// VIBE_Amt_RunGoodsDistributionPass is the per-turn business-replenishment pass.
// It walks the building/person scene array (gilde.exe word_12CE910, stride 536
// bytes == 268 words, terminated at byte_1333110), counting "active" production
// buildings, draining over-stocked ones, and — when the active count falls at or
// below the threshold dbl_62598C (652.8) — spawning replacement businesses to top
// the city back up toward 40. Every world mutation in the original commits through
// the lockstep command queue (VIBE_Command_EnqueueObjectInteraction /
// QueueRequest16 / BeginDeltaPacket / ...) and reads the live Person/Building
// tables; here those are routed through GoodsDistribHooks (mocked in tests) and a
// small per-building POD view, so the deterministic cadence + spawn arithmetic are
// recovered standalone. RNG is VIBE_Math_RandomModulo == crt::RandNext()%n.
//
// VIBE_Amt_RefreshGuildState is the 3-call command pump the pass spins on while
// waiting for an enqueued build command to resolve (FlushSendQueue +
// ReceiveAndQueue + ExecCommands); modelled as a settable hook.
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered constant.
// ===========================================================================
// dbl_62598C @0x62598C == 0x4084666666666666 == 652.8 — the "city is well
// supplied" threshold. The spawn step only runs when the active production count
// is <= this; the type-8 drain step only runs when it is strictly >.
constexpr double kGoodsThreshold = 652.8;   // dbl_62598C

// ===========================================================================
// Per-building scene view (subset of the 536-byte record the pass walks).
// ===========================================================================
// Offsets recovered from the decompile (rec = a building/person slot base):
//   +0x00  marker      (word)  0xFFFF == free slot
//   +0x02  typeByte    (byte)  3 == production business, 8 == "Betrieb"/firm
//   +0x08  occupied    (byte)  != 0 == active; the type-8 drain needs > 1
//   +0x0A  worthWord   (word)  v2[5]: building worth/level (the 44+rand gate)
//   +0x16C dead0       (byte)  +358: a "destroyed/closed" guard (must be 0 to drain)
//   +0x16E dead1/dead2 (bytes) +360/+361: further guards (must be 0)
// The type-8 step additionally reads two owner ids and a stock pair; we surface
// only the booleans the rules need (ownersClear, overStocked) since resolving the
// Person records is sim-owned.
struct GoodsBuilding {
    bool present = false;     // +0x00 != 0xFFFF
    u8   typeByte = 0;        // +0x02
    u8   occupied = 0;        // +0x08 (drain-8 needs > 1; active needs != 0)
    u16  worthWord = 0;       // +0x0A (v2[5])
    bool guardsClear = true;  // +358/+360/+361 all zero
    bool hasSupplier = false; // VIBE_Building_FindMatchingSupplier() != 0
    bool ownersClear = false; // both owner persons null-or-kind-15 (type-8 path)
    bool overStocked = false; // stock >= capacity (the type-8 alt drain gate)
    bool activeType = false;  // VIBE_Character_IsActiveType() (the count branch)
    bool hasField123 = false; // *((_DWORD*)v4 + 123) != 0 (the v33 sub-count)
};

// ===========================================================================
// Mutation / pump hooks (command lockstep + RNG). Defaults: spawns "succeed"
// (return a fake committed command), RNG comes from crt::RandNext.
// ===========================================================================
struct GoodsDistribHooks {
    virtual ~GoodsDistribHooks() = default;
    // VIBE_Building_AdjustStockAndNotify: drain one over-stocked building by
    // -(driftWeight+1) (the value is the engine's; we only count the call).
    virtual void DrainStock(int buildingIndex) = 0;
    // VIBE_Command_EnqueueObjectInteraction(16,...) then QueueRequest16: spawn a
    // replacement "special" firm. Returns true if the command committed (status 1).
    virtual bool SpawnSpecialFirm() = 0;
    // VIBE_Command_EnqueueObjectInteraction(3,...,variant) then QueueRequest16 +
    // delta: spawn a replacement business of the given building variant. Returns
    // true on commit.
    virtual bool SpawnBusiness(int variantIndex) = 0;
    // VIBE_Math_RandomModulo(n): RandNext()%n (n==0 -> 0).
    virtual int RandomModulo(int n) = 0;
};

// gilde.exe 0x57dd84 — counts active production buildings: occupied (+8 != 0) and
// present (+0 != 0xFFFF). This is the leading `v0` accumulator.
int GoodsCountActive(const GoodsBuilding* b, int count);

// gilde.exe 0x57dd84 — the type-3 drain predicate (first walk). A production
// building (type 3) is drained when it is present, occupied, has a supplier, all
// destroy-guards clear, and EITHER its worth word reaches the per-tick threshold
// (worthWord >= RandomModulo(4)+44) OR the city is already over the threshold
// (activeCount >= 652.8). Returns true when the building should be drained.
bool GoodsType3ShouldDrain(const GoodsBuilding& b, int activeCount, int rand4);

// gilde.exe 0x57dd84 — the spawn-count selector for the replenish step (only when
// activeCount <= threshold):
//   firms >= 40           -> 0
//   firms >= 30 (< 40)    -> RandomModulo(2)+1   (1 or 2)
//   firms < 30            -> (40 - firms) / 2
// `firms` is the running v32 firm tally; `rand2` is RandomModulo(2).
int GoodsSpawnCount(int firms, int rand2);

// ===========================================================================
// The full pass result (counts, for the e2e reference).
// ===========================================================================
struct GoodsDistribResult {
    int activeCount = 0;     // v0 after the drain walks (active production count)
    int firms = 0;           // v32 (firm tally feeding the spawn-count selector)
    int field123 = 0;        // v33 (sub-tally gating the special-firm spawn)
    int drained = 0;         // DrainStock calls issued (both walks)
    bool spawnedSpecial = false; // the v33<6 && rand%4==0 special-firm spawn fired
    int spawnCount = 0;      // the replenish spawn target (GoodsSpawnCount output)
    int spawned = 0;         // replenish businesses actually committed
};

// gilde.exe 0x57dd84 — run the whole pass over a building view. Drains over-stocked
// type-3 and type-8 buildings, tallies firms (v32) / field123 (v33), fires the
// rand-gated special-firm spawn, then — if the active count is at/below threshold —
// spawns GoodsSpawnCount() replacement businesses. Mutations route through `hooks`.
GoodsDistribResult GoodsRunDistributionPass(const GoodsBuilding* b, int count,
                                            GoodsDistribHooks& hooks);

// ===========================================================================
// VIBE_Amt_RefreshGuildState 0x4becdc — the command-queue pump.
// ===========================================================================
// The real function is exactly:
//   VIBE_Command_FlushSendQueue();
//   VIBE_Command_ReceiveAndQueue();
//   return VIBE_Command_ExecCommands();
// We model it as a settable hook (the net/command layer is sim-owned); the default
// is a no-op returning 0. The distribution pass calls this in its spin-wait loops.
using RefreshGuildStateHook = int (*)(void* ctx);
void AmtSetRefreshGuildStateHook(RefreshGuildStateHook hook, void* ctx);
int  AmtRefreshGuildState();

} // namespace guild::world
