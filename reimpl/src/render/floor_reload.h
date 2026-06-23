#pragma once
#include "guild/common/types.h"
#include <string>

// =============================================================================
// guild::render — per-floor texture reload (gilde.exe d3_floor.c).
//
//   gilde.exe 0x5bd2d8 — VIBE_Floor_ReloadTextures (__usercall eax=fn(floor@eax))
//
// Rebuilds every floor-tile texture after a device/gamma/quality change. Called
// from VIBE_Render_SetGammaTable @0x5b9ef4 (and the snow scene path @0x42a2cc).
//
// CONTROL FLOW (1:1 from the decompile):
//   if (!floor) return 0;
//   Floor_InvalidateTiles(floor);                 // 0x5ba704 (== InvalidateTiles)
//   for (slot = 0; slot < 8; ++slot) {            // outer v19 0..7
//       nameRow = floor + 6756 + 64*slot;         // v18 (+64 per slot)
//       suffix  = dword_5B8C90;                    // v22 (11-byte stride table)
//       tile    = floor + 4*slot;                  // v1
//       end     = floor + (96 + 4*slot);          // v21 == v17 + v15
//       for (; tile != end; tile += 32) {         // 3 mip layers per slot
//           name = copy(nameRow) + copy(suffix);  // template + mip suffix
//           path = Texture_BuildBmpPath(name);    // 0x5d97e8 (VFS resolve)
//           if (path && tile[48] /*surface buf*/) // *(v1+48)
//               Bmp_LoadBuffer(path, tile[48], 17, 0, ...);  // 0x5f0ce4 DECODE
//           suffix += 11;                          // next mip layer
//       }
//   }
//
// The tile count per slot is (end - tile)/32 == 96/32 == 3 == the 3 mip layers
// (suffixes "", "_high_1", "_high_2"; see floorgfx_recon kFloorMipSuffix). The
// outer 8 == the floor's 8 texture slots.
//
// THE BOUNDARY (rule 3): Bmp_LoadBuffer @0x5f0ce4 is the BMP file decode + pixel
// upload into the tile's surface buffer — the texture-upload path. Its selection
// / name-build / iteration logic is reconstructed here 1:1; the decode itself is
// routed through the upload hook (it lands in render/bmp.cpp / the device).
// =============================================================================
namespace guild::render {

// gilde.exe 0x5f0ce4 — VIBE_Bmp_LoadBuffer dispatch hook (the decode boundary).
//   al = fn(path, destBuffer, flags=17, palette=0, ...). Returns success.
// `path` is the resolved "*"+name+".BMP" VFS path; `dest` is the tile surface
// buffer (tile[48]). flags 17 == (0x10|0x01): preserve-alpha + load-pixels.
using BmpLoadFn = bool (*)(const char* path, u8* dest, int flags);

// Resolves "*" + name + ".BMP" through the VFS (VIBE_Texture_BuildBmpPath
// @0x5d97e8). Returns the resolved path, or empty when it does not exist.
using BuildBmpPathFn = std::string (*)(const std::string& name);

// Floor-tile accessor boundary: lets the live caller hand the real floor record
// to the reconstructed iteration without this module knowing the 0x5bd2d8 byte
// layout of every field. Exactly one per (slot, mip) cell.
struct FloorTileAccess {
    // slotTemplateName(slot): the per-slot base texture name (the string at
    // floor + 6756 + 64*slot). 8 slots.
    const char* (*slotTemplateName)(void* floor, int slot) = nullptr;
    // tileSurfaceBuffer(slot, mip): the destination buffer (tile[48]) for the
    // (slot, mip) tile, or null if that tile has no surface (skip the load).
    u8* (*tileSurfaceBuffer)(void* floor, int slot, int mip) = nullptr;
    // invalidate(floor): VIBE_Floor_InvalidateTiles @0x5ba704.
    void (*invalidate)(void* floor) = nullptr;
};

// Install the decode + path-resolve boundaries (defaults: no-op decode that
// fails, identity path build — so the headless build links and the iteration is
// still testable via the counters returned by ReloadTextures).
void SetFloorBmpLoader(BmpLoadFn load);
void SetFloorBmpPathBuilder(BuildBmpPathFn build);

// gilde.exe 0x5bd2d8 — VIBE_Floor_ReloadTextures. Iterates the 8 slots x 3 mip
// layers, building each tile's name (template + suffix), resolving the BMP path,
// and decoding it into the tile surface (when both path and surface exist).
//
// `floor` is the opaque floor record; `acc` provides the field accessors. Returns
// the number of tiles whose decode was ATTEMPTED (path resolved AND surface
// present) — a testable proxy for the original's loop coverage. Returns 0 for a
// null floor (matching the early-out).
int ReloadTextures(void* floor, const FloorTileAccess& acc);

} // namespace guild::render
