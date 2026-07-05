#pragma once
// road_network — faithful 1:1 reconstruction of the city building-upgrade /
// road-network layout solver of gilde.exe (32-bit x86, imagebase 0x400000).
//
// Reconstructed functions (absolute addresses):
//   0x592c7c  VIBE_Map_ComputeBuildingChainDepth  -> RoadComputeChainDepth
//   0x592d98  VIBE_Map_ComputeRoadNetworkLayout   -> RoadComputeNetworkLayout
//   0x5c6b08  VIBE_Coord_ConvertX (truncate)      -> reused via util::ConvertX
//
// The single live caller is VIBE_Building_OpenUpgradeTreeWindow @0x594100, which:
//   1. resolves the building-type record (a1 -> a type byte; record stride 589),
//   2. calls RoadComputeNetworkLayout(a1, a2, width>>16, height>>16),
//   3. on success calls VIBE_Building_BuildUpgradeTree @0x59361c (fills coordY),
//   4. draws the tree with VIBE_Paintbox_DrawLine.
// So this module lays out a *layered DAG* (Sugiyama-style) of the buildings that
// can be reached from a starting building type via its two "parent" upgrade links;
// the result is the tidy node X-positions of the upgrade graph.
//
// === Memory model (recovered exactly) ===
// The original stores nodes in one packed 44-byte (0x2C) record array based at the
// BSS global 0x12CDD68 (dword_13CE28C nodes live). The per-field IDA globals
// (word_12CDD68, dword_12CDD6C, dword_12CDD8E, dword_12CDD92, word_12CDD96,
// dword_12CDD98 ...) all alias ONE array at stride 44; the apparent base mismatch
// (0x12CDD68 vs dword_12CDD8E+2 = 0x12CDD90) is the `add eax,0x2C` that the init
// loop performs *between* two groups of field stores (0x592ebf). The verified
// field byte-offsets within the 44-byte record are encoded in RoadNode below.
//
// We model the node store and the level-boundary array (word_12CE890[]) and the
// scalar counters (dword_13CE28C node count, dword_13CE284 level count) as an
// explicit RoadLayoutState so tests can inspect them and so there is no hidden
// global state. The input building-type record + type table are passed as raw byte
// pointers exactly as the binary reads them (no reinterpretation of the format).
//
// NOTE: src/sim/pathfind_map.{h,cpp} contains an earlier, deliberately simplified
// sketch named MapComputeRoadNetworkLayout (operating on a pre-populated graph and
// omitting the input-record scan, the within-level cost sort, the equal-cost gap
// spread, the 80px min-spacing push, and the float X-rescale pass). THIS module is
// the faithful 1:1 body and is what the real caller path uses. The two coexist in
// different namespaces (guild::sim vs guild::world); no symbol is redefined.

#include "guild/common/types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Recovered constants (imagebase 0x400000).
// ---------------------------------------------------------------------------
constexpr int kRoadTypeRecordStride = 589;   // 0x24D — building TYPE record size
constexpr int kRoadTypeFlagStride   = 65;    // 0x41  — per-type category-flag stride
constexpr int kRoadNodeMax          = 64;    // working capacity (game stays far below)
constexpr int kRoadLevelMax         = 33;    // levelStart capacity (+1 terminator)

// Type-record field offsets (bytes), recovered from the entry-scan.
constexpr int kRoadRecCountOff   = 34;   // u8  count of upgrade entries
constexpr int kRoadRecEntryOff   = 35;   // u16[] entry type ids (bit15 = flag, masked)
constexpr int kRoadRecLinkLoOff  = 163;  // u16 parent-from id (0xA3) in the dword[] view
constexpr int kRoadRecLinkHiOff  = 165;  // u16 parent-to   id (0xA5)

// Type-category bytes that terminate the entry scan (the two "stop" kinds).
constexpr int kRoadStopCategoryA = 2;    // 0x592e5e
constexpr int kRoadStopCategoryB = 6;    // 0x592e9e
constexpr int kRoadSpecialTarget = 253;  // 0xFD — *a2==253 special (drain-the-count)

// Placement constants (pixels).
constexpr int kRoadNodeHalf      = 24;   // half node width subtracted from X centres
constexpr int kRoadRowHeight     = 96;   // per-level Y stride before clamping
constexpr int kRoadMinGap        = 80;   // minimum sibling X spacing enforced (0x50)
constexpr int kRoadTopMargin     = 16;   // Y base offset / bottom-margin reserve

// ---------------------------------------------------------------------------
// One node of the layered upgrade graph — the 44-byte (0x2C) record.
// Byte offsets are the recovered field positions within the original record.
// ---------------------------------------------------------------------------
struct RoadNode {                  // sizeof == 44 in the original
    u16 childType;     // +0x00 word_12CDD68 — entry type id ([rec+165] copy)
    u16 reserved02;    // +0x02 word_12CDD6A — init 0xFFFF (parent index scratch)
    i32 coordX;        // +0x04 dword_12CDD6C — RAW slot; node j's laid-out X is
                       //   stored one record up (abs 0x30+44j, aliasing `cost`);
                       //   the consumer 0x59361c reads LOWORD(dword_12CDD98[11j])
    i32 coordY;        // +0x08 dword_12CDD70 — RAW slot; node j's laid-out Y is
                       //   one record up (abs 0x34+44j, LOWORD(dword_12CDD9C[11j]))
    i32 link10;        // +0x10 dword_12CDD78 — init -1 (scratch link slot)
    i32 link14;        // +0x14 dword_12CDD7C — init -1
    i32 link18;        // +0x18 dword_12CDD80 — init -1
    i32 link1C;        // +0x1C dword_12CDD84 — init -1
    i32 link20;        // +0x20 dword_12CDD88 — init -1
    u8  flag24;        // +0x24 byte_12CDD8C  — init 0xFF
    i32 nodeId;        // hi-word of (dword_12CDD8E+2) — this node's own id (match key)
    i32 parentFromId;  // dword_12CDD92 lo-word — first parent link id
    i32 parentToId;    // dword_12CDD92 hi-word — second parent link id
    u16 depth;         // word_12CDD96 — chain depth (0xFFFF == not yet computed)
    i32 cost;          // dword_12CDD98 — averaged/relaxed X cost (sort key per level)
};

// ---------------------------------------------------------------------------
// The solver's full working state (replaces the BSS globals).
// ---------------------------------------------------------------------------
struct RoadLayoutState {
    RoadNode nodes[kRoadNodeMax];      // the node array (dword_12CDD8E aliased store)
    int      nodeCount  = 0;           // dword_13CE28C
    int      levelCount = 0;           // dword_13CE284
    u16      levelStart[kRoadLevelMax] // word_12CE890[] level start indices
                 = {0};                //   (levelStart[levelCount] == nodeCount)
};

// gilde.exe 0x592c7c — VIBE_Map_ComputeBuildingChainDepth.
// Longest-chain depth of node `index`: follows parentFromId / parentToId to the
// OTHER node whose nodeId matches (never itself) and returns 1 + max(parent
// depths). A cached depth (!= 0xFFFF) short-circuits. Returns 0 for a root with
// no parents, or when a parent id has no matching node.
int RoadComputeChainDepth(RoadLayoutState& st, int index);

// gilde.exe 0x592d98 — VIBE_Map_ComputeRoadNetworkLayout.
//   typeRecord  : pointer to the *byte before* the building-type record's type id
//                 — exactly the original's a1 (it reads typeRecord[0] as the type
//                 byte to compute the 589-stride record base over `typeTableBase`).
//   typeTableBase  : dword_13CE294 base of the 589-stride type-record table.
//   typeFlagBase   : dword_13CE27C base of the 65-stride category-flag table.
//   targetIdPtr : pointer to the i16 target id being searched for (the original a2).
//   width,height: layout area in pixels (the caller passes rect fields >> 16).
// Fills `st` with the laid-out node graph and X positions. Returns 0 on success,
// 1 when the network is empty / the target id is not present.
int RoadComputeNetworkLayout(RoadLayoutState& st,
                             const u8* typeTableBase, const u8* typeFlagBase,
                             const i8* typeRecord, const i16* targetIdPtr,
                             int width, int height);

} // namespace guild::world
