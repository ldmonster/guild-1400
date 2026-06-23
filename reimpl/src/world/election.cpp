#include "world/election.h"

// Faithful 1:1 port of the candidate-scoring decision core of
// VIBE_Amt_ElectGuildMaster (gilde.exe 0x481228). The person scan / notify /
// office-table commit are sim/command-owned and routed through a hook.

namespace guild::world {

namespace {
ElectionInstallHook g_installHook = nullptr;
void*               g_installCtx  = nullptr;
} // namespace

// gilde.exe 0x481228 — quorum member predicate (the v2 counter):
//   *(_WORD*)(rec+39) != 0xFFFF && (rec[90] & 1) == 0
bool ElectionIsMember(const ElectionCandidate& c) {
    return c.employer != 0xFFFFu && !c.flagged;
}

// gilde.exe 0x481228 — candidate predicate: a member whose employer office-type
// byte (+356) is in [13..18].
bool ElectionIsCandidate(const ElectionCandidate& c) {
    if (!ElectionIsMember(c))
        return false;
    return c.officeType >= kElectionOfficeTypeMin
        && c.officeType <= kElectionOfficeTypeMax;
}

// gilde.exe 0x481228 — election decision.
ElectionOutcome ElectionRunGuildMaster(const ElectionCandidate* pool,
                                       int poolCount, i32 incumbentId,
                                       i32 incumbentWealth, bool incumbentValid) {
    ElectionOutcome out;
    if (!pool || poolCount <= 0)
        return out;

    // Collect candidates (deduping by person, capped at 16 like v16[16]) and
    // count guild members for the quorum.
    i32 candidateIds[kElectionMaxCandidates];
    i32 candidateWealth[kElectionMaxCandidates];
    int collected = 0;
    int members = 0;
    for (int i = 0; i < poolCount && collected < kElectionMaxCandidates; ++i) {
        const ElectionCandidate& c = pool[i];
        if (!ElectionIsMember(c))
            continue;
        ++members;                       // ++v2 for every employed non-flagged
        if (c.officeType < kElectionOfficeTypeMin
            || c.officeType > kElectionOfficeTypeMax)
            continue;
        // dedup: skip if this candidate id is already collected (the v16 scan).
        bool dup = false;
        for (int k = 0; k < collected; ++k)
            if (candidateIds[k] == c.personId) { dup = true; break; }
        if (dup)
            continue;
        candidateIds[collected]    = c.personId;
        candidateWealth[collected] = c.totalWealth;
        ++collected;
    }
    out.memberCount    = members;
    out.candidateCount = collected;

    // Quorum: v2 >= 3 members AND v1 (collected) >= 1.
    out.quorumMet = members >= kElectionQuorum && collected >= 1;
    if (!out.quorumMet)
        return out;

    // Winner pick (the v10/v14 loop @0x4812ff). The running-best wealth (ecx/v14)
    // is SEEDED with the INCUMBENT's ComputeTotalWealth (or 0 when there is no
    // incumbent record — the loc_4813AB `xor ecx,ecx` path), and the winner pointer
    // (v10/esi) starts NULL. The loop runs over ALL candidates from index 0 and
    // replaces the winner only when a candidate's wealth is STRICTLY greater than
    // the running best. A candidate must therefore out-earn the incumbent to win;
    // if none does, there is no winner and no install.
    i32 bestWealth = incumbentValid ? incumbentWealth : 0;
    int bestIdx    = -1;  // v10 starts NULL
    for (int k = 0; k < collected; ++k) {
        if (candidateWealth[k] > bestWealth) {
            bestWealth = candidateWealth[k];
            bestIdx    = k;
        }
    }

    out.winnerIndex  = bestIdx;
    if (bestIdx >= 0) {
        out.winnerId     = candidateIds[bestIdx];
        out.winnerWealth = bestWealth;
        // Install gate: winner exists (v10 != 0) and differs from the incumbent
        // (v10 != RecordById, i.e. winnerId != incumbentId).
        out.install = candidateIds[bestIdx] != incumbentId;
    }
    return out;
}

// --- command hook (mock) --------------------------------------------------
void ElectionSetInstallHook(ElectionInstallHook hook, void* ctx) {
    g_installHook = hook;
    g_installCtx  = ctx;
}

bool ElectionCommit(const ElectionOutcome& outcome, int officeSlot,
                    i32 incumbentId) {
    if (!outcome.install || outcome.winnerId < 0)
        return false;
    // gilde.exe: VIBE_Office_AddTableEntry(v17[0], v9, 1, 0, 255) then notify.
    if (g_installHook)
        g_installHook(officeSlot, outcome.winnerId, incumbentId, g_installCtx);
    return true;
}

} // namespace guild::world
