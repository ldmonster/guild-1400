#pragma once
// Guild membership / office-assignment passes — the deterministic decision cores
// that drive the per-turn "assign guild members & successors" governance sweep.
// Faithful 1:1 port of the VIBE_Amt_* assignment functions from gilde.exe that sit
// ABOVE the small holder-table search leaves (amt_slot_table.*) and the office
// mutation rules (office_assign.*). These walk the 24-byte OfficeHolder entries
// (law_types.h; gilde.exe byte_B59848) plus the live "office object" table.
//
// The Person record reads, the AI scoring (VIBE_AiPlayer_*), the network commit
// (VIBE_Command_*) and the slot install (VIBE_Office_AddTableEntry) are owned
// elsewhere and are surfaced as hooks in GuildAssignContext so the deterministic
// counting / relation-delta / vote-tally / tie-break math can be tested in
// isolation against golden vectors.
//
// Translated functions:
//   VIBE_Amt_ComputeGuildAssignment  0x47ff5c  (member-approach poll + successor
//                                               vote tally + random tie-break)
//   VIBE_Amt_PersonHasOfficeObject   0x480abc  (is a member's building present)
//   VIBE_Amt_CheckGuildMastersPresent 0x48091c (master present in a category)
//   VIBE_Amt_AssignGuildMembers      0x480634  (6-category orchestrator)
//   VIBE_Amt_HasOccupiedOffice       0x480cb4  (any guild-master seat occupied)
//   VIBE_Amt_AssignSlotData          0x56ea50  (placement-slot record writer)
#include <cstddef>

#include "guild/common/types.h"
#include "world/law_types.h"   // OfficeHolder

namespace guild::world {

// ---------------------------------------------------------------------------
// Holder entry states (the +16 state byte the assignment pass switches on).
// ---------------------------------------------------------------------------
enum GuildHolderState : u8 {
    kHolderMemberToAssign = 2,  // state 2 -> a member needs a relation/install pass
    kHolderNeedsSuccessor = 3,  // state 3 -> a vacant seat needs a successor vote
};

// ---------------------------------------------------------------------------
// The "office object" table (gilde.exe word_12CE910 @0x12CE910, 536-byte stride,
// 768 entries). Only the two fields the assignment / master-present scans read are
// modeled: the object id (dword_12CE914, the slot's +4 dword) and the category /
// office-type byte (byte_12CEA78, the slot's +0x168 byte). A 0 category byte means
// the slot is empty (the master-present scan treats byte_12CEA78[k*2] as a marker).
// ---------------------------------------------------------------------------
constexpr int kGuildObjectCount  = 768;   // 411648 / 536
constexpr int kGuildObjectStride = 536;
struct GuildOfficeObject {
    i32 id        = 0;   // +0x04  (dword_12CE914) the entry's object/person id
    u8  category  = 0;   // +0x168 (byte_12CEA78) office-type / category byte
    u8  promoFlag = 0;   // +0x16A (byte_12CE912) staff role code (6/7 == a master)
    u8  busyFlag  = 0;   // +0x249 (byte_12CEAC1) non-zero == role already filled
};

// ---------------------------------------------------------------------------
// A person record view (models VIBE_Person_FindRecordById). The assignment pass
// reads: byte +8 (has a building), byte +358 (is an eligible employer), byte +433
// (excluded flag) and byte +2 (profession/staff role 6|7 == a master). `valid`
// models a non-null FindRecordById result.
// ---------------------------------------------------------------------------
struct GuildPersonRec {
    bool valid       = false; // FindRecordById != null
    bool hasBuilding = false; // +8   byte truthy
    bool eligible    = false; // +358 byte truthy (employer/eligible)
    bool excluded    = false; // +433 byte truthy -> skip
    u8   profession  = 0;     // +2   staff role (6 or 7 == a guild master)
};

// ---------------------------------------------------------------------------
// The context: the engine hooks the deterministic cores route through. All are
// optional (a null hook degrades gracefully) so the counting / tie-break math can
// be exercised standalone. `ctx` is an opaque pointer passed back to each hook.
// ---------------------------------------------------------------------------
struct GuildAssignContext {
    // VIBE_Person_FindRecordById(id) -> the person view (valid==false if absent).
    GuildPersonRec (*findPerson)(i32 id, void* ctx) = nullptr;

    // VIBE_AiPlayer_EvaluateApproachDirection(voter, target, holder) -> {0,1,2}.
    // Returns the approach direction the relation deltas are keyed on.
    int (*approachDir)(i32 voterId, i32 targetId, i32 holderId, void* ctx) = nullptr;

    // VIBE_AiPlayer_PickBestTarget(voter, officeType) -> chosen target person id
    // (or -1 when the voter abstains).
    i32 (*pickTarget)(i32 voterId, u8 officeType, void* ctx) = nullptr;

    // VIBE_Office_IsNextRankInCategory(personRecValid, officeType) -> can this
    // person be promoted into `officeType`'s category (a successor candidate gate).
    bool (*isNextRank)(i32 personId, u8 officeType, void* ctx) = nullptr;

    // VIBE_Command_QueueRequestCoord27(a, b, delta) — a relation-change command.
    void (*relation)(i32 a, i32 b, i32 delta, void* ctx) = nullptr;

    // VIBE_Office_AddTableEntry(key, personId, state) — install / clear a seat.
    void (*install)(u8 key, i32 personId, u8 state, void* ctx) = nullptr;

    // The live "office object" table (word_12CE910 cluster) + its length.
    const GuildOfficeObject* objects = nullptr;
    int objectCount = 0;

    void* ctx = nullptr;
};

// ===========================================================================
// 1. ComputeGuildAssignment — gilde.exe 0x47ff5c.
// ===========================================================================
// The relation-delta tables (recovered from the v29/v30 and the else block):
//   winning side : approach 0 -> -20, 1 -> +20, 2 -> +4.
//   losing side  : approach 0 emits an extra -10 first; then 1 -> +10 else -4.
// These mirror guild_election.h's GuildAssignDelta* helpers; reused here.

// One member-assignment / successor pass over a holder array `holders[0..count-1]`.
// Phase A (members): for every state-2 entry, poll each voter's approach direction
//   toward the entry's primary holder, tallying approach buckets; if the "support"
//   bucket (v60) wins (>= v59), install the primary holder into the seat and emit a
//   "win" relation per voter; else emit "lose" relations and clear the seat (state
//   3). Phase B (successors): for every state-3 entry with a positive successor
//   count, collect up to 4 candidate office-objects matching the entry type, gather
//   each voter's PickBestTarget vote, tally per candidate, and install the winner
//   (random tie-break via VIBE_Math_RandomModulo). Returns the count of seats that
//   reached an install/clear decision (the original's last `result`, here a tally).
struct GuildAssignmentResult {
    int memberSeats    = 0; // state-2 entries seen
    int successorSeats = 0; // state-3 entries seen
    int installs       = 0; // AddTableEntry installs fired (member + successor)
    int clears         = 0; // member seats cleared (the "lose" branch)
    int relations      = 0; // relation commands emitted
};
GuildAssignmentResult ComputeGuildAssignment(OfficeHolder* holders, int count,
                                             GuildAssignContext& gx);

// ===========================================================================
// 2. PersonHasOfficeObject — gilde.exe 0x480abc.
// ===========================================================================
// For a state-2 holder entry `entry`, returns true iff: the entry is state 2, the
// secondary (+20) person exists & has a building & is NOT excluded, the primary (+4)
// person exists & has a building, AND one of the CollectByCategory-collected holder
// entries has PrimaryId (+4) == the entry's secondary id. The original collects the
// category buffer internally (VIBE_Office_CollectByCategory) and scans THAT buffer's
// +4 fields — NOT the office-object table; `collected`/`collectedCount` model that
// buffer. (gilde.exe 0x480abc: `v12[v10/4+1] == *(v9+20)`.)
bool PersonHasOfficeObject(const OfficeHolder& entry,
                           const OfficeHolder* collected, int collectedCount,
                           GuildAssignContext& gx);

// ===========================================================================
// 3. CheckGuildMastersPresent — gilde.exe 0x48091c.
// ===========================================================================
// Scans a holder array for category `category`. Sets a "member present" flag when a
// state-2 entry is a valid, non-excluded member whose object id reappears among the
// seats; sets a "master present" flag when a state-3 entry's office object resolves
// to a staff role 6|7 that is not busy, OR a swept member's person has profession
// 6|7 and is not excluded. Returns true iff a master is present (the &2 bit).
bool CheckGuildMastersPresent(const OfficeHolder* holders, int count,
                              u8 category, GuildAssignContext& gx);

// ===========================================================================
// 4. AssignGuildMembers — gilde.exe 0x480634.
// ===========================================================================
// The top-level orchestrator over the SIX guild categories (the recovered
// dword_47DE58 category-key table). For each category it gathers the holder
// entries, classifies them (member present / master present), and — when a member
// is present and no master yet exists — runs ComputeGuildAssignment for that
// category. Returns 1 if ANY category already has a master (the v23 latch), else 0.
//
// `categoryHolders(catKey, out, gx)` supplies the holder entries for a category
// (models VIBE_Office_CollectByCategory). It writes up to `maxOut` entries into
// `out` and returns the count.
struct GuildCategoryRule { u8 categoryKey; u8 rank; };
constexpr int kGuildCategoryCount = 6;
// The recovered 6-category table (dword_47DE58): category keys + the rank byte each
// maps to (the >>24 of the +21 dword in the original's v20 record).
const GuildCategoryRule* GuildCategoryTable();

using GuildCollectHolders = int (*)(u8 categoryKey, OfficeHolder* out, int maxOut,
                                    void* ctx);
int AssignGuildMembers(GuildCollectHolders collect, GuildAssignContext& gx);

// ===========================================================================
// 5. HasOccupiedOffice — gilde.exe 0x480cb4.
// ===========================================================================
// Walks the guild-master seats (the office-def category byte == 7 entries) from the
// tail of the holder table; returns true as soon as a seat whose city (+4) != -1
// resolves to a live person. The original scans byte_B59850 (office types) +
// dword_B5984C (city ids); modeled over a holder array with a per-entry
// "is guild-master category" predicate supplied via `isGuildMasterCat`.
struct GuildSeatView {
    u8  officeType = 0;  // byte_B59850[i] (the office type, mapped to a category)
    i32 city       = -1; // dword_B5984C[i] (-1 == vacant)
};
// `category(officeType)` models HIBYTE(dword_62EC8E[3*type]) — the office-def
// category byte (7 == a guild-master seat). `personLive(city)` models a live
// VIBE_Person_FindRecordById on the seat's city/holder id.
bool HasOccupiedOffice(const GuildSeatView* seats, int count,
                       u8 (*categoryOf)(u8 officeType, void* ctx),
                       bool (*personLive)(i32 city, void* ctx), void* ctx);

// ===========================================================================
// 6. AssignSlotData — gilde.exe 0x56ea50.
// ===========================================================================
// Writes a placement-slot record into a 64-entry table `slots` (24-byte stride; the
// AmtSlot layout in amt_slot_table.h). It first looks for an OCCUPIED slot already
// matching (x@+8, y@+9); failing that — and when `forceNew` is 0 — it claims the
// first FREE slot (marker +13 == 0xFF) and stamps a fresh key from a running
// counter. The chosen slot's type word (+10), coords (+8,+9), marker (+13) and
// book-cat byte (+12) are then written. Returns the slot index, or -1 when the
// table is full / `forceNew` and no match.
struct AssignSlotInputs {
    u8  x;            // a2  grid X (matched at +8)
    i16 typeWord;     // a3  the type word written at +10 (0 == clear)
    u8  y;            // a4  grid Y (matched at +9)
    bool forceNew;    // a5  (the original's last arg): when set, load model instead
    bool noMatchNew;  // v19 (the +1 spilled dword): when 0, allow claiming a free
                      //       slot if no (x,y) match is found.
    u8  bookCat;      // the office-type record's +66 byte (0 if no type record).
    bool hasTypeRec;  // v18 != null (a type record was resolved for typeWord).
};
// The running placement-key counter (dword_63D734). Reset per test as needed.
extern i32 g_guildSlotKeyCounter;
// Returns the chosen slot index, or -1.
int AssignSlotData(struct AmtSlot* slots, const AssignSlotInputs& in);

} // namespace guild::world
