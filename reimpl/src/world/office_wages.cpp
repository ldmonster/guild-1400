#include "world/office_wages.h"

#include "util/math_random.h"

// gilde.exe 0x57b480 — VIBE_Amt_ComputeOfficeWages. Faithful reconstruction of
// the wage arithmetic + control flow. The live office/building array reads are
// folded into OfficeWageInput by the caller; the two command emissions are
// routed through OfficeWageCommandSink.
//
// HANDOFF (rule 13): the engine's per-turn wage cycle is
// VIBE_Amt_ProcessAllOfficeWages @0x57b6bc, which walks every office record
// (index a1 in 0..N), fills an OfficeWageInput from
//   byte_12CEA76/79[536*a1], word_12CE910[268*a1], byte_12CE912[536*a1],
//   byte_12CEA918[536*a1], VIBE_Office_GetDefinition(...) ranks, law(14).float,
//   dword_12CE914[134*a1]
// and calls ComputeOfficeWages with dispatch = (the person/type-277 object
// resolved, i.e. VIBE_Person_QueryByGoodType(1,rec) && QueryFind(...,277)).
// Install SetOfficeWageCommandSink at boot to forward to the command queue.

namespace guild::world {

namespace {
OfficeWageCommandSink g_sink{};
OfficeWageRandFn      g_rand = nullptr;
} // namespace

void SetOfficeWageCommandSink(const OfficeWageCommandSink& sink) { g_sink = sink; }
void SetOfficeWageRandFn(OfficeWageRandFn fn) { g_rand = fn; }

// VIBE_Coord_ConvertX 0x5c6b08: frndint under round-toward-zero, then the
// caller's `(int)v9` cast. Truncation toward zero.
i32 OfficeWageTrunc(double x) {
    return static_cast<i32>(static_cast<long long>(x));
}

// 0x57b55c / 0x57b598: v9 = (double)rank * flt_6258AC * flt_6258B0 * lawRate.
// The single-precision constants and the float lawRate are promoted into the
// double multiply exactly as the x87 chain does (each term widened to 80-bit /
// here double); the final ConvertX + cast truncates toward zero.
i32 OfficeWageForSeat(int rank, float lawRate) {
    double v = static_cast<double>(rank)
             * static_cast<double>(kOfficeWageRankScale)
             * static_cast<double>(kOfficeWageBaseScale)
             * static_cast<double>(lawRate);
    return OfficeWageTrunc(v);
}

OfficeWageResult ComputeOfficeWages(const OfficeWageInput& in) {
    OfficeWageResult r;

    // 0x57b48f..0x57b4c1: write the seat title ids into the out record, zero the
    // two wage fields. (*(a3+0)=officeId, *(a3+1)=deputyId, *(a3+4)=*(a3+8)=0.)
    // We track the title ids implicitly via the input; the wages start at 0.

    // 0x57b4f1: early-out when vacant / saturated / disabled / no titles.
    //   word_12CE910 == -1  || byte_12CE912 >= 10 || !byte_12CEA918
    //   || (!byte_12CEA76 && !byte_12CEA79)
    if (in.personId == -1
        || in.stateByte >= 10
        || !in.enabled
        || (in.officeId == 0 && in.deputyId == 0)) {
        return r; // v21 == 0 (valid == false)
    }

    // 0x57b50d..0x57b528: lawRate = law record 14 float field (already in input).
    const float lawRate = in.lawRate;

    // 0x57b53d..0x57b56b: seat B (deputy) first -> *(a3+8).
    r.wageB = OfficeWageForSeat(in.rankB, lawRate);

    // 0x57b59c: v21 = 1 set before seat-A store. 0x57b574..0x57b5a5: seat A.
    r.valid = true;
    r.wageA = OfficeWageForSeat(in.rankA, lawRate);

    // 0x57b5aa: if (!a2) return v21 — compute-only, no dispatch.
    if (!in.dispatch)
        return r;

    // 0x57b5b2..0x57b5d8: the original resolves the office holder
    // (VIBE_Person_QueryByGoodType(1, rec)) and a type-277 object; only when both
    // resolve does it queue. The caller folds that into `dispatch`.

    // 0x57b5de: seat-A wage command (when non-zero).
    if (r.wageA) {
        if (g_sink.queueRequest16)
            g_sink.queueRequest16(in.officeObjectId, -1, r.wageA, 0);
    }
    // 0x57b601: seat-B wage command (when non-zero).
    if (r.wageB) {
        if (g_sink.queueRequest16)
            g_sink.queueRequest16(in.officeObjectId, -1, r.wageB, 0);
    }

    // 0x57b64d: when byte_12CE912 == 3, draw the random bonus.
    if (in.stateByte == 3) {
        const int base = in.bonusByte;                 // BYTE2(v19)
        const int n    = base + 2;                     // RandomModulo arg
        const int roll = g_rand ? g_rand(static_cast<u16>(n))
                                : guild::util::RandomModulo(static_cast<u16>(n));
        r.bonusEmitted = true;
        r.bonusAmount  = 6400 * (roll + base);         // 0x57b691
        r.bonusOp      = base;                          // RequestBuildOp90 arg
        if (g_sink.queueRequest16)
            g_sink.queueRequest16(in.officeObjectId, -1, r.bonusAmount, 0);
        if (g_sink.requestBuild)
            g_sink.requestBuild(in.officeObjectId, base);
    }

    return r;
}

} // namespace guild::world
