#include "world/law_text.h"

#include <cstring>

#include "util/math_random.h"
#include "world/law.h"   // g_lawTable (unk_631E98), kLawCount, LawRecord

// Faithful 1:1 port of the dword_4C1810 law-type text-id accessors
// (gilde.exe 0x4c2008..0x4c20e3, plus the severity check at 0x4c3978).
//
// The original addresses a 51 x 40-byte static table (dword_4C1810) and only ever
// reads field +4 of each record (a one-byte "variant" flag). We carry just that
// recovered column (kLawTypeVariantFlag) since nothing else is touched. The
// record-existence null guards in the originals (`if (v3)`, `if (&record)`) can
// never fail for a static array and are preserved as the in-range test only.

namespace guild::world {

// Recovered verbatim from dword_4C1810 (get_bytes 0x4C1810, 51 records x 40 B,
// field +4 of each record).
const u8 kLawTypeVariantFlag[kLawTypeCount] = {
    1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,
};

// gilde.exe 0x4c2008 — VIBE_Law_GetBaseTextId.
int LawGetBaseTextId() {
    return 51; // return 51;
}

// gilde.exe 0x4c2010 — VIBE_Law_WaitForChange  (__usercall, ax = current@ax).
int LawWaitForChange(u16 current) {
    int result;
    do {
        result = util::RandomModulo(0x33u); // VIBE_Math_RandomModulo(0x33u)
    } while (static_cast<u16>(result) == current);
    return result;
}

// gilde.exe 0x4c2034 — VIBE_Law_GetTextIdForType  (__usercall, ax = type@ax).
int LawGetTextIdForType(u16 type) {
    int fallback = 6662; // v1 = 6662
    // Original: `a1 < 0x33u && &dword_4C1810[10*a1]` — the address is always
    // non-null for a static array, so the in-range test alone decides.
    if (type < 0x33u)
        return util::RandomModulo(0xAu); // VIBE_Math_RandomModulo(0xAu)
    return fallback;
}

// gilde.exe 0x4c2070 — VIBE_Law_GetVariantTextId
//   (__usercall, ax = type@ax, edx = variant, ebx = fallback).
int LawGetVariantTextId(u16 type, int variant, int fallback) {
    if (static_cast<u16>(variant) >= 9u)
        variant = 9;
    else
        variant = static_cast<u16>(variant);
    if (type >= 0x33u)
        return fallback;
    // `v3 = &dword_4C1810[10*type]; if (!v3) return fallback;` — never null.
    // `*((_BYTE*)v3 + 4)` is the record's +4 variant flag.
    if (!kLawTypeVariantFlag[type])
        return variant + 6662;
    return variant + 6672;
}

// gilde.exe 0x4c20c8 — VIBE_Law_GetRecordPtr  (__usercall, ax = type@ax).
int LawGetRecordIndex(u16 type) {
    if (type < 0x33u)
        return static_cast<int>(type); // &dword_4C1810[10*type] -> record index
    return -1;                         // else return 0 (null ptr) -> -1 here
}

// gilde.exe 0x4c3978 — VIBE_Law_CheckSeverityAllowed  (__usercall, al=packed, dl=op).
bool LawCheckSeverityAllowed(u8 packed, char op) {
    u8 severity = static_cast<u8>(packed >> 4); // v2 = a1 >> 4
    bool result = false;                        // result = 0
    switch (op) {
        case 1:
            result = severity < 3u; // result = v2 < 3u
            break;
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            return result; // falls through to `return result` (== false)
        default:
            result = false;
            break;
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x4c2dc4 — VIBE_Gesetz_BuildPenaltyText.
// ===========================================================================
namespace {
void DefaultPenaltyRender(char* /*dest*/, int /*fmt*/, int /*prompt*/,
                          int /*arg*/, void* /*ctx*/) {}
PenaltyRenderFn g_penaltyRenderFn = &DefaultPenaltyRender;
void*           g_penaltyRenderCtx = nullptr;
} // namespace

void GesetzSetPenaltyRenderFn(PenaltyRenderFn fn, void* ctx) {
    g_penaltyRenderFn = fn ? fn : &DefaultPenaltyRender;
    g_penaltyRenderCtx = ctx;
}

PenaltyText GesetzBuildPenaltyText(char* dest, u8 lawId, int threshold) {
    PenaltyText out;
    if (lawId >= 26) // if (a2 >= 26) return 0;
        return out;

    // v5[0] = a3 (threshold); qmemcpy(&v5[1], record, 0x24).
    // The buffer is exactly the original's stack frame: a3 followed by the 36-byte
    // record. We mirror it byte-for-byte so the unaligned reads below match.
    u8 v5[40];
    std::memcpy(v5, &threshold, 4);                      // v5[0] = a3
    std::memcpy(v5 + 4, &g_lawTable[lawId], kLawStride); // qmemcpy(&v5[1], rec, 0x24)

    // record byte +1 == BYTE1(v5[1]) == v5 buffer byte 5 (the subcategory selector).
    u8 subcat = v5[5];
    // v5[7] == record dword[6] == record bytes 24..27 == LawRecord::threshold field.
    int v7;
    std::memcpy(&v7, v5 + 28, 4);

    int v3; // the per-subcategory text-id argument
    switch (subcat) {
        case 0: v3 = v7; break;
        case 1: v3 = v7 + 4113; break;
        case 2: v3 = v7 + 4122; break;
        case 3: v3 = v7 + 4128; break;
        default:
            return out; // result = 0
    }

    // category = *(_DWORD*)((char*)v5 + 1) >> 24 == top byte of bytes[1..4] of the
    // buffer == record byte +0 (the law-id byte).
    u32 spanning;
    std::memcpy(&spanning, v5 + 1, 4);
    int category = static_cast<int>(spanning >> 24);

    out.formatTextId = 5 * category + 4146;
    out.promptTextId = 5 * category + 4145;
    out.argTextId    = v3;
    out.valid        = true;
    g_penaltyRenderFn(dest, out.formatTextId, out.promptTextId, out.argTextId,
                      g_penaltyRenderCtx);
    return out;
}

} // namespace guild::world
