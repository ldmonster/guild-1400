#pragma once
// Guild governance per-turn cluster — the deterministic rule cores of the
// Amt guild-assignment / election / zunft passes plus the production-pass cadence,
// the office-wage cycle, the office-update sweep, and the Aemter serialization.
//
// All of these read the live Person/Office tables and emit lockstep commands; the
// person walk, the AI scoring (VIBE_AiPlayer_*), the building spawn (Universe/
// render/io) and the command commit are sim/ai/command owned. Recovered here
// byte-for-byte are the deterministic decision rules:
//
//   VIBE_Amt_ElectGuildMaster   0x481228  (full: quorum + incumbent lookup +
//                                          wealth winner + install gate)
//   VIBE_Amt_CalcZuenfte        0x4813d0  (rank->category table + election core;
//                                          the building-spawn branch is deferred)
//   VIBE_Amt_ComputeGuildAssignment 0x47ff5c (member/successor counting, the
//                                          relation-delta tables, the random
//                                          tie-break; the AI scoring is hooked)
//   VIBE_Amt_RunProductionPass  0x57d448  (the production tick cadence)
//   VIBE_Amt_ProcessAllOfficeWages 0x57b6bc (the 768-seat wage cycle)
//   VIBE_Amt_UpdateOffices      0x481978  (office-type -> rank-range table + the
//                                          re-validation + zunft/election order)
//   VIBE_Amt_SaveAemter         0x483198  (37-entry holder serialization)
//   VIBE_Amt_LoadAemter         0x4832d0  (37-entry holder deserialization)
#include <cstddef>

#include "guild/common/types.h"
#include "world/law_types.h"   // OfficeHolder, kOfficeDefCount

namespace guild::world {

// ===========================================================================
// 1. ElectGuildMaster (full) — 0x481228.
// ===========================================================================
// The candidate-scoring core lives in election.h (ElectionRunGuildMaster). This
// is the FULL pass: it adds the incumbent lookup (scan the holder table for the
// guild-master office-type byte == 34) and the install + notify. The person walk
// is modeled as a candidate pool; the holder table is the live g_officeHolders.
constexpr u8 kGuildMasterOfficeType = 34; // byte_B59850[idx] == 34 -> guild master

// One swept person (the QueryBegin/IterNext walk in the original).
struct GuildPerson {
    i32 personId = -1;     // record id (the +4 dword used for the install/dedup)
    u16 employer = 0xFFFF; // word @+39 (0xFFFF == unemployed -> not a member)
    bool flagged = false;  // (record+90 & 1) -> excluded
    u8  empOfficeType = 0; // employer office-type byte @+356 (candidate gate)
    u8  rankHighByte = 0;  // (int @+353) >> 24 (held guild rank, for CalcZuenfte)
    i32 totalWealth = 0;   // VIBE_Person_ComputeTotalWealth
};

struct GuildElectionResult {
    int  officeSlot = -1;  // v17[0] (holder entry key) of the guild-master seat
    i32  incumbentId = -1; // v17[1] (current holder), -1 if vacant
    int  members = 0;      // employed non-flagged count (quorum: >= 3)
    int  candidates = 0;   // collected candidates (employer office-type in [13..18])
    i32  winnerId = -1;    // wealthiest candidate (strictly greater)
    bool install = false;  // winner exists && winner != incumbent
};

// Notify hook: the original calls NotifyPersonMessage(old, 0x22) and
// NotifyOfficeMessage(new, 0x22). Args: (isIncumbent, personId, message).
using GuildNotifyHook = void (*)(bool isIncumbent, i32 personId, int message,
                                 void* ctx);
// Install hook: VIBE_Office_AddTableEntry(officeSlot, winner, 1, 0, 255).
using GuildInstallHook = void (*)(int officeSlot, i32 winnerId, void* ctx);
void GuildSetElectionHooks(GuildInstallHook install, GuildNotifyHook notify,
                           void* ctx);

// gilde.exe 0x481228 — run the full guild-master election. `pool`/`count` are the
// swept persons; the incumbent is found by scanning g_officeHolders for the
// guild-master seat (type 34). When an install fires the hooks are invoked
// (AddTableEntry(slot, winner, ..) then notify old + new). Returns the result.
GuildElectionResult ElectGuildMasterFull(const GuildPerson* pool, int count);

// ===========================================================================
// 2. CalcZuenfte (rank->category table + election core) — 0x4813d0.
// ===========================================================================
// Keyed by the guild rank byte a1 in {0x1E..0x21}. Each rank maps to five
// category bytes used by the person query + rank-range gate + office-type install.
// Recovered exactly from the switch in the original (HIBYTE fields):
//   rank 0x1E: query=14 office=34(v78) lo=39(v76) reqB=56(v77) memQuery=23(v79)
//   rank 0x1F: query=18 office=46      lo=51      reqB=59      memQuery=24
//   rank 0x20: query=20 office=58      lo=63      reqB=62      memQuery=25
//   rank 0x21: query=21 office=64      lo=69      reqB=65      memQuery=26
struct ZunftCategories {
    u8 memberQuery;  // v2 (QueryBegin category for the member scan)
    u8 officeLow;    // v78>>24 (rank-range low for the candidate gate)
    u8 officeHigh;   // v76>>24 (rank-range high)
    u8 reqQuery;     // v77>>24 (used by the building-spawn branch)
    u8 masterQuery;  // v79>>24 (the "is a master present" query category)
    bool valid;      // false for ranks outside 0x1E..0x21
};
// gilde.exe 0x4813d0 — the recovered rank->category mapping.
ZunftCategories ZunftCategoriesForRank(u8 rank);

// gilde.exe 0x4813d0 — the election core (the path taken when a qualifying master
// candidate is present). Identical decision shape to ElectGuildMasterFull but the
// candidate gate is the held-rank range [officeLow..officeHigh] and the install
// office type is `rank`. Members count employed non-flagged persons in that range.
// The building-spawn branch (no candidate present) is DEFERRED (engine/io).
GuildElectionResult CalcZunftElection(u8 rank, const GuildPerson* pool, int count);

// ===========================================================================
// 3. ComputeGuildAssignment (counting + relation deltas + tie-break) — 0x47ff5c.
// ===========================================================================
// The assignment pass walks a holder array (24 B entries). It (a) counts entries
// in state 2 (members to assign) and state 3 (seats needing a successor), (b) for
// member assignment polls an AI "approach direction" in {0,1,2} per voter and
// emits a relation command per voter, (c) for succession tallies AI "best target"
// votes per candidate and installs the top (random tie-break). The AI calls are
// hooked; the deterministic relation-delta tables + tie-break are recovered.

// gilde.exe 0x47ff5c — relation delta for the "winning side" assignment (v29/v30):
//   approach 0 -> -20, approach 1 -> +20, approach 2 -> +4.
i32 GuildAssignDeltaWin(int approach);
// gilde.exe 0x47ff5c — relation delta for the "losing side" assignment (the else
// branch, two commands per voter): approach 0 -> first cmd -10; then
//   approach 1 -> +10, else -4.
struct GuildAssignDeltaLose {
    bool emitNeg10 = false; // approach 0 emits an extra -10 command first
    i32  delta = 0;         // approach 1 -> +10, else -4
};
GuildAssignDeltaLose GuildAssignDeltaLoseFor(int approach);

// gilde.exe 0x47ff5c — count holders in a given state across the array.
int GuildCountByState(const OfficeHolder* holders, int count, u8 state);

// gilde.exe 0x47ff5c — the successor tie-break: among `n` candidate vote tallies,
// pick the max; on a tie (more than one candidate sharing the max) pick a random
// one via VIBE_Math_RandomModulo. Returns the chosen index (-1 if n<=0). When
// `useRng` is false the first max is returned (the unique-max path).
int GuildSuccessorPick(const i32* voteTallies, int n);

// ===========================================================================
// 4. RunProductionPass cadence — 0x57d448.
// ===========================================================================
// The pass (a) flips every "profession 18" person to state 8 (a delta-field
// commit) and (b) walks buildings, ticking production and (when a daily-output
// boundary is reached) emitting a worker-hire string command. The Person/Building
// reads + commits are sim/command owned; recovered here is the cadence skeleton:
//   - persons swept where profClass == 18 -> 1 "set state 8" command each;
//   - buildings swept where profClass < 10 && hasProductionFlag -> a production
//     tick; then RunGoodsDistributionPass.
// flt_625984 @0x625984 == 0xBF800000 == -1.0f (the daily-output boundary epsilon).
constexpr float kProductionBoundaryEps = -1.0f;

struct ProductionPerson { u8 profClass = 0; i32 personId = -1; };
struct ProductionBuilding {
    u8  profClass = 0;        // byte @+2 (< 10 to be active)
    u8  productionFlag = 0;   // byte_12CE918 (must be set)
    bool atBoundary = false;  // the daily-output boundary reached this tick
    bool hasWorkerSlot = false; // dword_12CEA94 (a worker can be hired)
    bool femaleWorker = false;  // LOBYTE(dword_12CE919) -> "magd_FRAU" else "stallbursche_MANN"
    i32 objectId = -1;
};

// A "set state 8" command for a profession-18 person.
using ProductionStateHook = void (*)(i32 personId, u8 state, void* ctx);
// A worker-hire string command (object, "magd_FRAU"/"stallbursche_MANN").
using ProductionHireHook  = void (*)(i32 objectId, bool female, void* ctx);
// The goods-distribution pass (deferred body; default no-op).
using ProductionGoodsDistHook = void (*)(void* ctx);
void ProductionSetHooks(ProductionStateHook state, ProductionHireHook hire,
                        ProductionGoodsDistHook goods, void* ctx);

struct ProductionResult {
    int stateFlips = 0; // profession-18 -> state 8 commits
    int ticks = 0;      // active building production ticks
    int hires = 0;      // worker-hire commands
    bool goodsRan = false;
};
// gilde.exe 0x57d448 — run the production cadence over the person/building tables.
ProductionResult RunProductionPass(const ProductionPerson* persons, int personCount,
                                   const ProductionBuilding* buildings,
                                   int buildingCount);

// ===========================================================================
// 5. ProcessAllOfficeWages cycle — 0x57b6bc.
// ===========================================================================
// The cycle calls VIBE_Amt_ComputeOfficeWages(seat, 1, ctx) for seat = 0..767.
// The per-seat wage math lives in amt.cpp (AmtComputeOfficeWages). This driver
// recovers the 768-seat loop; `payHook(seat)` is invoked per seat (a test wires
// it to the wage computation + observes the cumulative payout).
constexpr int kOfficeWageSeats = 768; // v2 < 768
using OfficeWagePayHook = void (*)(int seat, void* ctx);
// gilde.exe 0x57b6bc — run the wage cycle. Returns the number of seats processed.
int ProcessAllOfficeWages(OfficeWagePayHook payHook, void* ctx);

// ===========================================================================
// 6. UpdateOffices (office-type -> rank-range table) — 0x481978.
// ===========================================================================
// Re-validates every elective office: a holder is removed when the person is gone,
// has no building (+8 byte), the held office-type (+361) mismatches, or the held
// rank (+353>>24) is outside the office's [low..high] band. The bands by office
// type (the switch in the original):
//   0x1C: 1..6   0x1D: 40..45  0x1E: 34..39  0x1F: 46..51
//   0x20: 58..63 0x21: 64..69  0x22: 13..18
struct OfficeRankBand { u8 low; u8 high; bool valid; };
// gilde.exe 0x481978 — the recovered office-type -> rank-band mapping.
OfficeRankBand OfficeRankBandFor(u8 officeType);

// A person's re-validation inputs for one elective office entry.
struct OfficeValidatePerson {
    bool exists = false;      // VIBE_Person_FindRecordById != null
    bool hasBuilding = false; // (record+8) byte truthy
    u8   heldOfficeType = 0;  // record+361
    u8   heldRankHigh = 0;    // (record+353)>>24
};
// gilde.exe 0x481978 — true iff the holder must be REMOVED (re-validation failed).
bool OfficeShouldRemove(u8 officeType, const OfficeValidatePerson& p);

// ===========================================================================
// 7. SaveAemter / LoadAemter serialization — 0x483198 / 0x4832d0.
// ===========================================================================
// The on-disk layout (exact field order/sizes from the original): a u32 count
// (always 37) followed by 37 records, each writing ONLY these holder fields in
// this order (note: not the full 24-byte struct):
//   byte +0 (1), dword +4 (4), byte +8 (1), dword +12 (4), byte +16 (1),
//   dword +20 (4)  == 15 bytes per record.
constexpr int kAemterCount      = 37;  // v5[0] == 37
constexpr int kAemterRecordBytes = 15; // 1+4+1+4+1+4
constexpr int kAemterStreamBytes = 4 + kAemterCount * kAemterRecordBytes; // 559

// gilde.exe 0x483198 — serialize g_officeHolders[0..36] into `out` (must hold
// kAemterStreamBytes). Returns the number of bytes written (kAemterStreamBytes),
// or 0 if `out` is null.
size_t SaveAemter(u8* out, size_t cap);

// gilde.exe 0x4832d0 — deserialize into g_officeHolders[0..36] from `in`. Returns
// 1 on success, 0 if the leading count != 37 or the buffer is too small.
int LoadAemter(const u8* in, size_t len);

} // namespace guild::world
