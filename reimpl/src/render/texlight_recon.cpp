#include "render/texlight_recon.h"
#include <cmath>

namespace guild::render {

// ---------------------------------------------------------------------------
// Surface cache
// ---------------------------------------------------------------------------

// gilde.exe 0x5d93f4 — VIBE_SurfaceCache_Init(result@eax = capacity).
//   dword_1406A50 = cap; dword_1406A70 = 0; dword_1406A5C = 0;
//   if (cap) array = AllocDebug(20*cap); else array = 0.
void SurfaceCache::Init(u32 cap) {
    capacity  = cap;        // dword_1406A50
    liveCount = 0;          // dword_1406A5C
    if (cap)                // 20-byte entries
        entries.assign(cap, SurfaceEntry{});
    else
        entries.clear();
}

// gilde.exe 0x5d9430 — VIBE_SurfaceCache_FreeAll.
//   for each entry: if (key/[+8]) { release surf1; release surf0; key=0 }
//   then free array, liveCount=0, capacity=0.
void SurfaceCache::FreeAll() {
    for (u32 i = 0; i < capacity && i < entries.size(); ++i) {
        SurfaceEntry& e = entries[i];
        if (e.key) {                         // v2[2]
            if (e.surf1) {                   // v2[1]
                if (hooks.releaseSurface) hooks.releaseSurface(e.surf1);
                e.surf1 = 0;
            }
            if (e.surf0) {                   // *v2
                if (hooks.releaseSurface) hooks.releaseSurface(e.surf0);
                e.surf0 = 0;
            }
            e.key = 0;                       // v2[2] = 0
        }
    }
    entries.clear();
    liveCount = 0;
    capacity  = 0;
}

// gilde.exe 0x5d94c8 — VIBE_SurfaceCache_StoreEntry(a1@eax = entry, a2@edx = tex).
//   if (a1[2]) { release a1[1]; release *a1 }            // drop old surfaces
//   a1[16] = (32*tex[104])>>7;                            // flag0
//   a1[3]  = dword_649D58;                                // stamp
//   a1[2]  = tex[116];                                    // key
//   *a1    = tex[96];                                     // surf0
//   a1[17] = (16*tex[104])>>7;                            // flag1
//   if (surf0 && tex[124]==8 && probeAlt(surf0)) ReportDDrawError();
//   a1[1]  = tex[100];                                    // surf1
void SurfaceCache::StoreEntry(int entryIdx, const TexRecord& tex) {
    SurfaceEntry& e = entries[entryIdx];
    if (e.key) {                              // a1[2]
        if (e.surf1) {                        // a1[1]
            if (hooks.releaseSurface) hooks.releaseSurface(e.surf1);
            e.surf1 = 0;
        }
        if (e.surf0) {                        // *a1
            if (hooks.releaseSurface) hooks.releaseSurface(e.surf0);
            e.surf0 = 0;
        }
    }
    // (32*flags)>>7 isolates bit2; (16*flags)>>7 isolates bit3.
    e.flag0 = (u8)((u8)(32u * tex.flags) >> 7);   // a1[16]
    e.stamp = frame;                              // a1[3] = dword_649D58
    e.key   = tex.key;                            // a1[2] = tex[116]
    e.surf0 = tex.surf0;                          // *a1   = tex[96]
    e.flag1 = (u8)((u8)(16u * tex.flags) >> 7);   // a1[17]

    if (e.surf0 && tex.kind == 8 &&
        hooks.probeAlt && hooks.probeAlt(e.surf0) != 0) {
        // original: VIBE_Render_ReportDDrawError(); (no state change here)
    }
    e.surf1 = tex.surf1;                          // a1[1] = tex[100]
}

// gilde.exe 0x5d9580 — VIBE_SurfaceCache_EvictAndStore(a1@eax = tex).
int SurfaceCache::EvictAndStore(TexRecord& tex) {
    if (liveCount == capacity) {
        // Full: find occupied entry ([+8] != 0) with the smallest stamp that is
        // strictly below dword_649D58 (v2 starts at the current frame).
        u32 best = frame;                 // v2 = dword_649D58
        int chosen = -1;                  // v3 = 0
        for (u32 i = 0; i < capacity && i < entries.size(); ++i) {
            const SurfaceEntry& e = entries[i];
            if (e.key && best > e.stamp) {   // *(v4+8) && v2 > *(v4+12)
                chosen = (int)i;
                best   = e.stamp;
            }
        }
        if (chosen >= 0) {
            StoreEntry(chosen, tex);
            return chosen;
        }
        // Nothing evictable: release the tex's own surfaces and bail.
        if (tex.surf1) {                  // *(a1+100)
            if (hooks.releaseSurface) hooks.releaseSurface(tex.surf1);
            tex.surf1 = 0;
        }
        if (tex.surf0) {                  // *(a1+96)
            if (hooks.releaseSurface) hooks.releaseSurface(tex.surf0);
            tex.surf0 = 0;
        }
        return -1;
    }

    // Not full: advance past occupied slots ([+8] != 0); store at the first
    // free one. The original: while (result[2]) { ++v8; result+=5; if (v8>=cap)
    // return result; } then StoreEntry; ++liveCount.
    if (capacity) {
        u32 i = 0;
        while (i < entries.size() && entries[i].key != 0) {
            ++i;
            if (i >= capacity) return -1;     // ran off the end (no free slot)
        }
        if (i < entries.size()) {
            StoreEntry((int)i, tex);
            ++liveCount;                      // ++dword_1406A5C
            return (int)i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Hi-colour table bank — gilde.exe 0x5da04c VIBE_HiColTab_FindOrBuild
// ---------------------------------------------------------------------------
int HiColTabBank::FindOrBuild(const u8* rgb, u32 count, u8* outIndices) {
    // Fast path: the most-recently-used bank is full of headroom and == capacity
    //   if (used && count > banks[used].freeCount && used == capacity)
    //       return (capacity ^ used);   // (original returns a small int token)
    // banks[used] in the original is the 776*used record (one PAST the last
    // used record, i.e. the next-to-allocate slot). Modeled as banks[used].
    if (used && used < banks.size() &&
        count > (u32)banks[used].freeCount && used == capacity) {
        return (int)(capacity ^ used);
    }

    int chosen = -1;                  // v16 = 0
    if (used) {
        // Scan existing banks for one whose unmatched-colour count fits its free
        // slots: for bank j, v3 = number of input colours already present in the
        // bank; if (count - v3) <= bank.freeCount -> reuse bank j.
        for (u32 j = 0; j < used && j < banks.size(); ++j) {
            const HiColTab& bank = banks[j];
            int present = 0;                       // v3
            int bankUsed = 256 - bank.freeCount;   // 256 - *(rec+772)
            if (count) {
                for (u32 c = 0; c < count; ++c) {
                    const u8* in = rgb + 3 * c;
                    for (int k = 0; k < bankUsed; ++k) {
                        if (bank.rgb[3 * k]     == in[0] &&
                            bank.rgb[3 * k + 1] == in[1] &&
                            bank.rgb[3 * k + 2] == in[2]) {
                            ++present;             // ++v3
                            // (original keeps scanning; a colour can match once
                            //  per bank entry — matches the literal loop)
                        }
                    }
                }
            }
            if (count - (u32)present <= (u32)bank.freeCount) {
                chosen = (int)j;
                break;
            }
        }
    }

    if (chosen < 0) {
        // Allocate a new bank: alloc 0x8200 block, freeCount(=rec[193]) = 256,
        // if (count < 256) seed entry 0 via AddEntry(0,0,0).
        if (used >= banks.size()) banks.emplace_back();   // 776-byte record
        HiColTab& bank = banks[used];
        bank.data.assign(0x8200, 0);
        bank.freeCount = 256;
        chosen = (int)used;
        if (count < 0x100)
            HiColTabAddEntry(bank, 0, 0, 0);   // VIBE_HiColTab_AddEntry(0,0,tab,0)
        ++used;                                // ++dword_1406A60
    }

    // Map every input colour through AddEntry, writing the assigned index out.
    if (count) {
        HiColTab& bank = banks[chosen];
        for (u32 c = 0; c < count; ++c) {
            const u8* in = rgb + 3 * c;          // r=*v11, g=v11[1], b=v11[2]
            outIndices[c] = HiColTabAddEntry(bank, in[0], in[1], in[2]);
        }
    }
    return chosen;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d988c — InitTables log10 byte-table
// ---------------------------------------------------------------------------
void BuildLog10ByteTable(i8 out[4097]) {
    // gilde.exe 0x5d988c float loop @0x5d98f4..0x5d991d:
    //   fild(v6)  -> log10 -> VIBE_Coord_ConvertX -> fistp ; ++edx ; store@edx.
    // edx (write index) starts 0, the *loaded* integer is the pre-increment edx,
    // the store index is edx after ++. So out[k] = ConvertX(log10(k-1)) for
    // k = 1..4096.
    //
    // VIBE_Coord_ConvertX @0x5c6b08 sets the x87 control-word rounding field
    // (HIBYTE(cw)=0x1F => RC bits = 11 = ROUND TOWARD ZERO) and does frndint.
    // That is TRUNCATION toward zero, NOT round-to-nearest. The subsequent
    // fistp re-uses that same control word, so the conversion truncates.
    for (int v6 = 0; v6 < 4096; ++v6) {
        double l = std::log10((double)v6);   // VIBE_Math_Log10(v6)
        int r;
        if (std::isfinite(l)) {
            r = (int)std::trunc(l);           // VIBE_Coord_ConvertX -> frndint (toward 0)
        } else {
            // log10(0) == -inf : the x87 fistp yields integer-indefinite
            // 0x80000000; LOBYTE of that == 0 in the original.
            r = 0;
        }
        out[v6 + 1] = (i8)r;                  // byte_1406A8F[++edx]
    }
}

} // namespace guild::render
