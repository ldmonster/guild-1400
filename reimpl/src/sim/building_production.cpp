#include "sim/building_production.h"

#include <cstring>

#include "sim/building.h"   // BuildingTypeDefAt

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered float constants (byte-for-byte; decoded with get_bytes).
// ---------------------------------------------------------------------------
static constexpr double kHalf       = 0.5;                  // dbl_62649C
static constexpr double kInScale    = 0.0005;               // dbl_6264A4
static constexpr double kInScale0   = 0.0025;               // dbl_6264AC
static constexpr double kInBias0    = 0.5;                  // dbl_6264B4
static constexpr double kOutScale   = 0.5;                  // dbl_6264BC
static constexpr double kYieldLo    = 0.25;                 // dbl_6264C4
static constexpr double kYieldHi    = 5.0;                  // dbl_6264CC
static constexpr float  kYieldStep  = 0.4000000059604645f;  // flt_6264D4
static constexpr float  kYieldMin   = 512.0f;               // flt_6264D8

static constexpr float  kMP_Qty     = 0.009999999776482582f; // flt_626948
static constexpr float  kMP_Sub3    = 1.5f;                   // flt_62694C
static constexpr float  kMP_F50     = 28.0f;                  // flt_626950
static constexpr float  kMP_F54     = 32.0f;                  // flt_626954
static constexpr double kMP_D5C     = 0.08333333333333333;    // dbl_62695C (1/12)
static constexpr double kMP_D64     = 0.016666666666666666;   // dbl_626964 (1/60)
static constexpr double kMP_D6C     = 0.5;                    // dbl_62696C
static constexpr float  kMP_F74     = 2.200000047683716f;     // flt_626974
static constexpr float  kMP_F78     = 0.03125f;               // flt_626978

static constexpr double kPW_Wear    = 0.01;                  // dbl_6269CC
static constexpr float  kSV_Cap     = 2560000.0f;            // flt_6269D4
static constexpr double kSV_M2      = 0.04;                  // dbl_6269DC
static constexpr double kSV_M2b     = 0.09;                  // dbl_6269E4
static constexpr double kSV_StockM  = 0.3;                   // dbl_6269EC
static constexpr float  kRW_Mul     = 0.009999999776482582f; // flt_626A10

// trunc toward zero (mirrors the "v=x; VIBE_Coord_ConvertX(); (int)v" rounding).
static inline int truncToZero(double x) {
    return static_cast<int>(static_cast<long long>(x));
}

// ---------------------------------------------------------------------------
// Table storage.
// ---------------------------------------------------------------------------
SceneTypeDef g_sceneTypes[kSceneTypeCapacity];
bool g_sceneTypesLoaded = false;
u8   g_sceneTypeRemap[kSceneTypeRemap];
i32  g_prodStore[kProdStoreDwords];
ProdSchedule g_prodSchedules[kProdBuildingCapacity];

SceneTypeDef* SceneTypeDefAt(int prot) {
    if (!g_sceneTypesLoaded || prot < 0 || prot >= kSceneTypeCapacity)
        return nullptr;
    return &g_sceneTypes[prot];
}
ProdBuilding ProdBuildingAt(int idx) { return ProdBuilding{idx}; }
ProdSchedule* ProdScheduleAt(int idx) { return &g_prodSchedules[idx]; }

static IProductionHooks  g_defaultProdHooks;
static IProductionHooks* g_prodHooks = &g_defaultProdHooks;
void SetProductionHooks(IProductionHooks* hooks) {
    g_prodHooks = hooks ? hooks : &g_defaultProdHooks;
}
IProductionHooks* ProductionHooks() { return g_prodHooks; }

static int g_playerBuildingIndex = -1;  // byte_6477A1
void SetPlayerBuildingIndex(int idx) { g_playerBuildingIndex = idx; }
int  PlayerBuildingIndex() { return g_playerBuildingIndex; }

static int g_standaloneFlag = -1;       // dword_764CE0
void SetStandaloneFlag(int v) { g_standaloneFlag = v; }
int  StandaloneFlag() { return g_standaloneFlag; }

void ResetProductionTables() {
    std::memset(g_sceneTypes, 0, sizeof(g_sceneTypes));
    std::memset(g_sceneTypeRemap, 0, sizeof(g_sceneTypeRemap));
    std::memset(g_prodStore, 0, sizeof(g_prodStore));
    std::memset(g_prodSchedules, 0, sizeof(g_prodSchedules));
    g_sceneTypesLoaded = false;
    g_playerBuildingIndex = -1;
    g_standaloneFlag = -1;
}

// Slot prot id: high word of the slot's protPacked column (the original reads
// `dword_13C3B5C[..]+2 >> 16`, i.e. the slot+0x0E word).
static inline i16 SlotProt(ProdBuilding& pb, int slot) {
    return static_cast<i16>(pb.slotProtPacked(slot) >> 16);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x583304 — VIBE_GameTime_PackToRecord
//   out+2 = in[0] + 1400 (word); out+0 = 1; out+1 = 3*(in.day % 4)+1;
//   out+4 = in[+4]; out+5 = in[+6]; out+8 = in[+10].
// ---------------------------------------------------------------------------
PackedTime GameTime_PackToRecord(i32 day, u16 hourWord, i32 minute, i32 second) {
    PackedTime out{};
    // out+2 (word) = *(_WORD*)a1 + 1400, where *(_WORD*)a1 is the LOW WORD of the
    // source `day` field (gilde.exe 0x58330b `mov ax,[eax]` / 0x58330e add 578h).
    // (Previously this used hourWord — a real bug caught by the 1:1 diff test.)
    out.yearTag     = static_cast<u16>((day & 0xFFFF) + 1400);
    out.season      = 1;
    out.dayInSeason = static_cast<u8>(3 * (day % 4) + 1);
    out.hour        = static_cast<u8>(hourWord & 0xFF);
    out.minuteByte  = static_cast<u8>(minute & 0xFF);
    out.cursor      = second;
    return out;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x58f3d0 — VIBE_Building_ComputeMarketPrice
// ---------------------------------------------------------------------------
double Building_ComputeMarketPrice(i16 prot, u8 qty) {
    SceneTypeDef* td = SceneTypeDefAt(prot);
    if (!td)
        return 0.0;

    int cached = td->cachedPrice;          // *(_DWORD *)(v3 + 56)
    if (cached) {
        double v26 = static_cast<double>(32 * cached) * static_cast<double>(qty) * kMP_Qty;
        if (td->subtype == 3)              // *(_BYTE *)(v3 + 33) == 3
            v26 = v26 * kMP_Sub3;
        return v26;
    }

    // divisor: word @+54, or 1 if zero.
    int divisor = td->divisor + (td->divisor == 0 ? 1 : 0);
    double v23 = static_cast<double>(static_cast<u32>(td->baseValue)) / static_cast<double>(divisor);

    u8 remap = (prot >= 0 && prot < kSceneTypeRemap) ? g_sceneTypeRemap[prot] : 0;  // byte_13CE862[prot]
    double v27;
    if (remap) {
        const BuildingTypeDef* bt = BuildingTypeDefAt(remap);  // 589 * remap + dword_13CE294
        double acc = 0.0;
        if (bt) {
            for (int i = 0; i < 2; ++i) {
                double f = static_cast<double>(static_cast<i16>(bt->outputFactor[i]))
                           * kMP_F50 * kMP_F54 * kMP_D5C * kMP_D64;
                acc += f;
            }
        }
        v27 = acc;
    } else {
        v27 = 4.0;
    }

    double v28 = v27 * kMP_D6C * v23;
    if (td->kind == 23)                    // *(_BYTE *)v3 == 23
        return static_cast<double>(static_cast<float>(v28 * kMP_F74));

    // Sum component prices. The component prot is the high word of compType
    // (+46); 0xFFFF terminates, 0 means "no components" (leaf good).
    u16 compProt = static_cast<u16>(td->compType >> 16);
    if (compProt != 0 && compProt != 0xFFFF) {
        // The original walks up-to-4 component rows; we model the single-component
        // leaf path (multi-component aggregation reads scene rows not modeled).
        u16 priceField = td->priceField;              // *(_WORD *)(v13 + 38)
        i16 sub = static_cast<i16>(compProt);
        float scaled = static_cast<float>(priceField);
        double comp = Building_ComputeMarketPrice(sub, qty) * scaled;
        int dv = td->divisor;
        v28 += comp / static_cast<double>(dv ? dv : 1);
    }

    double v20 = v28 * kMP_F74 * kMP_F78;
    td->cachedPrice = truncToZero(v20);    // *(_DWORD *)(v3 + 56) = (int)v20
    return static_cast<double>(static_cast<float>(
        kMP_F74 * (v28 * static_cast<double>(qty) * kMP_Qty)));
}

// ---------------------------------------------------------------------------
// gilde.exe 0x584d34 — VIBE_Building_ComputeSlotInput
// ---------------------------------------------------------------------------
int Building_ComputeSlotInput(int building, int slot) {
    ProdBuilding pb = ProdBuildingAt(building);
    SceneTypeDef* td = SceneTypeDefAt(SlotProt(pb, slot));
    u8 kind = td ? td->kind : 0;
    if (kind == 23 || kind == 37)          // *v5 == 23 || == 37
        return 0;

    // Per-slot float factor (flt_13C3B68 == column +0x68, slot+0x08).
    float factor = reinterpret_cast<float&>(pb.slotCol(0x68, slot));

    double v6;
    if (building) {
        v6 = static_cast<double>(pb.inValue()) * static_cast<double>(factor) * kInScale;
    } else {
        // building 0: uses the global flt_641DA8 (player/base factor). We model
        // it as 0 here (the cold-IDB value); the term reduces to the bias.
        v6 = 0.0 * static_cast<double>(factor) * kInScale0 + kInBias0;
    }
    return truncToZero(v6);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x584de8 — VIBE_Building_ComputeSlotOutput
// ---------------------------------------------------------------------------
int Building_ComputeSlotOutput(int building, int slot) {
    ProdBuilding pb = ProdBuildingAt(building);
    if (!pb.slotActive(slot))
        return 0;

    // Sum worker contributions for the active slot(s). The original iterates the
    // Person array; routed through the hook.
    int v12 = g_prodHooks->SlotWorkerOutput(building, slot);

    int v13 = truncToZero(static_cast<double>(v12) * kOutScale);   // dbl_6264BC = 0.5
    if (v13 != 0) {
        // *(float*)(slot+36) / *(float*)(slot+32) * v13. slot+0x20 / slot+0x24:
        float num = pb.slotSmoothIn(slot);   // slot+0x20 (slot+32)
        float den = pb.slotF24(slot);        // slot+0x24 (slot+36)
        if (num == 0.0f)
            return v13;
        double v8 = static_cast<double>(den) / static_cast<double>(num)
                    * static_cast<double>(v13);
        return truncToZero(v8);
    }
    return v13;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x584ec8 — VIBE_Building_ComputeSlotYield
// ---------------------------------------------------------------------------
double Building_ComputeSlotYield(int building, int slot) {
    ProdBuilding pb = ProdBuildingAt(building);
    i16 prot = SlotProt(pb, slot);
    SceneTypeDef* td = SceneTypeDefAt(prot);

    int stored = g_prodHooks->SlotStoredQuantity(building, slot);
    bool hasWork = (stored >= 0);
    if (!hasWork && building != g_playerBuildingIndex)
        return Building_ComputeMarketPrice(prot, 100);

    float v26 = 0.0f;
    if (stored > 0)
        v26 = static_cast<float>(stored);

    // The original v11 points at &dword_13C3B50[1988*b + 4 + 32*slot] (the slot
    // base column band); v11[8]=slot+0x20 capacity, v11[9]=slot+0x24 output,
    // v11[14]=slot+0x38 price.
    float cap   = pb.slotSmoothIn(slot);  // slot+0x20  (v11[8])
    float outv  = pb.slotF24(slot);       // slot+0x24  (v11[9])
    float price = pb.slotYield(slot);     // slot+0x38  (v11[14])

    float v25;
    if (static_cast<double>(v26) <= static_cast<double>(cap) ||
        (td && td->kind == 23)) {
        v25 = 1.0f;
    } else {
        float denom = (v26 >= 1.0f) ? v26 : 1.0f;
        v25 = cap / denom;
    }
    float v27 = (cap != 0.0f) ? (outv / cap) : 0.0f;
    double v12 = static_cast<double>(v27) * static_cast<double>(v25);
    double v20 = (v12 <= kYieldLo) ? 0.25 : v12;
    double v18 = (v20 >= kYieldHi) ? 5.0 : v20;
    float v21 = static_cast<float>(v18);

    float current = pb.slotYield(slot);  // flt_13C3B98[..]
    double mp = Building_ComputeMarketPrice(prot, 100);
    double v28 = (mp * static_cast<double>(v21) - static_cast<double>(price))
                 * static_cast<double>(kYieldStep) + static_cast<double>(current);
    if (current <= 0.0f) {
        if (mp >= static_cast<double>(kYieldMin))
            return static_cast<double>(static_cast<float>(mp));
        return 512.0;
    }
    return v28;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5851fc — VIBE_Building_FindSlotByProt
// ---------------------------------------------------------------------------
i32* Building_FindSlotByProt(int building, i16 prot) {
    ProdBuilding pb = ProdBuildingAt(building);
    for (int k = 0; k < kProdSlotsPerBuilding; ++k) {
        if (SlotProt(pb, k) == prot)
            return &pb.slotFieldC(k);  // &dword_13C3B50[32*k+4+1988*b] band
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x585198 — VIBE_Building_GetSlotYieldByProt
// ---------------------------------------------------------------------------
double Building_GetSlotYieldByProt(int building, i16 prot) {
    ProdBuilding pb = ProdBuildingAt(building);
    for (int k = 0; k < kProdSlotsPerBuilding; ++k) {
        if (SlotProt(pb, k) == prot)
            return static_cast<double>(static_cast<float>(Building_ComputeSlotYield(building, k)));
    }
    return -1.0;
}

// ---------------------------------------------------------------------------
// Curve interpolation helper. Reproduces the input/output interpolation block
// of RunProductionTick (0x5847a0): find the keyframe segment bracketing `now`
// (a packed minute-of-period key) and lerp the value across it.
//   keys[] are (time,value) pairs; the active count is the run of strictly
//   increasing times. `nowKey` = packed time (v5 = v50>>16), `dayByte` = BYTE1.
// ---------------------------------------------------------------------------
static i32 InterpCurve(const ScheduleKey* keys, i32 nowKey, i32 dayByte) {
    // count active keyframes (strictly increasing time run, max 9).
    int n = 0;
    if (keys[0].time < keys[1].time) {
        do { ++n; } while (n < kSchedKeyframes && keys[n].time < keys[n + 1].time);
    }
    if (n >= kSchedKeyframes) n = kSchedKeyframes;

    // locate first keyframe whose time >= nowKey.
    int k = 0;
    while (k < n && nowKey > keys[k].time) ++k;

    if (k >= n - 1)
        return keys[n].value;  // dword_13CD700[2*n + ..] == keys[n].value

    i32 t0 = keys[k].time;
    if (nowKey == t0) {
        // exact key: interpolate within [k, k+1] by the day fraction.
        i32 span = 3 * (keys[k + 1].time - t0);
        i32 dv   = keys[k + 1].value - keys[k].value;
        i32 num  = dayByte * dv;
        return keys[k].value + num / (4 * span);
    }
    if (k <= 0)
        return keys[0].value;
    // interpolate within [k-1, k] using the full minute cursor.
    i32 prevTime = keys[k - 1].time;
    i32 dv   = keys[k].value - keys[k - 1].value;
    i32 span = 3 * (t0 - prevTime);
    i32 mins = 12 * nowKey + dayByte - 12 * prevTime;
    return keys[k - 1].value + dv * mins / (4 * span);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5847a0 — VIBE_Building_RunProductionTick
// ---------------------------------------------------------------------------
void Building_RunProductionTick(int building, i32 nowDay, i32 nowMinute) {
    ProdBuilding pb = ProdBuildingAt(building);
    ProdSchedule* sc = ProdScheduleAt(building);

    // v5 = v50 >> 16 (packed time key); dayByte = BYTE1(v50).
    i32 nowKey  = nowDay;
    i32 dayByte = nowMinute & 0xFF;

    // input curve -> inValue, scaled by inScale (per-mille).
    pb.inValue() = InterpCurve(sc->input, nowKey, dayByte);
    pb.inValue() = pb.inScale() * pb.inValue() / 1000;

    // output curve -> outValue, scaled by outScale.
    pb.outValue() = InterpCurve(sc->output, nowKey, dayByte);
    pb.outValue() = pb.outScale() * pb.outValue() / 1000;

    // refresh every slot.
    for (int slot = 0; slot < kProdSlotsPerBuilding; ++slot) {
        SceneTypeDef* td = SceneTypeDefAt(SlotProt(pb, slot));
        u8 kind = td ? td->kind : 0;

        if (kind != 23 && kind != 37) {
            int in = Building_ComputeSlotInput(building, slot);
            pb.slotSmoothIn(slot) = pb.slotSmoothIn(slot) +
                (static_cast<float>(in) - pb.slotSmoothIn(slot)) * static_cast<float>(kHalf);
        }

        int out;
        if (building == g_playerBuildingIndex)
            out = Building_ComputeSlotOutput(g_playerBuildingIndex, slot);
        else
            out = pb.slotOutBase(slot);
        pb.slotOutComp(slot) = out;

        double yield = Building_ComputeSlotYield(building, slot);
        pb.slotYield(slot) = static_cast<float>(yield);
        if (pb.slotActive(slot))
            pb.slotSmoothIn(slot) = static_cast<float>(pb.slotOutComp(slot));

        pb.slotCust0(slot) = 0;
        pb.slotCust1(slot) = 0;
        pb.slotCust2(slot) = 0;
        pb.slotCust3(slot) = 0;
    }

    if (g_standaloneFlag == -1) {
        g_prodHooks->SyncProductionState(building);
        g_prodHooks->RandomizeStockTransforms();
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x583c3c — VIBE_Building_RecalcAllProduction
// ---------------------------------------------------------------------------
void Building_RecalcAllProduction(i32 nowDay, i32 nowMinute) {
    if (!g_prodSchedules[0].hasProduction)
        return;
    int idx = 0;
    do {
        Building_RunProductionTick(idx, nowDay, nowMinute);
        ++idx;
    } while (idx < 4 && g_prodSchedules[idx].hasProduction);
}

}  // namespace guild::sim
