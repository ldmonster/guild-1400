#pragma once
#include "guild/common/types.h"
#include "render/heightmap.h"   // struct Heightmap (reused, no ODR)

// =============================================================================
// guild::render — heightmap record CONSTRUCTOR (gilde.exe d3_sm.c "d3_sm:Create").
//
//   gilde.exe 0x5c63a0 — VIBE_Heightmap_Create
//     (__usercall eax=fn(srcAsset@eax, size@edx, flag@bl))
//
// Allocates the 0x30-byte Heightmap record (see render/heightmap.h), its
// size*size elevation-byte grid (heights, +0x28) and, for size>0, its
// size*size * 24-byte tile-record array (entries, +0x24), then dispatches to
// VIBE_Heightmap_BuildTerrainMesh @0x5c5610 to populate the geometry.
//
// EXACT control flow (from the decompile):
//   v5 = 0;
//   if (size == 0) return 0;
//   v5 = AllocDebug(0x30, "d3_sm:Create");
//   v5[45] = flag;                       // Heightmap::flag2d  (+0x2D)
//   sz = abs(size);                       // *((u32*)v5+8)      (+0x20 size)
//   v5->heights = AllocDebug(sz*sz, "d3_sm:CreateHeight");      // +0x28
//   if (size <= 0) v5->entries = 0;       // +0x24
//   else v5->entries = AllocDebug(24*sz*sz, "d3_sm:CreateEntries");
//   if (!v5 || !v5->heights) return v5;   // OOM guard
//   BuildTerrainMesh(v5, srcAsset, *(float*)&v5);  // 0x5c5610
//   return v5;
//
// NOTE on the abs/sign quirk (1:1): `size` is abs()'d for the BUFFER sizes
// (sz = abs(size)), but the entries-vs-null branch and the OOM-guard test the
// ORIGINAL signed `size`. So a negative size still allocs heights (sz*sz) but
// sets entries = null. We reproduce this exactly.
//
// The AllocDebug calls are the host-heap boundary (Microsoft debug CRT in the
// original); routed through an installable allocator so the portable build needs
// no third-party heap. The BuildTerrainMesh draw-list walk + raster submission
// are render-coupled and DEFERRED (see render/heightmap.h DeriveGridScaleXZ for
// the recovered scale math); it is routed through an optional build hook.
// =============================================================================
namespace guild::render {

// gilde.exe 0x438f10 — VIBE_Memory_AllocDebug(size, tag): host debug-heap alloc.
// Boundary hook: defaults to operator new[] (zero-init NOT performed by the
// original; we leave the buffers uninitialized to match — BuildTerrainMesh /
// the engine zeroes what it needs).
using HeightmapAllocFn = void* (*)(unsigned long size, const char* tag);

// gilde.exe 0x5c5610 — VIBE_Heightmap_BuildTerrainMesh dispatch hook.
//   al = fn(hm@eax, srcAsset@edx, gridScale@ecx (a float reinterpret of hm ptr))
// The original passes *(float*)&v5 as the third (ecx) arg — i.e. the low 32 bits
// of the record pointer reinterpreted as a float (a decompiler artefact of the
// register-arg convention; BuildTerrainMesh ignores it and reads grid scale off
// the record). The hook receives the record + source asset.
using HeightmapBuildFn = void (*)(Heightmap* hm, int srcAsset);

// Install the allocator (e.g. wire to VIBE_Memory_AllocDebug @0x438f10). When
// unset, Create uses operator new[].
void SetHeightmapAllocator(HeightmapAllocFn alloc);

// Install the terrain-mesh builder (VIBE_Heightmap_BuildTerrainMesh @0x5c5610).
// When unset, Create allocates/initializes the record but skips the geometry
// build (the deferred render-coupled step).
void SetHeightmapBuilder(HeightmapBuildFn build);

// gilde.exe 0x5c63a0 — VIBE_Heightmap_Create. Returns the new record (caller
// owns it; Free @0x5c6438 nulls the buffers). Returns null when size == 0 or on
// allocation failure of the record itself.
Heightmap* Create(int srcAsset, int size, u8 flag);

} // namespace guild::render
