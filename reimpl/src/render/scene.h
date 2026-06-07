#pragma once
#include "render/geometry_types.h"
#include "render/mesh.h"

// =============================================================================
// guild::render — scene draw-list build + sort, and the generic scene-graph
// node walk that drives the per-object project/cull/append. Faithful 1:1
// reconstruction of the gilde.exe (d3_engine.c) draw-list cluster:
//
//   0x5AEF34  VIBE_Render_RadixSortDrawList     (LSB radix sort by the 768*L key)
//   0x5AC738  VIBE_SceneGraph_WalkAndInvoke     (generic node walk + callback)
//   0x5AEC88  VIBE_Render_RasterizeMeshList     (draw-list flush to the rasterizer)
//
// THE GLOBAL POLYLIST LAYOUT (recovered byte-for-byte)
// ---------------------------------------------------------------------------
// The engine keeps the sorted triangle draw list in TWO ping-pong buffers, each
// `8 * dword_13ECE80` bytes (8-byte DrawListEntry × max-poly capacity), allocated
// in VIBE_Render_InitEngineDevice:
//     dword_13FC584 = "d3:PolyList1" base   (DrawListEntry[capacity])
//     dword_13FC51C = "d3:PolyList2" base   (DrawListEntry[capacity])
//     dword_13FC570 = running append cursor (entry index during the build walk)
//     dword_13FC770 = appended entry count  (snapshot used by the sort+flush)
//     dword_13ECE80 = capacity (max polys)
// The radix sort uses a 256-entry histogram/prefix table:
//     dword_13D8380 = u32[256]  (1024 bytes; cleared each radix pass)
// and ping-pongs the two PolyLists via two source/dest cursors:
//     dword_13FC4CC = current source PolyList base
//     dword_13FC4C8 = current dest   PolyList base
//
// Each DrawListEntry is 8 bytes: [+0] u32 sortKey (= 768 * maxVertexLightIndex,
// see render/mesh.cpp + render/light.h), [+4] Polygon* (the draw-list entry the
// project stage appended). The sort key's 4 bytes are radix-sorted LSB-first.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// DrawListBuffers — the ping-pong PolyList globals gathered into one re-entrant
// record. base1/base2 mirror dword_13FC584/dword_13FC51C; `count` mirrors
// dword_13FC770; `capacity` mirrors dword_13ECE80. The radix histogram
// (dword_13D8380, 256 u32) lives here so the sort is self-contained/testable.
// ---------------------------------------------------------------------------
struct DrawListBuffers {
    DrawListEntry* base1;       // dword_13FC584  "d3:PolyList1"
    DrawListEntry* base2;       // dword_13FC51C  "d3:PolyList2"
    i32            count;       // dword_13FC770  appended entry count
    i32            capacity;    // dword_13ECE80  max polys
    u32            histogram[256]; // dword_13D8380 radix count/prefix table

    // A DrawList sink (mesh.cpp) that appends into PolyList1 (base1) with the
    // shared count cursor. ProjectVerticesToScreen writes through this.
    DrawList AppendSink() { return DrawList{base1, count, capacity}; }
};

// gilde.exe 0x5AEF34 — VIBE_Render_RadixSortDrawList
//   (__usercall fn(count@eax, twoPassOnly@dl)). Stable LSB radix sort of `count`
// 8-byte DrawListEntry records by their 4-byte u32 sort key. Each pass:
//   1. clear the 256-entry histogram (dword_13D8380),
//   2. count occurrences of the current key byte across all entries,
//   3. prefix-sum the histogram into bucket end offsets,
//   4. scatter entries from source PolyList into dest PolyList back-to-front
//      (decrementing the bucket offset -> stable order),
//   ping-ponging src/dest each pass. Byte 0 (PolyList1->2), byte 1 (2->1); when
// `twoPassOnly` is 0 it continues byte 2 (1->2) and byte 3 (2->1) so the result
// ends back in PolyList1. The original's 4-way memset-alignment dance over the
// 1024-byte histogram is just `memset(histogram,0,1024)`; reproduced as such.
// `db.base1` holds the final sorted order. Returns the count (unchanged for
// count<=1, in which case no sorting is done — already trivially sorted).
u32 RadixSortDrawList(DrawListBuffers& db, u32 count, bool twoPassOnly);

// ---------------------------------------------------------------------------
// Scene-graph node walk (0x5AC738).
// ---------------------------------------------------------------------------
// The original walked a node tree via raw offsets:
//   *(node+530) high byte -> per-node visibility flag tested by
//       VIBE_SceneGraph_TestNodeFlag(flag, walkMask)  (the a4 walk mask)
//   node[127] (= +508) -> child list head (descended when callback returns >=0)
//   node[124] (= +496) -> next sibling (unless walk-mask bit 0x200 stops it)
//   *(node+528) bit0   -> sibling-chain terminator
// The callback `a3()` is invoked per visited node and returns: 0 = stop the
// whole walk, <0 = visit this node but do NOT descend its children, >0 = descend.
//
// To keep this decoupled from the full engine node struct (as the existing
// render/scenegraph.h CullCallbacks does), we model the node accessors via a
// vtable of function pointers. The control flow (the visibility gate, the child
// recursion gated on `callback >= 0`, the sibling chain with the 0x200 stop and
// the +528 bit0 terminator) is reproduced verbatim.
struct WalkVTable {
    // Test the node's +530 high-byte flag against the walk mask (TestNodeFlag).
    bool (*testFlag)(void* node, i16 walkMask);
    // Invoke the per-node callback; return 0=stop, <0=no-descend, >0=descend.
    char (*invoke)(void* node, void* ctx, i32 userArg);
    void* (*child)(void* node);    // node[127]: child list head
    void* (*sibling)(void* node);  // node[124]: next sibling
    bool  (*stopAtSibling)(void* node); // *(node+528) bit0 terminator
};

// gilde.exe 0x5AC738 — VIBE_SceneGraph_WalkAndInvoke
//   (__userpurge fn(root@eax, node@edx, callback@ecx, walkMask@bx, userArg)).
// Walks the sibling list starting at `node` (or the root's child list when
// node==null), invoking the callback per visible node and recursing into
// children. `walkMask` (a4) gates visibility (0 => return 0 immediately) and its
// 0x200 bit suppresses sibling traversal (the v14 = a4 & 0xFDFF child mask). The
// `ctx`/`userArg` are threaded to the callback unchanged. Returns 1 on a full
// walk, 0 if the callback aborted.
char WalkAndInvoke(void* root, void* node, void* ctx, i16 walkMask, i32 userArg,
                   const WalkVTable& vt);

} // namespace guild::render
