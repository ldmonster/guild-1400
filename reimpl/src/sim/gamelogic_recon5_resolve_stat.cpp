#include "sim/gamelogic_recon5_resolve_stat.h"

namespace guild::sim {

// ===========================================================================
// Hooks plumbing (inert defaults) — mirrors command_recon4_resolve.
// ===========================================================================
namespace {

void* DefFindRecordById(i32) { return nullptr; }
i16   DefTableTypeWord(u16) { return -1; }
u8    DefTableAlive(u16) { return 0; }
u8    DefTableKind(u16) { return 0; }
u16   DefRecordSlot(void*) { return 0xFFFF; }
i32   DefComputeTotalWealth(u16) { return 0; }
void* DefQueryByGoodType(const char*) { return nullptr; }
i32   DefPersonAnchorId(void*) { return 0; }
void* DefQueryFindBuilding(i32) { return nullptr; }
i32   DefBuildingChildAnchor(void*) { return 0; }
int   DefBuildingChildCount(void*) { return 0; }
u8    DefChildCategory(void*, int) { return 0; }
i32   DefChildQuantity(void*, int) { return 0; }
i32   DefParseStatPercent(const char*) { return 0; }
i32   DefConvertToDisplayCoord(i32 v, u8) { return v; }
void  DefRenderMessage(char*, const char*, i32, i32) {}

const Recon5StatHooks kDefaults = {
    &DefFindRecordById, &DefTableTypeWord, &DefTableAlive, &DefTableKind,
    &DefRecordSlot, &DefComputeTotalWealth,
    &DefQueryByGoodType, &DefPersonAnchorId, &DefQueryFindBuilding,
    &DefBuildingChildAnchor, &DefBuildingChildCount, &DefChildCategory,
    &DefChildQuantity,
    &DefParseStatPercent, &DefConvertToDisplayCoord, &DefRenderMessage,
};

Recon5StatHooks g_hooks = kDefaults;

} // namespace

void SetRecon5StatHooks(const Recon5StatHooks* h) {
    if (!h) { g_hooks = kDefaults; return; }
    g_hooks = *h;
    if (!g_hooks.findRecordById)        g_hooks.findRecordById        = kDefaults.findRecordById;
    if (!g_hooks.tableTypeWord)         g_hooks.tableTypeWord         = kDefaults.tableTypeWord;
    if (!g_hooks.tableAlive)            g_hooks.tableAlive            = kDefaults.tableAlive;
    if (!g_hooks.tableKind)             g_hooks.tableKind             = kDefaults.tableKind;
    if (!g_hooks.recordSlot)            g_hooks.recordSlot            = kDefaults.recordSlot;
    if (!g_hooks.computeTotalWealth)    g_hooks.computeTotalWealth    = kDefaults.computeTotalWealth;
    if (!g_hooks.queryByGoodType)       g_hooks.queryByGoodType       = kDefaults.queryByGoodType;
    if (!g_hooks.personAnchorId)        g_hooks.personAnchorId        = kDefaults.personAnchorId;
    if (!g_hooks.queryFindBuilding)     g_hooks.queryFindBuilding     = kDefaults.queryFindBuilding;
    if (!g_hooks.buildingChildAnchor)   g_hooks.buildingChildAnchor   = kDefaults.buildingChildAnchor;
    if (!g_hooks.buildingChildCount)    g_hooks.buildingChildCount    = kDefaults.buildingChildCount;
    if (!g_hooks.childCategory)         g_hooks.childCategory         = kDefaults.childCategory;
    if (!g_hooks.childQuantity)         g_hooks.childQuantity         = kDefaults.childQuantity;
    if (!g_hooks.parseStatPercent)      g_hooks.parseStatPercent      = kDefaults.parseStatPercent;
    if (!g_hooks.convertToDisplayCoord) g_hooks.convertToDisplayCoord = kDefaults.convertToDisplayCoord;
    if (!g_hooks.renderMessage)         g_hooks.renderMessage         = kDefaults.renderMessage;
}
const Recon5StatHooks& GetRecon5StatHooks() { return g_hooks; }

// ===========================================================================
// Exact image constants.
//   flt_6207A8 == flt_6207AC == 0x3c23d70a == 0.01f  (percent scale)
//   byte_6477A1 == 0 (image value) — the render flag passed to ConvertToDisplay
// ===========================================================================
namespace {
constexpr f32 kPercentScaleSelected = 0.01f; // flt_6207A8
constexpr f32 kPercentScaleBuilding = 0.01f; // flt_6207AC
constexpr u8  kRenderFlag6477A1     = 0;     // byte_6477A1

// param slot: a2 + 8*index + 4 read as a 32-bit id (same as recon4).
inline i32 readParamId(u8* params, int index) {
    i32 v;
    const u8* p = params + 8 * index + 4;
    v = (i32)((u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24));
    return v;
}
} // namespace

// ===========================================================================
// gilde.exe 0x4fa290 — VIBE_Command_ResolveTargetSelectedStat
// ===========================================================================
int ResolveTargetSelectedStat(u8 kind, u8* params, const char* name, int index, char* out) {
    if (kind != 2) return 0;            // a1 != 2
    if (!params)   return 0;            // !a2
    if (index == 8) return 0;           // a4 == 8

    void* recBase = g_hooks.findRecordById(readParamId(params, index));
    if (!recBase) return 0;             // !RecordById

    u16 slot = g_hooks.recordSlot(recBase);            // v8 = *RecordById
    if (g_hooks.tableTypeWord(slot) == -1) return 0;   // word_12CE910[268*slot]==-1
    if (!g_hooks.tableAlive(slot))         return 0;   // !byte_12CE918[536*slot]
    if (g_hooks.tableKind(slot) >= 10)     return 0;   // byte_12CE912[536*slot]>=10

    i32 wealth = g_hooks.computeTotalWealth(slot);     // v9 = ComputeTotalWealth
    i32 parsed = g_hooks.parseStatPercent(name);       // StripNameTokens + ParseInt

    // v14 = (double)parsed * 0.01f * (double)wealth — EXACT op order.
    double v14 = (double)parsed * (double)kPercentScaleSelected * (double)wealth;
    i32 statVal = (i32)v14;                            // v19 = (int)v14

    i32 coord = g_hooks.convertToDisplayCoord(statVal, kRenderFlag6477A1);
    g_hooks.renderMessage(out, name, coord, (i32)kRenderFlag6477A1);
    return 1;
}

// ===========================================================================
// gilde.exe 0x4fa3bc — VIBE_Command_ResolveTargetBuildingStat
// ===========================================================================
int ResolveTargetBuildingStat(const char* name, char* out) {
    void* person = g_hooks.queryByGoodType(name);   // QueryByGoodType(1, this)
    if (!person) return 0;                            // result == 0

    i32 anchor = g_hooks.personAnchorId(person);      // *(result+93)
    void* building = g_hooks.queryFindBuilding(anchor); // QueryFind(..,2,6,..,277)
    if (!building) return 0;                          // result == 0

    // Iterate child objects; v17 starts as the default (the v4 init), updated to
    // the chosen child's quantity (*(i+7)) only if a category-9 child is found.
    i32 v17 = 0;                                      // v17 = v4 (uninit -> 0)
    int n = g_hooks.buildingChildCount(building);
    int found = -1;
    for (int ci = 0; ci < n; ++ci) {
        if (g_hooks.childCategory(building, ci) == 9) { found = ci; break; }
    }
    if (found >= 0)                                   // if ( i )
        v17 = g_hooks.childQuantity(building, found); // v17 = *(i+7)

    i32 parsed = g_hooks.parseStatPercent(name);      // StripNameTokens + ParseInt

    // v10 = (double)parsed * 0.01f * (double)v17 — EXACT op order.
    double v10 = (double)parsed * (double)kPercentScaleBuilding * (double)v17;
    i32 statVal = (i32)v10;                           // v17 = (int)v10

    i32 coord = g_hooks.convertToDisplayCoord(statVal, kRenderFlag6477A1);
    g_hooks.renderMessage(out, name, coord, (i32)kRenderFlag6477A1);
    return 1;
}

} // namespace guild::sim
