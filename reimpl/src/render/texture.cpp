#include "render/texture.h"

// =============================================================================
// guild::render texture record — implementation. See texture.h for the record
// layout and the texel-addressing reconciliation with raster.cpp.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe 0x5db724 (geometry init excerpt). The original computes:
//   *((_DWORD*)v21-7) = (w-1) | (w*w-1)              ; +88 texelMask
//   *((_DWORD*)v10+29)/(+30) = w  (a2 -> +116/+120)  ; mip/base width
//   +68 = AllocDebug(w*w)                            ; texel buffer
// (a2 is the width argument; the record stores it at both +116 and +120.)
// ---------------------------------------------------------------------------
void TextureSetSize(Texture& t, i32 width) {
    t.baseWidth  = width;            // +120
    t.mipWidth   = width;            // +116
    t.texelMask  = TexelMask(width); // +88  (w-1)|(w*w-1)
    t.widthShift = WidthShift(width);
    t.texels.assign((size_t)((i64)width * width), 0); // +68 AllocDebug(w*w)
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5da244 — VIBE_Texture_FindActiveRecord is the *free-slot* finder
// (scans from the back while +64 > 0). The by-NAME lookup lives in LoadByName;
// we expose both. Here FindFree mirrors the slot scan (first record whose
// refCount <= 0).
// ---------------------------------------------------------------------------
int TextureSet::FindFree() const {
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].refCount <= 0)
            return (int)i;
    return -1;
}

// Name lookup as performed by VIBE_Texture_LoadByName before it decides to
// create: linear scan of active records comparing the name string.
int TextureSet::FindActive(const std::string& name) const {
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].refCount > 0 && records[i].name == name)
            return (int)i;
    return -1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5db724 — VIBE_Texture_CreateRecord. Claim a free slot, copy the
// (padded) name, set width geometry, refCount = 1 (*(v11+64) = 1), paletteId
// = -1 (*(v11+80) = -1). flags/mip fields zeroed by the record memset.
// ---------------------------------------------------------------------------
int TextureSet::CreateRecord(const std::string& name, i32 width) {
    int idx = FindFree();
    if (idx < 0)
        return -1;
    Texture& t = records[(size_t)idx];
    t = Texture{};                        // memset-equivalent zero init
    t.name = name.substr(0, 63);          // StrNCopyPad(_, _, 63)
    t.slot = idx;                         // +76 group/slot
    t.paletteId = -1;                     // +80 = -1
    t.refCount = 1;                       // +64 = 1
    TextureSetSize(t, width);
    return idx;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5da2e4 — VIBE_Texture_IncrementRefCount(idx, bumpGroup).
//   if (bumpGroup && mipLevels>0 && !isMip): bump +64 of every active record
//     sharing this record's paletteId (+80).
//   else: ++this.refCount.
// ---------------------------------------------------------------------------
void TextureSet::IncrementRef(int idx) {
    if (idx < 0 || (size_t)idx >= records.size())
        return;
    Texture& t = records[(size_t)idx];
    if (t.refCount <= 0)
        return;
    if (t.mipLevels > 0 && !t.isMip) {
        for (Texture& r : records)
            if (r.refCount > 0 && r.paletteId == t.paletteId)
                r.refCount += 1;          // *(result+64) = v5 + 1
    } else {
        ++t.refCount;                      // ++*(result+64)
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d9a0c — VIBE_Texture_ReleaseEntry. The original decrements the
// active count and, when the slot's refCount reaches 0, frees the texel buffer
// and clears the record (marking the slot reusable). We model the slot-state
// transition (the buffer free is implicit in clearing `texels`).
// ---------------------------------------------------------------------------
void TextureSet::ReleaseEntry(int idx) {
    if (idx < 0 || (size_t)idx >= records.size())
        return;
    Texture& t = records[(size_t)idx];
    if (t.refCount <= 0)
        return;
    if (--t.refCount <= 0) {
        t = Texture{};                     // free texels + reset slot
        t.refCount = 0;
    }
}

} // namespace guild::render
