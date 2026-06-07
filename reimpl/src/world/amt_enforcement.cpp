#include "world/amt_enforcement.h"

#include <cstring>

#include "crt/rand.h"

// Faithful 1:1 port of VIBE_Amt_EnforceLawViolations (gilde.exe 0x57bf20). The
// person walk (word_12CE910, stride 536) and the network commits route through
// hooks; the violation sweep reuses GesetzEvaluateViolation (world/law.cpp) so
// the caught/escaped roll is governed by law.cpp's RNG + guard hooks. RNG for the
// reputation-decay sweep is the same RandomFloatScaled path the original uses.

namespace guild::world {

namespace {
EnforceRepCommandHook  g_repHook  = nullptr;
void*                  g_repCtx   = nullptr;
EnforceFlagCommandHook g_flagHook = nullptr;
void*                  g_flagCtx  = nullptr;

// flt_62675C @0x62675C == 0x38000100 (1/32768-ish). Same scale law.cpp uses; kept
// local so this module's RNG path matches VIBE_Math_RandomFloatScaled exactly.
float RandFloatScaledF() {
    static const float scale = []() {
        u32 bits = 0x38000100u;
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }();
    // (double)(int)RandNext() * flt_62675C, then used in a float compare. The
    // original promotes to double for the multiply; we return the float-truncated
    // product to match the subsequent single-precision compare.
    return static_cast<float>(static_cast<double>(guild::crt::RandNext())
                              * static_cast<double>(scale));
}
} // namespace

void EnforceSetRepCommandHook(EnforceRepCommandHook hook, void* ctx) {
    g_repHook = hook;
    g_repCtx  = ctx;
}
void EnforceSetFlagCommandHook(EnforceFlagCommandHook hook, void* ctx) {
    g_flagHook = hook;
    g_flagCtx  = ctx;
}

// gilde.exe 0x57bf20 — shared qualifying predicate (the v1/v7 + v3/v9 gates):
//   word @+0 != 0xFFFF; byte @+2 in (0,10); officeType not in {6,7,8};
//   reputationWord (+10) >= 12.
bool EnforceQualifies(const EnforcePerson& p) {
    if (!p.present)
        return false;
    if (!(p.profClass < 10 && p.profClass > 0))
        return false;
    if (p.officeType == 6 || p.officeType == 7 || p.officeType == 8)
        return false;
    if (p.reputationWord < kEnforceMinReputationWord)
        return false;
    return true;
}

EnforceResult EnforceRun(int gameTurn, const EnforcePerson* people, int count) {
    EnforceResult r;

    // Cadence gate: (gameTurn % 4) == 0.  (qword_13CE852 % 4 in the original.)
    if (gameTurn % 4 != 0)
        return r;
    r.ran = true;

    // Read law #2 to select the branch (VIBE_Gesetz_GetRecord(2, v13); v14 ==
    // record.threshold at +24).
    LawRecord law;
    if (!GesetzGetRecord(2, &law))
        return r; // law 2 absent -> nothing to do (matches: v14 uninitialised gate)
    r.threshold = law.threshold;

    if (!people || count <= 0)
        return r;

    if (law.threshold == 2) {
        // -------- reputation-decay sweep (v14 == 2) --------
        r.decayBranch = true;
        for (int i = 0; i < count; ++i) {
            const EnforcePerson& p = people[i];
            if (!EnforceQualifies(p))
                continue;
            // RandomFloatScaled() * 0.2f > reputation(+460)
            if (RandFloatScaledF() * kEnforceRepRollScale > p.reputation) {
                ++r.swept;
                u8 flagVal = static_cast<u8>(p.flag12 == 0); // LOBYTE(v16[0])
                // penalty = RandomFloatScaled()*0.5 + 0.25, queued as cmd 460.
                float penalty = RandFloatScaledF() * kEnforcePenaltyScale
                              + kEnforcePenaltyBase;
                if (g_repHook)
                    g_repHook(p.personId, 460, penalty, g_repCtx);
                ++r.penalties;
                if (g_flagHook)
                    g_flagHook(p.personId, flagVal, g_flagCtx);
                ++r.flagsSet;
            }
        }
    } else {
        // -------- law-violation sweep (v14 != 2) --------
        // v14 truthy -> v5(value)=0, v15(guardSel)=1; else v5=1, v15=0.
        int value, guardSel;
        if (law.threshold != 0) {
            value = 0;
            guardSel = 1;
        } else {
            value = 1;
            guardSel = 0;
        }
        for (int i = 0; i < count; ++i) {
            const EnforcePerson& p = people[i];
            if (!EnforceQualifies(p))
                continue;
            // (int @+9) >> 24 != v15
            if ((p.guardHighByte >> 24) == guardSel)
                continue;
            ++r.swept;

            // victim/extra: v10 = building link; v11 = (v10 ? *(v10+1) : -1).
            i32 extra = p.buildingLink ? p.buildingVictim : -1;

            // EvaluateViolation(law 2, value, victim=-1, perp, extra).
            int outcome = GesetzEvaluateViolation(2, value, -1, p.personId,
                                                  extra, nullptr);
            ++r.penalties;
            if (outcome != kViolationEscaped) // != -1
                ++r.caught;

            // reputation(+460) > 0.1f -> queue cmd 460 = -0.1f (clamp).
            if (p.reputation > kEnforceRepClampLevel) {
                if (g_repHook)
                    g_repHook(p.personId, 460, kEnforceRepClampValue, g_repCtx);
                ++r.repClamps;
            }
        }
    }
    return r;
}

} // namespace guild::world
