#pragma once
// MeisterAi per-faction economy turn (gilde.exe 0x5321ec MeisterAi_ProcessPlayerTurn)
// plus the staffing/production decision cores and a turn-pass driver.
//
// ProcessPlayerTurn is the Guild-Master AI's whole economy turn for one faction.
// It does NOT mutate state directly: every decision is emitted as a network
// command (Command_Queue*/Enqueue*) applied deterministically on every peer. We
// reproduce its control flow and the exact ORDER of emitted commands, routing
// each through an injectable command sink (the "mock command hook") so the
// sequence is verifiable. The deeply entity/query/render-coupled leaf reads
// (entity arrays, GameObject_QueryFind, Building_*, Coord_ConvertX, Relation_*)
// are injected through MeisterEnv so the pass runs on a synthetic faction.
//
// The pass has three phases, in this order:
//   1. Object/building sweep over the 256-slot object array (owner == player):
//      cmd25 request (flag 0x800), security sweep (kind 4/16/19), banker tax
//      (kind 5), guard target (kind 11/12/13), market supervision + confrontation
//      (Quad56 + SlotReset28), end-of-building mood-decay roll.
//   2. Worker stocking + mood pass over the faction's production workers (kind 6):
//      per entry-slot QueueRequest16 (item value), mood/relation delta -> Coord27,
//      State22 relation delta, confrontation (variant 52/51), profession coord.
//   3. Production-worth pass over the faction's craft buildings (kind 4):
//      EnqueueCmd15 for two product-sum payouts.
#include <vector>
#include "guild/common/types.h"

namespace guild::ai {

// --- command sink (the mock command hook) -----------------------------------
// Every Command_* the turn emits is tagged with a TurnCmd op + its operands, in
// emission order. This mirrors the deterministic command channel.
enum class MeisterCmd : int {
    kRequestArgs25,        // Command_QueueRequestArgs25 (cmd25 needs request)
    kSecuritySweepHeat,    // BeginDeltaPacket+AppendDeltaField+State22 (heat clamp)
    kSecuritySweepCmd14,   // EnqueueCmd14 (no guarding building found)
    kSecuritySweepQuad43,  // QueueRequestQuad43 (heat <= 5)
    kBankerStats23,        // QueueRequestState23 (banker stat reset)
    kBankerTax15,          // EnqueueCmd15 (16000 * multiplier)
    kGuardTarget61,        // QueueRequestGuardTarget61
    kSuperviseQuad56,      // QueueRequestQuad56 (market supervision)
    kSuperviseReset28,     // QueueRequestSlotReset28 (op 27 supervise)
    kSuperviseReset28b,    // QueueRequestSlotReset28 (op 49 stammtisch)
    kMoodDecay,            // Person_AdjustMoodAndNotify (decay)
    kStockRequest16,       // QueueRequest16 (item value to worker)
    kWorkerSpawn,          // Character_SpawnAtBuildingEntrance
    kWorkerMoodCoord27,    // QueueRequestCoord27 (mood/relation delta)
    kWorkerState22,        // BeginDeltaPacket+State22 (relation delta packet)
    kConfront52,           // QueueRequestSlotReset28 op 52 (challenge)
    kConfront51,           // QueueRequestSlotReset28 op 51 (duel)
    kProfessionCoord,      // BeginDeltaPacket+State22 (profession coord update)
    kProductionCmd15,      // EnqueueCmd15 (production-worth payout)
};

struct MeisterCmdRecord {
    MeisterCmd op;
    i32 a = 0, b = 0, c = 0, d = 0;
};

// --- synthetic faction model ------------------------------------------------
// A building/object record (object array slot, stride 169 in the original). Only
// the fields the turn reads are modeled.
struct MeisterBuilding {
    bool alive = true;
    i32  ownerPlayer = 0;     // +39 (word) owner/player id
    u8   kind = 0;            // +0 record kind byte (4/5/6/11/12/13/16/19/...)
    i32  id = 0;              // +1 building id
    i32  sceneNodeId = 0;     // +93 associated scene node id
    bool needsCmd25 = false;  // (+91 flag) bit 0x800 set => emit cmd25
    u8   aiMultiplier = 1;    // AiPlayer +583 (banker tax 16000*mult)
    u8   guardTargetHi = 0;   // AiPlayer +544 HIBYTE
    // security sweep objects (QueryFind .. 202): per object {heat at +55,
    // guarding building security level (>=0) or -1 if no building}.
    struct SecObj { int heat; int securityLevel; i32 entityId; };
    std::vector<SecObj> securityObjects;
    bool isMarketSupervised = false; // matches dword_12CEA7C (op49 stammtisch)
    bool flagBit0 = false;    // Begin[45]&1 (mood-decay exclusion)
};

// A production worker the stocking/mood pass processes (Person kind 6).
struct MeisterWorker {
    i32  personId = 0;        // dword_12CE914 column value (commands)
    int  attitudeA = 100;     // +61
    int  attitudeB = 100;     // +65
    int  relation  = 0;       // player<->worker relation (Relation_LookupMatrixEntry)
    u8   profession = 0;      // +357 (2/1/19/20/16/15 => craft tiers, coord update)
    int  itemValue = 0;       // precomputed QueueRequest16 value (Coord_ConvertX of
                              //   ComputeItemBaseValue * attitudeA*0.01)
    i32  workerEntityId = 0;  // the worker's own entity id (State22 target)
};

// A craft building the production-worth pass processes (Person kind 4).
struct MeisterCraftBuilding {
    i32  ownerPersonId = 0;   // dword_12CE914 column (command target)
    int  productSumA = 0;     // GameObject_SumValuesAtLocation (node 232) if v102[12]
    int  productSumB = 0;     // GameObject_SumValuesAtLocation (node 240) if v102[15]
    bool hasWorthA = false;   // v102[12] gate
    bool hasWorthB = false;   // v102[15] gate
    i32  productIdA = 0, productIdB = 0;
};

struct MeisterFaction {
    i32 playerId = 0;
    std::vector<MeisterBuilding> buildings;     // phase 1 (object sweep)
    std::vector<MeisterWorker>   workers;       // phase 2 (stocking/mood)
    std::vector<MeisterCraftBuilding> crafts;   // phase 3 (production worth)
};

// gilde.exe 0x5321ec — run a full MeisterAi player turn on `faction`. Appends the
// emitted commands (in emission order) to `out`. Consumes the CRT RNG exactly as
// the original (RandomModulo draws for mood-decay and confrontation). Returns the
// total command count (the original's `v3` accumulator, modulo the 32-flush
// reset which is not modeled here — the running total is preserved across the
// flush points so the test reference stays single-valued).
int ProcessPlayerTurn(const MeisterFaction& faction, std::vector<MeisterCmdRecord>& out);

// --- staffing decision cores ------------------------------------------------

// gilde.exe 0x45c670 (core) — HireStaff decision. Given the current production-
// staff `staffCount`, the building's staff cap `staffCap` (AiPlayer +561 + +562),
// the wage `wage`, the available `budget`, whether the busy flag (+456 & 8) is
// set `busyFlag`, and whether a worker handler already exists `hasHandler`,
// decide whether to hire. Hire iff staffCount < staffCap AND (staffCount==0 OR
// (!busyFlag AND wage<=budget AND RandomModulo(0x48) >= 36 - 2*staffCount)) AND
// !hasHandler. Consumes one RandomModulo(0x48) draw only when staffCount>0.
// Returns true if a hire command should be emitted.
bool HireStaffDecision(int staffCount, int staffCap, int wage, int budget,
                       bool busyFlag, bool hasHandler);

// gilde.exe 0x45d2ac (core) — TrainStaff decision. Given the gate threshold
// `threshold` (a3), whether the busy flag (+456 & 8) is set `busyFlag`, whether a
// trainer handler exists `hasTrainer`, the current trainer count `trainerCount`,
// and the budget `budget`, decide whether to train. Train iff !busyFlag AND
// RandomModulo(100) >= threshold AND !hasTrainer AND trainerCount<3 AND
// budget>=38400. Consumes one RandomModulo(100) draw (only when !busyFlag).
// Returns true if a train command should be emitted (cost 12800).
bool TrainStaffDecision(int threshold, bool busyFlag, bool hasTrainer,
                        int trainerCount, int budget);
constexpr int kTrainStaffCost = 12800;       // EnqueueCmd15 amount
constexpr int kTrainStaffMinBudget = 38400;  // budget gate

// gilde.exe 0x4596e4 (core) — PlanProduction output multiplier. The production
// scaling applied to the building's stock count based on the AiPlayer multiplier
// tier (+583): 1 -> *flt_6198F0 (1.5625e-05), 2 -> *flt_6198EC (7.8125e-06),
// 3 -> *flt_6198E8 (3.906e-06), else the
// stock count is left unscaled (the switch default). The result is then
// (scaled * 0.5 + 0.25) (flt_6198F4 / flt_6198F8). Returns the production ratio.
double PlanProductionRatio(u8 multiplierTier, int stockCount);

} // namespace guild::ai
