#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::sim — VIBE_Object_ComputeMarketValue @0x594df0
//
// The building/estate "market value" appraisal. Reconstructed 1:1 from the
// gilde.exe decompile (the x87 accumulation order, the constant tables, the
// integer index math). The handful of record-walking LEAVES it calls
// (Building_SumWorkstationByCategory, Person_FindActiveByEntity,
// Person_FindRecordById, Building_ComputeOutputRatio, Ai_AverageObjectFavorability)
// all already have faithful bodies elsewhere in the tree, but they read deep,
// raw building/person record layouts. To keep ComputeMarketValue's OWN math
// byte-exact and golden-pinnable in isolation (and to match the established
// hooks pattern used across the object/scene modules), the leaf calls are routed
// through MarketValueHooks; the pure appraisal arithmetic is reconstructed here.
//
// THE OBJECT / BUILDING RECORD (recovered byte offsets, all from the raw
// *(type*)(base+off) accesses in the decompile):
//   a1 (the object record `p`):
//     +0x00  (0)    char   buildingType index (589 * type + dword_13CE294 ==
//                          this object's building sale-record `v21`)
//     +0x27  (39)   u16    entity/person id (used for the favorability category
//                          and the fallback person-template lookup)
//     +0x41  (65)   i32    a "level/tier" field; (value - 2) feeds dbl_626B54
//   v21 = 589*type + dword_13CE294  (this object's building sale record):
//     +0x22F (559)  u8     owner/category byte compared against each worker's
//                          *(int*)(rec+354) >> 24
//   each worker person record `RecordById`:
//     +0x162 (354)  i32    packed field; (>> 24, arithmetic) gives the category
//                          byte compared against v21+559
//   Person template fallback table: word_12CE910[268 * (a1+39)] (stride 268
//   *int16* == 536 bytes). +2 (byte) == "kind" (6 or 7 => the +129 size term);
//   +0x81 (129) (byte) == the size used by the (6|7) branch.
//
// CONSTANTS (get_global_value-verified IEEE-754 doubles at 0x626B34..):
//   dbl_626B34 = 0.01                          (workstation-count weight)
//   dbl_626B3C = 0.003968253968253968 (1/252)  (size -> value scale)
//   dbl_626B44 = 0.25                           (size + favorability weight)
//   dbl_626B4C = -0.5                           (favorability bias)
//   dbl_626B54 = 0.1                            (level weight)
//   dbl_626B5C = 0.3                            (per-matching-worker bonus)
// =============================================================================
namespace guild::sim {

// ---------------------------------------------------------------------------
// MarketValueHooks — the record-walking leaves ComputeMarketValue invokes.
// Each is a faithful boundary onto an already-reconstructed body. A null field
// behaves as the original's "not found / zero" path so the formula is testable
// with synthetic returns. Mirrors the signatures of the real leaves.
// ---------------------------------------------------------------------------
struct MarketValueHooks {
    // 0x5904fc VIBE_Building_SumWorkstationByCategory(a1, type=1, ebx=1)
    //   -> ConvertX-truncated weighted workstation sum for this object.
    int (*sumWorkstationByCategory)(const void* obj, char type, int rounded)
        = nullptr;

    // 0x5920b0 VIBE_Person_FindActiveByEntity(a1) -> active person rec, or null.
    const void* (*personFindActiveByEntity)(const void* obj) = nullptr;

    // The person-template fallback table word_12CE910[268 * (a1+39)] — returns
    // a record pointer for the entity id when personFindActiveByEntity is null.
    const void* (*personTemplateForEntity)(unsigned entityId) = nullptr;

    // 0x58bc6c VIBE_Person_FindRecordById(id) -> worker person rec, or null.
    const void* (*personFindRecordById)(int id) = nullptr;

    // 0x57d384 VIBE_Building_ComputeOutputRatio(rec) -> 0..1 output ratio.
    float (*buildingComputeOutputRatio)(const void* personRec) = nullptr;

    // 0x594928 VIBE_Ai_AverageObjectFavorability(entityId, count, ids)
    //   -> averaged 0..1-ish favorability (0.5 when no valid ids).
    double (*aiAverageObjectFavorability)(unsigned entityId, int count,
                                          const int* ids) = nullptr;

    // Field accessors on the records above (kept explicit so the byte offsets
    // are not assumed by this module; the real wiring supplies them):
    //   activeKind(personRec)       -> *(u8*)(rec+2)
    //   activeSize(personRec)       -> *(u8*)(rec+129)
    //   ownerCategory(objBuildRec)  -> *(u8*)(v21+559)   (v21 = this obj's rec)
    //   workerCategory(personRec)   -> *(int*)(rec+354) >> 24  (arithmetic)
    unsigned char (*activeKind)(const void* personRec) = nullptr;
    unsigned char (*activeSize)(const void* personRec) = nullptr;
    unsigned char (*ownerCategory)(const void* objBuildRec) = nullptr;
    int           (*workerCategory)(const void* personRec) = nullptr;

    // a1+0 (building type), a1+39 (entity id), a1+65 (level) raw fields.
    int          (*objBuildingType)(const void* obj) = nullptr; // *(char*)(a1+0)
    unsigned     (*objEntityId)(const void* obj) = nullptr;     // *(u16*)(a1+39)
    int          (*objLevel)(const void* obj) = nullptr;        // *(int*)(a1+65)
};

// gilde.exe 0x594df0 — VIBE_Object_ComputeMarketValue
//   (__usercall st0=fn(obj@eax, count@edx, ids@ebx)).
//   `obj` is the appraised building/object record, `ids` is a `count`-long array
//   of worker person ids (-1 == empty slot). Returns the appraisal value as a
//   double (the original leaves it in st0; the live caller truncates to float).
//
// The exact reconstructed formula (x87 accumulation order preserved):
//   sumWS  = ConvertX(SumWorkstationByCategory(obj,1,1))     [int]
//   base   = (double)sumWS * 0.01 + 1.0                       [v17]
//   active = FindActiveByEntity(obj) ?: TemplateForEntity(obj+39)
//   size   = (kind==6||7) ? (double)activeSize * (1/252) * 0.25 : 0.0   [v14]
//   v18    = base + size
//   for each id in ids[0..count): if id!=-1 and FindRecordById(id):
//        v22 += ComputeOutputRatio(rec)
//        if ownerCategory(v21) == workerCategory(rec): v23 += 0.3
//        ++matched
//   if matched: inv = 1.0/matched; v15 = v22*inv; v23 = v23*inv
//   v19 = (AverageObjectFavorability(obj+39,count,ids) + (-0.5)) * 0.25 + v18
//   v20 = (double)(objLevel - 2) * 0.1 + v19
//   return (v15 + v23) * v20
double ObjectComputeMarketValue(const void* obj, int count, const int* ids,
                                const MarketValueHooks& hooks);

// Install the process-wide hooks used by the default ObjectComputeMarketValue
// entry the live caller (gui_dialogs8) binds to. Returns the previous hooks.
MarketValueHooks SetMarketValueHooks(const MarketValueHooks& hooks);

// Convenience entry matching the live caller's signature
//   float(const char* p, int n, const int* ids)  (gui_dialogs8 hook slot).
float ObjectComputeMarketValueDefault(const char* obj, int count,
                                      const int* ids);

}  // namespace guild::sim
