#include "world/straftat_sync.h"

#include "world/crime.h"

// Faithful 1:1 port of the crime network-sync / accusation-broadcast rules
// (gilde.exe 0x4c33f4 / 0x4c3728). The crime table's parallel globals
// (dword_11BC760 id, dword_11BC785 provenState, dword_11BC776 perp) map to
// g_crimeTable fields; the evidence pairs map to g_evidenceOwner/g_evidenceCrimeId.

namespace guild::world {

// ===========================================================================
// Sync hooks.
// ===========================================================================
namespace {
constexpr int kSyncLogCap = 64;
StraftatSyncCommand g_syncLog[kSyncLogCap];
int g_syncLogCount = 0;
void DefaultSyncHook(const StraftatSyncCommand& c, void*) {
    if (g_syncLogCount < kSyncLogCap) g_syncLog[g_syncLogCount++] = c;
}
StraftatSyncHook g_syncHook = &DefaultSyncHook;
void* g_syncCtx = nullptr;

bool DefaultHandlerExists(i32 /*provenState*/) { return false; }
StraftatHandlerExistsFn g_handlerFn = &DefaultHandlerExists;

constexpr int kAccLogCap = 64;
AccusationMessage g_accLog[kAccLogCap];
int g_accLogCount = 0;
void DefaultAccHook(const AccusationMessage& m, void*) {
    if (g_accLogCount < kAccLogCap) g_accLog[g_accLogCount++] = m;
}
AccusationHook g_accHook = &DefaultAccHook;
void* g_accCtx = nullptr;
} // namespace

void StraftatSetSyncHook(StraftatSyncHook hook, void* ctx) {
    g_syncHook = hook ? hook : &DefaultSyncHook;
    g_syncCtx = hook ? ctx : nullptr;
}
void StraftatSyncLogReset() {
    g_syncHook = &DefaultSyncHook; g_syncCtx = nullptr; g_syncLogCount = 0;
}
const StraftatSyncCommand* StraftatSyncLog(int* outCount) {
    if (outCount) *outCount = g_syncLogCount;
    return g_syncLog;
}
void StraftatSetHandlerExistsFn(StraftatHandlerExistsFn fn) {
    g_handlerFn = fn ? fn : &DefaultHandlerExists;
}

void StraftatSetAccusationHook(AccusationHook hook, void* ctx) {
    g_accHook = hook ? hook : &DefaultAccHook;
    g_accCtx = hook ? ctx : nullptr;
}
void StraftatAccusationLogReset() {
    g_accHook = &DefaultAccHook; g_accCtx = nullptr; g_accLogCount = 0;
}
const AccusationMessage* StraftatAccusationLog(int* outCount) {
    if (outCount) *outCount = g_accLogCount;
    return g_accLog;
}

// ===========================================================================
// gilde.exe 0x4c33f4 — VIBE_Straftat_SyncAllToNetwork.
// ===========================================================================
int StraftatSyncAllToNetwork() {
    int emitted = 0;
    for (int i = 0; i < kCrimeCount; ++i) {
        i32 state = g_crimeTable[i].provenState; // *(dword_11BC785 + 45*i)
        int flag;
        bool emit = false;
        if (state == 1) {
            flag = 0;
            emit = true;
        } else if (state > 1 && !g_handlerFn(state)) {
            flag = 1;
            emit = true;
        } else {
            flag = 0;
        }
        if (emit) {
            StraftatSyncCommand cmd{g_crimeTable[i].id, flag};
            g_syncHook(cmd, g_syncCtx);
            ++emitted;
        }
    }
    return emitted;
}

// ===========================================================================
// gilde.exe 0x4c3728 — VIBE_Straftat_BroadcastAccusation.
// ===========================================================================
int StraftatBroadcastAccusation(i32 crimeId, i32 ownerId, i32 mask,
                                AccusationRecipient* recipients,
                                int recipientCount, i32 /*perpetratorId*/perp,
                                bool perpetratorResolves) {
    // First pass: confirm at least one crime record matches `crimeId` and its
    // perpetrator resolves (the original requires v3 != 0 to proceed).
    bool found = false;
    for (int i = 0; i < kCrimeCount; ++i) {
        if (g_crimeTable[i].id == crimeId) {
            found = true; // the matching record exists
        }
    }
    if (!found || !perpetratorResolves)
        return 0;

    int delivered = 0;
    for (int j = 0; j < recipientCount; ++j) {
        AccusationRecipient& r = recipients[j];
        if ((r.kind == 6 || r.kind == 7) && ownerId != r.ownerId &&
            (mask & r.flagField) == 0) {
            // Require an evidence pair (owner==recipientOwnerId, crimeId==crimeId).
            // The original's inner while-loop counts the pair SLOTS scanned (v8)
            // before the match; v8 == matching pair index (e/2). If no pair is
            // found it `goto LABEL_8` (no delivery), so v8 is meaningful only on a
            // hit.
            bool haveEvidence = false;
            int v8 = 0;
            for (int e = 0; e < kEvidenceDwords; e += 2, ++v8) {
                if (g_evidenceOwner[e] == r.ownerId &&
                    g_evidenceCrimeId[e] == crimeId) {
                    haveEvidence = true;
                    break;
                }
            }
            if (haveEvidence) {
                AccusationMessage msg{r.ownerId, crimeId, perp};
                g_accHook(msg, g_accCtx);
                // 0x4c382b: dword_12CEAF4[134 * v8] &= v12 — mask the flagField of
                // the recipient at table index v8 (the pair-scan count), NOT j.
                // Faithful bug-for-bug; guarded to the supplied recipient view.
                if (v8 < recipientCount)
                    recipients[v8].flagField &= mask;
                ++delivered;
            }
        }
    }
    return delivered;
}

} // namespace guild::world
