#pragma once
#include "guild/common/types.h"
#include "render/quant.h"
#include <vector>

// =============================================================================
// guild::render — octree colour quantizer core (gilde.exe d3:Quant.*).
//
// Faithful 1:1 reconstruction of the octree/min-heap median-reduction quantizer
// that turns a 24-bit RGB image into a <=256-colour palette + an 8-bit indexed
// image. Reconstructs the full non-dithered pipeline:
//
//   0x6029F0  VIBE_Quant_BuildPalette         (driver: alloc->hist->reduce->collect->map)
//   0x602BE0  VIBE_Quant_AllocColorNodes      (6 octree levels: 1/8/64/512/4096/32768)
//   0x602C8C  VIBE_Quant_FreeColorNodes
//   0x602D2C  VIBE_Quant_BuildHistogram       (per-pixel leaf accumulation + heap init)
//   0x602F3C  VIBE_Quant_HeapSiftDown         (min-heap on node total count)
//   0x603068  VIBE_Quant_HeapReduceColors     (merge least-used leaves up the tree)
//   0x603180  VIBE_Quant_TreeCollectPalette   (DFS: assign palette indices + centroids)
//   0x603280  VIBE_Quant_TreeFindNearest      (DFS nearest-palette search by sq-dist)
//   0x6033B4  VIBE_Quant_MapImageToPalette    (non-dithered path: per-pixel index map)
//
// THE OCTREE (byte-for-byte)
// ---------------------------------------------------------------------------
// 6 levels; level L has 8^L nodes (1,8,64,512,4096,32768). Each node is 24 bytes:
//   +0  weighted R sum   (sum over pixels of (R5*8 + 4))   [int32]
//   +4  weighted G sum                                      [int32]
//   +8  weighted B sum                                      [int32]
//   +12 pixel count       (histogram weight)                [int32]
//   +16 reduce/heap count (count propagated up for ordering)[int32]
//   +20 child-present mask (bit k set => child k occupied)  [u8]
//   +21 palette index      (assigned in TreeCollectPalette) [u8]
// The leaf (level-5) index of colour (R,G,B) is the 15-bit interleave
//   bucket = spreadHi(R) + spreadLo((G+1)&0xFF) + spreadMid((B+1)&0xFF)
// (a bijection over the 32768 (R5,G5,B5) buckets — verified). A node's parent at
// level L-1 is (indexL >> 3); its slot in the parent is (indexL & 7).
//
// THE MIN-HEAP (byte-for-byte)
// ---------------------------------------------------------------------------
// The heap holds one 4-byte entry {level:u8, indexInLevel:u16} per occupied
// node, ordered by node[+16] (the propagated count). HeapReduceColors pops the
// smallest-count node repeatedly, folding its sums/count/mask into its parent,
// until the occupied-node count drops to the requested palette size.
//
// THE NON-DITHERED MAP (a5 == 0 path of MapImageToPalette)
// ---------------------------------------------------------------------------
// Build a 32768-entry index map: for each occupied leaf bucket, run
// FindClosestColor on the bucket's centre colour to get its palette index; then
// map every pixel by its bucket. (The serpentine Floyd-Steinberg dithered path,
// a5 != 0, is DEFERRED — see module report.)
//
// Re-entrancy: the original kept the octree, heap, palette and lookup tables in
// file-scope globals. We gather them into one OctreeQuantizer record so the
// pipeline is testable without global state. The arithmetic is otherwise verbatim.
// =============================================================================
namespace guild::render {

// One 24-byte octree node (offsets match the original record, see header above).
struct OctreeNode {
    i32 rSum   = 0;   // +0
    i32 gSum   = 0;   // +4
    i32 bSum   = 0;   // +8
    i32 count  = 0;   // +12
    i32 reduce = 0;   // +16  (count used for heap ordering)
    u8  mask   = 0;   // +20  child-present bitmask
    u8  palIdx = 0;   // +21
};

// A heap entry: which node (level + index-in-level) it refers to.
struct HeapEntry {
    u8  level = 0;    // node[0] in the original 4-byte entry
    u16 index = 0;    // node[2..3]
};

struct OctreeQuantizer {
    // 6 octree levels (sizes 1,8,64,512,4096,32768).
    std::vector<OctreeNode> level[6];
    // Min-heap (1-based; slot 0 unused, matching the original's 1-based indexing).
    std::vector<HeapEntry>  heap;
    int heapCount = 0;            // dword_1409A08

    // Palette (parallel R/G/B), filled by TreeCollectPalette.
    u8  palR[256] = {};           // byte_1409708
    u8  palG[256] = {};           // byte_1409808
    u8  palB[256] = {};           // byte_1409907 (note +1 idx quirk; see .cpp)
    int palCount = 0;             // dword_140A214

    // Bit-spread bucket tables (consumer semantics; see header).
    u16 bucketR[256] = {};        // word_1409508[R]
    u16 bucketG[256] = {};        // word_14092F0[G]
    u16 bucketB[256] = {};        // word_14090F0[B]

    OctreeQuantizer();
    // Leaf bucket index (15-bit) for an 8-bit RGB colour.
    int Bucket(u8 r, u8 g, u8 b) const {
        return bucketR[r] + bucketG[g] + bucketB[b];
    }
};

// gilde.exe 0x602BE0 — allocate/zero the 6 octree levels.
void QuantAllocColorNodes(OctreeQuantizer& q);

// gilde.exe 0x602D2C — accumulate the histogram from `rgb` (count pixels, 3 B
// each), seed the leaf node centroids/masks, and build the min-heap.
void QuantBuildHistogram(OctreeQuantizer& q, const u8* rgb, unsigned count);

// gilde.exe 0x602F3C — sift the heap entry at 1-based slot `i` down into place.
void QuantHeapSiftDown(OctreeQuantizer& q, unsigned i);

// gilde.exe 0x603068 — reduce the occupied-node count down to `target` palette
// colours by merging the least-used leaves into their parents.
void QuantHeapReduceColors(OctreeQuantizer& q, int target);

// gilde.exe 0x603180 — DFS the tree assigning palette indices + emitting the
// per-node centroid colour into palR/palG/palB; returns/advances palCount.
void QuantTreeCollectPalette(OctreeQuantizer& q, int nodeIndex, int level);

// gilde.exe 0x6033B4 (a5==0 path) — map `rgb` (w*h pixels) to `out` (w*h indices)
// using the reduced palette. Returns true on success.
bool QuantMapImageToPalette(OctreeQuantizer& q, const u8* rgb, u8* out,
                            int width, int height);

// gilde.exe 0x6029F0 — full driver. Quantize `rgb` (w*h, 3 B/pixel) to `out`
// (w*h indices) and emit the planar palette into `palOut` (768 B: R[256]G[256]
// B[256]). `maxColors` is the target palette size. Returns true on success.
bool QuantBuildPalette(const u8* rgb, int width, int height, int maxColors,
                       u8* out, u8* palOut);

} // namespace guild::render
