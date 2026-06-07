#pragma once
// Office (Amt) holder-mutation rules: assign-to-candidate, transfer-holdership,
// swap-holders, apply-for-candidacy, the slot-placement/add-table-entry path, and
// the release/clear-character-holdings rules. Faithful 1:1 port of the
// VIBE_Office_* mutation functions from gilde.exe.
//
// These operate on the shared office-holder table g_officeHolders (law_types.h /
// office.cpp; gilde.exe byte_B59848, 24 B x 37) and on a Person *view* (the
// originals read/write the live 589-byte sim Person record at offsets +358 held
// office-type, +359 held rank, +360 candidacy office-type, +361 secondary held
// office-type, +4 id, +404 money). Person mutation routes through an
// OfficePersonStore the caller supplies; the live-engine path commits the slot
// changes through VIBE_Command_RequestBuildOp* (the lockstep network queue),
// which is surfaced here as a settable command hook (mock).
//
// Translated functions:
//   VIBE_Office_AssignToCandidate        0x47e4e0  (full rule)
//   VIBE_Office_ApplyForCandidacy        0x47e1b8  (rule + command emit)
//   VIBE_Office_AddTableEntry            0x47e750  (slot-find + command emit)
//   VIBE_Office_AddEntryForCharacter     0x47e6e4
//   VIBE_Office_AddEntryIfValid          0x47e72c
//   VIBE_Office_TransferHoldership       0x47e870  (full holder mutation)
//   VIBE_Office_SwapHolders              0x47ec64  (full holder swap)
//   VIBE_Office_ReleaseCharacterHoldings 0x47ed68
//   VIBE_Office_ClearCharacterHoldings   0x47ee44
//   VIBE_Office_CheckPrerequisitesMet    0x47e7d8  (gate)
#include <cstddef>

#include "guild/common/types.h"
#include "world/law_types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Person view (the live sim Person fields the office mutators read/write).
// ---------------------------------------------------------------------------
// The originals dereference VIBE_Person_FindRecordById(id) and touch:
//   +0   (word) -1 == invalid record marker            -> `valid`
//   +4   (dword) person/owner id                       -> `ownerId`
//   +358 (byte) held office-type                       -> `office358`
//   +359 (byte) held rank                              -> `office359`
//   +360 (byte) candidacy office-type (0 == none)      -> `office360`
//   +361 (byte) secondary held office-type             -> `office361`
//   +404 (dword) money / standing                      -> `money`
struct OfficePersonRec {
    i32 ownerId   = -1;   // +4
    u8  office358 = 0;    // +358 held office type
    u8  office359 = 0;    // +359 held rank
    u8  office360 = 0;    // +360 candidacy office type
    u8  office361 = 0;    // +361 secondary held office type
    i32 money     = 0;    // +404
    bool valid    = false;// models a successful FindRecordById (and +0 != 0xFFFF)
};

// The store models VIBE_Person_FindRecordById: maps an id to a (mutable) record,
// or nullptr when the id is -1 / not found. Tests back it with a small table.
struct OfficePersonStore {
    virtual ~OfficePersonStore() = default;
    virtual OfficePersonRec* Find(i32 id) = 0;
};

// ---------------------------------------------------------------------------
// Command hook (mock) — the lockstep network queue the mutators commit through.
// ---------------------------------------------------------------------------
// The originals build a small packed command record and call
// VIBE_Command_RequestBuildOp68 (candidacy) / RequestBuildOp69 (add-entry). We
// record the emitted command(s) so tests can assert against them. opcode is the
// build-op number; the payload fields are the bytes/dwords the originals pack.
struct OfficeCommand {
    int opcode;       // 68 (candidacy) / 69 (add table entry)
    i32 personId;     // packed person/owner id (-1 if none)
    i32 secondaryId;  // second packed id (RequestBuildOp69 v14; -1 if none)
    u8  holderA;      // first holder char id (v19 / v10)
    u8  holderB;      // second holder char id (v20; 0xFF if none)
    u8  officeType;   // office type (v12 / a2)
    u8  state;        // entry state (v13 / a5)
};
using OfficeCommandHook = void (*)(const OfficeCommand& cmd, void* ctx);
void OfficeSetCommandHook(OfficeCommandHook hook, void* ctx);
// Resets the hook to the default (record into the internal log) and clears the log.
void OfficeCommandLogReset();
// Returns the commands emitted since the last reset.
const OfficeCommand* OfficeCommandLog(int* outCount);

// ---------------------------------------------------------------------------
// History notify hook (mock) — TransferHoldership / SwapHolders call
// VIBE_History_NotifyOfficeTransfer / _NotifyOfficeSwap on the master-visible
// path. Surfaced so tests can observe the notification.
// ---------------------------------------------------------------------------
struct OfficeNotify {
    enum Kind { Transfer, Swap } kind;
    i32 a, b, c; // person ids involved (c unused for Transfer)
};
using OfficeNotifyHook = void (*)(const OfficeNotify& n, void* ctx);
void OfficeSetNotifyHook(OfficeNotifyHook hook, void* ctx);
void OfficeNotifyLogReset();
const OfficeNotify* OfficeNotifyLog(int* outCount);

// ---------------------------------------------------------------------------
// Command/request structs (the packed input the originals receive in a1).
// ---------------------------------------------------------------------------

// VIBE_Office_AssignToCandidate input (a1): the decompiler reads
//   [a1+0] candidate person id (dword)   [a1+4] primary holder char-id key (byte)
//   [a1+5] secondary holder char-id key (byte; 0xFF == none)
//   [a1+6] office type to match (byte)
struct AssignRequest {
    i32 candidateId; // [+0]
    u8  keyA;        // [+4]
    u8  keyB;        // [+5] (0xFF == none)
    u8  officeType;  // [+6]
};

// gilde.exe 0x47e4e0 — VIBE_Office_AssignToCandidate.
// Marks the candidate person's +360 candidacy flag and bumps the matched slot's
// rank counter (and a second slot's, in the keyB path). Returns 1 on success, 0
// on any gate failure. Mutates g_officeHolders and the candidate record in `ps`.
int OfficeAssignToCandidate(const AssignRequest& req, OfficePersonStore& ps);

// gilde.exe 0x47e1b8 — VIBE_Office_ApplyForCandidacy  (a1=person rec ptr, dl=type).
// Scans for open slot(s) of `officeType`, and on success emits the candidacy
// command (build-op 68) packing the applicant id + 1..2 holder char ids. Returns
// the emit count (>0) on success, -1 on any gate failure. `applicant` is the
// person record (read +360 candidacy guard, +4 id).
int OfficeApplyForCandidacy(OfficePersonRec& applicant, u8 officeType);

// gilde.exe 0x47e750 — VIBE_Office_AddTableEntry  (al=holderKey, edx=primaryRec,
//   cl=officeType, ebx=secondaryRec, +stack state). Finds the first slot whose
// holder char-id (+0) == holderKey (37-entry / 888-byte scan); if found, packs
// and emits build-op 69. Returns the command result (0 here) or -1 if not found.
// `primary`/`secondary` may be null (the originals pass -1 ids for null).
int OfficeAddTableEntry(u8 holderKey, const OfficePersonRec* primary,
                        u8 officeType, const OfficePersonRec* secondary, u8 state);

// gilde.exe 0x47e6e4 — VIBE_Office_AddEntryForCharacter.
//   if GetHolderEntryByCity(cityId) && successorRec: AddTableEntry(holder, city, 2, succ, 1)
// Returns the AddTableEntry result or -1.
int OfficeAddEntryForCharacter(i32 cityId, const OfficePersonRec* successor,
                               OfficePersonStore& ps);

// gilde.exe 0x47e72c — VIBE_Office_AddEntryIfValid.
//   if rec: AddTableEntry(officeType, rec, 1, null, state)  else -1
int OfficeAddEntryIfValid(const OfficePersonRec* rec, u8 officeType, u8 state);

// VIBE_Office_TransferHoldership input (a1): a packed command
//   [a1+0] holder char-id key (byte)
//   [a1+1] new primary holder person id (dword)  -> becomes the slot's city/owner
//   [a1+5] new entry state (byte)
//   [a1+7] incoming/seated person id (dword)      -> v17: office fields updated,
//                                                    stored as the slot secondary
struct TransferRequest {
    u8  holderKey;  // [+0]
    i32 primaryId;  // [+1] new slot owner id (-1 == none -> slot owner -1)
    u8  state;      // [+5] state to install
    i32 seatedId;   // [+7] incoming person whose office fields update (-1 == none)
};

// gilde.exe 0x47e870 — VIBE_Office_TransferHoldership.
// Locates the slot by holderKey (37-entry scan), clears the prior occupant's
// office fields (when it differs from the new primary), installs the new primary
// as the slot owner (city) with the seated person id as secondary + state + rank,
// and updates the seated person's office-type fields (+358/+359/+361/+360).
// Returns 1 on success, 0 on a gate failure. Notifies on the master-visible path.
int OfficeTransferHoldership(const TransferRequest& req, OfficePersonStore& ps);

// VIBE_Office_SwapHolders input (a1):
//   [a1+0] person A id (dword)   [a1+4] holder char-id key A (byte)
//   [a1+5] holder char-id key B (byte)   [a1+6] money cost (dword)
struct SwapRequest {
    i32 personAId; // [+0]
    u8  keyA;      // [+4]
    u8  keyB;      // [+5]
    i32 cost;      // [+6]
};

// gilde.exe 0x47ec64 — VIBE_Office_SwapHolders.
// Swaps the holders (the city/owner id at +4) of the two slots found by keyA/keyB,
// swaps the two seated persons' office-type fields, charges personA the cost, and
// notifies. Returns 1 on success, 0 on a gate failure.
int OfficeSwapHolders(const SwapRequest& req, OfficePersonStore& ps);

// gilde.exe 0x47ed68 — VIBE_Office_ReleaseCharacterHoldings  (eax=person rec).
// Releases every slot the person occupies (primary +4 match -> vacant; secondary
// +20 match -> state 1), decrements the candidacy slot's rank, and clears the
// person's +358/+360/+361 office fields. Returns a nonzero "changed" flag (bit0).
// `vacantStateBig` models the `WORD2(qword_13CE852) > 0xB` branch (state 3 vs 4).
int OfficeReleaseCharacterHoldings(OfficePersonRec& person, bool vacantStateBig);

// gilde.exe 0x47ee44 — VIBE_Office_ClearCharacterHoldings  (eax=person, dl=full).
// Like Release but gated: the primary-slot vacate + the +358/+361 clear only
// happen when `full` is true; the candidacy-rank decrement and +360 clear always
// run. Returns 222*4 (the loop terminator the original returns).
int OfficeClearCharacterHoldings(OfficePersonRec& person, bool full,
                                 bool vacantStateBig);

// gilde.exe 0x47e7d8 — VIBE_Office_CheckPrerequisitesMet (a1=packed transfer req).
// Returns 1 when: officeType (+6) == 0xFF (always ok), OR the slot found by the
// holder key (+0) has state == 1 and secondary (+20) == -1 and both referenced
// persons (+7, +1) resolve. Returns 0 otherwise.
int OfficeCheckPrerequisitesMet(u8 holderKey, u8 officeType, i32 idA, i32 idB,
                                OfficePersonStore& ps);

} // namespace guild::world
