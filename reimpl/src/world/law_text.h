#pragma once
// Law-type text-id descriptor lookups (gilde.exe dword_4C1810 family). These are
// the small accessors the law-book UI uses to map a law-type index (0..50) to a
// localized text id, plus a couple of tiny rule helpers. Faithful 1:1 port.
//
// dword_4C1810 is a SEPARATE table from the Gesetz law table (unk_631E98, see
// law.h): it is a 51-entry x 40-byte law-TYPE descriptor table. Each record holds
// a type/category dword at +0, a one-byte "variant" flag at +4, then an ASCII
// name string. The accessors below only ever read field +4 (the variant flag)
// and rely on the record's existence (it is a static array, so &record is never
// null — the original's `&dword_4C1810[10*a]` / `if (v3)` null-checks can never
// fail and are preserved structurally).
//
// Translated functions:
//   VIBE_Law_GetBaseTextId        0x4c2008  (the leaf the sim references as a hook)
//   VIBE_Law_WaitForChange        0x4c2010
//   VIBE_Law_GetTextIdForType     0x4c2034
//   VIBE_Law_GetVariantTextId     0x4c2070
//   VIBE_Law_GetRecordPtr         0x4c20c8
//   VIBE_Law_CheckSeverityAllowed 0x4c3978
//   VIBE_Gesetz_BuildPenaltyText  0x4c2dc4  (reads the Gesetz table, law.h)
#include "guild/common/types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// VIBE_Gesetz_BuildPenaltyText (0x4c2dc4) penalty-message text-id maths.
// ---------------------------------------------------------------------------
// The original copies the 36-byte Gesetz record (unk_631E98, law.h: g_lawTable)
// for law `lawId` and renders a localized penalty line via
// VIBE_Text_RenderFormattedMessage @0x59f99c (a GUI text leaf). The leaf is a
// mock hook here; the two text ids + threshold argument it is called with are the
// load-bearing, deterministic result, so the port computes and returns them.
struct PenaltyText {
    bool valid = false;     // result (1 == rendered, 0 == out of range / bad subcat)
    int  formatTextId = 0;  // 5*category + 4146  (first RenderFormattedMessage arg)
    int  promptTextId = 0;  // 5*category + 4145  (second arg)
    int  argTextId = 0;     // v3: threshold + per-subcategory base
};

// The penalty-text render leaf (VIBE_Text_RenderFormattedMessage @0x59f99c).
// dest = scratch buffer; the three computed ids are passed through. Default
// no-op; tests install a recorder.
using PenaltyRenderFn = void (*)(char* dest, int formatTextId, int promptTextId,
                                 int argTextId, void* ctx);
void GesetzSetPenaltyRenderFn(PenaltyRenderFn fn, void* ctx);

// gilde.exe 0x4c2dc4 — VIBE_Gesetz_BuildPenaltyText
//   (__usercall, eax = dest@eax, dl = lawId@dl, edi = threshold@edi).
// For lawId >= 26 returns {valid=false}. Otherwise copies g_lawTable[lawId] and
// switches on the record's subcategory byte (record +1, BYTE1 of dword[0]):
//   0 -> arg = threshold;          1 -> threshold + 4113;
//   2 -> threshold + 4122;         3 -> threshold + 4128;  else -> valid=false.
// The render ids derive from the record's law-id byte (record +0): category is
// that byte; formatTextId = 5*category + 4146, promptTextId = 5*category + 4145.
PenaltyText GesetzBuildPenaltyText(char* dest, u8 lawId, int threshold);

// Number of law-type descriptor records (0x33 == 51), the loop/range bound used
// by every accessor (`a1 < 0x33u`).
constexpr int kLawTypeCount = 51;

// The variant flag (record field +4) for each of the 51 law-type records,
// recovered verbatim from dword_4C1810 via get_bytes. GetVariantTextId branches
// on whether this byte is zero.
extern const u8 kLawTypeVariantFlag[kLawTypeCount];

// gilde.exe 0x4c2008 — VIBE_Law_GetBaseTextId. Returns the constant base text id
// (51) the law-book uses as the first law-type label slot.
int LawGetBaseTextId();

// gilde.exe 0x4c2010 — VIBE_Law_WaitForChange  (__usercall, ax = current@ax).
// Rolls VIBE_Math_RandomModulo(51) until it differs from `current`, then returns
// it (a "pick a different law type than the one shown" helper). The full 32-bit
// result is returned (the loop compares only the low 16 bits against `current`).
int LawWaitForChange(u16 current);

// gilde.exe 0x4c2034 — VIBE_Law_GetTextIdForType  (__usercall, ax = type@ax).
// If `type` is in range (< 51) returns RandomModulo(10) (a random variant text id
// 0..9); otherwise returns the fallback id 6662. (The original's `&record` null
// guard can never fail for a static array, so an in-range type always rolls.)
int LawGetTextIdForType(u16 type);

// gilde.exe 0x4c2070 — VIBE_Law_GetVariantTextId
//   (__usercall, ax = type@ax, edx = variant, ebx = fallback).
// Clamps `variant` to [0,9] (>= 9 -> 9, else its low 16 bits). For an out-of-range
// `type` returns `fallback`. Otherwise returns variant + 6662 when the record's
// +4 flag is zero, or variant + 6672 when it is set.
int LawGetVariantTextId(u16 type, int variant, int fallback);

// gilde.exe 0x4c20c8 — VIBE_Law_GetRecordPtr  (__usercall, ax = type@ax).
// Returns the index of the law-type record for `type` (0..50), or -1 if `type`
// is out of range. (The original returns &dword_4C1810[10*type]; we return the
// record index, which is the portable equivalent. -1 == the original's null.)
int LawGetRecordIndex(u16 type);

// gilde.exe 0x4c3978 — VIBE_Law_CheckSeverityAllowed  (__usercall, al=packed, dl=op).
// Takes the high nibble of `packed` (severity = packed >> 4). For op == 1 the
// severity is allowed iff severity < 3; for op 2..7 it returns false (the
// original falls through with result==0); for any other op it returns false.
bool LawCheckSeverityAllowed(u8 packed, char op);

} // namespace guild::world
