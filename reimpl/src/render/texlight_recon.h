#pragma once
#include "guild/common/types.h"
#include "render/hicoltab.h"
#include <vector>

// =============================================================================
// guild::render — texlight_recon
//
// Faithful 1:1 reconstruction of the *pure* record / cache / colour-table logic
// from the gilde.exe "d3_ts" texture manager (texture-surface cache, hi-colour
// remap-table bank lookup, and the InitTables log10 byte-table). These pieces
// were originally fused with DirectDraw/Direct3D COM surface objects and the
// scene graph; the data-structure management and the integer/FP math below are
// reproduced exactly, and every place the original called into a GPU surface
// (release/create a DirectDraw surface) is routed through an inert hook so the
// portable/headless build needs no GPU backend.
//
// Functions reconstructed here (provenance addresses inline):
//   0x5d93f4  VIBE_SurfaceCache_Init
//   0x5d9430  VIBE_SurfaceCache_FreeAll
//   0x5d94c8  VIBE_SurfaceCache_StoreEntry
//   0x5d9580  VIBE_SurfaceCache_EvictAndStore
//   0x5da04c  VIBE_HiColTab_FindOrBuild
//   0x5d988c  VIBE_Texture_InitTables (the log10 byte table + record-array sizing)
//
// Already present elsewhere in the reimpl (NOT redefined here):
//   0x5d9db8  VIBE_HiColTab_AddEntry   -> src/render/hicoltab.cpp (HiColTabAddEntry)
//   0x434f30  VIBE_Result_Handler_Final-> src/render/colorformat.cpp (PackColor)
//   0x5b9368.. terrain-TILE LRU cache  -> src/render/texture_cache.cpp (TileCache)
// =============================================================================
namespace guild::render {

// -----------------------------------------------------------------------------
// SURFACE CACHE — 20-byte entries (5 dwords), the original indexes base+20*i.
// gilde.exe globals it tracks:
//   dword_64A204  entry array base        -> SurfaceCache::entries
//   dword_1406A50 capacity                 -> SurfaceCache::capacity
//   dword_1406A5C live (occupied) count    -> SurfaceCache::liveCount
//   dword_1406A70 (init-time scratch, =0)
//   dword_649D58  global frame counter (LRU stamp source)
//
// Entry record (20 bytes):
//   [+0]  primary surface id   (*a1   — was a DirectDraw surface*)
//   [+4]  secondary surface id  (a1[1])
//   [+8]  texture key           (a1[2] == texture-record +116; 0 = free slot)
//   [+12] LRU stamp             (a1[3] == dword_649D58 at store time)
//   [+16] flag byte0            ((32*tex[+104])>>7  == bit2 of tex flags)
//   [+17] flag byte1            ((16*tex[+104])>>7  == bit3 of tex flags)
// -----------------------------------------------------------------------------
struct SurfaceEntry {
    u32 surf0 = 0;   // [+0]
    u32 surf1 = 0;   // [+4]
    u32 key   = 0;   // [+8]  texture-record +116 (0 == free)
    u32 stamp = 0;   // [+12] LRU timestamp
    u8  flag0 = 0;   // [+16]
    u8  flag1 = 0;   // [+17]
};

// The texture-record fields the cache reads (texture-record base +N). Only the
// fields the reconstructed cache logic touches are modeled.
struct TexRecord {
    u32 surf0 = 0;   // +96  primary surface id
    u32 surf1 = 0;   // +100 secondary surface id
    u8  flags = 0;   // +104 flag byte (bit1=skip, bit2->flag0, bit3->flag1)
    u8  kind  = 0;   // +124 (==8 selects the alt store path in the original)
    u32 key   = 0;   // +116 texture key
};

// Inert GPU hooks (the original's COM Release vtable call / surface-create).
// Default no-ops; a real backend can swap these in. Return values are ignored
// by the reconstructed control flow exactly as in the original.
struct SurfaceCacheHooks {
    // Release a surface id (original: (*vtbl[2])(surf)). No-op by default.
    void (*releaseSurface)(u32 surf) = nullptr;
    // Probe the kind==8 alt path (original: (*vtbl[31])(surf,0) != 0). The
    // original branches to an error report and still stashes surf1; the data
    // path is identical either way, so default returns 0 (no error).
    int  (*probeAlt)(u32 surf) = nullptr;
};

struct SurfaceCache {
    std::vector<SurfaceEntry> entries; // dword_64A204, 20-byte stride
    u32 capacity  = 0;                 // dword_1406A50
    u32 liveCount = 0;                 // dword_1406A5C
    u32 frame     = 0;                 // dword_649D58 source for LRU stamps
    SurfaceCacheHooks hooks;

    // gilde.exe 0x5d93f4 — VIBE_SurfaceCache_Init(capacity).
    void Init(u32 cap);
    // gilde.exe 0x5d9430 — VIBE_SurfaceCache_FreeAll. Releases occupied surfaces.
    void FreeAll();
    // gilde.exe 0x5d94c8 — VIBE_SurfaceCache_StoreEntry(entryIdx, tex).
    // Overwrites entry[entryIdx] with tex's surfaces + stamp/flags/key.
    void StoreEntry(int entryIdx, const TexRecord& tex);
    // gilde.exe 0x5d9580 — VIBE_SurfaceCache_EvictAndStore(tex).
    // If full, evict the lowest-stamp occupied entry & store; else fill first
    // free slot. Returns the entry index used, or -1 if nothing was stored
    // (the original released the tex's own surfaces in that case).
    int EvictAndStore(TexRecord& tex);
};

// -----------------------------------------------------------------------------
// HI-COLOUR TABLE BANK — gilde.exe 0x5da04c VIBE_HiColTab_FindOrBuild.
//
// The engine keeps a small array of HiColTab banks (dword_1406A68 base, 776-byte
// records; dword_1406A60 = used count, dword_1406A64 = capacity). Given a palette
// of `count` RGB triples it finds a bank that can already represent all-but-N of
// them (N <= that bank's remaining free slots) or allocates a new bank, then maps
// each input colour through HiColTabAddEntry, writing the assigned index into
// `outIndices`. Returns a pointer to the bank's 0x8200 data block (the *v16).
// -----------------------------------------------------------------------------
struct HiColTabBank {
    std::vector<HiColTab> banks; // dword_1406A68, one HiColTab per 776-byte rec
    u32 used     = 0;            // dword_1406A60
    u32 capacity = 0;            // dword_1406A64

    // gilde.exe 0x5da04c. `rgb` = count*3 source bytes, `outIndices` = count
    // bytes (the original wrote into a3++). Returns index of the chosen bank
    // (banks[i]), or -1 on the fast-path early return (see code).
    int FindOrBuild(const u8* rgb, u32 count, u8* outIndices);
};

// -----------------------------------------------------------------------------
// gilde.exe 0x5d988c — VIBE_Texture_InitTables log10 byte-table.
// Builds byte_1406A8F[1..4096] = (i8)trunc(log10((double)(k-1))) using the
// original's round-TOWARD-ZERO FPU mode (VIBE_Coord_ConvertX @0x5c6b08 sets
// cw RC=11 then frndint, the fistp inherits it -> truncation). Entry
// index 0 is left untouched (the original writes from index 1). log10(0) is
// -inf -> the x87 cvt yields the integer-indefinite 0x80000000, truncated to a
// byte = 0, so out[1] == 0. Caller supplies a 4097-byte buffer.
void BuildLog10ByteTable(i8 out[4097]);

// gilde.exe 0x5d988c — the hi-colour record-array byte size for `count` banks
// (the original computes (((count*4-count)<<5)+count)<<3 == 776*count).
inline u32 HiColRecordArrayBytes(u32 count) {
    return (((count * 4u - count) << 5) + count) << 3;
}
// The per-texture-record stride (count << 7 == 128 bytes/record).
inline u32 TextureRecordArrayBytes(u32 count) { return count << 7; }

} // namespace guild::render
