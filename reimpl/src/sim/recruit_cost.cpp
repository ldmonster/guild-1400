#include "sim/recruit_cost.h"
#include "sim/person.h"
#include "sim/entity.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

// Faithful 1:1 port of the recruitment cost formula and the candidate
// eligibility matcher from gilde.exe. The cost formula's float constants are
// recovered byte-for-byte from the IDB (see below); the eligibility matcher is
// a direct translation of the bit-flag cascade, reading the candidate's Person
// record by the same byte offsets the binary uses (PersonField).

namespace guild::sim {

// ===========================================================================
// Recovered float/double constants (exact bytes from gilde.exe .rdata).
// ===========================================================================
constexpr float  kWealthRatioScale = 5.0f;   // flt_624A44 (0x40A00000)
constexpr float  kFavorBase        = 100.0f; // flt_624A48 (0x42C80000)
constexpr float  kFavorScale       = 0.1f;   // flt_624A4C (0x3DCCCCCD)
constexpr double kCashNumerator    = 24.0;   // dbl_624A54 (0x4038000000000000)
constexpr double kRepHighThresh    = 210.0;  // dbl_624A5C (0x406A400000000000)
constexpr double kRepMidThresh     = 168.0;  // dbl_624A64 (0x4065000000000000)

// gilde.exe dword_6498E4 — the "candidate currently in the recruit window"
// record pointer; a candidate whose record == this is excluded. Modeled as a
// settable pointer (tests leave it null).
static const Person* g_recruitFocus = nullptr;  // dword_6498E4
void RecruitSetFocusRecord(const Person* rec) { g_recruitFocus = rec; }

// ===========================================================================
// Leaf hooks.
// ===========================================================================
static TotalWealthFn  g_wealthFn  = nullptr;
static FavorabilityFn g_favorFn   = nullptr;
static CostDebugFn    g_debugFn    = nullptr;
void RecruitSetTotalWealthHook(TotalWealthFn fn)  { g_wealthFn = fn; }
void RecruitSetFavorabilityHook(FavorabilityFn fn) { g_favorFn = fn; }
void RecruitSetCostDebugHook(CostDebugFn fn)       { g_debugFn = fn; }

static int Wealth(u16 marker, const Person* owner) {
    return g_wealthFn ? g_wealthFn(marker, owner) : 0;
}
static double Favor(int a, int b, int mode) {
    return g_favorFn ? g_favorFn(a, b, mode) : 0.0;
}

// ===========================================================================
// VIBE_Recruit_ComputeRecruitmentCost  0x55d674
// ---------------------------------------------------------------------------
// Register mapping (from disasm): eax=recruiterId -> esi (recruiter rec),
// edx=candidateId -> ecx (candidate rec). The marker word at rec+0 is the live
// slot index passed to ComputeTotalWealth/GetCashAmount/Favorability.
//
//   cand[+0x60] -> FindRecordById -> w0 = TotalWealth(that.marker, recruiter)
//   cand[+0x64] -> FindRecordById -> w1 = TotalWealth(that.marker, recruiter)
//   wCand = TotalWealth(cand.marker, recruiter); if <=0 -> wCand = 0 (recomputed)
//   peak = max(wCand, max(w0, w1))                 // v31
//   wRec = TotalWealth(rec.marker, recruiter)
//   a = peak / wRec * 5.0 + 1.0                     // v26
//   fav = Favorability(rec.marker, cand.marker, 1)
//   b = (100.0 - fav) * 0.1                         // v27
//   fee = 24.0 / GetCashAmount(cand.marker) * (a + b)   (truncated to int) v34
//   for each of the 5 reputation bytes cand[+0x80..+0x84]:
//       if byte > 210 -> fee += 2; else if byte > 168 -> fee += 1
//   if (DebugDispatch(16,rec,buf) & 2) fee /= 2
//   clamp: fee>24 -> 25; fee<=2 -> 2; else fee.
// ===========================================================================
int RecruitComputeRecruitmentCost(i32 recruiterId, i32 candidateId) {
    Person* rec  = PersonFindRecordById(recruiterId);
    Person* cand = PersonFindRecordById(candidateId);
    if (!rec || !cand)
        return 0;

    auto markerOf = [](const Person* p) -> u16 {
        return static_cast<u16>(PersonGetWord(p, 0));
    };

    // w0: candidate's +0x60 link (office-superior / relation) wealth.
    float w0 = 0.0f;
    Person* p60 = PersonFindRecordById(PersonGetDword(cand, 0x60));
    if (p60)
        w0 = static_cast<float>(Wealth(markerOf(p60), rec));
    // w1: candidate's +0x64 link wealth.
    float w1 = 0.0f;
    Person* p64 = PersonFindRecordById(PersonGetDword(cand, 0x64));
    if (p64)
        w1 = static_cast<float>(Wealth(markerOf(p64), rec));

    // Candidate's own wealth; the original recomputes it after a >0 test (the
    // first call is just the sign probe). If <=0, treated as 0.
    float wCand;
    int probe = Wealth(markerOf(cand), rec);
    if (static_cast<double>(probe) <= 0.0)
        wCand = 0.0f;
    else
        wCand = static_cast<float>(Wealth(markerOf(cand), rec));

    // peak = max(wCand, max(w0, w1)) — done with the original's branch order.
    float maxLinks = (w0 <= static_cast<double>(w1)) ? w1 : w0;       // v29
    float peak;                                                        // v31
    if (wCand <= static_cast<double>(maxLinks))
        peak = (w0 <= static_cast<double>(w1)) ? w1 : w0;
    else
        peak = wCand;

    int wRec = Wealth(markerOf(rec), rec);
    float a = static_cast<float>(peak / static_cast<double>(wRec)
                                     * kWealthRatioScale + 1.0);
    double fav = Favor(markerOf(rec), markerOf(cand), 1);
    float b = static_cast<float>((kFavorBase - fav) * kFavorScale);

    double cash = PersonGetCashAmount(markerOf(cand));
    double feeF = kCashNumerator / cash * (static_cast<double>(a)
                                           + static_cast<double>(b));
    // 0x55d84d: VIBE_Coord_ConvertX is called immediately before the fistp at
    // 0x55d852 — ConvertX (0x5c6b08) sets the x87 control word to RC=11
    // (truncate toward zero), so the store TRUNCATES. (An earlier reading
    // assumed default round-to-nearest and used std::lrint — refuted by the
    // ConvertX RC=11 semantics established for this tree.)
    int fee = static_cast<int>(feeF);

    // Reputation bytes cand[+0x80..+0x84]: the loop runs from cand+0 incrementing
    // the pointer until it reaches cand+5, but reads [ptr+0x80] each iteration —
    // i.e. five bytes at +0x80,+0x81,+0x82,+0x83,+0x84.
    for (int i = 0; i < 5; ++i) {
        double rep = static_cast<double>(
            static_cast<i16>(PersonGetByte(cand, 0x80 + i)));
        if (rep > kRepHighThresh)
            fee += 2;
        else if (rep > kRepMidThresh)
            ++fee;
    }

    u8 buf[544] = {};
    buf[0] = 1;
    int dbg = g_debugFn ? g_debugFn(16, rec, buf) : 0;
    if (dbg & 2)
        fee /= 2;

    if (fee > 24)
        return 25;
    if (fee <= 2)
        return 2;
    return fee;
}

// ===========================================================================
// VIBE_Person_EvaluateCandidateEligibility  0x5596f8
// ---------------------------------------------------------------------------
// `a1`=reference index (the recruiter / "self"), `a2`=candidate index, `a3`=
// 24-byte filter descriptor. Reads candidate columns by byte offset; the index
// is the 0..767 person slot. All accesses are 1:1 with the decompilation.
// ===========================================================================
namespace {

// VIBE_Character_IsActiveType leaf (a3+6 bit 0x10 gate). Settable; default
// returns false. VIBE_Combat_IsTargetUnderfull leaf (a3+5 gates) likewise.
bool (*g_activeTypeFn)(u16 idx) = nullptr;
TargetUnderfullFn g_underfullFn = nullptr;

bool TargetUnderfull(u16 idx) {
    return g_underfullFn ? g_underfullFn(idx) : false;
}

}  // namespace

void RecruitSetActiveTypeHook(bool (*fn)(u16 idx)) { g_activeTypeFn = fn; }
void RecruitSetTargetUnderfullHook(TargetUnderfullFn fn) { g_underfullFn = fn; }

// Helpers reading the candidate/reference Person columns by index.
static u8  KindOf(u16 idx)   { return PersonGetByte(&g_persons[idx], kPfKind); }
static i32 IdOf(u16 idx)     { return PersonGetDword(&g_persons[idx], kPfId); }
static i16 FamilyOf(u16 idx) { return PersonGetWord(&g_persons[idx], kPfFamilyWord); }
static i32 EmployerOf(u16 idx) {
    return PersonGetDword(&g_persons[idx], kPfRelationBase);  // +0x5C
}

static bool IsCitizenClass(u8 k) { return k == 6 || k == 7 || k == 5; }

int PersonEvaluateCandidateEligibility(u16 referenceIdx, u16 candidateIdx,
                                       const CandidateFilter* filter) {
    const u16 ref  = referenceIdx;   // v26 / a1
    const u16 cand = candidateIdx;   // v3  / a2

    // Equal index => returns a2^a1 == 0 (ineligible against self).
    if (ref == cand)
        return 0;

    // Candidate slot must be live, not already recruited, and not the focus.
    if (g_persons[cand].marker == -1
        || PersonGetByte(&g_persons[cand], kPfRecruited) != 0
        || &g_persons[cand] == g_recruitFocus)
        return 0;

    // Null / empty filter (a3 == 0 or a3+4 flags all zero) => eligible.
    if (!filter || filter->flagsA() == 0)
        return 1;

    if (KindOf(cand) >= 10)
        return 0;

    const u32 fA = filter->flagsA();
    const u8  fB = filter->flagsB();
    const u8  fC = filter->flagsC();
    const u8  fD = filter->flagsD();

    // a3+13 bit2: exclude-id list at a3+14 (4 words). If candidate marker
    // matches a non-0xFFFF entry => ineligible.
    if (filter->flagsE & 2) {
        for (int i = 0; i < 4; ++i) {
            u16 ex = filter->excludeIds[i];
            if (ex != 0xFFFF && cand == ex)
                return 0;
        }
    }

    // The low 18 bits of flagsA select the "by-attribute" gates; when none of
    // them are set the matcher skips straight to the class/favor/rank block
    // (LABEL_62 in the original).
    bool goLabel62 = (fA & 0x3FFFF) == 0;

    if (!goLabel62) {
        // gender gates (a3+4 bit0/bit1) vs candidate gender low byte (+9).
        u8 genderLow = static_cast<u8>(PersonGetByte(&g_persons[cand], kPfGender));
        if (((fA & 1) && genderLow == 1) || ((fA & 2) && genderLow == 0))
            return 0;

        // a3+5 bit0x10: candidate must be unemployed (employer not a live actor).
        if ((fB & 0x10) && EmployerOf(cand) != -1) {
            Person* emp = PersonFindRecordById(EmployerOf(cand));
            if (emp && PersonGetByte(emp, kPfIsPlayer))
                return 0;
        }

        // a3+4 bit2: candidate must share household with the reference.
        if ((fA & 4)
            && FamilyOf(cand) != FamilyOf(ref)
            && EmployerOf(cand) != IdOf(ref))
            return 0;
        // a3+4 bit3: candidate must NOT share household with the reference.
        if ((fA & 8)
            && (FamilyOf(cand) == FamilyOf(ref) || EmployerOf(cand) == IdOf(ref)))
            return 0;
        // a3+4 bit0x40: candidate kind must be 1.
        if ((fA & 0x40) && KindOf(cand) != 1)
            return 0;
        // a3+4 bit0x10: candidate must have a profession and kind 0.
        if ((fA & 0x10)
            && (!PersonGetByte(&g_persons[cand], kPfProfession) || KindOf(cand)))
            return 0;
        // a3+5 bit0: candidate must be a citizen-class OR hold an office.
        if (fB & 1) {
            u8 k = KindOf(cand);
            if (!IsCitizenClass(k)
                && !PersonGetByte(&g_persons[cand], kPfOffice)
                && !PersonGetByte(&g_persons[cand], kPfOffice2))
                return 0;
        }

        // a3+5 bit1 (0x559c07): `(fB&2)==0 || (result = IsTargetUnderfull(cand))
        // != 0` gates the remaining checks; in the ELSE branch the original
        // returns `result` — which is the ZERO IsTargetUnderfull just returned.
        bool b5b1 = (fB & 2) != 0;
        if (!b5b1 || TargetUnderfull(cand)) {
            // a3+5 bit2: reject if IsTargetUnderfull(cand).
            if ((fB & 4) && TargetUnderfull(cand))
                return 0;
            // a3+5 bit0x20: reject if candidate is jailed (+0x184 != 0).
            if ((fB & 0x20) && PersonGetDword(&g_persons[cand], kPfJailStatus))
                return 0;
            // a3+5 bit0x40: reject citizen-class candidates.
            if (fB & 0x40) {
                if (IsCitizenClass(KindOf(cand)))
                    return 0;
            }
            // a3+5 sign bit (bit0x80): require citizen-class.
            if (static_cast<i8>(fB) < 0) {
                if (!IsCitizenClass(KindOf(cand)))
                    return 0;
            }
            // a3+5 bit3: require an office (either slot).
            if ((fB & 8)
                && !PersonGetByte(&g_persons[cand], kPfOffice)
                && !PersonGetByte(&g_persons[cand], kPfOffice2))
                return 0;
            // a3+6 bit0/bit1 + a3+4 bit0x20 / sign: status/turn-bit gates.
            if (((fC & 1) && !PersonGetDword(&g_persons[cand], kPfStatusFlag))
                || ((fC & 2)
                    && (PersonGetDword(&g_persons[cand], kPfTurnBits) & 0x800000))
                || ((fA & 0x20)
                    && (PersonGetDword(&g_persons[cand], kPfTurnBits) & 0x40000000))
                // 0x559ac5: `*(char*)(a3+4) < 0` — the SIGN of the LOW BYTE of
                // flagsA (fA bit 7 == 0x80), not bit 31 of the dword.
                || ((fA & 0x80)
                    && !PersonGetDword(&g_persons[cand], kPfMiscFlag)))
                return 0;
            // fall through to LABEL_62.
        } else {
            // b5b1 set and NOT underfull => `return result` at 0x559739 with
            // result == the 0 the IsTargetUnderfull call just produced —
            // INELIGIBLE. (An earlier reading returned 1 — decompile-refuted.)
            return 0;
        }
    }

    // ----- LABEL_62: class-set membership, favor window, office-rank distance -
    if (fC & 0x3C) {
        bool ok = false;
        if (fC & 4) {
            u8 k = KindOf(cand);
            if (k == 6 || k == 7)
                ok = true;
        }
        if ((fC & 8) && KindOf(cand) == 5)
            ok = true;
        if ((fC & 0x10) && KindOf(cand) != 5 && g_activeTypeFn
            && g_activeTypeFn(cand))
            ok = true;
        if ((fC & 0x20) && KindOf(cand) == 0)
            ok = true;
        if (!ok)
            return 0;
    }

    // a3+24: favor window [a3+28, a3+32].
    if (filter->favorActive) {
        double favv = Favor(ref, cand, 1);
        if (favv < filter->favorMin)
            return 0;
        if (favv > static_cast<double>(filter->favorMax))
            return 0;
    }

    // a3+7 bit3: office-rank distance gate.
    if ((fD & 8) == 0)
        return 1;

    int span = static_cast<int>((fA & 0x7C00000u) >> 22);
    int rRank = PersonComputeOfficeRank(ref, 0);
    int cRank = PersonComputeOfficeRank(cand, 0);
    if (std::abs(rRank - cRank) <= span)
        return 1;

    // Within-class fallback: both reference and candidate are citizen-class.
    if (IsCitizenClass(KindOf(ref)))
        return IsCitizenClass(KindOf(cand)) ? 1 : 0;
    return 0;
}

}  // namespace guild::sim
