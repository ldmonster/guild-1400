#include "gui/tooltip_build.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace guild::gui {

namespace {

// VIBE_Util_StrToUpper @0x5e9f50 — ASCII upper-case in place (local faithful clone; the
// io-module copy lives in a different namespace, so this is not an ODR redefinition).
void StrToUpperLocal(char* s) {
    for (; *s; ++s) {
        char c = *s;
        if (c >= 'a' && c <= 'z')
            *s = static_cast<char>(c - 32);
    }
}

} // namespace

// gilde.exe 0x4f73f0 — colour palette qmemcpy'd into the building tooltip (7 dwords).
// Recovered byte-exact via get_bytes(0x4F73F0,28):
//   00 00 00 00 | 49 00 00 00 | 49 49 00 00 | 49 49 49 00
//   49 56 00 00 | 56 00 00 00 | 56 49 00 00
const u32 kBuildingColors[kBuildingColorCount] = {
    0x00000000u, 0x00000049u, 0x00004949u, 0x00494949u,
    0x00005649u, 0x00000056u, 0x00004956u,
};

// gilde.exe 0x4f83e8 (key-construction core) — copy the contact name, upper-case it,
// and format "_HILFE_%s+0". The original copies the name into a 140-byte buffer, calls
// VIBE_Util_StrToUpper, then VIBE_Crt_Sprintf_0(buf, "_HILFE_%s+0", name).
int Tooltip_BuildContactKey(const char* name, char* out, std::size_t outCap) {
    char upper[140];
    std::size_t i = 0;
    // The decompile's two-byte copy loop is just a NUL-terminated string copy bounded by
    // the 140-byte stack buffer; reproduce it as such.
    if (name) {
        for (; name[i] && i + 1 < sizeof(upper); ++i)
            upper[i] = name[i];
    }
    upper[i] = '\0';
    StrToUpperLocal(upper);
    if (!out || outCap == 0)
        return static_cast<int>(std::strlen("_HILFE_") + std::strlen(upper) + std::strlen("+0"));
    int n = std::snprintf(out, outCap, "_HILFE_%s+0", upper);
    return n;
}

// gilde.exe 0x4f83e8 — resolve a contact tooltip. Builds the key, looks it up via the
// (mockable) text-array finder, and reports whether the help text exists. When found the
// builder renders "$Z$[%s$]" from the resolved index and the following entry.
ContactTooltip Tooltip_ResolveContact(const char* name,
                                      int (*findIndex)(const char* key)) {
    ContactTooltip r;
    char key[160];
    Tooltip_BuildContactKey(name, key, sizeof(key));
    int idx = findIndex ? findIndex(key) : -1;
    if (idx != -1) {
        r.hasText = true;
        r.textIndex = idx;
    }
    return r;
}

// gilde.exe 0x4f78e4 — building tooltip layout values. Disasm-verified (DISASM > Hex-Rays):
//   4f78fb movsx esi, al            -> code is a SIGNED 8-bit (al); record/text use (i8)code
//   4f793e mov al,[ebp+247h]        -> selector = *(u8*)(record + 583)
//   4f7944 add eax,esp / 4f794b push -> v10[sel] read UNCONDITIONALLY (no bounds check)
//   4f794c..4f795f 14*esi + 0x436   -> nameTextId = 14*(i8)code + 1078       (id 0x27)
//   4f7986 VIBE_Object_AddToWindow(dword_62D230,0) -> icon uses a GLOBAL handle, NOT code+1010
//   4f798b var_1C(=14*code) + 0x437 -> descTextId = 14*(i8)code + 1079       (RichString)
//   4f79ab mov dl,[ebp+247h]        -> colour again                          (id 0x2A)
//   4f79e2 VIBE_Building_ComputeSalePrice                                    (id 0x28, deferred)
//   4f79f2 mov edx,[ebp+243h]       -> *(record + 579)                       (id 0x29)
BuildingTooltipLayout Tooltip_BuildingLayout(const u8* record, int code, int salePrice) {
    BuildingTooltipLayout l{};
    // 14 * (signed char)code — the binary receives `al` and sign-extends it (movsx).
    int sc = static_cast<int>(static_cast<signed char>(code));
    u32 color = 0;
    if (record) {
        unsigned sel = record[583];          // *(unsigned __int8 *)(v2 + 583)
        // The binary indexes v10[sel] with no bounds check (4f7944 add eax,esp). Real
        // records always carry sel in [0,7); we clamp the out-of-palette case (which the
        // original leaves as an adjacent-stack over-read / C++ UB) to 0 rather than read
        // out of bounds. Identical for every valid input.
        if (sel < static_cast<unsigned>(kBuildingColorCount))
            color = kBuildingColors[sel];
    }
    l.titleColor   = color;
    l.nameTextId   = 14 * sc + 1078;          // 14*(i8)code + 1078   (4f795a add eax,436h)
    l.descTextId   = 14 * sc + 1079;          // 14*(i8)code + 1079   (4f798f add eax,437h)
    l.descColor    = static_cast<int>(color);
    l.salePrice    = salePrice;
    // id 0x29: *(record + 579) — an unaligned dword (offset % 4 == 3). The original does
    // an unaligned x86 load; read via memcpy (byte-identical) to avoid the alignment UB.
    std::int32_t extra = 0;
    if (record) std::memcpy(&extra, record + 579, sizeof(extra));
    l.extraField   = extra;
    return l;
}

// gilde.exe 0x4f8154 — VIBE_Tooltip_BuildUpgrade early-out: class byte == 29 -> return -1.
// Disasm: 4f815c movsx edx,ax / shl eax,6 / add edx,eax -> index = 65*(i16)code; the
// argument arrives in `ax`, so it is sign-extended from 16 bits (not 8, not 32).
bool Tooltip_UpgradeApplies(const u8* objectBase, int objectCode) {
    if (!objectBase)
        return false;
    int idx = kObjectStride * static_cast<i16>(objectCode); // 65*(i16)code, dword_13CE27C base
    u8 cls = objectBase[idx];                               // *(_BYTE*)(65*(i16)code + base)
    return cls != kUpgradeSkipClass;
}

} // namespace guild::gui
