#include "ai/desire_table.h"

namespace guild::ai {

// ===========================================================================
// LookupAttributeIndex  (gilde.exe 0x4794e4)
// ===========================================================================
namespace {

// Local case-insensitive compare matching VIBE_Util_StrCmpNoCase @0x5cb8f0
// (returns 0 when equal). Implemented locally to keep this module self-contained
// and to avoid colliding with the several file-local StrCmpNoCase definitions
// elsewhere in the tree (guild::io / guild::sim / guild::world).
int StrCmpNoCase(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 32);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        ++a;
        ++b;
    }
    unsigned char ca = static_cast<unsigned char>(*a);
    unsigned char cb = static_cast<unsigned char>(*b);
    if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 32);
    if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 32);
    if (ca == cb) return 0;
    return ca < cb ? -1 : 1;
}

// The compare chain order from the binary: each (name,index) pair tested in turn.
struct AttrName { const char* name; i8 index; };
const AttrName kAttributeNames[] = {
    {"APS", 0},               // aAps              @0x61aab8
    {"UNVERSEHRTHEIT", 1},    // aUnversehrtheit
    {"WOHNUNG", 2},           // aWohnung
    {"GELD", 3},              // aGeld
    {"BERUF", 4},             // aBeruf_0
    {"VERGNUEGEN", 5},        // aVergnuegen
    {"ANSEHEN", 6},           // aAnsehen
    {"AMT", 7},               // aAmt
    {"BILDUNG", 8},           // aBildung_0
    {"RECHTSCHAFFENHEIT", 9}, // aRechtschaffenh
    {"GEMEINHEIT", 10},       // aGemeinheit
    {"SICHERHEIT", 11},       // aSicherheit
    {"FORTPFLANZUNG", 12},    // aFortpflanzung
    {"TRAEGHEIT", 13},        // aTraegheit
};

} // namespace

// gilde.exe 0x4794e4 — VIBE_AiNeeds_LookupAttributeIndex
i8 LookupAttributeIndex(const char* name) {
    for (const AttrName& a : kAttributeNames) {
        if (StrCmpNoCase(name, a.name) == 0)
            return a.index;
    }
    // VIBE_Crt_Sprintf_0(scratch, byte_61AB4C, name): debug "Unbekanntes
    // Beduerfnis: %s" — no observable sim effect, so only the -1 result is kept.
    return -1;
}

// ===========================================================================
// ComputeWeights  (gilde.exe 0x47936c)
// ===========================================================================
namespace {

// Recovered goods table dword_6496A9 @0x6496A9 (23 rows, 21-byte / 0x15 stride).
// Per row: a category id (the dword's high byte, compared to plan.categoryId) plus
// 8 good-id words. The high byte of the dword is the table key; the 8 words begin
// at +4 (word_6496AD). All recovered byte-for-byte via get_bytes.
struct GoodsRow {
    u8  categoryId;
    u16 goods[8];
};
const GoodsRow kGoodsTable[kDesireGoodsRows] = {
    {0x29, {0x000, 0x000, 0x1d4, 0x155, 0x156, 0x000, 0x000, 0x000}},
    {0x2a, {0x000, 0x000, 0x1d4, 0x155, 0x156, 0x158, 0x157, 0x000}},
    {0x2b, {0x000, 0x000, 0x1d4, 0x000, 0x155, 0x156, 0x158, 0x157}},
    {0x32, {0x1d5, 0x000, 0x000, 0x15b, 0x15c, 0x000, 0x000, 0x000}},
    {0x33, {0x1d5, 0x000, 0x15b, 0x15c, 0x15d, 0x15e, 0x000, 0x000}},
    {0x34, {0x1d5, 0x000, 0x15b, 0x15c, 0x15d, 0x15e, 0x160, 0x15f}},
    {0x21, {0x1d7, 0x000, 0x000, 0x000, 0x16d, 0x16e, 0x000, 0x000}},
    {0x22, {0x1d7, 0x000, 0x000, 0x000, 0x16d, 0x16e, 0x16f, 0x170}},
    {0x23, {0x1d7, 0x000, 0x000, 0x000, 0x16d, 0x16e, 0x16f, 0x170}},
    {0x17, {0x1d8, 0x000, 0x000, 0x173, 0x174, 0x000, 0x000, 0x000}},
    {0x18, {0x1d8, 0x000, 0x000, 0x173, 0x174, 0x175, 0x177, 0x000}},
    {0x19, {0x1d8, 0x000, 0x000, 0x173, 0x174, 0x175, 0x177, 0x178}},
    {0x2f, {0x1d6, 0x000, 0x000, 0x161, 0x162, 0x000, 0x000, 0x000}},
    {0x30, {0x1d6, 0x000, 0x000, 0x161, 0x162, 0x163, 0x164, 0x000}},
    {0x31, {0x1d6, 0x000, 0x000, 0x161, 0x162, 0x163, 0x164, 0x165}},
    {0x35, {0x000, 0x000, 0x000, 0x000, 0x1d9, 0x179, 0x17a, 0x000}},
    {0x36, {0x000, 0x000, 0x000, 0x1d9, 0x179, 0x17a, 0x17b, 0x17c}},
    {0x37, {0x000, 0x000, 0x000, 0x1d9, 0x179, 0x17a, 0x17b, 0x17c}},
    {0x14, {0x1da, 0x000, 0x000, 0x000, 0x167, 0x168, 0x000, 0x000}},
    {0x15, {0x1da, 0x000, 0x000, 0x000, 0x167, 0x168, 0x169, 0x16a}},
    {0x16, {0x1da, 0x000, 0x000, 0x000, 0x167, 0x168, 0x169, 0x16a}},
    {0x1f, {0x000, 0x000, 0x000, 0x000, 0x1cd, 0x000, 0x000, 0x000}},
    {0x00, {0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000}},
};

} // namespace

// gilde.exe 0x47936c — VIBE_AiNeeds_ComputeWeights
int ComputeWeights(DesirePlan& plan, DesirePriceEnv& env) {
    // for (i=0,v2=0; i<483; i+=21) if (row[i].key == *a1) break; else ++v2;
    int rowIdx = 0;
    for (; rowIdx < kDesireGoodsRows; ++rowIdx) {
        if (kGoodsTable[rowIdx].categoryId == plan.categoryId)
            break;
    }
    if (rowIdx >= kDesireGoodsRows) // (v2 >= 23) -> not found
        return 0;

    // Copy the row's non-zero good-ids into the plan's good list (packed), count
    // them in goodCount.
    const GoodsRow& row = kGoodsTable[rowIdx];
    int count = 0;
    for (int k = 0; k < 8; ++k) {
        u16 g = row.goods[k];
        if (g) {
            plan.goodIds[count] = g; // *(WORD*)&a1[2*count + 8] = g
            ++count;
        }
    }
    plan.goodCount = count; // *((DWORD*)a1+1) = v4
    if (!count)             // if (!v4) return v7 ^ v6  (== 0)
        return 0;

    plan.maxRatioSlot = 0; // *((DWORD*)a1+38) = 0  (a1+0x98)
    plan.minRatioSlot = 0; // *((DWORD*)a1+39) = 0  (a1+0x9C)

    // Per-good loop: ratio = cachedPrice / basePrice; track sum, max slot, min slot.
    float sum = 0.0f; // v17 / var_1C
    for (int i = 0; i < count; ++i) {
        u16 g = plan.goodIds[i]; // HIWORD(*(DWORD*)(edi+6)) with edi += 2

        // cached = (int)ConvertX(LookupCachedMarketPrice(g, byte_6477A1));
        double cached = env.CachedMarketPrice(g);
        plan.cachedPrice[i] = static_cast<i32>(cached);   // [ecx+44h]

        // base = (int)ConvertX(ComputeMarketPrice(g, 100));
        double base = env.BaseMarketPrice(g);
        plan.basePrice[i] = static_cast<i32>(base);       // [ecx+1Ch]

        // ratio = (double)cachedPrice / (double)basePrice  (fild/fild/fdivp).
        float ratio = static_cast<float>(static_cast<double>(plan.cachedPrice[i])
                                       / static_cast<double>(plan.basePrice[i]));
        plan.ratio[i] = ratio; // [ecx+6Ch]

        sum += ratio; // var_1C += ratio

        // if (ratio > ratio[maxRatioSlot]) maxRatioSlot = i;  (fcomp / jbe)
        if (ratio > plan.ratio[plan.maxRatioSlot])
            plan.maxRatioSlot = i;
        // if (ratio < ratio[minRatioSlot]) minRatioSlot = i;  (fcomp / jnb)
        if (ratio < plan.ratio[plan.minRatioSlot])
            plan.minRatioSlot = i;
    }

    // avg = sum / count  (fild count / fdivr var_1C / fstp [ebx+94h]).
    plan.avgRatio = static_cast<float>(static_cast<double>(sum)
                                     / static_cast<double>(count));
    return 1;
}

// ===========================================================================
// Intrigue NpcAction "perform" wrappers.
// ===========================================================================
namespace {
UseBackCmdFn g_useBackCmd = nullptr;
} // namespace

void SetUseBackCmdHook(UseBackCmdFn fn) { g_useBackCmd = fn; }

// gilde.exe 0x471840 — VIBE_NpcAction_PerformEnterBuilding
u8 PerformEnterBuilding(bool execThreatenOk, u8 rejectCode) {
    return execThreatenOk ? 40 : rejectCode;
}

// gilde.exe 0x471dfc — VIBE_NpcAction_PerformOpenDoorLarge
u8 PerformOpenDoorLarge(bool slanderFormatted, u8 rejectCode) {
    return slanderFormatted ? 43 : rejectCode;
}

// gilde.exe 0x471f24 — VIBE_NpcAction_PerformOpenDoorSmall
u8 PerformOpenDoorSmall(bool slanderFormatted, u8 rejectCode) {
    return slanderFormatted ? 44 : rejectCode;
}

// gilde.exe 0x471cb4 — VIBE_NpcAction_PerformUseBack
u8 PerformUseBack(bool execSlanderOk, i32 targetEntityId) {
    if (!execSlanderOk)
        return 0;
    // VIBE_Command_QueueRequestArgs25(*(_DWORD*)(actor+4), 484, 512, 4, 0).
    if (g_useBackCmd)
        g_useBackCmd(targetEntityId);
    return 42;
}

} // namespace guild::ai
