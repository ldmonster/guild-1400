#include "sim/combat_escape.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace guild::sim {

namespace {

// abs32 — the original's integer absolute value (signed 32-bit).
inline int Abs32(int v) { return v < 0 ? -v : v; }

// Tile-type byte at (tileX, tileZ): entries[24 * (x + z*size)] (24-byte stride;
// type byte at offset 0). 0 == impassable; 13 == special-blocked. Mirrors
//   *(_BYTE *)(*(_DWORD *)(v7 + 36) + 24 * (i + v10 * *(_DWORD *)(v7 + 32)))
inline u8 TileType(const guild::render::Heightmap* hm, int x, int z) {
    return hm->entries[24 * (x + z * hm->size)];
}

// 3D world distance from a tile to a unit's mesh origin. The scorers compute
//   pos = world(tileX, tileZ);  d = |pos - unitOrigin|
inline double TileToUnitDist(const guild::render::Heightmap* hm,
                             int tileX, int tileZ, const float unitPos[3]) {
    float w[3];
    guild::render::TileToWorld(hm, tileX, tileZ, w);
    float dx = w[0] - unitPos[0];
    float dy = w[1] - unitPos[1];
    float dz = w[2] - unitPos[2];
    return std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz));
}

} // namespace

// gilde.exe 0x4865ec — VIBE_Coord_Distance3D.
double CoordDistance3D(const guild::render::Heightmap* hm,
                       int tileAx, int tileAz, int tileBx, int tileBz) {
    float a[3];
    float b[3];
    guild::render::TileToWorld(hm, tileAx, tileAz, a);   // v7
    guild::render::TileToWorld(hm, tileBx, tileBz, b);   // v8
    float dx = a[0] - b[0];   // v9
    float dy = a[1] - b[1];   // v10
    float dz = a[2] - b[2];   // v11
    return std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz));
}

// gilde.exe 0x486524 — VIBE_Combat_DistanceObjectToTarget.
double DistanceObjectToTarget(const guild::render::Heightmap* hm,
                              const float unitPos[3], int tileX, int tileZ) {
    float w[3];                                          // v6
    guild::render::TileToWorld(hm, tileX, tileZ, w);
    // v6[1] = 0.0; — world Y forced to zero, and the unit Y term cancels
    // (sqrt(... + 0.0*0.0 + ...)), so only the X and Z deltas contribute.
    float dx = w[0] - unitPos[0];                        // v7
    float dz = w[2] - unitPos[2];                        // v9
    return std::sqrt(static_cast<double>(dx * dx + 0.0f * 0.0f + dz * dz));
}

// gilde.exe 0x48ae54 — VIBE_Combat_ComputeTileThreatScore.
//
// In the original, `v6`/`v7` select which roster is the "enemy" sweep (added to
// the score, +70/d) and which is the "ally" sweep (subtracted, -70/d, excluding
// self). When the acting unit's team == dword_6311E8 (side A), the +192 roster
// is the enemy set and +128 is the ally set; otherwise they swap. ThreatField
// presents these pre-resolved as enemyRoster / allyRoster, so here:
//   * enemy roster  -> v8/v9 loop -> score += 70 / dist
//   * ally  roster  -> v10/v11 loop -> score -= 70 / dist (skipping self)
double ComputeTileThreatScore(const ThreatField& field, int tileX, int tileZ,
                              float radius) {
    const guild::render::Heightmap* hm = field.heightmap;
    double score = 0.0;   // v25

    float tilePos[3];     // v17/v18/v19 — world point of the tile
    guild::render::TileToWorld(hm, tileX, tileZ, tilePos);

    // --- enemy roster (adds danger): v8..v9 ---
    for (int i = 0; i < field.rosterCount; ++i) {
        i32 id = field.enemyRoster[i];
        if (id == -1)                                   // *v8 != -1
            continue;
        const void* u = field.findUnitById(id);         // FindUnitById(*v8)
        if (!u)
            continue;
        if (!field.unitAlive(u))                         // *(UnitById + 8)
            continue;
        float p[3];
        field.unitWorldPos(u, p);
        float dx = tilePos[0] - p[0];                    // v20
        float dy = tilePos[1] - p[1];                    // v21
        float dz = tilePos[2] - p[2];                    // v22
        double d = std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz)); // v14
        if (d <= radius)
            score += kThreatUnitWeight / d;              // flt_61B87C / v24 + v25
    }

    // --- ally roster (reduces danger): v10..v11, skipping self ---
    for (int i = 0; i < field.rosterCount; ++i) {
        i32 id = field.allyRoster[i];
        if (id == -1)                                   // *v10 != -1
            continue;
        const void* u = field.findUnitById(id);
        if (!u)
            continue;
        if (!field.unitAlive(u))
            continue;
        if (u == field.self)                             // v15 != a1
            continue;
        float p[3];
        field.unitWorldPos(u, p);
        float dx = tilePos[0] - p[0];
        float dy = tilePos[1] - p[1];
        float dz = tilePos[2] - p[2];
        double d = std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz)); // v16
        if (d <= radius)
            score -= kThreatUnitWeight / d;              // v25 - flt_61B87C / v23
    }

    return score;
}

namespace {

// The diamond-spiral tile sweep shared by FindSafestTileInRange and
// FindMostThreatenedTile. Faithful to both originals (identical loop skeleton,
// differing only in the radius constant, the comparison direction, and the
// accept test on the final extreme). `pick(score, best)` returns true when
// `score` should replace `best` (i.e. < for safest, > for most-threatened).
template <class Pick>
void SpiralSweep(const ThreatField& field, int centreX, int centreZ, int steps,
                 double radiusFactor, float initScore,
                 TileSearchResult& out, double& bestScore, Pick pick) {
    const guild::render::Heightmap* hm = field.heightmap;
    bestScore = initScore;        // v27
    if (steps <= 0)               // if ( a3 > 0 )
        return;

    int width = hm->size;         // *(v7 + 32)
    float tileScaleX = hm->scaleX;// *(float *)(v7 + 16)

    int v21 = centreZ;            // grows: ring upper-Z bound source
    int v22 = centreZ;            // shrinks: ring lower-Z bound source
    for (int ring = 0; ring < steps; ++ring) {   // v24 = 0 .. a3-1
        int hi = (width - 1 < v21) ? (width - 1) : v21;  // v23 = min(width-1, v21)
        int lo = (v22 < 0) ? 0 : v22;                    // v9 = max(0, v22)
        int z = lo;                                       // v10
        if (z <= hi) {
            int v19 = centreZ - lo;   // a4 - v9 (signed row offset, decreases)
            do {
                unsigned colHalf = static_cast<unsigned>(ring - Abs32(v19)); // v24 - abs(v19)
                int xHi = static_cast<int>(colHalf + centreX);               // v11 + a2
                if (width - 1 < xHi)
                    xHi = width - 1;
                int xLo = centreX - static_cast<int>(colHalf);               // a2 - v11
                if (xLo < 0)
                    xLo = 0;
                for (int x = xLo; x <= xHi; ++x) {
                    u8 type = TileType(hm, x, z);          // v15
                    if (type != 0 && type != 13) {
                        float pradius = static_cast<float>(
                            static_cast<double>(steps) * tileScaleX * radiusFactor); // v18
                        double s = ComputeTileThreatScore(field, x, z, pradius);     // v16
                        if (pick(s, bestScore)) {
                            out.tileX = x;
                            out.tileZ = z;
                            bestScore = s;                 // v28 -> v27
                        }
                    }
                }
                ++z;
                --v19;
            } while (z <= hi);
        }
        ++v21;
        --v22;
    }
}

} // namespace

// gilde.exe 0x48b01c — VIBE_Combat_FindSafestTileInRange.
bool FindSafestTileInRange(const ThreatField& field, int centreX, int centreZ,
                           int steps, TileSearchResult& out) {
    double best;
    SpiralSweep(field, centreX, centreZ, steps, kSafestRadiusFactor,
                kSafestInitScore, out,
                best, [](double s, double b) { return s < b; });
    // return v27 != flt_61B88C  (1e9 sentinel: unchanged means nothing found)
    out.found = (static_cast<float>(best) != kSafestInitScore);
    return out.found;
}

// gilde.exe 0x48b1b4 — VIBE_Combat_FindMostThreatenedTile.
bool FindMostThreatenedTile(const ThreatField& field, int centreX, int centreZ,
                            int steps, TileSearchResult& out) {
    double best;
    SpiralSweep(field, centreX, centreZ, steps, kThreatRadiusFactor,
                0.0f, out,
                best, [](double s, double b) { return s > b; });
    // return (|v27| & 0x7FFFFFFF) != 0 && v27 > 5.0
    float bf = static_cast<float>(best);
    out.found = (bf != 0.0f) && (bf > kThreatMinScore);
    return out.found;
}

// gilde.exe 0x48b350 — VIBE_Combat_EscapeTileCallback.
bool EscapeTileCollect(const char* nodeName, const void* node,
                       std::vector<const void*>& out) {
    // if ( !VIBE_Util_StrCmpNoCase(name, "sp_ESCAPE") ) collect (StrCmpNoCase
    // returns 0 on equal).
#if defined(_WIN32)
    bool equal = (_stricmp(nodeName, kEscapeNodeName()) == 0);
#else
    bool equal = (strcasecmp(nodeName, kEscapeNodeName()) == 0);
#endif
    if (equal)
        out.push_back(node);
    return true;   // always 1 (keep walking)
}

// gilde.exe 0x48b384 — VIBE_Combat_FindNearestEscapeTile.
const void* FindNearestEscapeTile(
    const float unitOrigin[3],
    const std::vector<const void*>& escapeNodes,
    const std::function<void(const void* node, float out[3])>& nodeOrigin) {
    const void* best = nullptr;   // v3
    float bestDist = 1000000.0f;  // v14
    for (const void* node : escapeNodes) {
        float p[3];
        nodeOrigin(node, p);                  // node->[19..21]
        float dx = unitOrigin[0] - p[0];      // v10
        float dy = unitOrigin[1] - p[1];      // v11
        float dz = unitOrigin[2] - p[2];      // v12
        float d = static_cast<float>(
            std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz))); // v7/v15
        if (d < bestDist) {
            best = node;
            bestDist = d;
        }
    }
    return best;
}

} // namespace guild::sim
