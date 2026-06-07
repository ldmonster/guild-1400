#include "render/quant_octree.h"
#include <cstring>
#include <cstddef>

// =============================================================================
// guild::render — octree colour quantizer core (implementation). Transcribed
// from the Hex-Rays pseudocode of the gilde.exe VIBE_Quant_* cluster. The 24-byte
// node layout, the 15-bit leaf bucket, the level-propagation, the min-heap, and
// the centroid arithmetic are all byte-for-byte. See quant_octree.h for the map.
// =============================================================================
namespace guild::render {

namespace {

// Bit-spread sub-tables (gilde.exe VIBE_Quant_InitLookupTables @0x602AA4).
// The high table is read directly (word_1409508[c]); the mid/low tables are read
// through a +1-word alias (word_14090F0 / word_14092F0 == base+2), so for input
// byte c the consumer reads the value the init computed at index c. We bake that
// aliasing into the bucket tables: bucketG uses spreadLo(g+1), bucketB uses
// spreadMid(b+1) (verified to be a bijection over the 32768 buckets).
inline u16 SpreadHi(int i) {
    int v2 = i & 0x80, v3 = i & 0x40, v4 = i & 0x20, v9 = i & 0x10, v5 = i & 8;
    return (u16)((v5 >> 1) | (2 * v9) | (32 * v3) | (v2 << 7) | (8 * v4));
}
inline u16 SpreadMid(int i) {
    i &= 0xFF;
    int v2 = i & 0x80, v3 = i & 0x40, v4 = i & 0x20, v9 = i & 0x10, v5 = i & 8;
    return (u16)((v5 >> 3) | (32 * v2) | (8 * v3) | (2 * v4) | (v9 >> 1));
}
inline u16 SpreadLo(int i) {
    i &= 0xFF;
    int v2 = i & 0x80, v3 = i & 0x40, v4 = i & 0x20, v9 = i & 0x10, v5 = i & 8;
    return (u16)((v5 >> 2) | v9 | (4 * v4) | (16 * v3) | (v2 << 6));
}

// Decode a 15-bit leaf bucket back to its 8-bit channel centre (top-5-bits).
// Matches the bit-extract expressions in BuildHistogram / MapImageToPalette.
inline int CenterR(int k) {
    return (2 * (k & 4)) | ((k & 0x20) >> 1) | ((k & 0x100) >> 3)
         | ((k & 0x800) >> 5) | ((k & 0x4000) >> 7);
}
inline int CenterG(int k) {
    return (4 * (k & 2)) | (k & 0x10) | ((k & 0x80) >> 2)
         | ((k & 0x2000) >> 6) | ((k & 0x400) >> 4);
}
inline int CenterB(int k) {
    return (2 * (k & 8)) | ((k & 0x40) >> 1) | ((k & 0x200) >> 3)
         | ((k & 0x1000) >> 5) | (8 * (k & 1));
}

const int kLevelSize[6] = {1, 8, 64, 512, 4096, 32768};

} // namespace

OctreeQuantizer::OctreeQuantizer() {
    for (int i = 0; i < 256; ++i) {
        bucketR[i] = SpreadHi(i);
        bucketG[i] = SpreadLo((i + 1) & 0xFF);
        bucketB[i] = SpreadMid((i + 1) & 0xFF);
    }
}

// gilde.exe 0x602BE0 — VIBE_Quant_AllocColorNodes. Zero-initialised levels.
void QuantAllocColorNodes(OctreeQuantizer& q) {
    for (int L = 0; L < 6; ++L)
        q.level[L].assign(kLevelSize[L], OctreeNode{});
    // Heap holds up to (#leaves)+1 entries (1-based). Worst case = 32768 leaves.
    q.heap.assign(32768 + 1, HeapEntry{});
    q.heapCount = 0;
    q.palCount = 0;                 // dword_140A214 = 0
}

// Node accessor: level L, index within level.
static inline OctreeNode& Node(OctreeQuantizer& q, int L, int idx) {
    return q.level[L][idx];
}

// gilde.exe 0x602D2C — VIBE_Quant_BuildHistogram.
void QuantBuildHistogram(OctreeQuantizer& q, const u8* rgb, unsigned count) {
    // Pass 1: per-pixel leaf histogram (node[+12] count).
    const u8* p = rgb;
    for (unsigned i = 0; i < count; ++i) {
        int bucket = q.Bucket(p[0], p[1], p[2]);   // R=p[0],G=p[1],B=p[2]
        ++q.level[5][bucket].count;
        p += 3;
    }
    // Pass 2: for each occupied leaf, seed the centroid sums + reduce count, push
    // a heap entry, and propagate the count + child mask up levels 4..0.
    q.heapCount = 0;
    int heapSlot = 0;
    for (int v9 = 0; v9 < 0x8000; ++v9) {
        int cnt = q.level[5][v9].count;
        if (!cnt)
            continue;
        ++heapSlot;
        q.heap[heapSlot].level = 5;
        q.heap[heapSlot].index = (u16)v9;
        ++q.heapCount;

        OctreeNode& leaf = q.level[5][v9];
        leaf.reduce = cnt;                          // +16
        leaf.rSum = cnt * (CenterR(v9) + 4);        // +0
        leaf.gSum = cnt * (CenterG(v9) + 4);        // +4
        leaf.bSum = cnt * (CenterB(v9) + 4);        // +8

        int idx = v9;
        for (int L = 4; L >= 0; --L) {
            int child = idx & 7;
            idx >>= 3;
            OctreeNode& n = q.level[L][idx];
            n.reduce += cnt;                        // +16
            n.mask |= (u8)(1 << child);             // +20
        }
    }
    // Heapify (1-based, sift-down each internal node from heapCount..1).
    for (int i = q.heapCount; i; --i)
        QuantHeapSiftDown(q, (unsigned)i);
}

// gilde.exe 0x602F3C — VIBE_Quant_HeapSiftDown. Min-heap keyed on node[+16].
void QuantHeapSiftDown(OctreeQuantizer& q, unsigned i) {
    HeapEntry e = q.heap[i];                         // save {level,index}
    i32 key = Node(q, e.level, e.index).reduce;      // v8
    unsigned half = (unsigned)q.heapCount >> 1;      // v9
    unsigned v1 = i;
    if (v1 <= half) {
        unsigned child;
        do {
            child = 2 * v1;                          // left child
            if (child < (unsigned)q.heapCount) {
                HeapEntry& cl = q.heap[child];
                HeapEntry& cr = q.heap[child + 1];
                if (Node(q, cr.level, cr.index).reduce
                  < Node(q, cl.level, cl.index).reduce)
                    ++child;                         // pick smaller child
            }
            HeapEntry& cs = q.heap[child];
            if (key <= Node(q, cs.level, cs.index).reduce)
                break;
            q.heap[v1] = cs;                         // promote child
            v1 = child;
        } while (child <= half);
    }
    q.heap[v1] = e;                                  // place saved entry
}

// gilde.exe 0x603068 — VIBE_Quant_HeapReduceColors. Pop least-used leaves and
// fold them into their parent until heapCount drops to `target`.
void QuantHeapReduceColors(OctreeQuantizer& q, int target) {
    // `i` is the fixed target; the loop runs while heapCount exceeds it. Each
    // iteration pops the root (smallest-count node) and folds it into its parent.
    while ((unsigned)target < (unsigned)q.heapCount) {
        HeapEntry root = q.heap[1];                  // {level v9, index v8}
        int v9 = root.level;
        unsigned v8 = root.index;                    // index within level v9
        unsigned parentIdx = v8 >> 3;                // v3
        int parentLevel = v9 - 1;

        if (Node(q, parentLevel, parentIdx).count) {
            // Parent already a palette node: drop this entry (shrink the heap by
            // moving the last entry to the root) but still fold sums in below.
            int last = 4 * q.heapCount--;            // v7 = 4*dword_1409A08--
            (void)last;
            q.heap[1] = q.heap[q.heapCount + 1];     // heap[1] = heap[old end]
        } else {
            // Re-point the root at the parent (it becomes the merged node).
            q.heap[1].level = (u8)parentLevel;
            q.heap[1].index = (u16)parentIdx;
        }
        OctreeNode& parent = Node(q, parentLevel, parentIdx);
        OctreeNode& child  = Node(q, v9, v8);
        parent.count += child.count;                 // +12
        parent.rSum  += child.rSum;                  // +0
        parent.gSum  += child.gSum;                  // +4
        parent.bSum  += child.bSum;                  // +8
        parent.mask  &= (u8)~(1 << (v8 & 7));         // clear this child's bit
        QuantHeapSiftDown(q, 1u);
    }
}

// gilde.exe 0x603180 — VIBE_Quant_TreeCollectPalette.
void QuantTreeCollectPalette(OctreeQuantizer& q, int nodeIndex, int level) {
    OctreeNode& n = Node(q, level, nodeIndex);
    if (n.mask) {
        int base = 8 * nodeIndex;                    // child base index
        for (int slot = 7; slot >= 0; --slot) {
            if ((1 << slot) & n.mask)
                QuantTreeCollectPalette(q, base + slot, level + 1);
        }
    }
    if (n.count) {
        n.palIdx = (u8)q.palCount;                   // +21 = palette index
        i32 R = n.rSum, G = n.gSum, B = n.bSum, W = n.count;
        // centroid = (sum + W/2) / W   (the original's (W>>1) rounding)
        q.palR[q.palCount] = (u8)((R + (W >> 1)) / W);
        q.palG[q.palCount] = (u8)((G + (W >> 1)) / W);
        // NOTE: the original writes B into byte_1409907[palCount+1] (a +1-shifted
        // array vs byte_1409708/808). byte_1409907 == byte_1409908 - 1, so
        // byte_1409907[palCount+1] aliases byte_1409908[palCount]; readers
        // (TreeFindNearest, MapImage) use byte_1409908[idx]. We store B by idx.
        q.palB[q.palCount] = (u8)((B + (W >> 1)) / W);
        ++q.palCount;
    }
}

// gilde.exe 0x6033B4 (non-dithered a5==0 path) — VIBE_Quant_MapImageToPalette.
bool QuantMapImageToPalette(OctreeQuantizer& q, const u8* rgb, u8* out,
                            int width, int height) {
    int total = width * height;
    // Build the 32768-entry bucket -> palette-index map. For occupied leaves the
    // index comes from FindClosestColor on the bucket centre colour.
    std::vector<u8> bucketMap(0x8000, 0);
    QuantState fc;                                   // FindClosestColor state
    QuantInitLookupTables(fc);
    fc.palCount = q.palCount;
    std::memcpy(fc.palR, q.palR, sizeof(q.palR));
    std::memcpy(fc.palG, q.palG, sizeof(q.palG));
    std::memcpy(fc.palB, q.palB, sizeof(q.palB));
    for (int k = 0; k < 0x8000; ++k) {
        if (q.level[5][k].count) {
            bucketMap[k] = (u8)QuantFindClosestColor(
                fc, (u8)CenterR(k), (u8)CenterG(k), (u8)CenterB(k));
        }
    }
    // Map each pixel by its bucket index.
    const u8* p = rgb;
    for (int i = 0; i < total; ++i) {
        int bucket = q.Bucket(p[0], p[1], p[2]);
        out[i] = bucketMap[bucket];
        p += 3;
    }
    return true;
}

// gilde.exe 0x6029F0 — VIBE_Quant_BuildPalette (driver).
bool QuantBuildPalette(const u8* rgb, int width, int height, int maxColors,
                       u8* out, u8* palOut) {
    OctreeQuantizer q;
    QuantAllocColorNodes(q);
    QuantBuildHistogram(q, rgb, (unsigned)(width * height));
    QuantHeapReduceColors(q, maxColors);
    QuantTreeCollectPalette(q, 0, 0);
    if (!QuantMapImageToPalette(q, rgb, out, width, height))
        return false;
    // CopyPaletteEntries: planar R[256] G[256] B[256].
    if (palOut) {
        for (int i = 0; i < 256; ++i) {
            palOut[i]       = q.palR[i];
            palOut[i + 256] = q.palG[i];
            palOut[i + 512] = q.palB[i];
        }
    }
    return true;
}

} // namespace guild::render
