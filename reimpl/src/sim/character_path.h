#pragma once
// character_path — the VIBE_Character_* path / physics / AI *leaf* functions of
// gilde.exe (32-bit x86, imagebase 0x400000) that the sibling character*.cpp /
// charaction*.cpp files had not yet covered. Faithful 1:1 ports.
//
// Three families dominate this slice:
//   * the live-actor slot-table leaves (0x402254, 0x4022c8, 0x406e68, 0x4073f0):
//     the 512-entry pointer array dword_66F0D0 (owned by character_query.cpp as
//     guild::sim::g_live) is allocated, indexed and scanned. We REUSE that real
//     table and its LiveActor record rather than re-modeling it.
//   * the per-turn AI behaviour leaves (0x4525fc CountActiveByTurn, 0x452d38
//     UpdateGuardBehavior, 0x4526d8 FindNearestTarget): they walk the 768-entry
//     person palette / He-handler ring, computing a rank-weighted favourability
//     and emitting command-delta packets. The deterministic core (the palette
//     scan, the rank gate, the favourability curve) is exposed for golden tests.
//   * the path/waypoint builders (0x4087f4 FindWaypointsToPoint, 0x408c4c
//     WalkPathActionUpdate, 0x4062c0 WaitSlotCallback, 0x406344 PickWaitAnimation).
//
// Cross-module callees with no reconstructed target (the heightmap tile probes,
// the line-of-sight tracer, the waypoint-list builder, the command-delta codec,
// the He-handler ring, the AI needs/method evaluators, the scene-graph walker)
// are routed through CharacterPathHooks with inert defaults defined in
// character_path.cpp. Tests install their own. Where a real sibling exists
// (util::RandomModulo, util::VectorWithinTolerance, sim::PersonComputeOfficeRank,
// the mem AllocDebug heap) we delegate to it through the hook so the unified
// build keeps one definition.
//
// Translated functions (absolute addresses):
//   0x402254 VIBE_Character_AllocSlot
//   0x4022c8 VIBE_Character_AllocSlotAtIndex
//   0x406e68 VIBE_Character_FindNearbyWide
//   0x4073f0 VIBE_Character_CreateMapNode
//   0x4062c0 VIBE_Character_WaitSlotCallback
//   0x406344 VIBE_Character_PickWaitAnimation
//   0x4525fc VIBE_Character_CountActiveByTurn
//   0x452d38 VIBE_Character_UpdateGuardBehavior
//   0x4526d8 VIBE_Character_FindNearestTarget
//   0x4087f4 VIBE_Character_FindWaypointsToPoint
//   0x408c4c VIBE_Character_WalkPathActionUpdate
// Deferred: 0x407724 VIBE_Character_FindNewPath (4124 bytes of unreconstructed
//   x87 ring-buffer A* with several Hex-Rays garbage-register reads; not safely
//   translatable 1:1 without the path/grid siblings).
#include "guild/common/types.h"

namespace guild::sim {

struct LiveActor;   // character_query.h — the dword_66F0D0[512] record

// ===========================================================================
// Recovered constants (imagebase 0x400000).
// ===========================================================================
constexpr int kCharSlotCount = 512;          // dword_66F0D0 length
constexpr int kCharRecordSize = 0x204;       // AllocSlot request (516 bytes)

// FindNearestTarget favourability curve (flt_619138..flt_619144):
constexpr float kRankDeltaScale   = 0.5f;    // flt_619138
constexpr float kRankCurveScale   = 0.10000000149011612f; // flt_61913C
constexpr float kRankCurveBase    = 10.0f;   // flt_619140
constexpr float kFavorabilityCeil = 34.0f;   // flt_619144

// FindNearbyWide proximity box half-extent (300.0 in the tolerance test).
constexpr float kNearbyWideTol = 300.0f;
constexpr int   kNearbyWideMax = 64;          // v5 < 64 output cap

// ===========================================================================
// Person palette layout (the 768-entry person turn table, stride 536). The AI
// leaves index it the way the originals do; we model the touched fields as a
// flat byte view supplied by the host through hooks so the scan stays testable.
// ===========================================================================
constexpr int kPersonCount = 768;            // word_12CE910 entries

// ===========================================================================
// Cross-module leaf hooks. Records are raw byte buffers (the originals dereference
// *(_TYPE*)(base+off)); nullptr / 0 models an empty / inert world. The host
// installs real wiring; the default set is inert.
// ===========================================================================
struct CharacterPathHooks {
    // --- slot-table allocation (mem heap; AllocDebug/FreeDebug, ErrorLog) ---
    // VIBE_Memory_AllocDebug(size, info) (0x438f10). Inert default: malloc.
    void* (*allocDebug)(unsigned size, const char* info);
    // VIBE_Memory_FreeDebug(ptr) (0x43923c). Inert default: free.
    void  (*freeDebug)(void* ptr);
    // VIBE_Light_SetGrayColorThunk(value, count, base) (0x5c6af0) — zero-fills the
    // freshly allocated record (count bytes at base). Inert default: memset 0.
    void  (*clearRecord)(int value, int count, void* base);
    // VIBE_ErrorLog_ReportMessage(msg) — slot overflow report. Inert: no-op.
    void  (*reportError)(const char* msg);

    // --- He-handler ring (CountActiveByTurn) ---
    // VIBE_He_FindFirstHandlerByFilter(a,b,c) -> nonzero if a match starts a walk.
    int (*heFindFirst)(int a, int b, int c);
    // VIBE_He_FindNextMatchingHandler() -> nonzero while more matches. Each call
    // (in the original) bumps the running tally edx; we surface the tally via a
    // separate accessor so the count stays deterministic.
    int (*heFindNext)();
    int   heActiveTally;     // value left in edx by the He walk (the v0 in 0x452638)
    int   heTurnMultiplier;  // dword_62EB98 — per-turn multiplier
    unsigned char heTurnByte;// byte_63CC1D — per-turn enable byte

    // --- AI target selection (FindNearestTarget / UpdateGuardBehavior) ---
    // VIBE_Math_RandomModulo(n) (0x58b89c). Wire to util::RandomModulo for the
    // real LCG stream. Inert default: 0 (deterministic).
    int (*randomModulo)(u16 n);
    // VIBE_Person_ComputeOfficeRank(idx, stopAtSelf) (0x58bccc). Wire to
    // sim::PersonComputeOfficeRank. Inert default: 0.
    int (*computeOfficeRank)(u16 idx, int stopAtSelf);
    // VIBE_Ai_ComputePersonFavorability(candId, refId, mode) (0x594330). 0..100.
    double (*computeFavorability)(int candId, int refId, int mode);
    // VIBE_Math_VectorWithinTolerance(a, b, tol) (0x5caa4c). Wire to
    // util::VectorWithinTolerance. Inert default: false.
    bool (*vectorWithinTolerance)(const float* a, const float* b, float tol);

    // --- command-delta codec emitted by FindNearestTarget (no-op in tests) ---
    void (*beginDeltaPacket)(void* actor, int id);
    void (*appendDeltaField)(unsigned size, unsigned count, const void* src, unsigned field);
    void (*appendRawField)(unsigned size, unsigned count, const void* src, unsigned off);
    void (*queueRequestState22)();
    void (*queueRequestCoord27)(int a, int id, int c);
    void (*historyNotifyTargetReachedA)(void* actor, void* rec);
    void (*historyNotifyTargetReachedB)(void* actor, void* rec);
    void (*historyNotifyTargetFound)(void* actor, void* rec);
    void* (*personFindRecordById)(int id);

    // --- path / heightmap leaves (FindWaypointsToPoint / WalkPathActionUpdate) ---
    // VIBE_Heightmap_WorldToTileWithHeight(grid, world, outTile, outHeight) -> hit.
    int (*worldToTileWithHeight)(int grid, const float* world, int* outTile, float* outH);
    // VIBE_Heightmap_FindNearestWalkableTile(grid, col, outCol, row, outRow).
    void (*findNearestWalkableTile)(int grid, int col, int* outCol, int row, int* outRow);
    // VIBE_Heightmap_TileToWorld(grid, col, out3, row) -> hit.
    int (*tileToWorld)(int grid, int col, float* out, int row);
    // VIBE_Map_TraceLineOfSight(grid, from, fromRow, to, toRow, outCol, outRow, mode, flags).
    void (*traceLineOfSight)(int grid, const float* from, int fromRow, const float* to,
                             int toRow, int* outCol, int* outRow, int mode, int flags);
    // VIBE_Path_BuildWaypointList(outLen, grid, col, row, mode) -> 0xFFFF on fail.
    u16 (*buildWaypointList)(int* outLen, int grid, int col, int row, int mode);
    // VIBE_Path_ResamplePolyline(inLen, out, grid) -> resampled count.
    int (*resamplePolyline)(const int* inLen, void* out, int grid);
    int (*resolveMesh)(int actor);                  // VIBE_Character_ResolveMesh
};

// Install hooks; nullptr restores the inert defaults. Returns the previous set.
CharacterPathHooks CharacterPathSetHooks(const CharacterPathHooks* hooks);
const CharacterPathHooks& CharacterPathGetHooks();

// ===========================================================================
// Slot-table leaves (reuse g_live / LiveActor from character_query.cpp).
// ===========================================================================

// gilde.exe 0x402254 — VIBE_Character_AllocSlot. Allocates a record, finds the
// first free slot in g_live[0..511], stores the record there and writes the slot
// index back to record[0]. Returns the record, or null on overflow (record freed).
LiveActor* AllocSlot();

// gilde.exe 0x4022c8 — VIBE_Character_AllocSlotAtIndex (eax = index). Allocates a
// record; if g_live[index] is occupied, returns null (record leaked, as in the
// original). Otherwise stores it at the fixed index and writes index to record[0].
LiveActor* AllocSlotAtIndex(int index);

// gilde.exe 0x406e68 — VIBE_Character_FindNearbyWide (eax=self, edx=outArray).
// Scans g_live[0..511] for live actors in the same universe (+136) and group
// (+44) as `self`, with a non-culled mesh, within the kNearbyWideTol box of
// `self`'s mesh world position. Appends each match's pointer to `outArray`
// (capped at kNearbyWideMax). Returns the match count.
int FindNearbyWide(LiveActor* self, LiveActor** outArray);

// A small "map node" record (20 bytes): node[4] (byte +16) holds the payload.
struct MapNode {
    int  _pad[4];   // +0..+12 (uninitialised in the original)
    int  payload;   // +16
};
// gilde.exe 0x4073f0 — VIBE_Character_CreateMapNode (ecx=this, edx=payload).
// Allocates a 20-byte node and stores `payload` at +16. Returns the node.
MapNode* CreateMapNode(int payload);

// ===========================================================================
// Per-turn AI leaves.
// ===========================================================================

// gilde.exe 0x4525fc — VIBE_Character_CountActiveByTurn. Walks the He-handler
// ring (FindFirst/FindNext), then returns tally + heTurnMultiplier*heTurnByte.
int CountActiveByTurn();

// The favourability weight FindNearestTarget computes for a rank delta:
//   (kRankCurveBase - rankDelta*kRankDeltaScale) * kRankCurveScale
// (flt-exact 1:1 with the original's st7 chain at 0x45277e).
float NearestTargetRankWeight(int rankDelta);

// The capped "walk steps" clamp FindNearestTarget applies to the distance-derived
// step count (v30 at 0x452917..0x452947): start = |dist|/3 + 1, clamped into
// [2*debugSpeed+15, 2*debugSpeed+35]. Returns the clamped byte (0..255).
int NearestTargetWalkSteps(int dist, int debugSpeed);

// ===========================================================================
// Path builders (operate on an opaque action record `a1`; the touched fields are
// byte offsets into it — see the .cpp). These mostly orchestrate the path hooks;
// the deterministic clamp helper below is shared and golden-tested.
// ===========================================================================

// The tile clamp both FindWaypointsToPoint and WalkPathActionUpdate apply: a tile
// coord <1 becomes 1; a coord > gridDim-2 becomes gridDim-2. Returns the clamp.
int ClampTileCoord(int coord, int gridDim);

// gilde.exe 0x4062c0 — VIBE_Character_WaitSlotCallback (al=actorId, edx=slotRec).
// Copies the 0x3C0-byte "dummy wait" template, scans its 15 64-byte entries for an
// active one whose callback (loc_5CB930) accepts it; on a hit, appends `actorId`
// to slotRec's queue at slotRec+[+128] and bumps the count. Returns count<32.
// `waitTemplate` is the 960-byte (15*64) entry block; `slotCallback` is the
// per-entry predicate (loc_5CB930): returns nonzero to accept entry i.
bool WaitSlotCallback(int actorId, int* slotRec, const unsigned char* waitTemplate,
                      int (*slotCallback)(int entry, const unsigned char* rec));

// gilde.exe 0x406344 — VIBE_Character_PickWaitAnimation. Walks the scene graph
// (off_649D64) collecting up to 256 candidate ids into a 32-slot buffer; if none,
// returns 0; if exactly one, returns it; otherwise returns buf[RandNext()%count].
// `collect` fills `out` with up to `cap` ids and returns the count (models
// VIBE_SceneGraph_WalkAndInvoke); `randNext` models VIBE_Util_RandNext.
int PickWaitAnimation(int (*collect)(int* out, int cap), unsigned (*randNext)());

// gilde.exe 0x4087f4 — VIBE_Character_FindWaypointsToPoint. The deterministic
// path-math core (the two tile clamps + the start/goal tile resolution that drives
// the waypoint build) is shared with WalkPathActionUpdate and exposed as
// ResolvePathEndpoints below; the full action-record orchestration (which also
// touches the render/anim/action-queue leaves outside this path slice) is the
// caller's. See report — the builder bodies are deferred for that reason.

// Resolve and clamp the two endpoint tiles for a path build: probe `startWorld`
// and `goalWorld` on `grid` via the heightmap hook, clamp each tile coord into
// [1, gridDim-2], and (when both probes hit) build the waypoint list. Returns the
// VIBE_Path_BuildWaypointList result (0xFFFF on failure / probe miss), and writes
// the clamped start tile to *startTile, goal tile to *goalTile. `gridDim` is the
// grid's tile dimension (*(grid+32)); `mode` is the per-mover step mode.
u16 ResolvePathEndpoints(int grid, int gridDim, const float* startWorld,
                         const float* goalWorld, int mode,
                         int* startTile, int* goalTile, int* outLen);

} // namespace guild::sim
