#pragma once
// Guild-master election: candidate canvassing + winner install + the
// CalcZuenfte dispatch gate + the master-notification broadcast.
//
// This is the SECOND guild-master election variant (distinct from
// guild_election.h's ElectGuildMaster 0x481228 / CalcZunftElection 0x4813d0).
// Where that one keys candidates on the employer office-type band [13..18] and a
// single guild-master seat (type 34), THIS pass (VIBE_Amt_CollectGuildCandidates
// 0x480e4c) is the TWO-seat variant: it canvasses two candidate bands in two
// person sweeps (employer category [1..6] then [40..45]), scores the field by
// total wealth, and installs the winner into ONE of two office seats (type 28 or
// 29) chosen by the winner's own category, with a rank-distance tie gate.
//
// Recovered byte-for-byte are the deterministic rules:
//   VIBE_Amt_CollectGuildCandidates 0x480e4c — the two-pass canvass, the dedup
//       cap-16 candidate table, the quorum gate, the dual-seat incumbent lookup,
//       the wealthiest-winner pick, the rank-distance gate, and the seat split.
//   VIBE_Amt_IsOfficeBuildingValid  0x481b38 — the CalcZuenfte dispatch / the
//       query-category map {1->24, 2->25, 3->26, 5->23}.
//   VIBE_Amt_NotifyOfficeMessage    0x480d08 — the office-message broadcast
//       (message-id 6182/6184, base offset 525/560 by the +9 byte).
//   VIBE_Amt_NotifyPersonMessage    0x480da8 — the person-message broadcast
//       (message-id 6190, same base-offset rule).
//
// The Person walk (QueryBegin/IterNext), the ComputeTotalWealth scoring, the
// office-table install (VIBE_Office_AddTableEntry) and the entity-message
// transport (VIBE_He_SendEntityMessage) are owned by sim/io/command and routed
// through pools + hooks so the decision math is exercised standalone.
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// 1. CollectGuildCandidates (the two-seat guild-master election) — 0x480e4c.
// ===========================================================================
// Candidate canvassing constants (recovered from the two sweeps + the v33[16]
// candidate table). Pass 1 collects employer category in [1..6]; pass 2 collects
// employer category in [40..45]. Both sweeps count EVERY employed non-flagged
// person toward the quorum (v2). The two seats installed into are office-types
// 28 and 29 (byte_B59850 scan); the winner's seat is chosen by its category.
constexpr u8  kCanvassBand1Min = 1;   // pass-1 employer category low  (v7 >= 1)
constexpr u8  kCanvassBand1Max = 6;   // pass-1 employer category high (v7 <= 6)
constexpr u8  kCanvassBand2Min = 40;  // pass-2 employer category low  (v12 >= 40)
constexpr u8  kCanvassBand2Max = 45;  // pass-2 employer category high (v12 <= 45)
constexpr int kCanvassQuorum    = 3;  // members (v2) >= 3
constexpr int kCanvassMaxCands  = 16; // v33[16] candidate slots
constexpr u8  kSeatTypeMaster   = 29; // byte_B59850[idx] == 29 -> v34/v35 seat
constexpr u8  kSeatTypeDeputy   = 28; // byte_B59850[idx] == 28 -> v36/v37 seat
constexpr int kRankDistanceGate = 6;  // |incRank - winRank| < 6 -> same-seat path

// One swept person (the QueryBegin/IterNext walk). `employer` is the +39 word
// (0xFFFF == unemployed). `flagged` is record+90 & 1. `empCategory` is the
// employer office-object's +356 category byte (word_12CE910[268*employer]+356).
// `totalWealth` is VIBE_Person_ComputeTotalWealth, `rankHigh` is (record+353)>>24.
struct CanvassPerson {
    i32 personId    = -1;     // candidate record id (for install + dedup)
    u16 employer    = 0xFFFF; // +39 word; 0xFFFF -> not a guild member
    bool flagged    = false;  // record+90 & 1 -> excluded
    u8  empCategory = 0;      // employer office-object +356 category byte
    i32 totalWealth = 0;      // VIBE_Person_ComputeTotalWealth score
    u8  rankHigh    = 0;      // (record+353)>>24 -> held rank-in-category
    u8  winnerCategory = 0;   // the winner's own +356 byte (decides seat 28 vs 29)
};

// The seat split chosen for the winner (mirrors the four install branches).
enum class CanvassSeatChoice {
    kNone,           // no install
    kMasterSeatOnly, // AddTableEntry(seatMaster, winner) ; no incumbent
    kDeputySeatOnly, // AddTableEntry(seatDeputy, winner) ; no incumbent
    kSameSeat,       // rank-distance < 6: install into the seat by category
    kSwapMaster,     // far rank, master category: clear deputy, set master
    kSwapDeputy,     // far rank, deputy category: clear master, set deputy
};

struct CanvassResult {
    int  members    = 0;   // employed non-flagged persons (v2 quorum counter)
    int  candidates = 0;   // collected candidates (v1)
    bool quorumMet  = false; // members >= 3 && candidates >= 1
    int  seatMaster = -1;  // type-29 holder key (v34 +0), -1 if none
    int  seatDeputy = -1;  // type-28 holder key (v36 +0), -1 if none
    i32  incumbentId = -1; // v37 (deputy secondary) else v35 (master secondary)
    u8   incumbentRankHigh = 0; // (incumbent record+353)>>24
    i32  winnerId   = -1;  // wealthiest candidate (strictly greater)
    int  winnerIndex = -1; // index into the collected candidate table
    bool install    = false; // winner exists && winner != incumbent && quorum
    CanvassSeatChoice choice = CanvassSeatChoice::kNone;
    int  installSeat  = -1; // the holder key written for the winner
    int  clearedSeat  = -1; // the holder key cleared (swap branches only)
    u8   notifyMessage = 0; // the byte passed to NotifyOffice (361 / 28 / 29)
};

// Office install hook: VIBE_Office_AddTableEntry(seatKey, personId, 1, 0, 255)
// for an install, or (seatKey, 0, 1, 0, 255) to clear. `personId` == 0 -> clear.
using CanvassInstallHook = void (*)(int seatKey, i32 personId, void* ctx);
// Notify hooks: VIBE_Amt_NotifyPersonMessage(incumbent, msg) /
// VIBE_Amt_NotifyOfficeMessage(winner, msg).
using CanvassNotifyHook  = void (*)(bool isOffice, i32 personId, u8 message,
                                    void* ctx);
void CanvassSetHooks(CanvassInstallHook install, CanvassNotifyHook notify,
                     void* ctx);

// gilde.exe 0x480e4c — run the two-seat guild-master canvass. `band1`/`n1` is the
// first sweep (employer category [1..6]); `band2`/`n2` the second ([40..45]). The
// two incumbent seats are read from the live g_officeHolders table (type 28/29).
// `incumbentRankHigh` defaults from the winner-supplied field when the caller
// can't supply the incumbent record; pass it explicitly via SetIncumbentRank.
CanvassResult CollectGuildCandidates(const CanvassPerson* band1, int n1,
                                     const CanvassPerson* band2, int n2);

// Optional: supply the incumbent person's rank-high byte (record+353>>24) for the
// rank-distance gate. When unset (or no incumbent) the gate degrades to the
// "no incumbent" install path. Returns the previous value.
u8 CanvassSetIncumbentRank(u8 rankHigh);

// ===========================================================================
// 2. IsOfficeBuildingValid (CalcZuenfte dispatch + query-category map) — 0x481b38.
// ===========================================================================
// The map from the input role byte to the QueryBegin member category:
//   1 -> 24, 2 -> 25, 3 -> 26, 5 -> 23  (else: no query, result 0).
// gilde.exe 0x481b38 — returns the member-query category for `role`, or -1 when
// `role` is not in {1,2,3,5}.
int OfficeBuildingQueryCategory(u8 role);

// The outcome of the dispatcher: either it fans CalcZuenfte over ranks
// 0x1E..0x21 (when a handler-filter match exists) and returns 0, or it runs a
// single member query for the mapped category and returns whether a non-flagged
// member is present.
struct OfficeBuildingValidInputs {
    bool handlerMatch = false; // VIBE_He_FindFirstHandlerByFilter(1,0,128) != null
    u8   role         = 0;     // the switch selector (v2)
    bool memberPresent = false;// QueryBegin(...) != null && (member+90 & 1) == 0
};
// CalcZuenfte fan-out hook (the handler-match branch). Called for each rank in
// {0x1E,0x1F,0x20,0x21} with the original arg a1.
using ZuenfteFanHook = void (*)(u8 rank, int arg, void* ctx);
void OfficeBuildingSetZuenfteHook(ZuenfteFanHook hook, void* ctx);

// gilde.exe 0x481b38 — the full dispatcher. Returns 0 on the handler-match path
// (after fanning CalcZuenfte 0x1E..0x21 via the hook), 1 when a valid member is
// present for the mapped category, 0 otherwise.
int IsOfficeBuildingValid(const OfficeBuildingValidInputs& in, int arg);

// ===========================================================================
// 3. NotifyOfficeMessage / NotifyPersonMessage broadcast — 0x480d08 / 0x480da8.
// ===========================================================================
// Both gate on the target's profession byte (+2): only roles {5,6,7} broadcast.
// The message-id base offset is 560 when the target's +9 byte is set, else 525;
// the per-message argument is `message + offset`. The broadcast walks the 768
// office-object slots and sends to every slot whose role byte (+0x16A) is 6 or 7.
constexpr int kNotifyOfficeMsgId  = 6182; // office: VIBE_Text_RenderFormattedMessage id
constexpr int kNotifyOfficeMsgId2 = 6184; // office: the second template id
constexpr int kNotifyPersonMsgId  = 6190; // person: the template id
constexpr int kNotifyOffsetSet    = 560;  // +9 byte set
constexpr int kNotifyOffsetClear  = 525;  // +9 byte clear

// A target record view for the notify gate.
struct NotifyTarget {
    bool valid       = false; // result != null (person variant only)
    u8   profession  = 0;     // +2 byte (5/6/7 -> broadcast)
    u16  identity    = 0;     // *result (+0 word) -> the message subject
    bool secondFlag  = false; // +9 byte -> 560 vs 525 base offset
};

// gilde.exe 0x480d08 — true iff this office target broadcasts (profession in
// {5,6,7}); `outArg` receives message + (secondFlag ? 560 : 525). The original
// also broadcasts to all type-6/7 office slots; that transport is the hook.
bool NotifyOfficeComputeArg(const NotifyTarget& t, u8 message, int& outArg);

// gilde.exe 0x480da8 — same gate, but guarded by `valid` (null check) first.
bool NotifyPersonComputeArg(const NotifyTarget& t, u8 message, int& outArg);

// The broadcast: send `arg`/`subject` to every office slot whose role is 6 or 7.
struct NotifySlot { u8 role = 0; i32 objectId = 0; }; // +0x16A role, +4 id
using NotifySendHook = void (*)(i32 objectId, int subject, int arg, void* ctx);
// Runs the slot sweep; returns the number of sends. `roleOf`/`idOf` come from the
// 536-byte office-object table. Faithful to the (v4 += 134; != 102912) loop.
int NotifyBroadcast(const NotifySlot* slots, int count, int subject, int arg,
                    NotifySendHook send, void* ctx);

} // namespace guild::world
