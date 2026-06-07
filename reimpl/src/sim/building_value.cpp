#include "sim/building_value.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered float constants (byte-for-byte from gilde.exe). See the header for
// the address map; values below were decoded with get_bytes + struct.unpack.
// ---------------------------------------------------------------------------
static constexpr float  kStatScale     = 0.003968254197388887f; // flt_62670C  (~1/252)
static constexpr float  kHandlerDecay  = 0.5f;                  // flt_626710
static constexpr float  kSlotBonus     = 0.1666666716337204f;   // flt_626714
static constexpr float  kHandlerStat4  = 0.33333334f;           // (stat==4 handler step)
static constexpr float  kHandlerStatN  = 0.1666666716337204f;   // (else handler step)
static constexpr float  kF626734       = 0.3330000042915344f;   // flt_626734
static constexpr double kD62671C       = 0.2;                   // dbl_62671C
static constexpr double kD626724       = 0.166666666666667;     // dbl_626724  (~1/6)
static constexpr double kD62672C       = 0.333;                 // dbl_62672C

static constexpr float  kCurveA_F4 = 0.6000000238418579f; // flt_6266F4
static constexpr float  kCurveA_F8 = 0.4000000059604645f; // flt_6266F8
static constexpr float  kCurveA_FC = 0.5f;                // flt_6266FC
static constexpr float  kCurveB_00 = 0.6000000238418579f; // flt_626700
static constexpr float  kCurveB_04 = 0.4000000059604645f; // flt_626704
static constexpr float  kCurveB_08 = 0.5f;                // flt_626708
static constexpr float  kPixels    = 252.0f;              // flt_6266F0

static constexpr float  kRateF1 = 28.0f;                  // flt_6268E4
static constexpr float  kRateF2 = 32.0f;                  // flt_6268E8
static constexpr double kRateD1 = 0.08333333333333333;    // dbl_6268EC  (1/12)
static constexpr double kRateD2 = 0.016666666666666666;   // dbl_6268F4  (1/60)
static constexpr double kPriceMode = 0.25;                // dbl_6268FC

// Cross-module hooks (default inert). Mirrors the He_*/Inventory_* indirection.
static IBuildingRatingHooks  g_defaultRatingHooks;
static IBuildingRatingHooks* g_ratingHooks = &g_defaultRatingHooks;
void SetBuildingRatingHooks(IBuildingRatingHooks* hooks) {
    g_ratingHooks = hooks ? hooks : &g_defaultRatingHooks;
}
IBuildingRatingHooks* BuildingRatingHooks() { return g_ratingHooks; }

// The price-mode global the originals read (dword_63C744). The live game sets it
// from the difficulty/market state; defaults to 0 in the cold IDB. We expose it
// as a settable module global so ComputeItemBaseValue stays faithful + testable.
static int g_priceMode = 0;   // dword_63C744

// trunc-toward-zero (every "v=x; VIBE_Coord_ConvertX(); (int)v" rounds to zero).
static inline int truncToZero(double x) {
    return static_cast<int>(static_cast<long long>(x));
}

// gilde.exe 0x58a794 — VIBE_Building_EvalProductionRating
float Building_EvalProductionRating(const BuildingRec* b, int stat) {
    if (!b || stat >= 5)
        return -1.0f;

    // base = statLevel[stat] / 252
    double v14 = static_cast<double>(b->statLevel[stat]) * kStatScale;

    if (stat != 1) {
        // Handler-weighted penalty. The original loops the matching handler list
        // accumulating v16 += v18 with v18 *= 0.5 each step (== sum of 0.5^k).
        // Our hook returns that accumulated weight directly.
        (void)kHandlerDecay;  // documents the per-step decay folded into the hook
        float v16 = g_ratingHooks->HandlerStatWeight(b->typeIndex, stat);
        float step = (stat == 4) ? kHandlerStat4 : kHandlerStatN;
        v14 -= static_cast<double>(v16 * step);
    }

    if (stat == 3)
        v14 += static_cast<double>(g_ratingHooks->InventorySlotActive(b, 346)) * kSlotBonus;

    // Staff/equipment term decoded from the packed +44 bitfield. The original
    // uses shift forms like (v<<9>>29); the equivalent masked extractions are
    // documented per case.
    const u32 v = b->staffBits;
    double v19 = 0.0;
    switch (stat) {
        case 0: {
            double v20 = static_cast<double>((v >> 20) & 7u) * kF626734 * kSlotBonus; // v<<9>>29
            double v6  = static_cast<double>((v >> 4) & 0xFu) * kD62671C * kD626724;   // v<<24>>28
            v19 = v6 + v20;
            break;
        }
        case 1: {
            double v20 = static_cast<double>((v >> 8) & 0xFu) * kD62671C * kD626724;   // v<<20>>28
            double v6  = static_cast<double>((v >> 20) & 7u) * kF626734 * kSlotBonus;  // v<<9>>29
            v19 = v6 + v20;
            break;
        }
        case 2: {
            double v20 = static_cast<double>((v >> 17) & 7u) * kD62672C * kD626724;    // v<<12>>29
            double v6  = kD626724 * (static_cast<double>((v >> 25) & 0xFu) * kD62671C);// 8*v>>28
            v19 = v6 + v20;
            break;
        }
        case 3: {
            double v20 = static_cast<double>((v >> 8) & 0xFu) * kD62671C * kD626724;    // v<<20>>28
            double v6  = kD62671C * static_cast<double>((v >> 25) & 0xFu) * kD626724;   // 8*v>>28
            v19 = v6 + v20;
            break;
        }
        case 4: {
            double v20 = static_cast<double>((v >> 12) & 3u) * kSlotBonus;             // v<<18>>30
            double v6  = static_cast<double>((v >> 4) & 0xFu) * kD62671C * kD626724;   // v<<24>>28
            v19 = v6 + v20;
            break;
        }
        default:
            break;
    }

    double v15 = v14 - v19;
    if (v15 > 1.0 || v15 > 0.0)
        return (v15 <= 1.0) ? static_cast<float>(v15) : 1.0f;
    return 0.0f;
}

// gilde.exe 0x58a6e8 — VIBE_Building_ComputeRatingCurveA
double Building_ComputeRatingCurveA(int stat, const BuildingRec* a, const BuildingRec* b) {
    float v7 = Building_EvalProductionRating(a, stat);
    float v5 = Building_EvalProductionRating(b, stat);
    return ((v7 * v7 - v5) * kCurveA_F8 + kCurveA_F4
            + (v7 - v5 * v5) * kCurveA_F4 + kCurveA_F8) * kCurveA_FC;
}

// gilde.exe 0x58a73c — VIBE_Building_ComputeRatingCurveB
double Building_ComputeRatingCurveB(float a1, float a2) {
    return ((a1 * a1 - a2) * kCurveB_04 + kCurveB_00
            + (a1 - a2 * a2) * kCurveB_00 + kCurveB_04) * kCurveB_08;
}

// gilde.exe 0x58a6bc — VIBE_Building_ComputeProductionPixels
int Building_ComputeProductionPixels(int stat, const BuildingRec* b) {
    double v3 = Building_EvalProductionRating(b, stat) * kPixels;
    return truncToZero(v3);   // VIBE_Coord_ConvertX -> trunc toward zero
}

// gilde.exe 0x58f328 — VIBE_Building_ComputeItemBaseValue
float Building_ComputeItemBaseValue(const BuildingRec* b, u8 typeIndex,
                                    int outIdx, int inIdx) {
    // The original resolves typeDef = dword_13CE294 + 589*typeIndex and reads
    // the input (a4/+553) or output (a3/+563) factor byte. We resolve the bytes
    // through the table accessor in the caller's typeIndex; to stay leaf-pure we
    // fetch them via the helper that mirrors the table read.
    // (Caller supplies the type via typeIndex; the factor lookup uses the same
    // BuildingTypeDef the value aggregators pass.)
    // To keep this function self-contained for testing we take the building's
    // own type table entry through the global accessor declared in building.h.
    extern const BuildingTypeDef* BuildingTypeDefAt(u8 typeIndex);
    const BuildingTypeDef* td = BuildingTypeDefAt(typeIndex);

    float v9 = 0.0f;
    if (td) {
        if (inIdx != -1) {                                 // a4 != -1: input factor
            v9 = static_cast<float>(896 * td->inputFactor[inIdx]);
        } else if (outIdx != -1) {                         // a3 != -1: output factor
            v9 = static_cast<float>(896 * td->outputFactor[outIdx]);
        }
    }

    u8 kind = b ? b->objectKind : 0;                       // *(BYTE*)(a1+2)
    if (kind != 6 && kind != 7)
        return v9;
    // sale/auction price discount: (1 - (2 - g_priceMode)*0.25) * v9
    return static_cast<float>((1.0 - static_cast<double>(2 - g_priceMode) * kPriceMode) * v9);
}

void SetBuildingPriceMode(int mode) { g_priceMode = mode; }   // sets dword_63C744
int  BuildingPriceMode() { return g_priceMode; }

// gilde.exe 0x58f268 — VIBE_Building_ComputeProductionRate
double Building_ComputeProductionRate(const BuildingTypeDef& typeDef,
                                      u32 scenePriceField, u16 sceneDivisor) {
    // sum over i in 0..1 of outputFactor[i] * 28 * 32 * (1/12) * (1/60)
    double v2 = 0.0;
    for (int i = 0; i < 2; ++i) {
        v2 += static_cast<double>(typeDef.outputFactor[i])
              * kRateF1 * kRateF2 * kRateD1 * kRateD2;
    }
    return static_cast<double>(scenePriceField) * static_cast<float>(v2)
           / static_cast<double>(sceneDivisor);
}

// gilde.exe 0x57d26c — VIBE_Building_ComputeCurrentOutput
float Building_ComputeCurrentOutput(const BuildingRec* b) {
    if (b->typeIndex == 0xFFFF || !b->activeFlag)  // *(WORD*)a1 == 0xFFFF
        return -1.0f;
    double fill = static_cast<double>(b->fillLevel);
    float out;
    if (fill <= b->fillCap)
        out = (b->outFullFill - b->outZeroFill) * static_cast<float>(fill) / b->fillCap
              + b->outZeroFill;
    else
        out = b->outFullFill;
    double total = static_cast<double>(b->outBonus) + out;
    if (total > 0.0)
        return static_cast<float>(total);
    return 0.0f;
}

// gilde.exe 0x57d310 — VIBE_Building_ComputeMaxOutput
float Building_ComputeMaxOutput(const BuildingRec* b) {
    if (b->typeIndex == 0xFFFF || !b->activeFlag)  // *(WORD*)a1 == 0xFFFF
        return -1.0f;
    double fill = static_cast<double>(b->fillLevel);
    if (fill > b->fillCap)
        return b->outFullFill;
    return (b->outFullFill - b->outZeroFill) * static_cast<float>(fill) / b->fillCap
           + b->outZeroFill;
}

// gilde.exe 0x57d384 — VIBE_Building_ComputeOutputRatio
float Building_ComputeOutputRatio(const BuildingRec* b) {
    if (b->typeIndex == 0xFFFF || !b->activeFlag)  // *(WORD*)a1 == 0xFFFF
        return 0.0f;
    float cur = Building_ComputeCurrentOutput(b);
    return cur / Building_ComputeMaxOutput(b);
}

}  // namespace guild::sim
