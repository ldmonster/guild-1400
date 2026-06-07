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

// gilde.exe 0x4f78e4 — building tooltip layout values.
//   v9    = &v10[record[+583]]            -> kBuildingColors[record[583]]  (id 39)
//   id 39 subject : VIBE_Text_RenderRichString(39, 14*code + 1078, color)
//   icon          : VIBE_Object_AddToWindow(..., code + 1010)
//   id 42         : color again
//   id 40         : VIBE_Building_ComputeSalePrice(code)   (deferred -> salePrice arg)
//   id 41         : *(record + 579)
BuildingTooltipLayout Tooltip_BuildingLayout(const u8* record, int code, int salePrice) {
    BuildingTooltipLayout l{};
    u32 color = 0;
    if (record) {
        unsigned sel = record[583];          // *(unsigned __int8 *)(v2 + 583)
        if (sel < static_cast<unsigned>(kBuildingColorCount))
            color = kBuildingColors[sel];
        else
            color = 0;                        // out of palette -> default (faithful guard)
    }
    l.titleColor   = color;
    l.nameTextId   = 14 * code + 1078;        // 14 * v1 + 1078
    l.iconObjectId = code + 1010;             // v1 + 1010
    l.descColor    = static_cast<int>(color);
    l.salePrice    = salePrice;
    l.extraField   = record ? *reinterpret_cast<const std::int32_t*>(record + 579) : 0;
    return l;
}

// gilde.exe 0x4f8154 — VIBE_Tooltip_BuildUpgrade early-out: class byte == 29 -> skip.
bool Tooltip_UpgradeApplies(const u8* objectBase, int objectCode) {
    if (!objectBase)
        return false;
    u8 cls = objectBase[kObjectStride * objectCode]; // *(_BYTE*)(65*a1 + dword_13CE27C)
    return cls != kUpgradeSkipClass;
}

} // namespace guild::gui
