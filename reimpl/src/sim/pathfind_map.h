#pragma once
// pathfind_map — a slice of the VIBE_Path_* / VIBE_Map_* / VIBE_ObjectSearch_*
// leaves of gilde.exe (32-bit x86, imagebase 0x400000). Faithful 1:1 ports of the
// untranslated functions in those prefixes that the sibling files (path.cpp,
// map.cpp, objectsearch.cpp) had not yet covered.
//
// Two deterministic families dominate:
//   * the ObjectSearch palette/colour probes (0x47a8dc..0x47b1cf): each walks the
//     768-entry person palette in pseudo-random order, stepping by a stride drawn
//     from kPaletteStrideTable, applying a person-eligibility gate and an optional
//     favourability range filter.
//   * the city/road-network map builders (0x592c7c, 0x592d98, 0x5776d8, 0x5286..,
//     0x52e1..): recursive build-chain depth, the road-node layout solver, and the
//     bauplatz-edge rasteriser / city marker placement.
//
// Cross-module callees with no reconstructed target (person eligibility /
// favourability, the object-ring iterator, coordinate projection, object spawning,
// scene/save I/O, light-cache rebuilds) are routed through PathfindMapHooks with
// inert defaults defined in pathfind_map.cpp. Tests install their own. The pure
// deterministic cores are exposed directly for golden vectors.
//
// `Coord_ConvertX` (0x5c6b08) in the originals is the x87 control-word fixup around
// a (int)double truncation; we model it with PathfindMapTruncToInt.
//
// Translated functions (absolute addresses):
//   0x4074c0 VIBE_Path_BuildMarkerPoints
//   0x47a8dc VIBE_ObjectSearch_FindMatchingColors
//   0x47aad4 VIBE_ObjectSearch_FindMatchingColor
//   0x47abfc VIBE_ObjectSearch_FindByPaletteRange
//   0x47ae48 VIBE_ObjectSearch_FindOneByPaletteRange
//   0x47b008 VIBE_ObjectSearch_FindPeopleByPalette
//   0x528bd0 VIBE_Map_LoadCityFile
//   0x52e194 VIBE_Map_SpawnCityTowerMarker
//   0x52e2d0 VIBE_Map_SpawnCityPointMarker
//   0x5776d8 VIBE_Map_RasterizeBauplatzEdge
//   0x592c7c VIBE_Map_ComputeBuildingChainDepth
//   0x592d98 VIBE_Map_ComputeRoadNetworkLayout
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Recovered constants (imagebase 0x400000).
// ===========================================================================
// dword_478410 — 16-entry stride table the palette probes step the 768-entry
// person palette by. Each is co-prime with 768 so the probe walks every entry.
constexpr int kPaletteStrideCount = 16;
extern const int kPaletteStrideTable[kPaletteStrideCount];   // 0x478410
constexpr int kPaletteSize = 768;                             // 0x300

// dbl_61AC00 / dbl_61AC08 / dbl_61AC10 — the favourability "no upper bound"
// sentinel each palette search compares maxR against (== 100.0).
constexpr double kFavorabilityFullRange = 100.0;

// flt_6231A0 / flt_6231A4 — the city-tower marker offset scale factors.
constexpr float kTowerMarkerScaleX = 0.0014285714f;    // 0x3abb3ee7 (~1/700)
constexpr float kTowerMarkerScaleY = 0.0013333333f;    // 0x3aaec33e (~1/750)
// dbl_62562C — the bauplatz-edge "round up half a tile" bias (== 0.9).
constexpr double kRasterEdgeBias = 0.9;

// x87 truncate-toward-zero of a (int)double store (Coord_ConvertX @0x5c6b08).
i32 PathfindMapTruncToInt(double v);

// ===========================================================================
// Cross-module leaf hooks. Records are raw byte buffers, matching the originals'
// *(_TYPE*)(base + off). nullptr / 0 models an empty / inert world.
// ===========================================================================
struct PathfindMapHooks {
    // VIBE_Person_EvaluateCandidateEligibility(refId, candId, filter) (0x5596f8).
    // Returns non-zero when candidate `candId` passes the filter for `refId`.
    int (*evaluateEligibility)(u16 refId, u16 candId, void* filter);
    // VIBE_Ai_ComputePersonFavorability(candId, refId, mode) (0x594330). 0..100.
    double (*computeFavorability)(int candId, u16 refId, int mode);
    // VIBE_Light_SetGrayColorThunk(value, shift) (0x5c6af0) — palette gray fixup.
    void (*setGrayColor)(int value, int shift);
    // VIBE_Person_ComputeOfficeRank(idx, stopAtSelf) (0x58bccc). Used by the people
    // palette search for its rank-delta gate. Reconstructed in person.cpp; routed
    // through a hook so this slice stays self-contained for tests. Inert => 0.
    int (*computeOfficeRank)(u16 idx, int stopAtSelf);

    // --- the object-ring iterator the colour searches walk (0x4784cc) ---
    // VIBE_ObjectRing_AdvanceIterator() returns the current 3-dword record and
    // advances. The colour searches read [0]=id (0 terminates) and [1]=float key.
    const i32* (*objectRingAdvance)();
    int objectRingCount;            // dword_62EB9C — ring length (0 => empty)
    const i32* objectRingPinned;    // dword_62EB8C — the pinned-first record (or null)

    // --- city/marker spawning (0x52e1.., 0x52e2..) ---
    // VIBE_Object_FindByHandle(a, kind, name, d, out) (0x5b7be4) -> record (0=miss).
    void* (*findObjectByHandle)(int a, int kind, const char* name, int d, void* out);
    // VIBE_Object_AttachToUniverseNode(a, xform, name, d) (0x5b3e30) -> node base.
    void* (*attachToUniverseNode)(int a, const float* xform, const char* name, int d);
    void (*lightBuildObjectCache)(void* node);          // 0x5c8218
    void (*loadObjectAnimation)(int a, const char* baf, int c); // 0x426488

    // --- city-file load orchestration (0x528bd0) ---
    void (*universeSwitchActiveSlot)(int a, int b, int c, int d); // 0x5b4a24
    void (*universeResetCurrentSlot)(int a, int b);               // 0x5b44c4
    void (*sceneEnterCity)(const char* a, int b);                 // 0x503a9c
    void (*saveWriteGameFile)(const char* path, const char* tag, int c, int d); // 0x5a348c
    void (*buildingResetAll)();                                    // 0x5896fc
    void (*worldResetPersonTable)();                               // 0x58389c
    void (*worldRelinkObjectOwners)();                             // 0x5838d4
    void (*objectDestroySpawned)(int a, int b);                    // 0x4fff10
    int  (*sceneLoadFromStream)(const char* a, int b, i16 c, int d); // 0x5e7e38

    // --- path marker projection (0x4074c0) ---
    // VIBE_Coord_ProjectPoint(cam, world, out3) (0x407428) — projects a world point.
    void (*coordProjectPoint)(const float* cam, const float* world, i32* out3);
    // VIBE_Heightmap_TileToWorld(grid, col, out, row) (0x5c65d4) -> non-zero on hit.
    int (*heightmapTileToWorld)(int grid, int col, float* out, int row);
};

// Install hooks; nullptr restores the inert defaults. Returns the previous set.
PathfindMapHooks PathfindMapSetHooks(const PathfindMapHooks* hooks);
const PathfindMapHooks& PathfindMapGetHooks();

// ===========================================================================
// ObjectSearch palette / colour probes.
// ===========================================================================
// Shared probe order: starting at `probeStart` (0..767, the live RNG's
// RandomModulo(768)), step by kPaletteStrideTable[strideIndex] modulo 768 for up
// to 768 iterations. `strideIndex` is the live RNG's RandomModulo(16). Passing the
// would-be RNG draws keeps the search deterministic for golden tests.

// gilde.exe 0x47abfc — VIBE_ObjectSearch_FindByPaletteRange. For each of `count`
// (<=3) output slots, probe the palette for the first candidate that passes the
// eligibility gate and (when minR/maxR is a real range) the favourability filter;
// the first slot also honours the pinned object. Writes the found palette indices
// into outIds[0..count-1]. Returns 1 on full success, 0 if any slot found nothing.
int ObjectSearchFindByPaletteRange(const u16* refId, unsigned count, void* filter,
                                   float minR, float maxR, u16* outIds,
                                   int strideIndex, int probeStart);

// gilde.exe 0x47ae48 — VIBE_ObjectSearch_FindOneByPaletteRange. Single-slot form of
// the above (count fixed to 1, no pinned-only fast path beyond slot 0). Writes the
// found palette index into *outId. Returns 1 on hit, 0 on miss.
int ObjectSearchFindOneByPaletteRange(const u16* refId, void* filter,
                                      float minR, float maxR, u16* outId,
                                      int strideIndex, int probeStart);

// gilde.exe 0x47b008 — VIBE_ObjectSearch_FindPeopleByPalette. Collects up to
// `maxPeople` palette indices into outIds[], skipping people whose type byte (the
// 536-stride people-type table) is 5/7/8/9, requiring the office-rank delta to the
// reference to be < 5, and applying the favourability filter. Returns the count.
// `peopleTypeTable` is byte_12CE912 (the 536-stride people-type table); the
// office-rank gate uses the computeOfficeRank hook.
int ObjectSearchFindPeopleByPalette(const u16* refId, int maxPeople, float minR,
                                    float maxR, u16* outIds, int strideIndex,
                                    int probeStart, const u8* peopleTypeTable);

// gilde.exe 0x47a8dc — VIBE_ObjectSearch_FindMatchingColors. Like FindByPaletteRange
// but probes the object-ring (not the palette) for each slot; the first slot honours
// the pinned ring record. Returns 1 on full success, 0 otherwise.
int ObjectSearchFindMatchingColors(const u16* refId, unsigned count, void* filter,
                                   float minR, float maxR, u16* outIds);

// gilde.exe 0x47aad4 — VIBE_ObjectSearch_FindMatchingColor. Single-slot object-ring
// colour match: returns 1 and writes *outId on the first ring record that passes the
// eligibility + favourability gate, 0 on miss.
int ObjectSearchFindMatchingColor(const u16* refId, void* filter, float minR,
                                  float maxR, u16* outId);

// ===========================================================================
// City / road-network map builders.
// ===========================================================================
// The road solver works over a packed 44-byte node record array. The originals
// fold the fields into overlapping per-field globals all aliasing one array based
// at 0x12CDD68; we model the record explicitly. Field offsets are byte-faithful.
// Field semantics (recovered from the offsets the road solver folds into the
// overlapping per-field globals all aliasing one 44-byte record at 0x12CDD68):
//   nodeId       — this node's own road id, compared against other nodes' parent
//                  links. (dword_12CDD8E hi-word, read at +40 via the `>>16` view)
//   parentFromId — first parent link id (the `*(...+2)>>16` view at +44)
//   parentToId   — second parent link id (dword_12CDD92 hi-word at +44)
// MapComputeBuildingChainDepth follows parentFromId/parentToId to the OTHER node
// whose nodeId matches, never itself, so the graph is a DAG and the recursion
// terminates.
struct RoadNode {                       // 44 bytes (0x2C)
    i32  coordX;       // dword_12CDD6C (laid-out X position) +4
    i32  coordY;       // dword_12CDD70 (laid-out Y position) +8
    i32  nodeId;       // this node's own id (the parent-link match key)
    i32  parentFromId; // first parent link target id (0 == none)
    i32  parentToId;   // second parent link target id (0 == none)
    i16  depth;        // word_12CDD96 chain depth (0xFFFF == unknown) +46
    i32  cost;         // dword_12CDD98 (averaged X / accumulated cost) +48
};

// The road solver's mutable global state (the originals' dword_13CE28C node count,
// dword_13CE284 depth-count, word_12CE890[] per-level start indices). Modeled as an
// explicit context so tests can inspect the result.
struct RoadNetwork {
    RoadNode nodes[64];      // dword_12CDD8E aliased array (game caps far below 64)
    int      nodeCount;      // dword_13CE28C
    int      depthCount;     // dword_13CE284
    u16      levelStart[33]; // word_12CE890[] (one extra terminator slot)
};

// gilde.exe 0x592c7c — VIBE_Map_ComputeBuildingChainDepth. Recursive longest-chain
// depth of node `index` in `net`: follows the fromHi / toHi parent links to other
// nodes (matched by hi-word id) and returns 1 + max(parent depths); a cached depth
// (!= 0xFFFF) short-circuits. Mutates net.nodes[index].depth as a memo only on hit.
int MapComputeBuildingChainDepth(RoadNetwork& net, int index);

// gilde.exe 0x592d98 — VIBE_Map_ComputeRoadNetworkLayout. Given the node graph in
// `net` already populated (nodeCount set), assigns each node a depth (chain depth),
// sorts nodes by depth, groups them into levels, then distributes X/Y coordinates
// across `width` x `height` so the road network renders as a tidy layered layout.
// Returns 0 on success, 1 when the network is empty. (Pure integer / fixed math.)
int MapComputeRoadNetworkLayout(RoadNetwork& net, int width, int height);

// gilde.exe 0x5776d8 — VIBE_Map_RasterizeBauplatzEdge. Computes the per-edge
// subdivision step counts for a 4-corner bauplatz quad (`quad` = 8 floats: two edge
// endpoints x2). Returns the number of inner-loop samples written. The deterministic
// core — the step-count math — is exposed via MapRasterEdgeStepCount; the grid
// writes go through the hook-modelled grid (here a no-op count when fill==-1).
int MapRasterizeBauplatzEdge(const float* quad, int fillTerrain, int fillCell);

// Step count for one edge of length-squared `lenSq`: 2*(int)(sqrt(lenSq)+0.9),
// clamped to >= 1. Exposed for golden tests (matches the original's two call sites).
int MapRasterEdgeStepCount(float lenSq);

// gilde.exe 0x528bd0 — VIBE_Map_LoadCityFile. Orchestrates loading a city: builds
// the "%s/%s.NET" or "%s/%s.CTY" path under gamedata/cities, resets the world, and
// re-enters the chooser scene. `cityName` is the city stem; `loadNet` picks .NET vs
// .CTY. Returns the sceneLoadFromStream result (all I/O via hooks).
int MapLoadCityFile(int loadNet, const char* cityName);

// gilde.exe 0x52e194 — VIBE_Map_SpawnCityTowerMarker. Places the "sp_STADTTURM"
// tower marker between the two Kartentisch dummy objects, offset by (a2,a3) scaled
// by kTowerMarkerScaleX/Y. Returns non-zero on success (object spawn via hooks).
// `outXform` (>=8 floats) receives the computed world transform for inspection.
int MapSpawnCityTowerMarker(float offX, float offY, float* outXform);

// gilde.exe 0x52e2d0 — VIBE_Map_SpawnCityPointMarker. Upper-cases `name`, looks up
// the "dummy_<NAME>" object, projects its bone chain, and attaches the city-point
// marker. Returns non-zero on success. The deterministic core (the uppercase +
// "dummy_%s" name build) is exposed via MapBuildDummyName.
int MapSpawnCityPointMarker(const char* name, float param);

// Builds "dummy_<UPPER(name)>" into `out` (capacity must be >= strlen(name)+7).
// Mirrors the original's StrToUpper + "dummy_%s" sprintf. Returns `out`.
char* MapBuildDummyName(const char* name, char* out);

// ===========================================================================
// VIBE_Path_BuildMarkerPoints (0x4074c0).
// ===========================================================================
// Builds the screen-space marker polyline for a route segment. Because the geometry
// projection and heightmap lookup are cross-module, the exposed deterministic piece
// is the loop bookkeeping; the projection goes through coordProjectPoint /
// heightmapTileToWorld hooks. Returns 1 (as the original always does on completion).
// `routeRecord` is the route's tile record (raw bytes), `markerOut` the output
// vertex array (raw bytes; each vertex is 12 bytes: x,y, then a tag byte at +8).
int PathBuildMarkerPoints(int grid, const void* routeRecord, void* markerOut);

} // namespace guild::sim
