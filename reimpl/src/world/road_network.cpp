// road_network — faithful 1:1 reconstruction. See road_network.h for the memory
// model and the relationship to the simplified sim/pathfind_map sketch.
//
// gilde.exe 0x592d98 VIBE_Map_ComputeRoadNetworkLayout
// gilde.exe 0x592c7c VIBE_Map_ComputeBuildingChainDepth
// gilde.exe 0x5c6b08 VIBE_Coord_ConvertX (truncate-toward-zero of a double)
//
// The original folds the node array into many overlapping per-field BSS globals
// that all alias ONE 44-byte (0x2C) record array at 0x12CDD68, indexed at stride
// 44. Critically the fields overlap *across* records (e.g. node N's parentToId at
// +0x2C is the same physical word as node N+1's "childType" at +0x00), and the
// init loop performs `add eax,0x2C` between two groups of stores. To be provably
// byte-exact we model the whole store as one flat byte buffer and address every
// field by its literal symbol offset + 44*node, exactly as the disassembly does.
#include "world/road_network.h"
#include "util/coord.h"

#include <cstring>

namespace guild::world {

// ---------------------------------------------------------------------------
// Flat node store — addresses fields by absolute (symbolOffset + 44*node).
// Symbol offsets are relative to the record base 0x12CDD68.
// ---------------------------------------------------------------------------
namespace {

constexpr int kStride = 44;          // 0x2C bytes per node
// Field byte-offsets within the 44-byte record (relative to 0x12CDD68):
constexpr int OFF_CHILD   = 0x00;    // word_12CDD68 : entry child type ([v11+0xA5])
constexpr int OFF_PARIDX  = 0x02;    // word_12CDD6A : init 0xFFFF
constexpr int OFF_COORDX  = 0x04;    // dword_12CDD6C: laid-out X
constexpr int OFF_COORDY  = 0x08;    // dword_12CDD70: laid-out Y
constexpr int OFF_L10     = 0x10;    // dword_12CDD78: -1
constexpr int OFF_L14     = 0x14;    // dword_12CDD7C: -1
constexpr int OFF_L18     = 0x18;    // dword_12CDD80: -1
constexpr int OFF_L1C     = 0x1C;    // dword_12CDD84: -1
constexpr int OFF_L20     = 0x20;    // dword_12CDD88: -1
constexpr int OFF_FLAG    = 0x24;    // byte_12CDD8C : 0xFF
constexpr int OFF_D8E     = 0x26;    // dword_12CDD8E: hi-word(+0x28) == nodeId
constexpr int OFF_D92     = 0x2A;    // dword_12CDD92: lo(+0x2A)=parentFrom, hi(+0x2C)=parentTo
constexpr int OFF_DEPTH   = 0x2E;    // word_12CDD96 : chain depth
constexpr int OFF_D98     = 0x30;    // dword_12CDD98: cost (sort key)

// The flat store must hold every byte the solver touches. The highest absolute
// offset read is dword_12CDD9C (0x34) at node index nodeCount, i.e. the field
// dword_12CDD98 (0x30) viewed +1 record up. Allocate (kRoadNodeMax+2) records
// plus the highest field offset, all zero-initialised (matches BSS at rest).
constexpr int kBufBytes = (kRoadNodeMax + 2) * kStride + 0x40;

struct Store {
    u8 buf[kBufBytes];
    Store() { std::memset(buf, 0, sizeof(buf)); }
    i32&  d(int byteOff, int node)       { return *reinterpret_cast<i32*>(buf + byteOff + kStride * node); }
    i32   d(int byteOff, int node) const { return *reinterpret_cast<const i32*>(buf + byteOff + kStride * node); }
    u16&  w(int byteOff, int node)       { return *reinterpret_cast<u16*>(buf + byteOff + kStride * node); }
    u16   w(int byteOff, int node) const { return *reinterpret_cast<const u16*>(buf + byteOff + kStride * node); }
    u8&   b(int byteOff, int node)       { return buf[byteOff + kStride * node]; }
    // Swap two whole 44-byte records (the `rep movsd` of 0xB dwords at +2).
    void swapRec(int a, int c) {
        u8 tmp[kStride];
        u8* pa = buf + 2 + kStride * a;   // (dword_12CDD8E+2) base
        u8* pc = buf + 2 + kStride * c;
        std::memcpy(tmp, pa, kStride);
        std::memcpy(pa, pc, kStride);
        std::memcpy(pc, tmp, kStride);
    }
};

} // namespace

// ===========================================================================
// 0x592c7c — VIBE_Map_ComputeBuildingChainDepth (operates on the flat store).
// ===========================================================================
static int ChainDepth(Store& s, int nodeCount, int a1) {
    // v4 = word_12CDD96[a1]; if (v4 != 0xFFFF) return v4;
    u16 v4 = s.w(OFF_DEPTH, a1);
    if (v4 != 0xFFFFu) {
        return v4;                                       // 0x592ca4
    }
    int v3 = 0;
    int v11 = 0;
    // if (!LOWORD(dword_12CDD92[a1])) return 0;  (parentFromId)
    u16 parentFrom = s.w(OFF_D92, a1);                   // lo-word of dword_12CDD92
    if (parentFrom == 0) {
        return 0;                                        // 0x592d6e
    }
    // first link: scan for node whose nodeId == v6 (this node's parentFromId).
    if (nodeCount > 0) {
        // v6 = (dword_12CDD8E+2)[a1] >> 16 == hi-word of the +0x28 dword == word at
        // +0x2A == parentFromId (the lo-word of dword_12CDD92).
        int v6 = static_cast<i16>(s.w(OFF_D92, a1)); // word at +0x2A (parentFrom)
        int v5 = 0;
        // candidate nodeId = dword_12CDD8E[idx] >> 16 == hi-word of +0x26 dword ==
        // word at +0x28.
        while (v6 != static_cast<i16>(s.w(OFF_D8E + 2, v5))) {
            ++v5;
            if (v5 >= nodeCount) {                        // v7 >= 11*nodeCount
                goto label7;
            }
        }
        v3 = ChainDepth(s, nodeCount, v5) + 1;            // 0x592d79
    }
label7:
    // second link: HIWORD(dword_12CDD92[a1]) (parentToId).
    {
        i16 parentTo = static_cast<i16>(s.w(OFF_D92 + 2, a1)); // hi-word of dword_12CDD92
        if (parentTo == 0) {
            return v3;                                    // 0x592d0a
        }
        if (nodeCount > 0) {
            int v8 = 0;
            while (parentTo != static_cast<i16>(s.w(OFF_D8E + 2, v8))) {
                ++v8;
                if (v8 >= nodeCount) {
                    goto label12;
                }
            }
            v11 = ChainDepth(s, nodeCount, v8) + 1;       // 0x592d4f
        }
    }
label12:
    return v3 <= v11 ? v11 : v3;                          // 0x592d57
}

// Public wrapper over RoadLayoutState (mirrors fields into a temporary store).
int RoadComputeChainDepth(RoadLayoutState& st, int index) {
    Store s;
    for (int i = 0; i < st.nodeCount; ++i) {
        const RoadNode& n = st.nodes[i];
        s.w(OFF_D8E + 2, i) = static_cast<u16>(n.nodeId);       // nodeId hi-word slot
        s.w(OFF_D92, i)     = static_cast<u16>(n.parentFromId); // parentFrom lo
        s.w(OFF_D92 + 2, i) = static_cast<u16>(n.parentToId);   // parentTo hi
        s.w(OFF_DEPTH, i)   = n.depth;
    }
    int r = ChainDepth(s, st.nodeCount, index);
    // chain-depth mutates nothing observable except its recursion; depth memo in
    // the original is via the same alias and is recomputed by the layout caller,
    // so we leave st.nodes[*].depth untouched here (matches the standalone call).
    return r;
}

// ===========================================================================
// 0x592d98 — VIBE_Map_ComputeRoadNetworkLayout.
// ===========================================================================
int RoadComputeNetworkLayout(RoadLayoutState& st,
                             const u8* typeTableBase, const u8* typeFlagBase,
                             const i8* typeRecord, const i16* targetIdPtr,
                             int width, int height) {
    const int v80 = height;     // a4
    const int v81 = width;      // a3

    Store s;
    int nodeCount = 0;          // dword_13CE28C
    int levelCount = 0;         // dword_13CE284
    u16 levelStart[kRoadLevelMax];
    std::memset(levelStart, 0, sizeof(levelStart));

    // recBase = 589 * typeRecord[0] + dword_13CE294.
    const u8* rec = typeTableBase + kRoadTypeRecordStride * static_cast<int>(typeRecord[0]);
    const int count = rec[kRoadRecCountOff];
    const i16 targetId = *targetIdPtr;

    // --- find the entry whose (masked) id == targetId (0x592dd1) ---
    int j = 0;
    for (; count > j; ++j) {
        u16 entry = *reinterpret_cast<const u16*>(rec + kRoadRecEntryOff + 2 * j);
        entry &= 0x7FFFu;
        if (entry == static_cast<u16>(targetId)) {
            break;
        }
    }
    int v8 = j;
    if (count <= j) {
        return 1;                                          // 0x592e0e
    }

    // --- populate nodes from entries after the match (0x592e2c) ---
    ++j;
    nodeCount = 0;
    const u8* v10 = rec + 2 * (v8 + 1);
    const u8* v11 = rec + 4 * (v8 + 1);
    while (true) {
        if (count <= j) {
            break;                                         // 0x592e37
        }
        u16 entry = *reinterpret_cast<const u16*>(v10 + kRoadRecEntryOff);
        entry &= 0x7FFFu;
        int v103 = entry;
        u8 kind = typeFlagBase[kRoadTypeFlagStride * entry];
        if (kind == kRoadStopCategoryA || kind == kRoadStopCategoryB) {
            break;                                         // 0x592ea1
        }

        // HIWORD(dword_12CDD8E[n]) = v103  -> nodeId hi-word slot (+0x28).
        s.w(OFF_D8E + 2, nodeCount) = static_cast<u16>(v103);
        // LOWORD(dword_12CDD92[n]) = *(u16*)(v11+0xA3)  -> parentFrom (+0x2A).
        s.w(OFF_D92, nodeCount) =
            *reinterpret_cast<const u16*>(v11 + kRoadRecLinkLoOff);
        // (mid-loop add eax,0x2C: the next stores land in the NEXT record slot)
        // word_12CDD68[n+1] = *(u16*)(v11+0xA5) ; == parentTo (+0x2C) of node n.
        s.w(OFF_CHILD, nodeCount + 1) =
            *reinterpret_cast<const u16*>(v11 + kRoadRecLinkHiOff);
        s.w(OFF_PARIDX, nodeCount + 1) = 0xFFFFu;          // word_12CDD6A = -1
        s.d(OFF_COORDX, nodeCount + 1) = 0;                // dword_12CDD6C
        s.d(OFF_COORDY, nodeCount + 1) = 0;                // dword_12CDD70
        s.d(OFF_L10, nodeCount + 1) = -1;
        s.d(OFF_L14, nodeCount + 1) = -1;
        s.d(OFF_L18, nodeCount + 1) = -1;
        s.d(OFF_L1C, nodeCount + 1) = -1;
        s.d(OFF_L20, nodeCount + 1) = -1;
        s.b(OFF_FLAG, nodeCount + 1) = 0xFFu;              // byte_12CDD8C

        ++nodeCount;
        v10 += 2;
        v11 += 4;
        ++j;
    }

    // 0x592e63: targetId == 253 drains j (no node effect).
    if (targetId == kRoadSpecialTarget) {
        for (j = 0; count > j; ++j) {
            // empty (matches the original)
        }
    }

    if (nodeCount == 0) {
        return 1;                                          // 0x592f3a
    }

    // --- depth assignment + max (0x592f4e) ---
    levelCount = 0;
    for (j = 0; j < nodeCount; ++j) {
        u16 d = static_cast<u16>(ChainDepth(s, nodeCount, j));
        s.w(OFF_DEPTH, j) = d;
        u16 dd = s.w(OFF_DEPTH, j);
        if (dd >= static_cast<unsigned>(levelCount)) {     // 0x592f71 (>= keeps max)
            levelCount = dd;
        }
    }
    ++levelCount;                                          // 0x592f90

    // --- bubble-sort by depth, whole-record swap (0x592f96) ---
    bool swapped;
    do {
        swapped = false;
        for (j = 0; j < nodeCount; ++j) {
            for (int k = j + 1; k < nodeCount; ++k) {
                if (s.w(OFF_DEPTH, k) < s.w(OFF_DEPTH, j)) {
                    s.swapRec(j, k);
                    swapped = true;
                }
            }
        }
    } while (swapped);

    // --- level start indices (0x593052) ---
    int v23 = 0;
    levelStart[0] = 0;
    int v26 = 0;
    for (j = 0; j < nodeCount; ++j) {
        if (s.w(OFF_DEPTH, j) > static_cast<unsigned>(v23)) {
            ++v26;
            ++v23;
            levelStart[v26] = static_cast<u16>(j);
        }
    }
    levelStart[levelCount] = static_cast<u16>(nodeCount);

    // --- spread level 0 across height (0x5930c1) ---
    // v27 = v80 / (2 * word_12CE892[0]); word_12CE892[0] == levelStart[1].
    int count0 = static_cast<u16>(levelStart[1]);
    int v27 = v80 / (2 * count0);
    int v28 = 2 * v27;
    for (j = 0; j < count0; ++j) {
        s.d(OFF_COORDX, j) = v27 - kRoadNodeHalf;          // 0x5930ea dword_12CDD6C
        v27 += v28;
    }

    // --- per-level relaxation (0x593103) ---
    if (levelCount > 1) {
        int v97 = 0;
        for (int lvl = 1; lvl < levelCount; ++lvl) {
            int levStart = levelStart[lvl];
            int levEnd   = levelStart[lvl + 1];

            // (A) average cost from prior-level connected nodes (0x59314f).
            for (j = levStart; j < levEnd; ++j) {
                int v31 = 0;     // sum
                int v32 = 0;     // count
                int v33 = 0;
                int idx = 0;
                // v101 = (dword_12CDD8E+2)[j] >> 16 == word at +0x2A == parentFrom.
                int v101 = static_cast<i16>(s.w(OFF_D92, j));
                // parentTo = dword_12CDD92[j] >> 16 == word at +0x2C.
                int parentTo = static_cast<i16>(s.w(OFF_D92 + 2, j));
                while (v33 < levStart) {
                    int v36 = static_cast<i16>(s.w(OFF_D8E + 2, idx)); // candidate nodeId
                    if (v36 == v101) {                       // 0x59319b
                        ++v32;
                        v31 += s.d(OFF_D98, idx);
                    } else if (v36 == parentTo) {            // 0x5931ba
                        v31 += s.d(OFF_D98, idx);
                        ++v32;
                    }
                    ++idx;
                    ++v33;
                }
                s.d(OFF_D98, j) = v32 ? v31 / v32 : v31;     // 0x5931d3 / 0x5931f2
            }

            // (B) bubble-sort the level by cost, whole-record swap (0x5931fa).
            bool lvlSwapped;
            do {
                lvlSwapped = false;
                for (j = levStart; j < levEnd; ++j) {
                    for (int k = j + 1; k < levEnd; ++k) {
                        if (s.d(OFF_D98, k) < s.d(OFF_D98, j)) {
                            s.swapRec(j, k);
                            lvlSwapped = true;
                        }
                    }
                }
            } while (lvlSwapped);

            // (C) equal-cost runs spread across the band (0x593269).
            for (j = levStart; j < levEnd; ) {
                int v43 = j;
                while (v43 < levEnd && s.d(OFF_D98, j) == s.d(OFF_D98, v43)) {
                    ++v43;
                }
                if (v43 > j + 1) {                           // 0x593319
                    int v45 = v43 - j;
                    if (v45 > 0) {
                        int band = (v80 / count0) >> v97;    // 0x593348
                        int v95  = band - kRoadNodeHalf - kRoadNodeHalf * v45;
                        int v46  = 0;
                        int v87  = v95 >> 1;                 // 0x59336b
                        int v88  = v45 - 1;
                        for (int r = j; r < v43; ++r) {
                            int v49 = s.d(OFF_D98, r) - v87; // 0x59338f
                            int v50 = v49 + v46 / v88;       // 0x59339e
                            v46 += v95;
                            // the original writes dword_12CDD6C at +1 record bias:
                            s.d(OFF_COORDX, r + 1) = v50;    // 0x5933a2
                        }
                    }
                }
                j = v43;
            }

            // (D) enforce min 80px spacing within the level (0x5933b1).
            for (j = levStart; j < levEnd; ++j) {
                for (int k = j + 1; k < levEnd; ++k) {
                    int v56 = s.d(OFF_D98, k) - s.d(OFF_D98, j);  // 0x593407
                    if (v56 < kRoadMinGap) {
                        int v57 = (kRoadMinGap - v56) >> 1;       // 0x593421
                        s.d(OFF_D98, j) -= v57;
                        s.d(OFF_D98, k) += v57;
                    }
                }
            }
            ++v97;
        }
    }

    // --- cost min/max across all levels (0x593455) ---
    int v58 = -1;
    int v59 = v80 + 1;
    for (int lvl = 0; lvl < levelCount; ++lvl) {
        int sIdx = levelStart[lvl];
        int eIdx = levelStart[lvl + 1];
        for (j = sIdx; j < eIdx; ++j) {
            int c = s.d(OFF_D98, j);
            if (v59 >= c) v59 = c;                          // 0x59349c
            if (v58 <= c) v58 = c;                          // 0x5934a8
        }
    }

    // --- float X-rescale if the span overflows (0x5934da) ---
    if (v59 - kRoadNodeHalf < 0 || v58 + 4 * kRoadNodeHalf >= v80) {
        int v62  = v59 - kRoadNodeHalf;
        int v101 = v58 + 4 * kRoadNodeHalf - v62;
        double v91 = static_cast<double>(v80) / static_cast<double>(v101);
        double v63 = static_cast<double>(v62) * -v91;
        int v90 = static_cast<int>(util::ConvertX(v63));   // truncate-toward-zero
        for (int lvl = 0; lvl < levelCount; ++lvl) {
            int sIdx = levelStart[lvl];
            int eIdx = levelStart[lvl + 1];
            for (j = sIdx; j < eIdx; ++j) {
                double v71 = static_cast<double>(s.d(OFF_D98, j)) * v91;
                int scaled = static_cast<int>(util::ConvertX(v71));
                s.d(OFF_COORDX, j) = v90 + scaled;         // 0x593589
            }
        }
    }

    // --- Y placement per node (0x5935ab) ---
    int v72 = 4 * kRoadNodeHalf * levelCount;              // 96 * levelCount
    if (v72 >= v81 - kRoadTopMargin) {
        v72 = v81 - kRoadTopMargin;                        // clamp to width-16
    }
    for (j = 0; j < nodeCount; ++j) {
        int v77 = v72 * static_cast<u16>(s.w(OFF_DEPTH, j)) / levelCount;
        s.d(OFF_COORDY, j) = v77 + kRoadTopMargin;         // 0x593602 dword_12CDD70
    }

    // --- mirror the flat store into the public RoadLayoutState ---
    st.nodeCount  = nodeCount;
    st.levelCount = levelCount;
    for (j = 0; j < kRoadLevelMax; ++j) {
        st.levelStart[j] = levelStart[j];
    }
    for (j = 0; j < nodeCount && j < kRoadNodeMax; ++j) {
        RoadNode& n = st.nodes[j];
        n.childType    = s.w(OFF_CHILD, j);
        n.reserved02   = s.w(OFF_PARIDX, j);
        n.coordX       = s.d(OFF_COORDX, j);
        n.coordY       = s.d(OFF_COORDY, j);
        n.link10       = s.d(OFF_L10, j);
        n.link14       = s.d(OFF_L14, j);
        n.link18       = s.d(OFF_L18, j);
        n.link1C       = s.d(OFF_L1C, j);
        n.link20       = s.d(OFF_L20, j);
        n.flag24       = s.b(OFF_FLAG, j);
        n.nodeId       = static_cast<i16>(s.w(OFF_D8E + 2, j));
        n.parentFromId = static_cast<i16>(s.w(OFF_D92, j));
        n.parentToId   = static_cast<i16>(s.w(OFF_D92 + 2, j));
        n.depth        = s.w(OFF_DEPTH, j);
        n.cost         = s.d(OFF_D98, j);
    }
    return 0;                                              // 0x592e94
}

} // namespace guild::world
