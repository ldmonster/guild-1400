#include "render/texture_set_table.h"

#include <cstring>

// =============================================================================
// guild::render texture-set table (.TXS) — implementation. See the header for
// the byte-format recovery + the archive-wide cross-validation evidence.
// =============================================================================
namespace guild::render {

bool ParseTextureSetTable(const u8* data, std::size_t size, TextureSetTable& out) {
    out = TextureSetTable{};
    if (!data || size < 12)
        return false;

    u32 magic = (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) |
                ((u32)data[3] << 24);
    u32 sets = (u32)data[4] | ((u32)data[5] << 8) | ((u32)data[6] << 16) |
               ((u32)data[7] << 24);
    u32 per = (u32)data[8] | ((u32)data[9] << 8) | ((u32)data[10] << 16) |
              ((u32)data[11] << 24);
    if (magic != kTextureSetMagic)
        return false;
    // Plausibility gate. Shipped tables span sets 1..9, namesPerSet 1..32; a
    // header beyond the name bytes available is malformed.
    if (sets == 0 || per == 0 || (u64)sets * per > size)
        return false;

    std::size_t i = 12;
    const std::size_t total = (std::size_t)sets * per;
    out.names.reserve(total);
    for (std::size_t k = 0; k < total; ++k) {
        std::string s;
        for (;;) {
            if (i >= size)
                return false;            // truncated mid-name
            char c = (char)data[i++];
            if (!c)
                break;
            s.push_back(c);
        }
        out.names.push_back(std::move(s));
    }
    out.setCount = (i32)sets;
    out.namesPerSet = (i32)per;
    out.ok = true;
    return true;
}

// gilde.exe 0x506388 — VIBE_Object_HideFoliageDecor name gate. The original calls
// VIBE_Util_StrncmpN(a1, prefix, n) with a1 == the scene NODE name pointer at
// BYTE 0 (no basename split; StrncmpN @0x5e9ee0 is a plain byte compare from
// offset 0). Three case-sensitive prefixes, OR-combined (order irrelevant to the
// boolean result): "pfl_"/4, "vg_"/3, "!vg_"/4. We reproduce the byte-0 compare
// exactly — the archive member names this is applied to are bare (no path
// separators), so there is no observable difference for shipped content.
bool IsFoliageMeshName(const char* name) {
    if (!name)
        return false;
    return std::strncmp(name, "pfl_", 4) == 0 ||  // 0x5063c8 StrncmpN(a1,aPfl,4)
           std::strncmp(name, "vg_", 3) == 0 ||   //          StrncmpN(a1,aVg,3)
           std::strncmp(name, "!vg_", 4) == 0;    //          StrncmpN(a1,aVg_0,4)
}

} // namespace guild::render
