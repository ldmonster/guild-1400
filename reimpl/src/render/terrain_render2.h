#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — TERRAIN_RENDER2: a second slice of self-contained, deterministic
// leaves of the Heightmap / Floor terrain renderer (d3_engine.c). Faithful 1:1
// reconstructions, decompiled from gilde.exe (imagebase 0x400000). Each function
// keeps its ORIGINAL byte-offset record accesses; callees that are NOT reconstructed
// (the debug allocator, the texture/D3D layer, the live cursor-ray globals) are
// routed through an installable TerrainRender2Hooks dispatch table whose default
// implementations are inert so the math is testable in isolation.
//
// Functions reconstructed here (addr / original VIBE_ name):
//   0x5C31F0  VIBE_Heightmap_FindNearestEntryToPoint  (nearest illuminated entry)
//   0x5C34C4  VIBE_Heightmap_BlendSubdivideTerrain    (Catmull-Rom height subdivide)
//   0x5C4034  VIBE_Heightmap_ProjectPointToView       (box-face ray projection)
//   0x5C67B8  VIBE_Heightmap_RaycastFromCursor        (DDA terrain raycast)
//   0x5C2DDC  VIBE_Floor_PickTileAtPoint              (barycentric tile-height pick)
//   0x5BCB38  VIBE_Floor_AllocTileBuffers             (per-tile vertex/poly buffers)
//   0x5BCE10  VIBE_Floor_AllocInflateBuffers          (inflate/divide buffers)
//   0x5BCED8  VIBE_Floor_AllocLightBuffers            (light/divide buffers)
//
// DEFERRED (32-bit pointer-slot layout — not safely reconstructible as a raw
// 64-bit byte overlay): the original record packs buffer pointers in adjacent
// 4-byte slots (e.g. divide buffers at +36/+40/+44, tile buffers at +24/+28/...
// +48/+52). On LP64 a native pointer is 8 bytes, so neighbouring slots overlap
// and any "free a slot then read the next" walk corrupts/double-frees. These two
// need a proper struct model (à la heightmap.cpp's HeightmapGrid):
//   0x5BCCF8  VIBE_Floor_FreeTileBuffers   (frees from overlapping pointer slots)
//   0x5BD244  VIBE_Floor_SetLayerTexture   (frees a layer's overlapping mip slots)
// The kept Alloc* leaves expose only their RECOVERED SIZE MATH + scalar field
// side-effects (verified via the allocDebug hook at call time), which are layout-
// width-independent.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Recovered constants (gilde.exe .rdata, little-endian)
// ---------------------------------------------------------------------------
//   flt_628B5C = 0.5f           (round-bias for ConvertX truncate)
//   flt_628B60 = 255.0f         (height clamp ceiling in the subdivide)
//   flt_13FCF3C = 0.0f          (runtime entry-brightness scale; image default 0)
//   dbl_628C04 = 1e-4           (raycast axis-near-zero epsilon)
//   tile poly init constants: 16.0f, 20.0f, 40.0f
// ---------------------------------------------------------------------------

// ===========================================================================
// Installable cross-module hooks. The originals call into the debug heap, the
// texture-cache / DirectDraw layer, and read live cursor-ray globals; none of
// those are reconstructed. Default implementations are inert (alloc returns a
// real malloc block so the buffer-init loops are exercised; free uses ::free;
// the rest are no-ops). Tests install recording / deterministic mocks.
// ===========================================================================
struct TerrainRender2Hooks {
    // VIBE_Memory_AllocDebug(size, tag) @0x438F10 — debug heap alloc. Default: malloc.
    void* (*allocDebug)(unsigned int size, const char* tag);
    // VIBE_Memory_FreeDebug(ptr, ...) @0x43923C — debug heap free. Default: free.
    void  (*freeDebug)(void* ptr);
    // VIBE_Floor_ComputeSlopeFlags @0x5BBDB0 — recomputes per-tile slope flags.
    void  (*computeSlopeFlags)(void* floor);
    // VIBE_Floor_BuildTilePolys @0x5BC45C — rebuilds tile poly visibility.
    void  (*buildTilePolys)(void* floor);
};
void SetTerrainRender2Hooks(const TerrainRender2Hooks* hooks);
const TerrainRender2Hooks& GetTerrainRender2Hooks();

// ---------------------------------------------------------------------------
// HeightmapGrid — the record the Heightmap_* leaves walk. RECOVERED from the
// *(a1 + OFF) accesses across the four heightmap functions. Only the modelled
// fields matter; offsets are the original byte offsets (32-bit fields).
//   +0x00 (a1[0])  size       grid edge length (cells)
//   +0x04 (a1[1])  tileSpan   samples per tile edge
//   +0x10 (+16)    stepX      world units per height-X step (float)
//   +0x14 (+20)    stepY      world units per height-Y step (float)
//   +0x18 (+24)    stepZ      world units per height-Z step (float)
//   +0x20 (+32)    cellCount  per-row cell count (== size, used by raycast bounds)
//   +0x28 (+40)    heights    pointer to the size*size height byte buffer
//   +0x90 (+144)   origin[3]  world origin of cell (0,0)
//   +0xA0 (+160)   axisX[3]   world step per cell-X
//   +0xB0 (+176)   axisY[3]   world step per cell-Y  (projection diagonal corner)
// The per-region "entry" array used by FindNearestEntryToPoint hangs off a1[62]
// (a 80-byte stride record list); the per-tile attribute byte is at +318.
// ---------------------------------------------------------------------------

// 0x5C31F0 — VIBE_Heightmap_FindNearestEntryToPoint
//   (__userpurge eax=fn(grid@eax, px@edx, py@ebx, radius, outVec3*)).
// Scans the 8x8 tile grid; within each tile (subdivided by the tile's +318 attr
// byte) walks the 80-byte entry records (a1[62]+...), considers those whose +76
// byte is negative (a "marked" entry), and finds the entry minimising
//   sqrt(d2)/radius * (entry.f8 / kEntryScale)
// over entries inside `radius` of (px,py). Writes the winning entry's first 3
// floats through `outVec3` and returns a packed (row<<16)|col cell index, or -1.
// `kEntryScale` is flt_13FCF3C (image default 0; pass the live value if nonzero).
int FindNearestEntryToPoint(int* grid, int px, int py, float radius,
                            float* outVec3, float kEntryScale = 0.0f);

// 0x5C34C4 — VIBE_Heightmap_BlendSubdivideTerrain
//   (__userpurge eax=fn(buf@eax, stride@edx, y0@ecx, x0@ebx, span, step)).
// Catmull-Rom interpolates the height byte buffer `buf` (row stride `stride`,
// torus mask == stride-1) to fill the in-between samples between coarse grid
// points spaced `step` apart, over the rectangle [x0, x0+span) x [y0, y0+span).
// First pass interpolates along columns (vertical), second along rows. Each
// produced value is clamped to [0, 255]. No-op when step==1. Returns the last
// row index processed (matches the original's `result`).
int BlendSubdivideTerrain(unsigned char* buf, int stride, int y0, int x0,
                          int span, int step);

// 0x5C4034 — VIBE_Heightmap_ProjectPointToView
//   (__usercall st0=fn(grid@eax, point@edx, view@ebx)).
// Projects `point` against the 6 faces of the heightmap's world bounding box
// (origin at grid+144, far corner at (size-4)*axis...), expressed in the `view`
// rotation frame (16-float matrix), and returns the nearest positive ray
// parameter that escapes the box (>= the box's near-extent), else |near-extent|.
// Fully determined by its arguments. `point` is float[3], `view` is float[16].
double ProjectPointToView(int* grid, const float* point, const float* view);

// 0x5C67B8 — VIBE_Heightmap_RaycastFromCursor
//   (__userpurge al=fn(grid@eax, sx@edx, sy@ebx, outRowCol*)).
// DDA-walks the height field from a screen-space cursor ray and returns 1 with
// the hit cell (row in out[0], col in *outCol) when the ray drops to/below the
// terrain, else 0. The live original reads the cursor-ray source from the global
// view block dword_13FCD1C; here the source is supplied explicitly via `ray`
// (origin[3], dir[3]) so the math is deterministic. `grid` fields used:
// stepX/stepY/stepZ (+16/+20/+24), cellCount (+32), heights (+40).
struct CursorRay { float origin[3]; float dir[3]; };
int RaycastFromCursor(int* grid, const CursorRay* ray, int* outRow, int* outCol);

// 0x5C2DDC — VIBE_Floor_PickTileAtPoint
//   (__userpurge al=fn(floor@eax, point@edx, useLod@bl, outRowCol*, outHeight*)).
// Converts world `point` (float[3]) to a Floor cell (row in outRowCol[0] via the
// engine convention, col written first), bounds-checks it, and — when outHeight
// is non-null — bilinearly/barycentrically interpolates the cell's 4 corner
// height bytes (selecting the correct triangle by the u+v<=1 test) to produce a
// world-space height through `outHeight`. Returns 1 on a valid pick, else 0.
// Floor offsets: size(+0), tileSpan(+4), mask(+12), heights(+16), lodSrc(+36),
// origin.y(+148), origin.x(+144), origin.z(+152), axisX(+160), heightScale(+196),
// axisY(+184).
int PickTileAtPoint(int* floor, const float* point, int useLod,
                    int* outCol, int* outRow, float* outHeight);

// 0x5BCB38 — VIBE_Floor_AllocTileBuffers
//   (__usercall eax=fn(tile@eax, span@edx, lodShift@bl)).
// Allocates (via allocDebug) the per-tile point/poly/split/bp buffers sized from
// the LOD-subdivided count v6 = span >> lodShift, then zero-inits the point and
// poly records (point stride 80, poly stride 40). Returns the number of polys
// initialised. The size formulas are the load-bearing recovery:
//   points  = 80 * (v6+2)^2
//   polys   = 40 * (v6+1) * (2*v6+2)
//   split   = 48 * (v6+2)
//   bp      = 24 * ((2*v6+2)*(v6+1) + (8*v6+16))
int AllocTileBuffers(int* tile, unsigned int span, unsigned char lodShift);

// 0x5BCE10 — VIBE_Floor_AllocInflateBuffers
//   (__usercall eax=fn(floor@eax)). Allocates the inflate buffer (size*size+1)
// and the 3 mip "divide" buffers ((size>>i)^2 + 1), then (re)allocs every tile's
// buffers via AllocTileBuffers, recomputes slope flags, and sets the built bit
// (+7280 |= 1). Returns ComputeSlopeFlags' result.
int AllocInflateBuffers(int* floor);

// 0x5BCED8 — VIBE_Floor_AllocLightBuffers
//   (__usercall eax=fn(floor@eax)). Allocates the 3 divide buffers (sizes
//   size*size, /4, /16), the light buffer (size*size) and (if absent) the
//   light-offset buffer (4*size*size), zero-inits each tile's 100-byte header and
//   (re)allocs its buffers, recomputes slope flags, and rebuilds tile polys.
//   Sets +7276 = -1 and the built bit. Returns BuildTilePolys' result.
int AllocLightBuffers(int* floor);

} // namespace guild::render
