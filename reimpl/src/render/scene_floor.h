#pragma once
#include "guild/common/types.h"
#include "render/heightmap.h"

#include <cstddef>
#include <string>
#include <vector>

// =============================================================================
// guild::render — the FLOOR (terrain) block of a city scene stream, i.e. the
// payload behind the floor-flag byte of VIBE_Scene_LoadFromStream @0x5e7e38:
//
//   gilde.exe 0x5e78a8 — VIBE_WorldIo_LoadFloorRegions (__usercall,
//                        eax = stream, edx = ver/tag)   — the block grammar
//   gilde.exe 0x5dcca0 — VIBE_Bio_ReadArrayQuick        — the array framing
//   gilde.exe 0x5bd44c — VIBE_Floor_LoadFromHeightmap   — the consumer
//
// THE REAL GRAMMAR (decompiled; supersedes the wave-3 shipped-bytes recovery)
// ---------------------------------------------------------------------------
// LoadFloorRegions fills a 676-byte, zeroed CONTEXT struct from the stream and
// hands the WHOLE context to VIBE_Floor_LoadFromHeightmap @0x5bd44c. Context
// layout (stack @0x5e78d3, offsets verified against the 0x5bd44c field reads):
//
//   +0    char name[64]        "<scene>_height"          (Bio_ReadString)
//   +64   u8*  heights         ReadArrayQuick(N)         -> floor+16 "d3_fl:Height"
//   +68   u8*  waterHeights    ReadArrayQuick(N)         -> floor+24 "d3_fl:Water(height)"
//   +72   char textureName[64] "<scene>_texture"
//   +136  u8*  textureGrid     ReadArrayQuick(N)         -> floor+20 "d3_fl:Texture"
//   +140  u8*  lightOffsets    ReadArrayQuick(4*N)       -> floor+32 "d3_fl:LightOffset"
//   +144  f32  cellScale       (Bio_ReadDword — a FLOAT)
//   +148  f32  heightScale     (Bio_ReadDword — a FLOAT)
//   +152  i32  waterRegionCount
//   +156  i32  N               (grid edge; 64/128 in shipped scenes)
//   +160  u8*  waterRegions    344-byte-stride records   -> floor+6624 "d3_fl:Water(regions)"
//   +164  char typeNames[8][64] terrain-type table       -> Floor_LoadTexture slots
//
// VIBE_Bio_ReadArrayQuick @0x5dcca0 (stream@eax, expectedCount@edx, out@ebx):
//   u32 elemSize; u32 count;                        (two length dwords)
//   if (count == expectedCount) alloc+read count*elemSize payload bytes
//   else                        SEEK FORWARD count*elemSize, out = null,
//                               log "bio_rd_array_quick: Tried to load invalid
//                               array-sizes...".
// This resolves the old "three size dwords" misread: the heights record is
//   [u32 N] [u32 elemSize] [u32 count] [count*elemSize payload]
// with N == elemSize == count == grid edge in every shipped scene (payload =
// N rows of N bytes). The old SceneFloorHeights fields map as
//   sizeX -> N (ReadDwordSwapArgs), sizeY -> elemSize, third -> count.
//
// Stream order (ver = the scene tag dword; ver < 0x3A6C0009 -> error
// "Incompatible Floor-Versions... Sorry!"; shipped scenes are 0x3A6C00BB):
//   ReadString name;
//   if (name[0] && ver >= 0x3A6C00AD) {
//     ReadDword -> N;  ArrayQuick(N) -> heights;
//     if (ver >= 0x3A6C00BB) ArrayQuick(4*N) -> lightOffsets;   // 4*N*N bytes
//   }
//   if (ver >= 0x3A6C00AA) {
//     ReadByte waterFlag;
//     if (waterFlag) { ReadDword -> waterRegionCount;
//       if (count > 0) {
//         if (ver >= 0x3A6C00AD) ArrayQuick(N) -> waterHeights;
//         count x WATER REGION RECORD (see SceneFloorWaterRegion below); } }
//   }
//   ReadString textureName;
//   if (ver >= 0x3A6C00AD && textureName[0]) ArrayQuick(N) -> textureGrid;
//   if (ver <= 0x3A6C000F) 5 x ReadString (legacy, discarded);
//   K x ReadString -> typeNames, K = (ver >= 0x3A6C00B0) ? 8 : 6
//       (loc_5E7C9C: end = base+0x200; loc_5E7DDF: end = base+0x180);
//   ReadDword cellScale; ReadDword heightScale;               // FLOATS
//   if (ver <= 0x3A6C000F) 256 x ReadString (legacy, discarded);
//   floor = VIBE_Floor_LoadFromHeightmap(ctx) @0x5bd44c;
//   if (ver >= 0x3A6C000E) { ReadVec3 origin;
//       if (floor) floor dwords[36..38] (+144/+148/+152) = origin; }  // origin
//       OVERWRITES the placement the consumer derived (see FloorPlacement)
//   free all temp arrays (the Floor COPIES them).
//
// WHAT THE GRIDS ARE (consumer evidence, VIBE_Floor_LoadFromHeightmap @0x5bd44c):
//   * heights      -> floor+16 "d3_fl:Height", N*N elevation bytes (floor+0 = N,
//                     floor+12 = N-1, floor+8 = N*N-1). BMP fallback (editor):
//                     "boden/<name>.bmp".
//   * textureGrid  -> floor+20 "d3_fl:Texture", N*N per-cell TERRAIN-TYPE
//                     indices; after the 8 Floor_LoadTexture(@0x5bd010) slots
//                     are filled from typeNames, the grid is MIN-NORMALIZED
//                     (min/max scan, then every byte -= min) so cell values are
//                     0-based indices into the 8-slot texture table.
//                     BMP fallback "boden/<textureName>.bmp".
//   * lightOffsets -> floor+32 "d3_fl:LightOffset", 4*N*N bytes (4 light-offset
//                     bytes per cell).
//   * waterHeights -> floor+24 "d3_fl:Water(height)", N*N bytes; fallback
//                     "boden/<textureName>_w.bmp". floor byte+7277 = (u8)count;
//                     floor+6624 = copy of the 344*count region records.
// =============================================================================
namespace guild::render {

// ---- LoadFloorRegions version gates (gilde.exe @0x5e78a8) -------------------
constexpr u32 kFloorVerMin        = 0x3A6C0009u; // below: "Incompatible Floor-Versions"
constexpr u32 kFloorVerOriginVec3 = 0x3A6C000Eu; // >=: trailing origin vec3
constexpr u32 kFloorVerLegacyStrs = 0x3A6C000Fu; // <=: 5 + 256 legacy strings
constexpr u32 kFloorVerWater      = 0x3A6C00AAu; // >=: water flag + region block
constexpr u32 kFloorVerVec4Raw    = 0x3A6C00ACu; // >=: rec vec4 +24 NOT scaled by flt_62BEA0
constexpr u32 kFloorVerGrids      = 0x3A6C00ADu; // >=: ArrayQuick grids present
constexpr u32 kFloorVerTypeNames8 = 0x3A6C00B0u; // >=: 8 type names (else 6)
constexpr u32 kFloorVerWaterV20   = 0x3A6C00B6u; // >=: rec+20 from stream (else 63.0f)
constexpr u32 kFloorVerWaterTail  = 0x3A6C00B9u; // >=: rec byte+340 from stream (else 0)
constexpr u32 kFloorVerLightOffs  = 0x3A6C00BBu; // >=: lightOffsets ArrayQuick present

// One VIBE_Bio_ReadArrayQuick @0x5dcca0 record: two length dwords + payload.
// `accepted` mirrors the original's count==expectedCount gate — when false the
// payload bytes were skipped (Vfs_Seek forward) and the engine got a NULL array.
struct SceneFloorArray {
    u32 elemSize = 0;       // 1st length dword
    u32 count    = 0;       // 2nd length dword (compared against expectedCount)
    bool accepted = false;  // count == expectedCount and payload fully read
    std::vector<u8> data;   // count*elemSize payload bytes (when accepted)
};

// One water-region record of the floor block (the 344-byte "d3_int_io:
// FloorWaterRegions" / "d3_fl:Water(regions)" stride). Stream fields only —
// the loader's texture side effects (Texture_LoadByName @0x5da714 with flags
// (texByteC&1)|(4*(texByteB&0xF)), then Texture_UploadToSurface @0x5db234;
// the returned Texture* is stored at rec+0 AND rec+4) are NOT replayed here.
//
// RUNTIME LAYOUT + RENDER PATH (wave-5 re-decompile @0x5bf22c / @0x5be668 /
// @0x5be428 — HANDOFF to the floorwater (W5-WATER) owner):
//   * The loader copies `count` of these 344-byte records to Floor+6624
//     (== v78[1656] dword index) and stores the count byte at Floor+7277.
//   * rec+0 (this struct's first runtime dword, the Texture*/mesh ptr) is the
//     animation handle: the Phase-2 tail of the walk calls
//       VIBE_Floor_AnimateWaterVertices(globalMeshList, *(Floor+7277),
//                                       *(Floor+6624))   @0x5bf22c:1848
//     which iterates the records (86-dword / 344-byte stride), and for each with
//     a live rec+0 cycles its texture group member (rec+114 low nibble indexes
//     dword_5D93C8; rec+112 = group size) and wobbles its 16 quad vertices
//     (rec+308.. via the dbl_628AEC..dbl_628B24 sin/cos table, phases at
//     rec+312..316 = a3[78..81], scroll rec+328/332 = a3[82/83]).  ← W5-WATER.
//   * The per-TILE water tint/UV is applied in VIBE_Floor_TransformTileGeometry
//     @0x5be668: a tile poly-cell's region id byte (poly-cell+16) selects the
//     record `Floor+6624 + 344*id`; rec+20 (`param20`) scales the wave height
//     (`*(rec+20) * v85`, v85 = flt_628B2C - |sunDir.flt_5CA2B0|) and rec+8
//     (`packedFlags`) bit0 (rec+10&1) picks the flat-shade vs UV branch.
// The render-relevant fields below are exactly param20 (rec+20), packedFlags
// (rec+8), and vecA/vecB (rec+24/+40, the region quad corner offsets the wobble
// is added to). They are all parsed; the floorwater agent owns the animation +
// the Floor+6624 array build + the per-tile id lookup binding.
struct SceneFloorWaterRegion {
    std::string texName;    // e.g. "Wasser_Teich_Fluss_blau" (64-byte slot)
    u8 texByteA = 0;        // 1st byte after the name — read @0x5e79ea, unused
    u8 texByteB = 0;        // 2nd byte — (b&0xF)*4 into the texture-load flags
    u8 texByteC = 0;        // 3rd byte — bit0 into the texture-load flags
    u8 flagBytes[4] = {0, 0, 0, 0}; // the next 4 stream bytes, packed into rec+8:
    u32 packedFlags = 0;    // rec+8 = b0 | b1<<8 | ((b2&1)|((b3&1)<<1))<<16
    float param20 = 0;      // rec+20: ver<0x3A6C00B6 -> 0x427C0000 (63.0f), else stream
    u32 raw12 = 0;          // rec+12 (raw dword from the stream)
    u32 raw16 = 0;          // rec+16 (raw dword from the stream)
    float vecA[4] = {0, 0, 0, 0}; // rec+24..36 (ReadVec4; ver<0x3A6C00AC: each
                                  // component *= flt_62BEA0 @0x62BEA0 = 0x3EAAAAAB ~ 1/3)
    float vecB[4] = {0, 0, 0, 0}; // rec+40..52 (ReadVec4)
    u8 tail340 = 0;         // rec+340: ver<0x3A6C00B9 -> 0, else low byte of a dword
};

// The fully-parsed floor block — every stream field of LoadFloorRegions
// @0x5e78a8 (the 676-byte context), field-for-field.
struct SceneFloorBlock {
    bool ok = false;            // grammar replayed without running off the stream
    bool headerOk = false;      // scene header (0x5e7e38) parsed
    bool floorPresent = false;  // the floor-flag byte was nonzero
    u32 ver = 0;                // the scene tag (= LoadFloorRegions' edx arg)

    std::string name;           // ctx+0   "<scene>_height"
    u32 gridN = 0;              // ctx+156 N (grid edge)
    SceneFloorArray heights;    // ctx+64  N*N elevation bytes
    SceneFloorArray lightOffsets; // ctx+140 4*N*N bytes (ver >= 0x3A6C00BB)
    u8  waterFlag = 0;          // the water presence byte (ver >= 0x3A6C00AA)
    i32 waterRegionCount = 0;   // ctx+152
    SceneFloorArray waterHeights; // ctx+68 N*N bytes
    std::vector<SceneFloorWaterRegion> waterRegions; // ctx+160 records
    std::string textureName;    // ctx+72  "<scene>_texture"
    SceneFloorArray textureGrid; // ctx+136 N*N terrain-type indices (pre-normalize)
    int typeNameCount = 0;      // 8 (ver >= 0x3A6C00B0) or 6
    std::string typeNames[8];   // ctx+164+64*i ("SAND", "EINFACHER_WEG", ...)
    float cellScale = 0;        // ctx+144 (FLOAT read via Bio_ReadDword)
    float heightScale = 0;      // ctx+148 (FLOAT read via Bio_ReadDword)
    bool hasOrigin = false;     // ver >= 0x3A6C000E
    float origin[3] = {0, 0, 0}; // trailing vec3 -> floor +144/+148/+152 override
};

// Parse the WHOLE floor block out of an in-memory scene stream (a stadt_*.ed3
// buffer or the embedded .cty scene blob): replays the 0x5e7e38 header +
// 0x5e67c8 object skip-walk to the floor flag, then the full 0x5e78a8 grammar.
SceneFloorBlock ParseSceneFloorBlock(const u8* ed3, std::size_t size);
inline SceneFloorBlock ParseSceneFloorBlock(const std::vector<u8>& v) {
    return ParseSceneFloorBlock(v.data(), v.size());
}

// The floor-regions body only (gilde.exe 0x5e78a8, post floor-flag): `r` must
// sit on the first byte of the block; `ver` is the scene tag. Exposed for tests
// and for callers that already walked the scene. headerOk/floorPresent are set
// true (the caller got there).
class SceneReader; // render/scene_load.h
SceneFloorBlock ParseFloorRegions(SceneReader& r, u32 ver);

// ---------------------------------------------------------------------------
// COMPATIBILITY view — the original wave-3 heights-only record. Field mapping
// onto the real grammar (see the file header):
//   sizeX = N (the ReadDwordSwapArgs dword), sizeY = ArrayQuick elemSize,
//   third = ArrayQuick count; heights = the payload (N*N in shipped scenes).
// ---------------------------------------------------------------------------
struct SceneFloorHeights {
    bool ok = false;
    std::string name;        // "<scene>_height"
    u32 sizeX = 0;           // N — grid edge (64 / 128, power of two)
    u32 sizeY = 0;           // ArrayQuick elemSize (== N in every shipped scene)
    u32 third = 0;           // ArrayQuick count    (== N; the accept gate)
    std::vector<u8> heights; // count*elemSize payload (row-major, N*N)
};

// Heights-only parse (compatibility wrapper over ParseSceneFloorBlock).
// Returns ok=false when the header is invalid, the floor flag is zero, the
// ArrayQuick gate rejects (count != N), or the grid is not square/pow2/<=4096.
SceneFloorHeights ParseSceneFloorHeights(const u8* ed3, std::size_t size);
inline SceneFloorHeights ParseSceneFloorHeights(const std::vector<u8>& v) {
    return ParseSceneFloorHeights(v.data(), v.size());
}

// ---------------------------------------------------------------------------
// PURE-MATH mirrors of the VIBE_Floor_LoadFromHeightmap @0x5bd44c derivations
// (no Floor struct here — the terrain module owns it). Exact constants:
//   flt_628ADC @0x628ADC = 0x3F000000 (0.5)
//   flt_628AE0 @0x628AE0 = 0xC2800000 (-64.0)  [get_int verified: 3263168512;
//                          supersedes the earlier -50.0 (0xC2480000) misread]
//   floor+7268 = 0x41A00000 (20.0), floor+7272 = 0x42200000 (40.0)
// ---------------------------------------------------------------------------
struct FloorPlacement {
    float originX = 0;  // floor+144 = (double)(-N) * 0.5 * cellScale
    float originY = 0;  // floor+148 = heightScale * -64.0
    float originZ = 0;  // floor+152 = 0.5 * (double)N * cellScale
    float axisU[3] = {0, 0, 0}; // floor+160..168 = (cellScale, 0, 0)
    float axisV[3] = {0, 0, 0}; // floor+176..184 = (0, 0, -cellScale)
    float axisH[3] = {0, 0, 0}; // floor+192..200 = (0, heightScale, 0)
    i32 tileSpan = 0;   // floor+4 = N/8 (signed division)
};
// NOTE: when the stream version is >= 0x3A6C000E the trailing origin vec3
// OVERWRITES originX/Y/Z (floor dwords[36..38]) right after the Floor is built.
FloorPlacement DeriveFloorPlacement(i32 n, float cellScale, float heightScale);

// The 0x5bd44c texture-grid min-normalization: scan min/max over the N*N bytes
// (max starts 0, min starts 255 — the exact `if (max <= b)` / `if (min >= b)`
// updates), then subtract min from EVERY byte so cell values become 0-based
// indices into the 8 Floor_LoadTexture slots. outMin/outMax receive the scan
// results when non-null. Empty grids scan to (min=255, max=0), no subtraction.
void NormalizeFloorTextureGrid(u8* grid, std::size_t count,
                               u8* outMin = nullptr, u8* outMax = nullptr);

// ---------------------------------------------------------------------------
// Build the city terrain Heightmap from the parsed floor grid + the city world
// AABB, exactly as VIBE_Heightmap_BuildTerrainMesh @0x5c5610 derives the
// world<->tile mapping over the stored grid (heightmap.h):
//   originX/scaleX, originZ/scaleZ : DeriveGridScaleXZ (1:1,
//                                    flt_628BA4 @0x628BA4 = 0xBFE00000 = -1.75)
//   originY = minY + 1.0                       (@0x5c5b95: fld1; fadd minY)
//   scaleY  = (maxY - minY) * flt_628BA8       (@0x5c5bd8..0x5c5bec)
//     flt_628BA8 @0x628BA8 = 0x3B81848E = 0.0039525721f (~ 1/253.0)
//     — kTerrainScaleYNorm in heightmap.h, the exact dword.
// `storage` receives the heights copy the Heightmap points into (the caller
// keeps it alive alongside `hm`). Returns false when `f` is not ok.
// ---------------------------------------------------------------------------
bool BuildCityHeightmapFromFloor(const SceneFloorHeights& f,
                                 const float lo[3], const float hi[3],
                                 Heightmap& hm, std::vector<u8>& storage);
// Same build over the full block (heights must have been accepted).
bool BuildCityHeightmapFromFloor(const SceneFloorBlock& b,
                                 const float lo[3], const float hi[3],
                                 Heightmap& hm, std::vector<u8>& storage);

} // namespace guild::render
