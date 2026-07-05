// world_economy2 — see world_economy2.h. The office-action dialog family's
// deterministic kernels, byte-for-byte with the recovered constants; RNG / coord
// / rating / command-queue plumbing routed through installable inert-default hooks.
#include "world/world_economy2.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Hook table (process-global, like amt_economy2.cpp — internal linkage so the
// bare `g_hooks` name never collides with another guild::world module).
// ---------------------------------------------------------------------------
namespace { WorldEconomy2Hooks g_hooks{}; }

void WorldEconomy2SetHooks(const WorldEconomy2Hooks& hooks) { g_hooks = hooks; }
const WorldEconomy2Hooks& WorldEconomy2GetHooks() { return g_hooks; }
void WorldEconomy2ResetHooks() { g_hooks = WorldEconomy2Hooks{}; }

namespace {

// VIBE_Coord_ConvertX default == C truncation toward zero (matches the FPU
// round-toward-zero the original relies on for these positive magnitudes).
i32 DefaultTruncate(double v) { return static_cast<i32>(v); }
i32 Truncate(double v) {
    return g_hooks.truncate ? g_hooks.truncate(v) : DefaultTruncate(v);
}

// VIBE_Math_RandomFloatScaled (0x58b910) returns DOUBLE — the kernels consume
// the full-precision roll exactly as the x87 chains do.
double RandFloatScaled() {
    return g_hooks.randFloatScaled ? g_hooks.randFloatScaled() : 0.0;
}

float ProductionRating(const void* building, int stat) {
    return g_hooks.productionRating ? g_hooks.productionRating(building, stat) : 0.0f;
}

} // namespace

// ---------------------------------------------------------------------------
// ComputeOfficePaymentAmount — the shared payment formula.
// 0x482299 / 0x4822be (election) and 0x4829a / 0x4829c2 (action):
//   v25 = EvalProductionRating(building, 4);
//   v3  = ((RandomFloatScaled()+1.0) * (v25 * 15.0) + 5.0) * 2.55;
//   v27 = (int)ConvertX(...);    // truncate via Coord_ConvertX
// ---------------------------------------------------------------------------
i32 ComputeOfficePaymentAmount(const void* building) {
    // 0x482299: fstp v25 (the rating IS stored to a 4-byte float); the rest of
    // the chain (fmul 15.0f, faddp roll+1, fmulp, fadd 5.0, fmul 2.55) stays on
    // the FPU at double precision until ConvertX truncates it.
    float rating = ProductionRating(building, 4);
    double v = ((RandFloatScaled() + 1.0)
                * (static_cast<double>(rating) * static_cast<double>(kPayRatingScale))
                + kPayBase)
               * kPayFinalScale;
    return Truncate(v);
}

// ---------------------------------------------------------------------------
// ComputeAppointmentPrice — BuildGuildOfficeMenu (0x481f1c..0x481f40):
//   if (objectKind == 1)
//     v22 = (RandomFloatScaled()+1.0) * (rating*0.4f) + 1.2f;
//   else
//     v22 = 1.0 - ((RandomFloatScaled()+1.0) * (rating*0.3f) + 0.2);
//   v35 = (float)v22;
// BOTH branches are x87 chains held on the FPU stack at double precision (the
// roll v64 is a double, the float constants are promoted on load) with a single
// fstp to the 4-byte float v35 at the end — model with double throughout and one
// final float rounding (per the disasm at 0x481f1c..0x481f40).
// ---------------------------------------------------------------------------
float ComputeAppointmentPrice(float rating, int objectKind) {
    double r1 = RandFloatScaled() + 1.0;
    if (objectKind == 1) {
        return static_cast<float>(
            r1 * (static_cast<double>(rating) * static_cast<double>(kMenuPriceScaleHi))
            + static_cast<double>(kMenuPriceBaseHi));
    }
    double inner = r1
                   * (static_cast<double>(rating) * static_cast<double>(kMenuPriceScaleLo))
                   + kMenuPriceBaseLo;
    return static_cast<float>(1.0 - inner);
}

// ---------------------------------------------------------------------------
// RollActionDirection — BuildOfficeActionDialog (0x4825c9..0x4825e5):
//   v64 = RandomFloatScaled();  v63 = (v64 <= 0.3f) ? -1 : 1;
// 0x4825ce: fst v64 (double); fcomp flt_61AF50 — the DOUBLE roll is compared
// against the promoted 0.3f; no float rounding of the roll before the compare.
// ---------------------------------------------------------------------------
int RollActionDirection() {
    double roll = RandFloatScaled();
    return roll <= static_cast<double>(kActionRollGate) ? -1 : 1;
}

// ---------------------------------------------------------------------------
// ScanMaxCandidateRank — BuildGuildOfficeMenu (0x481e3e..0x481e6f):
//   v4 = 0;
//   for (i = QueryBegin(...); i; i = IterNext())
//     if (rank[*i] > v4) v4 = rank[*i];
//   if (!v4) return 0;   // (the v10 == 0 abort)
// We take the resolved candidate-rank list directly.
// ---------------------------------------------------------------------------
int ScanMaxCandidateRank(const u8* candidateRanks, int count) {
    int maxRank = 0;
    if (candidateRanks) {
        for (int i = 0; i < count; ++i) {
            if (static_cast<int>(candidateRanks[i]) > maxRank)
                maxRank = static_cast<int>(candidateRanks[i]);
        }
    }
    return maxRank;
}

// ---------------------------------------------------------------------------
// CountOfficeDependents — BuildElectionDialog (0x4822c6..0x482390):
//   v5 = 0;
//   for (v4 = 0; v4 < 768; ++v4)               // person index
//     if (present[v4] && targetEntity == personEntity[v4]) {
//       v11 = holderOfficeId;                  // *(u16*)(holder+39)
//       if (v11 != v4) {
//         ++v5;
//         // 0x482322..0x482343: imul eax,218h; ebx = -var_18 (the PAYMENT v27);
//         // eax = dword_12CE914[134*v11]; edx = dword_12CE914[134*v4]:
//         QueueRequestCoord27(objId[v11], objId[v4], -payment);
//       }
//     }
//   if (v5) {
//     // 0x48236d..0x482390: fild [a2+8]; fild v5; fdivp; ConvertX; fistp;
//     // mov ebp, trunc; add ebp, 20h   <- the queued per-head is trunc + 32
//     perHead = trunc((double)totalAmount / (double)v5) + 32;
//     ...second pass queues QueueRequest16(objId[v4], -1, perHead, currency)...
//   }
// The coord27 fan-out routes through the queueCoord27 hook; the pass-2 per-head
// amount (INCLUDING the +32) is returned in *outPerHead for the caller to queue.
// ---------------------------------------------------------------------------
int CountOfficeDependents(i32 targetEntity, const i32* personEntity,
                          const i32* personObjIds, const bool* present,
                          u16 holderOfficeId, int count, i32 totalAmount,
                          i32 paymentAmount, i32* outPerHead) {
    int dependents = 0;
    for (int i = 0; i < count; ++i) {
        bool live = present ? present[i] : true;
        if (live && personEntity && targetEntity == personEntity[i]) {
            // v11 (holderOfficeId) != v4 (the person index) — self-exclusion.
            if (static_cast<int>(holderOfficeId) != i) {
                ++dependents;
                if (g_hooks.queueCoord27) {
                    // dword_12CE914[134*v11] / dword_12CE914[134*v4] — both args
                    // are +4 object-ids, NOT the compared +364 entity values.
                    i32 a = personObjIds ? personObjIds[holderOfficeId]
                                         : static_cast<i32>(holderOfficeId);
                    i32 b = personObjIds ? personObjIds[i] : personEntity[i];
                    g_hooks.queueCoord27(a, b, -paymentAmount);
                }
            }
        }
    }
    if (outPerHead) {
        *outPerHead = dependents
                          ? Truncate(static_cast<double>(totalAmount)
                                     / static_cast<double>(dependents))
                                + 32   // add ebp, 20h @0x482390
                          : 0;
    }
    return dependents;
}

// ---------------------------------------------------------------------------
// ComputeGridLabelOffset — RunOfficeGridWindow (0x5583c0 AddCenteredLabel arg):
//   x = (slotX>>16) + 64 - (camX>>16);
// The caller passes the already >>16-shifted high words.
// ---------------------------------------------------------------------------
i32 ComputeGridLabelOffset(i32 slotScreenX, i32 cameraX) {
    return slotScreenX + kOfficeLabelBaseX - cameraX;
}

} // namespace guild::world
