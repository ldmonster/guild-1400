#pragma once
// Map / walkability grid + entity collision stamping for the Guild simulation.
// Faithful 1:1 port of the VIBE_Map_* family from gilde.exe.
//
// The "map" the sim walks on is the render heightmap's tile grid: a size*size
// array of 24-byte tile records (guild::render::Heightmap::entries, type byte at
// +0). The sim treats the first byte of each 24-byte record as a *collision /
// terrain-type* cell:
//   0  -> empty / outside-walkable  (blocked: a tile must be != 0 to be walkable)
//   13 -> explicitly blocked obstacle
//   other -> a terrain type id (walkable; indexes the per-profile cost table)
//
// Translated functions:
//   VIBE_Map_IsTileWalkable        0x486068
//   VIBE_Map_CheckPathWalkable     0x408740   (mesh-coupled; see note)
//   VIBE_Map_BuildCollisionGrid    0x404b90
//   VIBE_Map_ClearCollisionRegion  0x404bf4
//   VIBE_Map_StampCollisionArea    0x404d10
//   VIBE_Map_StampEntityCollision  0x404ef8
//   VIBE_Map_CheckPathWalkable     0x408740
//   VIBE_Path_FindNearestFreeTile  0x4860c8
//   VIBE_Path_FindNearestTileToPoint 0x4861d8
//   VIBE_Map_TraceLineOfSight      0x406f10
//
// The grid is addressed exactly as the originals: cell (x,y) lives at byte
// 24*(x + size*y) of the entries buffer; the collision/type byte is entries[..+0].
#include "guild/common/types.h"

namespace guild::render { struct Heightmap; }

namespace guild::sim {

// ---------------------------------------------------------------------------
// Map grid view. Mirrors the two fields the originals read off the scene-map
// object (`*((_DWORD*)off_649D64 + 44)` -> map; map+32 = size; map+36 = entries):
//   size    : the square grid edge (map+0x20).
//   entries : size*size * 24-byte tile records; byte +0 of each is the cell.
// We model it as a thin view over the heightmap's grid so the sim and render
// layers share one buffer (the originals literally read the heightmap entries).
// ---------------------------------------------------------------------------
struct MapGrid {
    i32 size;       // +0x20 of the scene-map object (grid edge length)
    u8* entries;    // +0x24: size*size 24-byte tile records (cell byte at +0)
};

// Tile-record stride in bytes (the 24-byte heightmap tile entry).
constexpr int kTileEntryStride = 24;
// Sentinel cell value meaning "explicitly blocked obstacle".
constexpr u8  kCellBlocked = 13;

// Build a MapGrid view over a render heightmap (size/entries fields).
MapGrid MapGridFromHeightmap(const guild::render::Heightmap* hm);

// Read/write the collision/type byte of cell (x,y). No bounds checking.
u8   MapCellAt(const MapGrid& g, int x, int y);
void MapSetCellAt(const MapGrid& g, int x, int y, u8 value);

// gilde.exe 0x486068 — VIBE_Map_IsTileWalkable  (__usercall eax=(x@eax,y@edx)).
// Returns true when (x,y) is inside the grid AND the cell byte is nonzero and
// not the blocked sentinel (13). The original samples the active scene map
// (off_649D64); here the grid is passed explicitly.
//   NOTE on bounds: the original tests x>size||x<0||y<0||y>size (inclusive of
//   `size`), then only samples the cell when x<size && y<size; for x==size or
//   y==size it leaves the result at the seed value a3. We reproduce that exactly.
bool MapIsTileWalkable(const MapGrid& g, int x, int y, int seed);

// Convenience wrapper: walkable with seed 0 (the common call form).
inline bool MapIsTileWalkable(const MapGrid& g, int x, int y) {
    return MapIsTileWalkable(g, x, y, 0);
}

// gilde.exe 0x404d10 — VIBE_Map_StampCollisionArea
//   (__userpurge eax=(cx@eax, cy@edx, value@cl, profile@bl, radius@a5)).
// Stamps `value` into a filled diamond (Manhattan disk) of the given `radius`
// centred on (cx,cy), clipped to the grid interior [1, size-2]. Updates the
// dirty-rectangle globals (clamped). Returns 1 when a grid was resolved, else 0.
// `profile` selects the map (0 == primary scene map). Returns 0 if no map.
int MapStampCollisionArea(const MapGrid& g, int cx, int cy, u8 value, int radius);

// gilde.exe 0x404b90 — VIBE_Map_BuildCollisionGrid: copies each cell's type byte
// from the 24-byte tile entries into a flat size*size shadow buffer `out`.
void MapBuildCollisionGrid(const MapGrid& g, u8* out);

// gilde.exe 0x404bf4 — VIBE_Map_ClearCollisionRegion: restores the cells inside
// the current dirty rectangle from the flat shadow buffer `shadow`, then resets
// the dirty rectangle to empty.
void MapClearCollisionRegion(const MapGrid& g, const u8* shadow);

// gilde.exe 0x4860c8 — VIBE_Path_FindNearestFreeTile
//   (__usercall eax=(cx@eax, cy@edx), out cols ecx/ebx).
// Spirals outward (expanding Manhattan rings, up to 64 rings) from (cx,cy) and
// returns the first walkable tile; writes its column into *outX and row into
// *outY. Returns 1 on success, 0 if none found within 64 rings.
int PathFindNearestFreeTile(const MapGrid& g, int cx, int cy, int* outX, int* outY);

// gilde.exe 0x4861d8 — VIBE_Path_FindNearestTileToPoint
//   (__usercall eax=fn(frame@eax, outX@edx, mode@ecx, outY@ebx)).
// Projects an object frame's origin point (frame[19..21], transformed through its
// bone chain) into the heightmap, then spirals expanding Manhattan rings 0..63
// around that tile. For every WALKABLE tile in the disk it measures the world-space
// distance from the object's world point and tracks BOTH the nearest (min) and the
// farthest (max) walkable tile, each gated to distance > 30.0 (dbl_61B12C). When
// `mode` is nonzero it returns the FARTHEST tile (outX/outY), otherwise the NEAREST.
// Returns 1 when any qualifying tile was found, else 0 (and the out cells are
// untouched). `hm` is the active scene heightmap (== the collision map). Uses the
// render TileToWorld / WorldToTileWithHeight samplers and the util bone-chain
// transform exactly as the original (off_649D64+44 resolves to one object).
int PathFindNearestTileToPoint(const guild::render::Heightmap* hm,
                               const float* frame, int mode,
                               int* outX, int* outY);

// gilde.exe 0x406f10 — VIBE_Map_TraceLineOfSight
//   (__userpurge eax=fn(hm@eax, query@edx, targetCol@ecx, ref@ebx,
//                       targetRow, outCol, outRow, maxRings, flags)).
// Finds an approach/visibility cell near target tile (targetCol,targetRow) by
// scanning expanding Manhattan rings (up to `maxRings`). `flags` selects the mode:
//   * bit4 (0x10): if the target cell itself is walkable, return it immediately;
//     otherwise use `ref` (a4[0..2]) as the target world point.
//   * bit0 (0x01) OR bit4: "best approach" scan — over all walkable cells keep the
//     one MINIMIZING dist(query, cell) + dist(targetWorld, cell); on no hit it
//     falls back to (targetCol,targetRow). Returns 1 if any out cell was set.
//   * otherwise: "first walkable" spiral — return the first walkable cell found
//     (immediate), else fall back to the target cell. Returns 1.
// Walkable == cell byte nonzero and != 13. `query` and `ref` are world points
// (x,y,z); the target world point is `ref` when bit4 is set, else TileToWorld of
// the target tile. Writes the chosen column/row into *outCol/*outRow. Returns 0
// only when hm/entries/out pointers are null. Uses the render TileToWorld sampler.
//   NOTE `query` is dereferenced unconditionally (as the original reads *a2 before
//   branching), so it must be a valid 3-float point even on the first-walkable
//   path; `ref` is read only when bit4 is set.
int MapTraceLineOfSight(const guild::render::Heightmap* hm, const float* query,
                        int targetCol, const float* ref, int targetRow,
                        int* outCol, int* outRow, int maxRings, int flags);

// gilde.exe 0x404ef8 — VIBE_Map_StampEntityCollision
//   (__usercall eax=fn(value@dl, radius@ecx, profile@bl, world@eax)).
// Resolves the scene map for `profile` (the original indexes a global per-profile
// scene-object array; here the caller passes the resolved heightmap `hm`), maps the
// world point `world` to its tile via WorldToTileWithHeight, and stamps a Manhattan
// diamond of `value` (radius `radius`, profile `profile`) there via
// MapStampCollisionArea. Returns the StampCollisionArea result, or -1 if `hm` is
// null or the world point is off the map.
int MapStampEntityCollision(const guild::render::Heightmap* hm, const float* world,
                            u8 value, int profile, int radius);

// gilde.exe 0x408740 — VIBE_Map_CheckPathWalkable
//   (__usercall eax=fn(character@eax, meshAction@edi)).
// Checks that the REMAINING waypoints of a walk path are still walkable: walks the
// packed (col,row) waypoint buffer from `curIdx` to `cap-2` (the original clamps the
// upper bound to cap-2 and curIdx to >=1) and returns 0 the moment any tile is blocked
// (cell byte 0 or 13). If every remaining waypoint is clear (or the range is empty) it
// returns `seed` (the original passes the prior result through untouched). `g` is the
// mesh collision grid; `waypoints` is the 2-bytes-per-point buffer; `cap`/`curIdx` are
// the walk record's +240/+248 fields.
int MapCheckPathWalkable(const MapGrid& g, const u8* waypoints, int cap, int curIdx,
                         int seed);

// gilde.exe 0x577320 — VIBE_Map_FindNearestDoorCell (scan portion)
//   (__userpurge eax=fn(record@eax, startCol@edx, doorCol@ecx, startRow@ebx, doorRow)).
// Locates the nearest "door" cell (terrain-type byte 6 or 11) to (startCol,startRow)
// by a full-grid scan minimizing the squared tile offset
//   (row-startRow)^2 + (col-startCol)^2 ; the initial best is 2*size*size.
// The scan only runs when no door is supplied yet (`*doorCol < 0 || *doorRow < 0`);
// otherwise the passed-in door cell is kept. On success *doorCol/*doorRow hold the
// chosen door tile and the function returns 1; returns 0 if none found. (The
// original then builds a waypoint route to the door via PathBuildWaypointList; that
// route step is left to the caller, which owns the waypoint buffer.)
int MapFindNearestDoorCell(const MapGrid& g, int startCol, int startRow,
                           int* doorCol, int* doorRow);

// ---- Dirty-rectangle globals (gilde.exe dword_62D09C..dword_62D0A8) ----------
// The stamp/clear pair tracks the modified region so the renderer can refresh
// only what changed. Exposed for tests; reset by MapResetDirtyRect().
extern int g_collisionDirtyMinX; // dword_62D09C
extern int g_collisionDirtyMaxX; // dword_62D0A0
extern int g_collisionDirtyMinY; // dword_62D0A4
extern int g_collisionDirtyMaxY; // dword_62D0A8
void MapResetDirtyRect();

} // namespace guild::sim
