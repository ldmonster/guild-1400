#include "sim/object_value.h"

// VIBE_Object_ComputeMarketValue @0x594df0 — 1:1 reconstruction of the building
// appraisal. See object_value.h for the record layout + constant provenance.
//
// Disasm cross-check (0x594df0): the function builds the object's own building
// sale record `v21 = 589 * *(char*)a1 + dword_13CE294` (var_24), then:
//   594e3e  SumWorkstationByCategory(a1, 1, 1)  -> eax        (v17 numerator)
//   594e47  v17 = (double)eax * 0.01 + 1.0                    (fild/fmul/fld1/fadd)
//   594e5f  FindActiveByEntity(a1); if null -> template[a1+39]
//   594e88  kind = *(u8*)(rec+2); if kind==6||7:
//   594e9c    v14 = (double)*(u8*)(rec+129) * (1/252) * 0.25  (fild word/fmul/fmul)
//            else v14 = 0.0
//   594eb4  v18 = v17 + v14
//   594ec4  loop ids[0..count): skip -1; FindRecordById(id) gate:
//   594f87    v22 += ComputeOutputRatio(rec)                  (fadd var_20)
//   594fa5    if *(u8*)(v21+559) == (*(int*)(rec+354) >> 24): v23 += 0.3
//             ++matched
//   594ee1  if matched: inv = 1.0/matched; v15 = v22*inv; v23 = v23*inv
//   594f13  AverageObjectFavorability(*(u16*)(a1+39), count, ids)
//   594f1c  v19 = (fav + (-0.5)) * 0.25 + v18
//   594f3a  v20 = (double)(*(int*)(a1+65) - 2) * 0.1 + v19
//   594f54  return (v15 + v23) * v20
//
// The x87 chain keeps 80-bit intermediates; we model them as `double` (the
// established convention for this tree — see util/matrix.cpp). All float stores
// to var_28/var_30/var_1C/var_20 narrow to 32-bit `float`, so those are kept as
// `float` here to reproduce the rounding the original applied at each spill.

namespace guild::sim {

// get_global_value-verified IEEE-754 doubles (0x626B34..0x626B5C).
namespace {
constexpr double kWsWeight    = 0.01;                 // dbl_626B34
constexpr double kSizeScale   = 0.003968253968253968;// dbl_626B3C (1/252)
constexpr double kSizeFavWt   = 0.25;                 // dbl_626B44
constexpr double kFavBias     = -0.5;                 // dbl_626B4C
constexpr double kLevelWeight = 0.1;                  // dbl_626B54
constexpr double kMatchBonus  = 0.3;                  // dbl_626B5C

MarketValueHooks g_hooks{};
}  // namespace

double ObjectComputeMarketValue(const void* obj, int count, const int* ids,
                                const MarketValueHooks& h) {
    // --- v17: workstation-count base term --------------------------------- //
    // 594e43: var_18 = SumWorkstationByCategory(a1, 1, 1) [already ConvertX'd
    // inside the leaf when its 3rd arg is non-zero]; v17 = (double)var_18*0.01+1.
    int sumWs = h.sumWorkstationByCategory
                    ? h.sumWorkstationByCategory(obj, /*type=*/1, /*rounded=*/1)
                    : 0;                                        // 594e3e
    float v17 = static_cast<float>(static_cast<double>(sumWs) * kWsWeight + 1.0);
    //          ^ fstp var_28 narrows to float                  // 594e5b

    // --- active person record -> size term v14 ---------------------------- //
    const void* active = h.personFindActiveByEntity
                             ? h.personFindActiveByEntity(obj)
                             : nullptr;                         // 594e5f
    if (!active) {
        unsigned eid = h.objEntityId ? h.objEntityId(obj) : 0;  // *(u16*)(a1+39)
        active = h.personTemplateForEntity
                     ? h.personTemplateForEntity(eid)
                     : nullptr;                                  // 594e7e
    }
    unsigned char kind = (active && h.activeKind) ? h.activeKind(active) : 0;
    float v14;                                                  // var_34
    if (kind == 6 || kind == 7) {                               // 594e8b / 594f64
        unsigned char size =
            (active && h.activeSize) ? h.activeSize(active) : 0;
        // fild WORD: signed 16-bit load of the byte-wide size (zero-extended by
        // the prior `xor eax,eax; mov al,..`), then *1/252 * 0.25.
        v14 = static_cast<float>(static_cast<double>(static_cast<short>(size)) *
                                 kSizeScale * kSizeFavWt);       // 594ea0
    } else {
        v14 = 0.0f;                                             // 594f6f
    }

    float v18 = static_cast<float>(static_cast<double>(v17) +
                                   static_cast<double>(v14));    // 594eb8

    // --- per-worker accumulation ----------------------------------------- //
    float v20accum = 0.0f;  // var_20 (sum of ComputeOutputRatio) == v22
    float v23 = 0.0f;       // var_1C (matching-owner bonus)       == v23
    int matched = 0;        // esi (v4)
    for (int i = 0; i < count; ++i) {                           // 594ec2..594edf
        int id = ids[i];                                       // 594ece
        if (id == -1)                                          // 594ed0
            continue;
        const void* rec =
            h.personFindRecordById ? h.personFindRecordById(id) : nullptr; // 594f78
        if (!rec)                                              // 594f7f -> skip
            continue;
        float ratio = h.buildingComputeOutputRatio
                          ? h.buildingComputeOutputRatio(rec)
                          : 0.0f;                                // 594f87
        v20accum = static_cast<float>(static_cast<double>(v20accum) +
                                      static_cast<double>(ratio)); // 594f94/fstp var_20
        unsigned char owner = h.ownerCategory ? h.ownerCategory(obj) : 0; // v21+559
        int worker = h.workerCategory ? h.workerCategory(rec) : 0;        // rec+354>>24
        if (static_cast<int>(owner) == worker)                 // 594fa5
            v23 = static_cast<float>(static_cast<double>(v23) + kMatchBonus); // 594fb7
        ++matched;                                             // 594fbb
    }

    float v15 = 0.0f;       // var_30
    if (matched) {                                             // 594ee1
        double inv = 1.0 / static_cast<double>(matched);       // 594ee9 fild/fld1/fdivrp
        v15 = static_cast<float>(static_cast<double>(v20accum) * inv); // 594ef5 fstp var_30
        v23 = static_cast<float>(inv * static_cast<double>(v23));      // 594efb fstp var_1C
    }

    // --- favorability + level terms -------------------------------------- //
    unsigned eid = h.objEntityId ? h.objEntityId(obj) : 0;     // *(u16*)(a1+39) 594f0d
    double fav = h.aiAverageObjectFavorability
                     ? h.aiAverageObjectFavorability(eid, count, ids)
                     : 0.5;                                     // 594f13
    float v19 = static_cast<float>((fav + kFavBias) * kSizeFavWt +
                                   static_cast<double>(v18));   // 594f1c..594f36

    int level = h.objLevel ? h.objLevel(obj) : 0;              // *(int*)(a1+65) 594f22
    float v20 = static_cast<float>(
        static_cast<double>(level - 2) * kLevelWeight +
        static_cast<double>(v19));                             // 594f3a..594f48

    // return (v15 + v23) * v20                                   594f4c..594f58
    return (static_cast<double>(v15) + static_cast<double>(v23)) *
           static_cast<double>(v20);
}

MarketValueHooks SetMarketValueHooks(const MarketValueHooks& hooks) {
    MarketValueHooks prev = g_hooks;
    g_hooks = hooks;
    return prev;
}

float ObjectComputeMarketValueDefault(const char* obj, int count,
                                      const int* ids) {
    // The live caller (gui_dialogs8 / tooltip_content) consumes a float; the
    // original returns the value in st0 and the caller narrows. We narrow here.
    return static_cast<float>(ObjectComputeMarketValue(
        static_cast<const void*>(obj), count, ids, g_hooks));
}

}  // namespace guild::sim
