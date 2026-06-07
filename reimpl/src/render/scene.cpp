#include "render/scene.h"

#include <cstring>  // memset

namespace guild::render {

namespace {

// One LSB radix pass over `count` DrawListEntry records, sorting by `byteIdx`
// (0..3) of the entry's 4-byte sort key, scattering from `src` to `dst`.
// Mirrors one of the four (identical) pass bodies in VIBE_Render_RadixSortDrawList:
//   - clear the 256-entry histogram (the original's memset-with-alignment over
//     the 1024-byte dword_13D8380 table; here a plain memset of 256 u32),
//   - count occurrences of the current key byte (read at &src[i].sortKey+byteIdx),
//   - prefix-sum into bucket *end* offsets (v10[1] += *v10 across 255 steps),
//   - scatter back-to-front (i = count-1 .. 0): bucket = --histogram[key];
//     dst[bucket] = src[i]  (copies both the 4-byte key and the 4-byte poly ptr).
// The back-to-front scatter with a pre-decremented end offset yields a STABLE
// sort, exactly as the original.
void RadixPass(u32* histogram, const DrawListEntry* src, DrawListEntry* dst,
               u32 count, int byteIdx) {
    std::memset(histogram, 0, 256 * sizeof(u32));

    for (u32 i = 0; i < count; ++i) {
        u32 key = src[i].sortKey;
        u8 b = (u8)(key >> (8 * byteIdx));
        ++histogram[b];
    }

    // Prefix sum: histogram[k+1] += histogram[k] for k = 0..254 (255 iterations),
    // turning per-bucket counts into bucket end offsets.
    for (int k = 0; k < 255; ++k)
        histogram[k + 1] += histogram[k];

    // Scatter back-to-front for stability.
    for (i32 i = (i32)count - 1; i >= 0; --i) {
        u32 key = src[i].sortKey;
        u8 b = (u8)(key >> (8 * byteIdx));
        u32 pos = --histogram[b];
        dst[pos] = src[i];
    }
}

} // namespace

// gilde.exe 0x5AEF34 — VIBE_Render_RadixSortDrawList
//
// The original ping-pongs PolyList1 (dword_13FC584) and PolyList2 (dword_13FC51C)
// via the src/dest cursors dword_13FC4CC/dword_13FC4C8:
//   pass 0: byte 0,  src=List1 dst=List2
//   pass 1: byte 1,  src=List2 dst=List1
//   if (!twoPassOnly):
//   pass 2: byte 2,  src=List1 dst=List2
//   pass 3: byte 3,  src=List2 dst=List1
// Two passes leave the result in List1 (low 16 bits sorted); four passes leave it
// in List1 fully sorted. For count<=1 the original skips sorting entirely.
u32 RadixSortDrawList(DrawListBuffers& db, u32 count, bool twoPassOnly) {
    if (count <= 1)
        return count;

    // pass 0: List1 -> List2 (key byte 0)
    RadixPass(db.histogram, db.base1, db.base2, count, 0);
    // pass 1: List2 -> List1 (key byte 1)
    RadixPass(db.histogram, db.base2, db.base1, count, 1);

    if (!twoPassOnly) {
        // pass 2: List1 -> List2 (key byte 2)
        RadixPass(db.histogram, db.base1, db.base2, count, 2);
        // pass 3: List2 -> List1 (key byte 3)
        RadixPass(db.histogram, db.base2, db.base1, count, 3);
    }
    return count;
}

// gilde.exe 0x5AC738 — VIBE_SceneGraph_WalkAndInvoke
//
// Control flow (verbatim):
//   if (!walkMask) return 0;                        // empty walk mask aborts
//   if (node) {                                     // sibling-list traversal
//     childMask = walkMask & 0xFDFF;                // clears the 0x200 stop bit
//     loop {
//       r = TestNodeFlag(node+530 high byte, walkMask) ? invoke() : 1;
//       if (!r) return 0;
//       child = node[127];
//       if (child && r >= 0) r = WalkAndInvoke(root, child, ..., childMask, ...);
//       if (!r) return 0;
//       node = (walkMask & 0x200) ? 0 : node[124];  // next sibling (or stop)
//       if (!node) return 1;
//       if (node+528 bit0) return 1;                // chain terminator
//     }
//   }
//   // node==null: descend the root's child list head (root+128) to terminator.
// We model the root-child-list path through the same vtable: the caller's
// `root` accessor returns the first child via vt.child(root) and the terminator
// via vt.sibling/stopAtSibling. To stay faithful, the null-node branch walks
// vt.child(root) as a sibling list until a null sibling.
char WalkAndInvoke(void* root, void* node, void* ctx, i16 walkMask, i32 userArg,
                   const WalkVTable& vt) {
    if (!walkMask)
        return 0;

    // node==null: start at the root's child list head. The original recursed via
    // WalkAndInvoke(root, root[128]-chain, ...); since the non-null branch below
    // already walks the entire sibling chain, we enter it with the child head as
    // the start node (descending root's child list as a sibling chain).
    if (!node)
        node = vt.child(root);
    if (!node)
        return 1;

    i16 childMask = (i16)(walkMask & 0xFDFF);
    while (true) {
        char r = 1;
        if (vt.testFlag(node, walkMask))
            r = vt.invoke(node, ctx, userArg);
        if (!r)
            return 0;

        void* child = vt.child(node);
        if (child && r >= 0)
            r = WalkAndInvoke(root, child, ctx, childMask, userArg, vt);
        if (!r)
            return 0;

        node = (walkMask & 0x200) ? nullptr : vt.sibling(node);
        if (!node)
            return 1;
        if (vt.stopAtSibling(node))
            return 1;
    }
}

} // namespace guild::render
