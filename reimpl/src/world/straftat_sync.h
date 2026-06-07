#pragma once
// Crime (Straftat) network sync + accusation broadcast. Faithful 1:1 port of the
// VIBE_Straftat_SyncAllToNetwork (0x4c33f4) and VIBE_Straftat_BroadcastAccusation
// (0x4c3728) rules core from gilde.exe.
//
// SyncAllToNetwork walks the crime table and, for each record whose proven-state
// (+37) is 1 or >1, enqueues a sync command (QueueRequestPair35) — batching in
// groups of 32 with a queue-drain between batches. BroadcastAccusation finds the
// perpetrator of a crime id and notifies every office holder (subjectKind 6/7)
// who isn't the crime owner, who passes the flag mask, and who has matching
// evidence on file. Both commit through the lockstep command/messaging layer,
// surfaced here as settable hooks (mock).
//
// The crime table is g_crimeTable (crime.h); the evidence pairs are g_evidence*
// (crime.h).
#include "guild/common/types.h"
#include "world/law_types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Sync command hook (mock) — QueueRequestPair35(crimeId, flag).
// ---------------------------------------------------------------------------
struct StraftatSyncCommand {
    i32 crimeId; // dword_11BC760[i]
    int flag;    // 0 (proven) / 1 (pending, no handler)
};
using StraftatSyncHook = void (*)(const StraftatSyncCommand& cmd, void* ctx);
void StraftatSetSyncHook(StraftatSyncHook hook, void* ctx);
void StraftatSyncLogReset();
const StraftatSyncCommand* StraftatSyncLog(int* outCount);

// Hook modeling VIBE_He_FindFirstHandlerByFilter(1,1,provenState): returns true
// when a handler already exists for a pending crime (so it is NOT re-synced).
// Default: no handler (always re-sync pending crimes).
using StraftatHandlerExistsFn = bool (*)(i32 provenState);
void StraftatSetHandlerExistsFn(StraftatHandlerExistsFn fn);

// gilde.exe 0x4c33f4 — VIBE_Straftat_SyncAllToNetwork.
// Scans the 512 crime records: provenState == 1 -> sync flag 0; provenState > 1
// (and no existing handler) -> sync flag 1. Records are emitted in order. Returns
// the number of sync commands emitted. (The 32-batch queue-drain + Sleep are I/O
// and are elided; the emit sequence is exact.)
int StraftatSyncAllToNetwork();

// ---------------------------------------------------------------------------
// Accusation broadcast.
// ---------------------------------------------------------------------------
// An office "recipient" (one of the holders the broadcast targets). The original
// walks dword_12CE9xx (134-dword stride): byte +2 == kind (6/7 == office holder),
// dword +5 == owner id (dword_12CE914), dword the flag-field (dword_12CEAF4).
struct AccusationRecipient {
    u8  kind;     // +2 (only 6/7 receive)
    i32 ownerId;  // +5 dword (the holder's account id)
    i32 flagField;// the per-recipient flag mask (dword_12CEAF4)
};

// A delivered accusation message (one per qualifying recipient with evidence).
struct AccusationMessage {
    i32 recipientOwnerId; // the holder notified
    i32 crimeId;          // the accused crime id
    i32 perpetratorId;    // resolved perpetrator person id
};
using AccusationHook = void (*)(const AccusationMessage& msg, void* ctx);
void StraftatSetAccusationHook(AccusationHook hook, void* ctx);
void StraftatAccusationLogReset();
const AccusationMessage* StraftatAccusationLog(int* outCount);

// gilde.exe 0x4c3728 — VIBE_Straftat_BroadcastAccusation  (edx:eax=crimeId+owner, ebx=mask).
// Finds the crime record by id, resolves its perpetrator; if it exists, scans the
// recipients and delivers the accusation to each holder (kind 6/7) that is not the
// crime owner (a1 high dword), passes (mask & flagField) == 0, and has an evidence
// pair (owner==recipientOwnerId, crimeId==a1). Returns the number delivered.
//   crimeId   = low dword of a1   ownerId = high dword of a1   mask = a2
int StraftatBroadcastAccusation(i32 crimeId, i32 ownerId, i32 mask,
                                const AccusationRecipient* recipients,
                                int recipientCount, i32 perpetratorId,
                                bool perpetratorResolves);

} // namespace guild::world
