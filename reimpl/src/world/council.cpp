#include "world/council.h"

// Faithful 1:1 port of the VOTING rules core of VIBE_Office_RunCouncilSession
// (gilde.exe 0x49dd8c). The cutscene/.esc/GUI shell is deferred; only the
// removal-vote tally + relation deltas and the election winner-pick are here.

namespace guild::world {

namespace {
CouncilRelationHook g_relHook  = nullptr;
CouncilTransferHook g_xferHook = nullptr;
void*               g_ctx      = nullptr;
} // namespace

// gilde.exe 0x49dd8c — removal vote tally.
//   The vote counters live as v205 (yes), v206 (no), v207[0] (abstain), bumped
//   by  ++*(&v205 + dword_11AB094[i]).  Outcome: KEPT iff (yes <= no).
CouncilVoteTally CouncilTallyRemoval(const CouncilVote* votes, int count) {
    CouncilVoteTally t;
    if (votes) {
        for (int i = 0; i < count; ++i) {
            switch (votes[i]) {
                case CouncilVote::kRemove:  ++t.yes;     break;
                case CouncilVote::kKeep:    ++t.no;      break;
                case CouncilVote::kAbstain: ++t.abstain; break;
            }
        }
    }
    // Original: if ( v205 <= v206 ) keep; else remove.
    t.removed = t.yes > t.no;
    return t;
}

// gilde.exe 0x49dd8c — per-vote relation delta (QueueRequestCoord27 amount).
int CouncilRelationDelta(CouncilVote vote, bool removed) {
    if (!removed) {
        // KEPT branch (v205 <= v206): vote0->-40, vote1->+10, vote2->+5.
        switch (vote) {
            case CouncilVote::kRemove:  return -40;
            case CouncilVote::kKeep:    return 10;
            case CouncilVote::kAbstain: return 5;
        }
    } else {
        // REMOVED branch (else): vote0->-40, vote1->+10, vote2->-20.
        switch (vote) {
            case CouncilVote::kRemove:  return -40;
            case CouncilVote::kKeep:    return 10;
            case CouncilVote::kAbstain: return -20;
        }
    }
    return 0;
}

// gilde.exe 0x49dd8c — election winner pick (v167/v169/v233 scan + tiebreak).
ElectionResult CouncilElectWinner(const int* ballots, int ballotCount,
                                  int candidateCount,
                                  ElectionRng rng, void* rngCtx) {
    ElectionResult r;
    if (candidateCount <= 0)
        return r;

    // Tally ballots into per-candidate counters (v204[], init 0). Capped to a
    // small fixed pool exactly like the original's 4-wide v204 (we allow up to
    // 16 to stay general; the original session uses 4 candidate slots max).
    int counts[16] = {0};
    int n = candidateCount > 16 ? 16 : candidateCount;
    if (ballots) {
        for (int i = 0; i < ballotCount; ++i) {
            int c = ballots[i];
            if (c >= 0 && c < n)      // dword_11AB094 != -1 skip
                ++counts[c];
        }
    }

    // Winner scan: v167 = max count, v169 = its index, v233 = tie flag.
    int maxCount = -1, maxIdx = -1;
    bool tie = false;
    for (int i = 0; i < n; ++i) {
        int c = counts[i];
        if (maxCount < c) {
            maxCount = c;
            maxIdx   = i;
            tie      = false;          // v233 = 0
        } else if (maxCount == c) {
            tie = true;                // v233 = 1
        }
    }

    // Tiebreak: if a tie (v233) or no leader chosen, start at RandInt(N) and walk
    // forward (mod N) to the first candidate whose count equals the max.
    if (tie && maxIdx != -1) {
        int idx = rng ? static_cast<int>(rng(static_cast<u32>(n), rngCtx)) : 0;
        int guard = n;
        while (guard > 0 && counts[idx] != maxCount) {
            --guard;
            idx = (idx + 1) % n;
        }
        if (guard > 0)
            maxIdx = idx;
    }

    r.winner      = maxIdx;
    r.winnerVotes = maxCount < 0 ? 0 : maxCount;
    r.tie         = tie;
    return r;
}

// --- command hooks (mock) -------------------------------------------------
void CouncilSetHooks(CouncilRelationHook relHook, CouncilTransferHook xferHook,
                     void* ctx) {
    g_relHook  = relHook;
    g_xferHook = xferHook;
    g_ctx      = ctx;
}

// gilde.exe 0x49dd8c — apply removal-vote relation deltas (one per voter whose
// object differs from the holder, mirroring  if ( dword_11AB010[i] != holderObj )).
void CouncilApplyRemovalRelations(const CouncilVote* votes,
                                  const i32* voterObjs, int count,
                                  i32 holderObj, bool removed) {
    if (!g_relHook || !votes || !voterObjs)
        return;
    for (int i = 0; i < count; ++i) {
        if (voterObjs[i] == holderObj)
            continue;
        g_relHook(voterObjs[i], holderObj,
                  CouncilRelationDelta(votes[i], removed), g_ctx);
    }
}

} // namespace guild::world
