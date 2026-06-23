#include "sim/npctarget.h"

#include "util/math_random.h"

namespace guild::sim {

// ===========================================================================
// Recovered float constants (0x61A680 block).
// ===========================================================================
static constexpr float kFavRejectSlope = 8.0f;   // flt_61A680
static constexpr float kFavRejectBase  = 52.0f;  // flt_61A684
static constexpr float kSeqAThreshold  = 66.0f;  // flt_61A688  (SeqA accepts "<")
static constexpr float kSeqBThreshold  = 33.0f;  // flt_61A68C  (SeqB accepts ">")

// ===========================================================================
// Leaf-hook plumbing.
// ===========================================================================
static const NpcTargetHooks kInertTargetHooks{};
static const NpcTargetHooks* g_thooks = &kInertTargetHooks;

void SetNpcTargetHooks(const NpcTargetHooks* hooks) {
    g_thooks = hooks ? hooks : &kInertTargetHooks;
}
const NpcTargetHooks& GetNpcTargetHooks() { return *g_thooks; }

// Small guarded-call helpers so the inert default (all-null fn pointers) behaves
// as "no data" without crashing.
namespace {
u8  KindOf(PersonHandle p)        { auto f = g_thooks->personKind;  return f ? f(p) : 0; }
i32 IdOf(PersonHandle p)          { auto f = g_thooks->personId;    return f ? f(p) : -1; }
u8  CatAOf(PersonHandle p)        { auto f = g_thooks->officeCatA;  return f ? f(p) : 0; }
u8  CatBOf(PersonHandle p)        { auto f = g_thooks->officeCatB;  return f ? f(p) : 0; }
u8  CatCOf(PersonHandle p)        { auto f = g_thooks->officeCatC;  return f ? f(p) : 0; }
double Fav(u16 a, u16 b) {
    auto f = g_thooks->favorability;
    return f ? f(a, b, 1) : 0.0;
}
} // namespace

// ===========================================================================
// gilde.exe 0x474dd4 — VIBE_NpcTarget_FindNearestEnemy.
// ---------------------------------------------------------------------------
// The original walks the raw office-holder ring (v6 stepping by 268 words ==
// one Person record) when the NPC holds no office of category A; that ring walk
// is the host's Office_GetHolderEntryByCity loop. We model the candidate set
// through the hooks: the office-holder path is collected via collectSuccessors
// over category C (+360), since the holder-ring favorability minimisation and the
// successor-candidate minimisation reduce to the same "lowest-favorability wins,
// then the count*8+52 reject curve" once the candidate set is provided. The
// no-office (cat A != 0) early ring is preserved as a distinct path.
// ===========================================================================
PersonHandle NpcTarget_FindNearestEnemy(PersonHandle person) {
    PersonHandle best = nullptr;
    double bestScore = 0.0;
    u16 selfKind = static_cast<u16>(KindOf(person));

    if (CatAOf(person) == 0) {
        // Office-holder ring: scan succession/holder candidates for the NPC's
        // office, keep the one with the lowest favorability (an adversary).
        u8 catC = CatCOf(person);
        PersonHandle cand[5] = {};
        int n = g_thooks->collectSuccessors
                    ? g_thooks->collectSuccessors(catC, 5, cand) : 0;
        // Guard: never read past the 5-slot buffer we handed the collector. The
        // original collector respects the cap (so valid runs are unchanged); this
        // only prevents OOB if a hook reports a count larger than the buffer.
        if (n < 0) n = 0;
        if (n > 5) n = 5;
        for (int i = 0; i < n; ++i) {
            PersonHandle c = cand[i];
            if (c == person)
                continue;
            double s = Fav(selfKind, static_cast<u16>(KindOf(c)));
            if (!best) {
                best = c;
                bestScore = s;
            } else if (s < bestScore) {
                best = c;
                bestScore = s;
            }
        }
        // Reject the chosen enemy if it is "too friendly": count*8 + 52 < score.
        if (best && (static_cast<double>(n) * kFavRejectSlope
                     + kFavRejectBase < bestScore)) {
            best = nullptr;
        }
        if (best)
            return best;
    }

    // Fallback: nearest *visible* enemy within radius [0, 25].
    i32 idx = -1;
    int hit = g_thooks->findNearestVisible
                  ? g_thooks->findNearestVisible(person, 0.0, 25.0, &idx) : 0;
    if (!hit)
        return best;
    return g_thooks->personByIndex ? g_thooks->personByIndex(idx) : nullptr;
}

// ===========================================================================
// Direction-pick shared core (SeqA / SeqB differ only in the accept rule).
// ---------------------------------------------------------------------------
// dir[0..5] is the 1..6 candidate-direction byte array (the `&v23[3]` block).
// The office "tier" derived from the NPC's selected category permutes it:
//   tier 1 -> dir[i] = (i+4)%6 + 1   (rotate by 4)
//   tier 2 -> dir[i] = 6 - i         (reverse)
// Then 6 rounds shuffle the two halves [0..2] and [3..5] independently via
// RandomModulo(3) swaps (two swaps per round, in the exact original order).
// ===========================================================================
namespace {
u8 PickCategory(PersonHandle person, u8 seed) {
    // Category selection precedence: +360, then +358, then +359.
    if (CatCOf(person))
        return CatCOf(person);
    if (CatAOf(person))
        return CatAOf(person);
    if (CatBOf(person))
        return CatBOf(person);
    return seed;
}

bool BuildAndShuffleDirs(PersonHandle person, u8 seed, u8 dir[6]) {
    // Initial 1..6.
    for (int i = 0; i < 6; ++i)
        dir[i] = static_cast<u8>(i + 1);

    u8 cat = PickCategory(person, seed);
    if (cat) {
        bool ok = false;
        u8 rank = g_thooks->officeRank ? g_thooks->officeRank(cat, &ok) : 0;
        if (!ok)
            return false;             // no such office -> caller returns 0
        int tier = (static_cast<int>(rank) - 1) / 3;
        if (tier == 1) {
            // rotate by 4: dir[i] = (4+i)%6 + 1
            int v = 4;
            for (int i = 0; i < 6; ++i)
                dir[i] = static_cast<u8>(v++ % 6 + 1);
        }
        if (tier == 2) {
            // reverse: dir[i] = 6 - i
            for (int i = 0; i < 6; ++i)
                dir[i] = static_cast<u8>(6 - i);
        }
    }

    // 6 rounds of paired RandomModulo(3) swaps over the two halves.
    for (int i = 0; i < 6; ++i) {
        u16 a = static_cast<u16>(util::RandomModulo(3));   // first half
        u16 b = static_cast<u16>(util::RandomModulo(3));
        u8 t = dir[a];
        dir[a] = dir[b];
        dir[b] = t;
        u16 c = static_cast<u16>(util::RandomModulo(3));   // second half (+3)
        u16 d = static_cast<u16>(util::RandomModulo(3));
        u8 t2 = dir[c + 3];
        dir[c + 3] = dir[d + 3];
        dir[d + 3] = t2;
    }
    return true;
}
} // namespace

// gilde.exe 0x474fe8 — VIBE_NpcTarget_PickDirectionSeqA. Accept rule: the average
// favorability of the candidates collected for direction `dir[k]` is < 66.0.
u8 NpcTarget_PickDirectionSeqA(PersonHandle person, u8 seed) {
    u8 dir[6];
    if (!BuildAndShuffleDirs(person, seed, dir))
        return 0;
    u16 selfKind = static_cast<u16>(KindOf(person));

    for (int k = 0; k < 5; ++k) {
        i32 ids[6] = {};
        int n = g_thooks->collectByCategory
                    ? g_thooks->collectByCategory(dir[k], 6, ids) : 0;
        // Guard: clamp to the 6-slot buffer we provided (original respects the cap).
        if (n < 0) n = 0;
        if (n > 6) n = 6;
        if (n >= 3) {
            double sum = 0.0;
            for (int i = 0; i < n; ++i) {
                PersonHandle p = g_thooks->findPersonById
                                     ? g_thooks->findPersonById(ids[i]) : nullptr;
                sum += Fav(selfKind, static_cast<u16>(KindOf(p)));
            }
            if (sum / static_cast<double>(n) < kSeqAThreshold)
                return dir[k];
        }
    }
    return 0;
}

// gilde.exe 0x475274 — VIBE_NpcTarget_PickDirectionSeqB. Accept rule: the average
// *pairwise* favorability (both directions, over distinct valid candidate pairs)
// is > 33.0. Self (record id) and -1 entries are skipped.
u8 NpcTarget_PickDirectionSeqB(PersonHandle person, u8 seed) {
    u8 dir[6];
    if (!BuildAndShuffleDirs(person, seed, dir))
        return 0;
    i32 selfId = IdOf(person);

    for (int k = 0; k < 5; ++k) {
        i32 ids[6] = {};
        int n = g_thooks->collectByCategory
                    ? g_thooks->collectByCategory(dir[k], 6, ids) : 0;
        // Guard: clamp to the 6-slot buffer we provided (original respects the cap).
        if (n < 0) n = 0;
        if (n > 6) n = 6;
        if (n >= 3) {
            double sum = 0.0;
            int pairs = 0;
            for (int i = 0; i < n - 1; ++i) {
                if (ids[i] == selfId || ids[i] == -1)
                    continue;
                PersonHandle pi = g_thooks->findPersonById
                                      ? g_thooks->findPersonById(ids[i]) : nullptr;
                if (!pi)
                    continue;
                for (int j = i + 1; j < n; ++j) {
                    if (ids[j] == selfId || ids[j] == -1)
                        continue;
                    PersonHandle pj = g_thooks->findPersonById
                                          ? g_thooks->findPersonById(ids[j]) : nullptr;
                    if (!pj)
                        continue;
                    u16 ki = static_cast<u16>(KindOf(pi));
                    u16 kj = static_cast<u16>(KindOf(pj));
                    sum += Fav(ki, kj);
                    sum += Fav(kj, ki);
                    pairs += 2;
                }
            }
            if (pairs) {
                if (sum / static_cast<double>(pairs) > kSeqBThreshold)
                    return dir[k];
            }
        }
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x475638 — VIBE_NpcTarget_EvalCombatOrMoveAction.
// ===========================================================================
int NpcTarget_EvalCombatOrMoveAction(PersonHandle person, bool busy, bool blocked,
                                     int guildRank, NpcActionDesc* out) {
    // if (blocked || busy || rank != 1) return 0;
    if (blocked || busy || guildRank != 1)
        return 0;

    // sub-method byte at +361 (30/31/32/33 = attack/move kinds).
    u8 v7 = g_thooks->subMethod ? g_thooks->subMethod(person) : 0;

    if (v7 == 30) {
        PersonHandle enemy = NpcTarget_FindNearestEnemy(person);
        if (!enemy)
            return 0;
        if (out) {
            out->verb = 7;
            out->mode = 1;
            out->target = IdOf(enemy);  // *(_DWORD*)(enemy+1) == person id
        }
        return 56;
    }
    if (v7 == 32) {
        u8 d = NpcTarget_PickDirectionSeqA(person, 0);
        if (!d)
            return 0;
        if (out) {
            out->verb = 22;
            out->mode = 4;
            out->target = d;
        }
        return 56;
    }
    if (v7 == 31 || v7 == 33) {
        u8 d = NpcTarget_PickDirectionSeqB(person, 0);
        if (!d)
            return 0;
        if (out) {
            out->verb = 22;
            out->mode = 4;
            out->target = d;
        }
        return 56;
    }
    return 0;
}

} // namespace guild::sim
