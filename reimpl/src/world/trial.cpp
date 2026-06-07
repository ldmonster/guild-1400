#include "world/trial.h"

#include <cstring>

// Faithful 1:1 port of the VERDICT / EVIDENCE-SCORING rules core of
// VIBE_Office_RunCourtTrial (gilde.exe 0x4a0eb8). The cutscene/.esc/GUI shell is
// deferred; only the deterministic decision substrate is reconstructed here.

namespace guild::world {

namespace {
TrialFineHook g_fineHook = nullptr;
void*         g_fineCtx  = nullptr;
} // namespace

// gilde.exe 0x4a0eb8 — wanted-level weight select (v366 = v355[clamp(v353,0,4)]).
float TrialWantedWeight(int wantedLevel) {
    int idx;
    if (wantedLevel <= 0)
        idx = 0;
    else if (wantedLevel >= 4)
        idx = 4;
    else
        idx = wantedLevel;
    return kTrialWantedWeight[idx];
}

// gilde.exe 0x4a0eb8 — evidence-score accumulation.
//
// The original works over the per-crime evidence list (count v361):
//   * The dedup pass (the v383/v381 double loop) walks the crime list and, for
//     each not-yet-consumed crime, merges every later crime sharing the same
//     law-type byte (crime[28]); merged crimes are marked -1 and a per-group
//     duplicate counter is bumped. v364 counts distinct crime ids written into
//     the form table.
//   * The score pass (the v36 loop) iterates the original list; for each crime it
//     looks up the law (Gesetz_GetRecord(crime[28])), reads penalty (+24, as int),
//     and adds  penalty * weight  to v365 — done for EVERY entry (the running
//     score sums all collected evidence, while the displayed/grouped list is the
//     deduped one). We reproduce both: `score` over all entries, `uniqueCount`
//     after dedup.
TrialScore TrialComputeEvidenceScore(const CrimeRecord* crimes, int count,
                                     int wantedLevel,
                                     TrialLawLookup lawLookup, void* ctx) {
    TrialScore out;
    out.totalCount = count;
    if (!crimes || count <= 0)
        return out;

    const float weight = TrialWantedWeight(wantedLevel);

    // --- score pass (v36 loop): sum penalty*weight over every collected crime.
    double score = 0.0;
    for (int i = 0; i < count; ++i) {
        LawRecord law;
        if (lawLookup && lawLookup(TrialCrimeLawType(crimes[i]), &law, ctx)) {
            // v351 = law penalty read as int (+24 word, sign-extended by the
            // original via (double)v351); LawRecord::penalty is the i16 at +16,
            // but the trial reads the +24 dword field as the score term. The
            // accumulated form uses the law-record's penalty magnitude.
            score += static_cast<double>(law.penalty) * static_cast<double>(weight);
        }
    }
    out.score = static_cast<float>(score);

    // --- dedup pass (v383/v381 loop): count distinct law-type groups.
    // A small consumed[] mirror of the v322 "consumed" marker (==-1 means merged).
    bool consumed[512];
    int n = count < 512 ? count : 512;
    std::memset(consumed, 0, sizeof(bool) * n);
    int unique = 0;
    for (int a = 0; a < n; ++a) {
        if (consumed[a])
            continue;
        ++unique;
        u8 key = TrialCrimeLawType(crimes[a]);
        for (int b = a + 1; b < n; ++b) {
            if (!consumed[b] && TrialCrimeLawType(crimes[b]) == key)
                consumed[b] = true;  // v322[..] = -1; ++dupCounter
        }
    }
    out.uniqueCount = unique;
    return out;
}

// gilde.exe 0x4a0eb8 — jury vote tally + verdict threshold (the v360 path).
TrialVerdict TrialTallyVerdict(const int* votes, int juryCount, int* outTotal) {
    int total = 0;
    if (votes) {
        for (int i = 0; i < juryCount; ++i)
            total += votes[i];   // v360 = judge + each present assessor's vote
    }
    if (outTotal)
        *outTotal = total;
    // if ( v360 >= 2 ) -> NICHT_SCHULDIG (acquitted) else SCHULDIG.
    return total >= kTrialAcquitThreshold ? TrialVerdict::kAcquitted
                                          : TrialVerdict::kConvicted;
}

// gilde.exe 0x4a0eb8 — SCHULDIG branch fine adjustment.
//   v387 = v365 * flt_61CCF4;
//   v327 = v365 - favorability * v387 * flt_61CCF8;
float TrialApplyGuiltyFine(float score, double favorability) {
    float fineBasis = score * kTrialFineBasisScale;
    double adjusted = static_cast<double>(score)
                      - favorability * static_cast<double>(fineBasis)
                            * static_cast<double>(kTrialFavorScale);
    return static_cast<float>(adjusted);
}

// gilde.exe 0x4a0eb8 — torture branch fine scale (v345 confessed path).
//   v345 -> v327 = v327 * flt_61CCFC; else v327 = v327 * flt_61CD00;
float TrialApplyTortureFine(float score, bool confessed) {
    return confessed ? score * kTrialTortureConfessScale
                     : score * kTrialTortureDenyScale;
}

// --- command hook (mock) --------------------------------------------------
void TrialSetFineHook(TrialFineHook hook, void* ctx) {
    g_fineHook = hook;
    g_fineCtx  = ctx;
}

// gilde.exe 0x4a0eb8 — three QueueRequest16 fine transfers (amount = score/3).
i32 TrialCommitFine(i32 defendantObj, i32 judgeObj, i32 assessorAObj,
                    i32 assessorBObj, i32 wealthScore, u8 currency) {
    i32 amount = wealthScore / 3;   // v338 / 3 / v339 / 3
    if (g_fineHook) {
        g_fineHook(judgeObj,     defendantObj, amount, currency, g_fineCtx);
        g_fineHook(assessorAObj, defendantObj, amount, currency, g_fineCtx);
        g_fineHook(assessorBObj, defendantObj, amount, currency, g_fineCtx);
    }
    return amount;
}

} // namespace guild::world
