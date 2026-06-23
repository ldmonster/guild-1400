#include "render/heightmap_create.h"

#include <cstdlib>   // abs
#include <new>       // operator new[]

namespace guild::render {

namespace {
HeightmapAllocFn g_alloc = nullptr;
HeightmapBuildFn g_build = nullptr;

// Default allocator: raw host heap (no zero-init, matching VIBE_Memory_AllocDebug
// which returns uninitialized debug-heap memory).
void* DefaultAlloc(unsigned long size, const char* /*tag*/) {
    return ::operator new[](static_cast<std::size_t>(size), std::nothrow);
}
} // namespace

void SetHeightmapAllocator(HeightmapAllocFn alloc) { g_alloc = alloc; }
void SetHeightmapBuilder(HeightmapBuildFn build)   { g_build = build; }

// gilde.exe 0x5c63a0 — VIBE_Heightmap_Create.
Heightmap* Create(int srcAsset, int size, u8 flag) {
    HeightmapAllocFn alloc = g_alloc ? g_alloc : &DefaultAlloc;

    // v5 = 0; if (!size) return v5;
    if (size == 0)
        return nullptr;

    // v5 = AllocDebug(0x30, "d3_sm:Create");
    // The binary's record is exactly 0x30 (48) bytes (4-byte pointers, 32-bit).
    // On a 64-bit host the same fields need sizeof(Heightmap) (entries/heights
    // are native 8-byte pointers); we allocate the host struct size so the field
    // writes are in-bounds. The "0x30" provenance is documented; the buffer
    // sizes below (size*size, 24*size*size) are the real byte counts and are
    // host-independent.
    Heightmap* hm = static_cast<Heightmap*>(alloc(sizeof(Heightmap), "d3_sm:Create"));
    if (!hm)
        return nullptr;        // null deref of v5[45] would crash; guard 1:1-safely.

    // v5[45] = a3;  (Heightmap::flag2d at +0x2D)
    hm->flag2d = flag;

    // v7 = abs(size); size field (+0x20) = v7.
    unsigned int sz = static_cast<unsigned int>(std::abs(size));
    hm->size = static_cast<i32>(sz);

    // heights (+0x28) = AllocDebug(sz*sz, "d3_sm:CreateHeight");
    hm->heights = static_cast<u8*>(alloc(sz * sz, "d3_sm:CreateHeight"));

    // if (size <= 0) entries = 0; else entries = AllocDebug(24*sz*sz, ...).
    if (size <= 0) {
        hm->entries = nullptr;
    } else {
        hm->entries = static_cast<u8*>(alloc(24u * sz * sz, "d3_sm:CreateEntries"));
    }

    // if (!v5 || !v5->heights) return v5;  (v5 already non-null here)
    if (!hm->heights)
        return hm;

    // BuildTerrainMesh(v5, srcAsset, ...) @0x5c5610 — deferred render-coupled
    // geometry build; routed through the optional hook (handoff documented in
    // the header). Scale math itself is render/heightmap.cpp DeriveGridScaleXZ.
    if (g_build)
        g_build(hm, srcAsset);

    return hm;
}

} // namespace guild::render
