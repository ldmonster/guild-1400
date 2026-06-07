#pragma once
// gilde.exe — Combat tactical-AI tile search: the threat-field scoring and the
// "safest / most-threatened / escape" tile pickers the order-tick AI uses to
// place a unit (namespace guild::sim). MODULE: combat (prefix VIBE_Combat_*).
//
// SCOPE. This file translates the deterministic tile-evaluation math the combat
// AI runs when it needs to move a unit defensively (flee the most dangerous
// tile, advance onto the most-pressuring tile, or sprint to a scripted escape
// node):
//   * VIBE_Coord_Distance3D            @0x4865ec — tile<->tile world distance.
//   * VIBE_Combat_ComputeTileThreatScore @0x48ae54 — signed threat at a tile:
//       sum over EVERY live unit of  +/- 70 / dist   (allies positive, enemies
//       negative; only counted within `radius`).
//   * VIBE_Combat_FindSafestTileInRange @0x48b01c — diamond spiral over a radius,
//       returns the passable tile with the MINIMUM threat score.
//   * VIBE_Combat_FindMostThreatenedTile @0x48b1b4 — same spiral, MAXIMUM score.
//   * VIBE_Combat_EscapeTileCallback   @0x48b350 — scene-walk collector that
//       gathers every "sp_ESCAPE" node into a flat array.
//   * VIBE_Combat_FindNearestEscapeTile @0x48b384 — nearest collected escape node
//       to a unit (Euclidean over the node's bone-chain world origin).
//
// These are the RULES (pure tile/threat math). Their world/scene leaves —
// heightmap tile<->world (reused from guild::render), the per-unit world pose,
// the live-unit lookup, the team rosters, and the scene-graph walk that finds
// the escape nodes — are surfaced through a small context object so the math is
// exercisable in isolation (mirrors the OrderWorldContext pattern in
// combat_orders.h). The originals address static globals (the active heightmap
// off_649D64[44], the two team rosters dword_631208+128/+192, the 32-slot unit
// table word_B5A350, and the scene root); the model is observationally identical.
//
// Determinism: no RNG here — the scoring and spiral traversal are fully
// deterministic and covered by golden vectors.
#include "guild/common/types.h"
#include "render/heightmap.h"

#include <functional>
#include <vector>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Threat-field context: the world/scene leaves the tile evaluators read.
// ---------------------------------------------------------------------------
//
// The originals key everything off the acting unit pointer `a1`:
//   * its team id            -> *(a1 + 364)
//   * its world pose         -> *(*(a1 + 388) + 52) + 76/80/84
// and the two team rosters dword_631208 + 128 / + 192 (16 dwords each, -1 ==
// empty) which hold unit IDs. Whether roster +192 or +128 is the "ally" set
// depends on whether the acting unit's team matches dword_6311E8 (side A).
// Threat then resolves each roster ID to a live unit via FindUnitById, gates on
// the unit's "alive" byte, and uses its world pose. We model exactly this:
struct ThreatField {
    // The active terrain grid (off_649D64[44]); used for tile<->world.
    const guild::render::Heightmap* heightmap = nullptr;

    // The two team rosters: IDs of the units on each side (-1 == empty slot).
    // `allies` is the side the acting unit belongs to; `enemies` the other side.
    // ComputeTileThreatScore subtracts ally contributions and adds enemy ones
    // relative to the tile (allies make a tile SAFER, enemies more dangerous) —
    // see the sign convention in the function comment.
    const i32* allyRoster  = nullptr;   // 16 entries (the acting unit's own side)
    const i32* enemyRoster = nullptr;   // 16 entries (the opposing side)
    i32        rosterCount = 16;

    // Resolve a roster unit id -> opaque unit handle (FindUnitById). Returns 0
    // when the id is absent. The handle is passed straight back to the pose/alive
    // accessors below.
    std::function<const void*(i32 unitId)> findUnitById;

    // True when the resolved unit is alive (the "alive" byte, *(u+8)).
    std::function<bool(const void* unit)> unitAlive;

    // The resolved unit's world position (mesh origin x,y,z) into out[3]
    // (*(*(u+388)+52)+76/80/84). Only called for alive units.
    std::function<void(const void* unit, float out[3])> unitWorldPos;

    // The acting unit handle (for the self-exclusion in the enemy-side loop —
    // the original skips `v15 == a1`). Resolve via findUnitById too if needed.
    const void* self = nullptr;
};

// gilde.exe 0x4865ec — VIBE_Coord_Distance3D  (__usercall, st0 = (...)).
// Euclidean distance between two tiles in WORLD space: converts each tile to a
// world point via the heightmap and returns the 3D distance. (Used by the
// tactical scorers via the heightmap; exposed for completeness/testing.)
//   d = |world(tileAx,tileAz) - world(tileBx,tileBz)|
double CoordDistance3D(const guild::render::Heightmap* hm,
                       int tileAx, int tileAz, int tileBx, int tileBz);

// gilde.exe 0x486524 — VIBE_Combat_DistanceObjectToTarget (__usercall, st0).
// Planar (X,Z) distance from a unit's mesh world origin to a TILE's world point.
// The original converts (tileX, tileZ) to world, forces the world Y term to 0,
// and the unit Y term cancels (the "0.0 * 0.0" in the sqrt) — so only X and Z
// count. `unitPos[3]` is the unit's mesh origin (*(*(unit+388)+52)+76/80/84).
//   d = sqrt( (tileWX - unitX)^2 + (tileWZ - unitZ)^2 )
double DistanceObjectToTarget(const guild::render::Heightmap* hm,
                              const float unitPos[3], int tileX, int tileZ);

// The signed-threat unit weight (flt_61B87C). Allies subtract  weight/dist from
// the score (safer), enemies add it (more dangerous).
constexpr float kThreatUnitWeight = 70.0f;   // 0x428C0000

// gilde.exe 0x48ae54 — VIBE_Combat_ComputeTileThreatScore (__userpurge, st0).
// Signed threat at tile (tileX, tileZ) for the acting unit:
//   score = 0
//   for each ALIVE enemy unit within `radius` of the tile:  score += 70 / dist
//   for each ALIVE ally  unit within `radius` (excluding self): score -= 70 / dist
// (i.e. enemies raise the danger, friendly units lower it.) Distance is the 3D
// world distance from the tile's world point to the unit's mesh origin.
double ComputeTileThreatScore(const ThreatField& field, int tileX, int tileZ,
                              float radius);

// The world-radius factors the two spiral searchers pass to the scorer:
//   FindSafestTileInRange:    radius = steps * tileScaleX * 2.0   (dbl_61B884)
//   FindMostThreatenedTile:   radius = steps * tileScaleX * 2.0   (dbl_61B894)
constexpr double kSafestRadiusFactor = 2.0;   // dbl_61B884
constexpr double kThreatRadiusFactor = 2.0;   // dbl_61B894
// Sentinels the searchers compare the running extreme against.
constexpr float kSafestInitScore = 1000000000.0f; // v27 init / flt_61B88C sentinel
constexpr float kThreatMinScore  = 5.0f;          // flt_61B890 (must exceed to count)

// Result of a spiral tile search: the chosen tile and whether one was found.
struct TileSearchResult {
    int  tileX = 0;
    int  tileZ = 0;
    bool found = false;
};

// gilde.exe 0x48b01c — VIBE_Combat_FindSafestTileInRange (__userpurge, BOOL).
// Walks a diamond (Manhattan) spiral of radius `steps` centred on (centreX,
// centreZ), clamped to the grid, skipping impassable tiles (type byte 0) and the
// special "13" tile type. For each candidate it scores threat with radius
// `steps * tileScaleX * 2.0` and keeps the MINIMUM. Returns true (and the tile
// in `out`) iff some passable tile scored below the 1e9 sentinel.
bool FindSafestTileInRange(const ThreatField& field, int centreX, int centreZ,
                           int steps, TileSearchResult& out);

// gilde.exe 0x48b1b4 — VIBE_Combat_FindMostThreatenedTile (__userpurge, BOOL).
// Same diamond spiral, but keeps the MAXIMUM threat score (the tile under the
// most enemy pressure). Returns true iff the best score is non-zero AND exceeds
// 5.0 (flt_61B890). `out` receives the chosen tile.
bool FindMostThreatenedTile(const ThreatField& field, int centreX, int centreZ,
                            int steps, TileSearchResult& out);

// ---------------------------------------------------------------------------
// Scripted "escape" node search.
// ---------------------------------------------------------------------------
//
// The originals walk the scene graph (VIBE_SceneGraph_WalkAndInvoke, type 192)
// invoking EscapeTileCallback on every node; the callback appends nodes named
// "sp_ESCAPE" to a flat pointer array (and bumps dword_631268). Then
// FindNearestEscapeTile picks the collected node nearest the acting unit's world
// origin (Euclidean over the node's bone-chain world point, fields [19..21]).
//
// We model the scene walk as supplying the collected escape-node origins and the
// acting unit's origin directly, since the walk/string-compare/bone-chain are
// engine leaves. The collector's accept rule (name == "sp_ESCAPE") and the
// nearest-pick math are the translated RULES.

// The node name the collector accepts (aSpEscape @0x61b228).
inline const char* kEscapeNodeName() { return "sp_ESCAPE"; }

// gilde.exe 0x48b350 — VIBE_Combat_EscapeTileCallback (__usercall, al).
// The scene-walk filter: if the node's name (case-insensitively) equals
// "sp_ESCAPE", append the node handle to `out` and return true (always 1, i.e.
// "keep walking"). `nodeName` is the node's name; `node` the opaque handle the
// nearest-pick later reads the world origin from.
bool EscapeTileCollect(const char* nodeName, const void* node,
                       std::vector<const void*>& out);

// gilde.exe 0x48b384 — VIBE_Combat_FindNearestEscapeTile (__usercall, eax).
// Among the collected escape nodes, returns the handle nearest (3D Euclidean) to
// `unitOrigin` (the acting unit's mesh world origin x,y,z). Returns nullptr when
// the list is empty. `nodeOrigin(node, out)` yields a node's world point (the
// original reads node fields [19..21] of its bone-chain-transformed origin).
const void* FindNearestEscapeTile(
    const float unitOrigin[3],
    const std::vector<const void*>& escapeNodes,
    const std::function<void(const void* node, float out[3])>& nodeOrigin);

} // namespace guild::sim
