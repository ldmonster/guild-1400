#pragma once
// Pathfinding over the Guild map grid. Faithful 1:1 port of the VIBE_Path_* core
// from gilde.exe: a bidirectional weighted A* whose search nodes are stored
// *inline* in the 24-byte heightmap tile entries (so a route reuses the same
// buffer the renderer/collision share).
//
// Translated functions:
//   VIBE_Path_FindRoute            0x43be20   (bidirectional A* driver)
//   VIBE_Path_ExpandNode           0x43c1c4   (expand one frontier node)
//   VIBE_Path_ReverseParentChain   0x43c594   (stitch fwd+bwd chains -> path)
//   VIBE_Path_BuildWaypointList    0x43bd70   (route -> packed tile waypoints)
//   VIBE_Path_ResamplePolyline     0x406810   (decimate a waypoint list by stride)
//
// Tile-entry search-node layout (24 bytes, recovered from the accessors):
//   +0  u8   cell type byte (terrain id; index into the cost table). value<0
//            (signed) means "blocked" via the cost table (cost < 0).
//   +2  u16  visitStamp  (== g_pathGeneration when this node belongs to the
//            current search; stale entries are reinitialised on touch).
//   +4  f32  f = g + h   (total estimated cost; open-list sort key)
//   +8  f32  g           (accumulated cost from the search origin)
//   +12 f32  h           (heuristic to the goal)
//   +16 u8   flags: bit0=on-open-list, bit1=expanded/closed,
//                   bit2=forward-touched, bit3=backward-touched.
//   +18 u16  openPrev    (open-list intrusive doubly-linked list, -1 == null)
//   +20 u16  openNext
//   +22 u16  parent      (path back-link, -1 == null)
//
// The node index of tile (col,row) is (row << shift) + col where
// shift = log2(size). The two search frontiers (from start, from goal) expand
// alternately until a node is touched by both (flags bits 2|3 == 0xC), at which
// point ReverseParentChain stitches the half-paths.
#include "guild/common/types.h"

namespace guild::sim {

struct MapGrid;

// ---------------------------------------------------------------------------
// Cost model (gilde.exe unk_62E630 + dword_62E75C/flt_62E770 + flt_62E610).
// ---------------------------------------------------------------------------
// Per-terrain-type traversal cost, indexed by the tile's type byte (+0). A
// negative entry marks the type impassable. FIVE profiles are shipped
// (unk_62E630..0x62E75B, 60 bytes each; dword_765308 = &unk_62E630 +
// 60*profile at 0x43bfe9). Profile 0:
//   {999, 20, 30, 30, 30, 30, 1, 30, 40, 30, -1, 10, 0.5, -1, 99}
constexpr int   kPathCostTypes = 15;
constexpr int   kPathCostProfileCount = 5;
extern const float kPathCostProfiles[kPathCostProfileCount * kPathCostTypes];
extern const float* const kPathTypeCost;  // profile-0 view

// Per-profile diagonal/heuristic weights. dword_62E75C[profile] (origin cost
// scale, 0.25) and flt_62E770[profile] (heuristic weight, 0.25). Heuristic
// manhattan weight dword_765300 = 7.0; step multipliers flt_62E610 =
// {1,1,1,1, √2,√2,√2,√2} for the 4 orthogonal + 4 diagonal neighbours.
constexpr float kPathHeuristicManhattanWeight = 7.0f;   // dword_765300

// A computed route step (one tile of the result path).
struct PathStep {
    int x;  // tile column
    int y;  // tile row
};

// gilde.exe 0x43be20 — VIBE_Path_FindRoute
//   (__userpurge eax=(map@eax, startY@dx, startX@ecx, goalY@ebx, goalX@a5, profile@a6)).
// Runs the bidirectional A*. Returns the linear node index of the start node on
// success (the route is reconstructed by following the `parent` (+22) links from
// the start node to the goal), or 0xFFFF (== -1 as u16) when no path exists.
// `profile` selects the cost table (5 shipped; profile 3 used by 0x577320).
//   NOTE the original's odd arg interleave (startX in ecx, startY in dx): a tile
//   (col,row) maps to node index (row<<shift)+col.
int PathFindRoute(const MapGrid& g, int startX, int startY,
                  int goalX, int goalY, int profile);

// gilde.exe 0x43bd70 — VIBE_Path_BuildWaypointList. Convenience wrapper that runs
// FindRoute and walks the parent chain into `out` (capacity `maxSteps`), writing
// each tile's (col,row). Returns the number of steps written, or -1 on no-path /
// out-of-range endpoints. The first step is the start tile.
int PathBuildWaypointList(const MapGrid& g, int startX, int startY,
                          int goalX, int goalY, int profile,
                          PathStep* out, int maxSteps);

// Returns the traversal cost of a terrain type byte (negative == impassable).
float PathTypeCost(u8 typeByte, int profile = 0);

// ---------------------------------------------------------------------------
// Waypoint polyline view (the `a1` struct VIBE_Path_ResamplePolyline reads).
//   +0  (dword) count   number of (col,row) byte-pairs in `tiles`
//   +8  (dword) tiles   packed 2-bytes-per-point buffer (col,row, col,row, ...)
// (The +4 slot is unused by the resampler; modeled as padding.)
// ---------------------------------------------------------------------------
struct PathPolyline {
    int count;     // +0
    int _pad4;     // +4
    u8* tiles;     // +8  (2 bytes per point: col, row)
};

// gilde.exe 0x406810 — VIBE_Path_ResamplePolyline
//   (__usercall eax=fn(line@eax, out@edx, frame@ebx)).
// Decimates the `line` waypoint polyline by a tile-scale-derived stride and packs
// the kept (col,row) pairs into `out` (2 bytes per point). The stride is
//   stride = (int)(12.0 / scaleX + 0.5)    (scaleX = *(float*)(frame+16))
// clamped to a minimum of 1; points are taken at indices 1, 1+stride, 1+2*stride,
// ... while < count, and the FINAL point (index count-1) is always appended unless
// the walk already landed on it. Returns the number of points written to `out`.
//   `out` must hold up to ceil(count/stride)+1 pairs. `scaleX` is the heightmap's
//   world-units-per-tile (frame+16); 12.0 / 0.5 are flt_6106E0 / flt_6106E4.
int PathResamplePolyline(const PathPolyline* line, u8* out, float scaleX);

} // namespace guild::sim
