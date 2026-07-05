#include "sim/path.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "sim/map.h"
#include "util/coord.h"

// Faithful 1:1 port of the VIBE_Path_* bidirectional A* from gilde.exe.
//
// The search keeps NO separate node array: each 24-byte heightmap tile entry IS
// the A* node for the cell at that linear index. A per-search generation stamp
// (g_pathGeneration, == word_765318) marks which entries belong to the current
// route; an entry with a stale stamp is reinitialised the first time it is
// touched. Two frontiers (one from start, one from goal) expand alternately;
// when a cell is touched by both (flags bits 2 and 3 set) the half-chains are
// stitched by ReverseParentChain.
//
// All field accesses use the recovered 24-byte layout (see path.h). Words are
// read/written via memcpy to stay faithful to the original unaligned x86 loads
// without invoking UB.

namespace guild::sim {

// gilde.exe unk_62E630 — FIVE cost profiles of 15 floats each (60 bytes per
// profile, 0x62E630..0x62E75B; get_bytes-verified). dword_765308 is set to
// &unk_62E630 + 60*profile at 0x43bfe9. Profile 3 is live (the door-cell
// scanner 0x577320 calls BuildWaypointList with a7=3 at 0x57744d).
// Negative == impassable.
const float kPathCostProfiles[kPathCostProfileCount * kPathCostTypes] = {
    // profile 0 @0x62E630
    999.0f, 20.0f, 30.0f, 30.0f, 30.0f, 30.0f, 1.0f, 30.0f,
    40.0f,  30.0f, -1.0f, 10.0f, 0.5f,  -1.0f, 99.0f,
    // profile 1 @0x62E66C
    99.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f,  1.0f, 1.0f, 1.25f, 0.75f, -1.0f, 99.0f,
    // profile 2 @0x62E6A8
    99.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 1.0f, 50.0f,
    60.0f, 50.0f, -1.0f, 10.0f, 0.5f, -1.0f, 99.0f,
    // profile 3 @0x62E6E4
    99.0f, 50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 1.0f, 50.0f,
    50.0f, 50.0f, -1.0f, 1.0f, 1.0f, -1.0f, 99.0f,
    // profile 4 @0x62E720
    99.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f,  1.0f, 99.0f, 1.0f, 1.0f, -1.0f, 1.0f,
};

// Profile 0 view kept for existing external references.
const float* const kPathTypeCost = kPathCostProfiles;

float PathTypeCost(u8 typeByte, int profile) {
    // The original indexes by the *signed* tile byte (char); negative bytes
    // would read before the table, but real cell types are 0..14.
    int idx = static_cast<i8>(typeByte);
    if (idx < 0 || idx >= kPathCostTypes)
        return -1.0f;
    if (profile < 0 || profile >= kPathCostProfileCount)
        return -1.0f;
    return kPathCostProfiles[kPathCostTypes * profile + idx];
}

// ---- search scratch (gilde.exe module globals) ----------------------------
namespace {

// Field offsets within a 24-byte tile entry.
constexpr int kF_type    = 0;   // u8  cell type
constexpr int kF_stamp   = 2;   // u16 generation stamp
constexpr int kF_f       = 4;   // f32 total cost
constexpr int kF_g       = 8;   // f32 cost-so-far
constexpr int kF_h       = 12;  // f32 heuristic
constexpr int kF_flags   = 16;  // u8
constexpr int kF_prev    = 18;  // u16 open-list prev
constexpr int kF_next    = 20;  // u16 open-list next
constexpr int kF_parent  = 22;  // u16 path back-link

int   g_colMask = 0;          // dword_62E5E8 = size-1
int   g_shift   = 0;          // dword_62E5EC = log2(size)
int   g_rowOff[8] = {0};      // word_62E600[8] (recomputed per route)
const int g_dx[8] = {-1, 0, 1, 0, -1, 1, 1, -1};  // word_62E5F0[8]
const float g_stepMul[8] = {1.0f, 1.0f, 1.0f, 1.0f,
                            1.4142099618911743f, 1.4142099618911743f,
                            1.4142099618911743f, 1.4142099618911743f};  // flt_62E610

u16  g_goalNode  = 0;         // word_765310
u16  g_startNode = 0;         // word_765316
u16  g_generation = 0;        // word_765318
const float* g_costTable = nullptr;  // dword_765308
u16  g_meetNode = 0;          // word_765314
float g_heurWeight = 7.0f;    // dword_765300 (set to 7.0)

// Per-direction expansion context.
u16  g_target = 0;            // word_76531A (cell being expanded toward)
u8   g_touchSelf = 0;         // byte_76531C  (bit this side sets on the cell)
u8   g_touchOther = 0;        // byte_76531D  (bit the OTHER side would have set)

u8* g_base = nullptr;         // tile-entry buffer base (map+36)

inline u8* Node(int idx) { return g_base + 24 * static_cast<u16>(idx); }

inline u16 RdW(const u8* n, int off) {
    u16 v; std::memcpy(&v, n + off, 2); return v;
}
inline void WrW(u8* n, int off, u16 v) { std::memcpy(n + off, &v, 2); }
inline float RdF(const u8* n, int off) {
    float v; std::memcpy(&v, n + off, 4); return v;
}
inline void WrF(u8* n, int off, float v) { std::memcpy(n + off, &v, 4); }

} // namespace

// gilde.exe 0x43c594 — VIBE_Path_ReverseParentChain.
// Reverses the back-links of the chain ending at node `a2` so that following
// +18 from `a3` walks start->...->a2, then copies the +18 links into the +22
// parent field used by the waypoint walk.
static int PathReverseParentChain(u16 a2, int a3) {
    u8* node = Node(a2);
    if (RdW(node, kF_parent) == 0xFFFF)
        return 0;  // result is unused by callers in this branch

    WrW(node, kF_prev, 0xFFFF);
    for (int i = a2; i != 0xFFFF;
         i = RdW(Node(i), kF_parent)) {
        u16 v7 = RdW(Node(i), kF_parent);
        if (v7 != 0xFFFF) {
            WrW(Node(v7), kF_prev, static_cast<u16>(i));
            a3 = RdW(Node(i), kF_parent);
        }
    }
    int result = a3;
    for (; result != 0xFFFF; result = RdW(Node(result), kF_prev)) {
        WrW(Node(result), kF_parent, RdW(Node(result), kF_prev));
    }
    return result;
}

// gilde.exe 0x43c1c4 — VIBE_Path_ExpandNode. Expands node `a2` (closing it),
// relaxing its 8 neighbours into the open list. Returns the meeting node index
// (when a neighbour was already touched by the other frontier) or the open-list
// head to expand next (v28). `j` carries the heuristic-target seed in its low
// byte from the caller; only the 8-neighbour loop reads it via g_dx/g_rowOff.
static int PathExpandNode(int a2, int /*j_in*/) {
    u8* cur = Node(a2);
    int v28 = RdW(cur, kF_prev);            // current open-list head candidate
    cur[kF_flags] = (cur[kF_flags] & 0xFC) | 2;   // mark expanded (bit1), clear open bits

    // Unlink `cur` from the open list (its successor's prev <- cur's prev).
    if (RdW(cur, kF_prev) != 0xFFFF)
        WrW(Node(RdW(cur, kF_prev)), kF_next, RdW(cur, kF_next));

    for (int dir = 0; dir < 8; ++dir) {
        int v27 = a2 + g_dx[dir] + static_cast<i16>(g_rowOff[dir]);
        u8* nb = Node(v27);

        // Already on the OTHER frontier? -> meeting point.
        if (RdW(nb, kF_stamp) == g_generation &&
            (g_touchOther & nb[kF_flags]) != 0) {
            nb[kF_flags] |= g_touchSelf;
            cur[kF_flags] |= g_touchOther;
            g_meetNode = static_cast<u16>(v27);
            return a2;
        }

        float typeCost = g_costTable[static_cast<i8>(nb[kF_type])];
        // 0x43c288: fld flt_62E610[i]; fmul [costTable+..]; fadd cur.g; fstp v23
        // — the whole chain stays on the x87 stack until the single fstp, so
        // model the intermediates as double and narrow once (tree convention).
        float tentativeG = static_cast<float>(
            static_cast<double>(g_stepMul[dir]) * static_cast<double>(typeCost) +
            static_cast<double>(RdF(cur, kF_g)));

        // Relax only if the type is passable and this is a fresh node OR we
        // found a cheaper g. The original compares against nb.g (+8 == kF_g).
        bool fresh = (RdW(nb, kF_stamp) != g_generation);
        bool improves = fresh || tentativeG < RdF(nb, kF_g);
        if (!(typeCost >= 0.0f && improves))
            continue;

        float hVal;
        if (!fresh && (nb[kF_flags] & 1) != 0) {
            // Neighbour already open: relink in the open list (remove + reinsert).
            hVal = RdF(nb, kF_h);
            u16 nxt = RdW(nb, kF_next);
            if (nxt == 0xFFFF) {
                v28 = RdW(nb, kF_prev);
            } else {
                WrW(Node(nxt), kF_prev, RdW(nb, kF_prev));
            }
            if (RdW(nb, kF_prev) != 0xFFFF)
                WrW(Node(RdW(nb, kF_prev)), kF_next, RdW(nb, kF_next));
        } else {
            // Fresh heuristic: weighted Manhattan distance to g_target.
            int dxv = std::abs((g_target & g_colMask) - (v27 & g_colMask));
            int dyv = std::abs(((g_target & 0xFFFF) >> g_shift) - ((v27 & 0xFFFF) >> g_shift));
            hVal = static_cast<float>(dxv + dyv) * g_heurWeight;
        }

        float fVal = tentativeG + hVal;
        WrF(nb, kF_f, fVal);
        WrF(nb, kF_g, tentativeG);
        WrF(nb, kF_h, hVal);
        WrW(nb, kF_parent, static_cast<u16>(a2));   // *(v7+11 word) == +22

        // (Re)insert into the sorted open list unless already expanded.
        if (RdW(nb, kF_stamp) != g_generation || (nb[kF_flags] & 2) == 0) {
            nb[kF_flags] = (nb[kF_flags] & 0xFD);            // clear expanded bit
            WrW(nb, kF_stamp, g_generation);
            nb[kF_flags] = static_cast<u8>(~g_touchOther) & nb[kF_flags];
            nb[kF_flags] = (nb[kF_flags] | 1);               // on open list
            nb[kF_flags] = g_touchSelf | nb[kF_flags];

            // 0x43c395: the binary tests only the LOW WORD of v28
            // (`(_WORD)v28 == 0xFFFF`); v28 may carry v27's upper bits.
            if (static_cast<u16>(v28) == 0xFFFF) {
                WrW(nb, kF_next, 0xFFFF);
                WrW(nb, kF_prev, 0xFFFF);
                v28 = v27;
            } else {
                // Insertion sort by f into the open list starting at v28.
                int j = v28;
                u8* it = nullptr;
                for (;; j = RdW(it, kF_prev)) {
                    it = Node(j);
                    if (RdW(it, kF_prev) == 0xFFFF)
                        break;
                    if (fVal < RdF(it, kF_f)) {
                        WrW(nb, kF_next, RdW(it, kF_next));
                        WrW(nb, kF_prev, static_cast<u16>(j));
                        u16 nn = RdW(it, kF_next);
                        if (nn == 0xFFFF)
                            v28 = v27;
                        else
                            WrW(Node(nn), kF_prev, static_cast<u16>(v27));
                        WrW(it, kF_next, static_cast<u16>(v27));
                        break;
                    }
                }
                it = Node(j);
                if (RdW(it, kF_prev) == 0xFFFF) {
                    WrW(nb, kF_next, static_cast<u16>(j));
                    WrW(nb, kF_prev, 0xFFFF);
                    WrW(it, kF_prev, static_cast<u16>(v27));
                }
            }
        }

        if (static_cast<u16>(v27) == g_target)
            break;
    }
    return v28;
}

// gilde.exe 0x43be20 — VIBE_Path_FindRoute.
int PathFindRoute(const MapGrid& g, int startX, int startY,
                  int goalX, int goalY, int profile) {
    if (!g.entries || g.size <= 0)
        return 0xFFFF;

    g_base = g.entries;
    const int size = g.size;

    // Per-route parameters.
    g_colMask = size - 1;             // dword_62E5E8
    g_shift = 0;                      // dword_62E5EC = log2(size)
    for (int i = size >> 1; i; i >>= 1)
        ++g_shift;

    // Neighbour row offsets (word_62E600..word_62E60E) recomputed from size:
    //   {0, -size, 0, +size, -size, -size, +size, +size}
    g_rowOff[0] = 0;
    g_rowOff[1] = -size;
    g_rowOff[2] = 0;
    g_rowOff[3] = size;
    g_rowOff[4] = -size;
    g_rowOff[5] = -size;
    g_rowOff[6] = size;
    g_rowOff[7] = size;

    g_meetNode = 0;                   // word_765312 cleared (loop counter in orig)

    // Goal node index = (goalX << shift) + goalY  (orig: a5<<shift + a3 / dx).
    // The original's arg interleave: start = (startY<<shift)+startX placed in v9;
    // goal placed in word_765310. We map (col,row) -> (row<<shift)+col.
    int goalNode = (goalY << g_shift) + goalX;
    g_goalNode = static_cast<u16>(goalNode);

    int startNode = (startY << g_shift) + startX;
    g_startNode = static_cast<u16>(startNode);

    int v9 = startNode;

    // Initialise the start node.
    g_generation++;                   // ++word_765318
    {
        u8* s = Node(v9);
        WrW(s, kF_stamp, g_generation);
        WrF(s, kF_g, 0.0f);
        WrF(s, kF_h, 0.0f);
        WrF(s, kF_f, 0.0f);
        s[kF_flags] = 5;              // open(1) | forward-touched(4)
        WrW(s, kF_prev, 0xFFFF);
        WrW(s, kF_next, 0xFFFF);
        WrW(s, kF_parent, 0xFFFF);
    }
    if (static_cast<u16>(v9) == g_goalNode)
        return v9;

    int v23 = g_goalNode;
    {
        u8* gn = Node(g_goalNode);
        WrW(gn, kF_stamp, g_generation);
        WrF(gn, kF_g, 0.0f);
        WrF(gn, kF_h, 0.0f);
        WrF(gn, kF_f, 0.0f);
        gn[kF_flags] = 9;             // open(1) | backward-touched(8)
        WrW(gn, kF_prev, 0xFFFF);
        WrW(gn, kF_next, 0xFFFF);
        WrW(gn, kF_parent, 0xFFFF);
    }

    // 0x43bfe9: dword_765308 = &unk_62E630 + 60*profile (5 shipped profiles).
    g_costTable = kPathCostProfiles + kPathCostTypes * profile;

    // If either endpoint sits on an impassable cell, fail.
    {
        u8* s = Node(v9);
        u8* gn = Node(g_goalNode);
        if (g_costTable[static_cast<i8>(s[kF_type])] < 0.0f ||
            g_costTable[static_cast<i8>(gn[kF_type])] < 0.0f)
            return 0xFFFF;
    }

    g_heurWeight = 7.0f;              // dword_765300 forced to 7.0

    int v16 = 0;                      // backward frontier head
    int fwd = v9;                     // forward frontier head

    while (true) {
        // Forward expansion (toward the goal).
        g_target = g_goalNode;        // word_76531A
        g_touchOther = 8;             // byte_76531D (other side = backward)
        g_touchSelf = 4;              // byte_76531C (this side = forward)
        int v18 = PathExpandNode(static_cast<u16>(fwd), v16);
        fwd = static_cast<u16>(v18);
        if (static_cast<u16>(v18) == 0xFFFF)
            return v18;
        if (static_cast<u16>(v18) == g_goalNode) {
            PathReverseParentChain(static_cast<u16>(v18), 0 /*v14 origin scale*/);
            return g_startNode;
        }
        if ((Node(v18)[kF_flags] & 0xC) == 0xC) {  // touched by both frontiers
            PathReverseParentChain(static_cast<u16>(v18), 0);
            WrW(Node(v18), kF_parent, g_meetNode);
            return g_startNode;
        }

        // Backward expansion (toward the start).
        g_target = g_startNode;       // word_76531A
        g_touchSelf = 8;
        g_touchOther = 4;
        v18 = PathExpandNode(static_cast<u16>(v23), 4 /*j seed*/);
        v16 = v18;
        v23 = static_cast<u16>(v18);
        if (static_cast<u16>(v18) == 0xFFFF)
            return v18;
        if (static_cast<u16>(v18) == g_startNode)
            return static_cast<u16>(v18);
        if ((Node(v18)[kF_flags] & 0xC) == 0xC)
            break;
    }

    PathReverseParentChain(g_meetNode, 0);
    WrW(Node(g_meetNode), kF_parent, static_cast<u16>(v16));
    return g_startNode;
}

// gilde.exe 0x406810 — VIBE_Path_ResamplePolyline.
// Stride numerator / rounding bias (flt_6106E0 == 12.0, flt_6106E4 == 0.5).
static const float kResampleStrideNum  = 12.0f;  // flt_6106E0
static const float kResampleStrideBias = 0.5f;   // flt_6106E4
int PathResamplePolyline(const PathPolyline* line, u8* out, float scaleX) {
    // stride = trunc(12.0 / scaleX + 0.5)  (VIBE_Coord_ConvertX truncates st0).
    double strideF = (double)kResampleStrideNum / scaleX + kResampleStrideBias;
    int stride = (int)guild::util::ConvertX(strideF);

    // step = max(stride, 1)  (asm: cmp stride,1; jge -> step=stride else step=1).
    int step = (stride >= 1) ? stride : 1;

    int outCount = 0;        // v7
    int idx = 1;             // v6 (source index, starts at 1)
    u8* dst = out;           // v9
    const int count = line->count;          // *a1
    const u8* tiles = line->tiles;          // a1[2]

    while (idx < count) {
        const u8* src = tiles + 2 * idx;    // 2*v6 + a1[2]
        dst[0] = src[0];                    // col
        dst[1] = src[1];                    // row
        dst += 2;
        idx += step;
        ++outCount;
    }

    // If the walk stopped exactly on the last point, it is already emitted.
    if (idx == count - 1)
        return outCount;

    // Otherwise append the final point (index count-1).
    dst[0] = tiles[2 * count - 2];
    dst[1] = tiles[2 * count - 1];
    return outCount + 1;
}

// gilde.exe 0x43bd70 — VIBE_Path_BuildWaypointList.
int PathBuildWaypointList(const MapGrid& g, int startX, int startY,
                          int goalX, int goalY, int profile,
                          PathStep* out, int maxSteps) {
    if (!g.entries)
        return -1;
    const int size = g.size;
    // gilde.exe 0x43bd9b: the binary bounds-checks ONLY the two args that arrive
    // as a5/a6 (BuildWaypointList stack slots), which — through the swizzled
    // register interleave into FindRoute — are goalX and goalY. startX/startY are
    // NOT range-checked here (FindRoute consumes them unvalidated).
    //   if ( a5 > v9 || a6 > v9 || a5 < 0 || a6 < 0 ) return 0xFFFF;
    if (goalX > size || goalY > size || goalX < 0 || goalY < 0)
        return -1;

    int route = PathFindRoute(g, startX, startY, goalX, goalY, profile);
    if (static_cast<u16>(route) == 0xFFFF)
        return -1;

    int count = 0;
    int node = g_startNode;
    while (count < maxSteps) {
        if (static_cast<u16>(node) == 0xFFFF)
            break;
        out[count].x = node & g_colMask;
        out[count].y = (node & 0xFFFF) >> g_shift;
        ++count;
        node = RdW(Node(node), kF_parent);
    }
    return count;
}

} // namespace guild::sim
