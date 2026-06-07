#pragma once
// Election rules — the guild-master election candidate SCORING core of
// VIBE_Amt_ElectGuildMaster (gilde.exe 0x481228). The full function scans all
// persons for eligible guild members, requires a quorum, scores the candidates by
// total wealth, and installs the wealthiest (if different from the incumbent) via
// the office table + notify messages. The person-scan / notify / office-table
// plumbing is sim/command-owned; recovered here byte-for-byte are the
// deterministic decision rules:
//
//   * candidate eligibility (office-type byte in [13..18], employed, not flagged),
//   * the quorum gate (>= 3 guild members present AND >= 1 candidate),
//   * the winner pick (strictly-greater total wealth; first-found wins ties),
//   * the install gate (winner exists and differs from the incumbent).
//
// Mutations (the office-table install + notify) route through a settable hook.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Candidate eligibility.
// ===========================================================================
// Per ElectGuildMaster, a person is a guild MEMBER (counts toward quorum) when
// employed (employer word @+39 != 0xFFFF) and not flagged (record+90 & 1 == 0).
// Such a member is a CANDIDATE when its employer's office-type byte (+356 of the
// employer record, word_12CE910[268*employer]) is in [13..18].
constexpr u8 kElectionOfficeTypeMin = 13;  // v6 >= 13
constexpr u8 kElectionOfficeTypeMax = 18;  // v6 <= 18
constexpr int kElectionQuorum       = 3;   // v2 >= 3 guild members
constexpr int kElectionMaxCandidates = 16; // v16[16] candidate slots

// A candidate's scoring inputs (filled by the caller from live person records).
struct ElectionCandidate {
    i32 personId;     // candidate person id (for the install + dedup)
    u8  officeType;   // employer office-type byte (+356)
    u16 employer;     // employer id (+39); 0xFFFF == none
    bool flagged;     // record+90 & 1 (excluded)
    i32 totalWealth;  // VIBE_Person_ComputeTotalWealth score
};

// gilde.exe 0x481228 — eligibility predicate.
//   employed (employer != 0xFFFF), not flagged, officeType in [13..18].
bool ElectionIsCandidate(const ElectionCandidate& c);

// Member predicate (quorum count): employed and not flagged (office-type need not
// be in range). The original increments v2 for every employed non-flagged person.
bool ElectionIsMember(const ElectionCandidate& c);

// ===========================================================================
// Winner selection.
// ===========================================================================
struct ElectionOutcome {
    int  winnerIndex   = -1;  // index of the winner among collected candidates
    i32  winnerId      = -1;  // winning person id (v9), or -1
    i32  winnerWealth  = 0;   // the winning total wealth
    int  memberCount   = 0;   // counted guild members (quorum check)
    int  candidateCount = 0;  // eligible candidates collected
    bool quorumMet     = false; // memberCount >= 3 && candidateCount >= 1
    bool install       = false; // quorumMet && winner exists && != incumbent
};

// gilde.exe 0x481228 — run the election decision over a candidate pool.
// `pool`/`poolCount` are the people considered (the QueryBegin/IterNext walk);
// only those passing ElectionIsCandidate are collected (capped at 16, the v16
// table). `incumbentId` is the current office holder (v17[1]); the winner is
// installed only when it exists and differs from the incumbent.
//
// Winner rule (the v9/v12 loop): scan collected candidates; the winner is the one
// whose totalWealth is STRICTLY greater than the running best (so the first of
// equal-wealth candidates wins). `memberCount` is every employed/non-flagged
// person in the pool (the v2 quorum counter).
ElectionOutcome ElectionRunGuildMaster(const ElectionCandidate* pool,
                                       int poolCount, i32 incumbentId);

// ===========================================================================
// Mutation (command) hook — mock. The original installs the winner via
// VIBE_Office_AddTableEntry(officeSlot, winnerObj, 1, 0, 255) and notifies both
// the old and new holder. We surface the install as a settable hook.
// ===========================================================================
using ElectionInstallHook = void (*)(int officeSlot, i32 winnerId, i32 incumbentId,
                                     void* ctx);
void ElectionSetInstallHook(ElectionInstallHook hook, void* ctx);

// Commits the election result (no-op when !outcome.install). `officeSlot` is the
// office-table key (v17[0]). Returns true if an install was committed.
bool ElectionCommit(const ElectionOutcome& outcome, int officeSlot,
                    i32 incumbentId);

} // namespace guild::world
