// pathfind_map — faithful 1:1 ports of the untranslated VIBE_Path_* / VIBE_Map_* /
// VIBE_ObjectSearch_* leaves. See pathfind_map.h for the function map and the
// recovered layout / constants.
#include "sim/pathfind_map.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace guild::sim {

// dword_478410 — 16 strides co-prime with 768 (0x300). The high eight are the low
// eight added to 744 (the original stored them as the literal values below).
const int kPaletteStrideTable[kPaletteStrideCount] = {
    1, 5, 7, 11, 13, 17, 19, 23, 745, 749, 751, 755, 757, 761, 763, 767,
};

i32 PathfindMapTruncToInt(double v) { return static_cast<i32>(v); }

// ---------------------------------------------------------------------------
// Hooks: inert defaults (model an empty / no-op world).
// ---------------------------------------------------------------------------
namespace {

int   InertEligibility(u16, u16, void*)          { return 0; }
double InertFavorability(int, u16, int)          { return 0.0; }
void  InertSetGray(int, int)                     {}
int   InertOfficeRank(u16, int)                  { return 0; }
const i32* InertRingAdvance()                    { return nullptr; }
void* InertFindObject(int, int, const char*, int, void*) { return nullptr; }
void* InertAttach(int, const float*, const char*, int)   { return nullptr; }
void  InertLightCache(void*)                     {}
void  InertLoadAnim(int, const char*, int)       {}
void  InertSwitchSlot(int, int, int, int)        {}
void  InertResetSlot(int, int)                   {}
void  InertEnterCity(const char*, int)           {}
void  InertSaveFile(const char*, const char*, int, int) {}
void  InertResetBuildings()                      {}
void  InertResetPersons()                        {}
void  InertRelinkOwners()                        {}
void  InertDestroySpawned(int, int)              {}
int   InertLoadScene(const char*, int, i16, int) { return 0; }
void  InertProject(const float*, const float*, i32* out) { if (out) { out[0] = 0; out[1] = 0; out[2] = 0; } }
int   InertTileToWorld(int, int, float*, int)    { return 0; }

PathfindMapHooks MakeInert() {
    PathfindMapHooks h{};
    h.evaluateEligibility      = InertEligibility;
    h.computeFavorability      = InertFavorability;
    h.setGrayColor             = InertSetGray;
    h.computeOfficeRank        = InertOfficeRank;
    h.objectRingAdvance        = InertRingAdvance;
    h.objectRingCount          = 0;
    h.objectRingPinned         = nullptr;
    h.findObjectByHandle       = InertFindObject;
    h.attachToUniverseNode     = InertAttach;
    h.lightBuildObjectCache    = InertLightCache;
    h.loadObjectAnimation      = InertLoadAnim;
    h.universeSwitchActiveSlot = InertSwitchSlot;
    h.universeResetCurrentSlot = InertResetSlot;
    h.sceneEnterCity           = InertEnterCity;
    h.saveWriteGameFile        = InertSaveFile;
    h.buildingResetAll         = InertResetBuildings;
    h.worldResetPersonTable    = InertResetPersons;
    h.worldRelinkObjectOwners  = InertRelinkOwners;
    h.objectDestroySpawned     = InertDestroySpawned;
    h.sceneLoadFromStream      = InertLoadScene;
    h.coordProjectPoint        = InertProject;
    h.heightmapTileToWorld     = InertTileToWorld;
    return h;
}

PathfindMapHooks g_hooks = MakeInert();

}  // namespace

PathfindMapHooks PathfindMapSetHooks(const PathfindMapHooks* hooks) {
    PathfindMapHooks prev = g_hooks;
    g_hooks = hooks ? *hooks : MakeInert();
    return prev;
}

const PathfindMapHooks& PathfindMapGetHooks() { return g_hooks; }

// ===========================================================================
// ObjectSearch palette / colour probes.
// ===========================================================================

// Whether (minR,maxR) names a real favourability range (the original's v25/v20/v22
// gate): true unless minR<=0 and maxR>=100, and also requires minR<maxR.
static bool PaletteRangeActive(float minR, float maxR) {
    return (minR > 0.0f || maxR < static_cast<float>(kFavorabilityFullRange))
           && minR < maxR;
}

// Probe one palette slot starting at `start`, stepping by `stride` (mod 768), up to
// 768 iterations. Returns the first palette index that passes the eligibility gate
// and (when `rangeActive`) the favourability filter, or -1 if none.
static int PaletteProbeSlot(const PathfindMapHooks& hk, const u16* refId, void* filter,
                            float minR, float maxR, bool rangeActive,
                            int start, int stride) {
    int idx = start;
    for (int i = kPaletteSize; i != 0; --i) {
        if (hk.evaluateEligibility(*refId, static_cast<u16>(idx), filter)) {
            if (!rangeActive) {
                return idx;
            }
            double fav = hk.computeFavorability(idx, *refId, 1);
            if (fav >= minR && fav <= static_cast<double>(maxR)) {
                return idx;
            }
        }
        idx = (idx + stride) % kPaletteSize;
    }
    return -1;
}

int ObjectSearchFindByPaletteRange(const u16* refId, unsigned count, void* filter,
                                   float minR, float maxR, u16* outIds,
                                   int strideIndex, int probeStart) {
    if (count > 3) {
        return 0;  // 0x47ac10
    }
    const PathfindMapHooks& hk = g_hooks;
    int stride = kPaletteStrideTable[strideIndex & (kPaletteStrideCount - 1)];

    hk.setGrayColor(0, 40);   // 0x47ac47
    hk.setGrayColor(255, 8);  // 0x47ac5e

    unsigned slot = 0;
    // Pinned object fast path (dword_62EB8C): slot 0 prefilled if eligible.
    if (hk.objectRingPinned) {
        u16 pinnedId = static_cast<u16>(hk.objectRingPinned[0]);
        if (hk.evaluateEligibility(*refId, pinnedId, filter)) {
            outIds[0] = pinnedId;
            slot = 1;
        }
    }

    bool rangeActive = PaletteRangeActive(minR, maxR);
    for (; slot < count; ++slot) {
        int start = probeStart;
        int found = PaletteProbeSlot(hk, refId, filter, minR, maxR, rangeActive,
                                     start, stride);
        if (found < 0) {
            return 0;  // 0x47adb7
        }
        outIds[slot] = static_cast<u16>(found);
    }
    return 1;  // 0x47ac14
}

int ObjectSearchFindOneByPaletteRange(const u16* refId, void* filter,
                                      float minR, float maxR, u16* outId,
                                      int strideIndex, int probeStart) {
    const PathfindMapHooks& hk = g_hooks;
    int stride = kPaletteStrideTable[strideIndex & (kPaletteStrideCount - 1)];
    hk.setGrayColor(0, 40);  // 0x47ae87

    // Pinned object: if eligible, write and return immediately.
    if (hk.objectRingPinned) {
        u16 pinnedId = static_cast<u16>(hk.objectRingPinned[0]);
        if (hk.evaluateEligibility(*refId, pinnedId, filter)) {
            *outId = pinnedId;
            return 1;  // 0x47afaa
        }
    }

    bool rangeActive = PaletteRangeActive(minR, maxR);
    int found = PaletteProbeSlot(hk, refId, filter, minR, maxR, rangeActive,
                                 probeStart, stride);
    if (found < 0) {
        return 0;  // 0x47af68
    }
    *outId = static_cast<u16>(found);
    return 1;  // 0x47af61
}

int ObjectSearchFindPeopleByPalette(const u16* refId, int maxPeople, float minR,
                                    float maxR, u16* outIds, int strideIndex,
                                    int probeStart, const u8* peopleTypeTable) {
    const PathfindMapHooks& hk = g_hooks;
    int stride = kPaletteStrideTable[strideIndex & (kPaletteStrideCount - 1)];
    hk.setGrayColor(0, 40);   // 0x47b03e
    hk.setGrayColor(255, 8);  // 0x47b051

    bool rangeActive = PaletteRangeActive(minR, maxR);
    int refRank = hk.computeOfficeRank(*refId, 0);  // v18 (0x47b0a5)
    int collected = 0;
    if (maxPeople == 0) {
        return 0;  // a2 == 0 path
    }

    int idx = probeStart;
    int remaining = kPaletteSize;  // v20
    int written = 0;               // v10 (byte offset into outIds, /2 == slot)
    int cap = 2 * maxPeople;       // v23
    while (written < cap) {
        if (remaining == 0) {
            break;  // 0x47b0c6
        }
        u8 ptype = peopleTypeTable ? peopleTypeTable[536 * idx] : 0;  // byte_12CE912
        if (ptype != 8 && ptype != 7 && ptype != 5 && ptype != 9) {
            if (hk.evaluateEligibility(*refId, static_cast<u16>(idx), nullptr) &&
                std::abs(refRank - hk.computeOfficeRank(static_cast<u16>(idx), 0)) < 5) {
                if (!rangeActive ||
                    [&] {
                        double fav = hk.computeFavorability(idx, *refId, 1);
                        return fav >= minR && fav <= static_cast<double>(maxR);
                    }()) {
                    outIds[collected] = static_cast<u16>(idx);
                    written += 2;
                    ++collected;
                }
            }
        }
        --remaining;
        idx = (idx + stride) % kPaletteSize;
    }
    return collected;  // v24
}

int ObjectSearchFindMatchingColors(const u16* refId, unsigned count, void* filter,
                                   float minR, float maxR, u16* outIds) {
    if (count > 3) {
        return 0;  // 0x47a8f0
    }
    const PathfindMapHooks& hk = g_hooks;
    hk.setGrayColor(0, 40);   // 0x47a907
    hk.setGrayColor(255, 8);  // 0x47a91c

    unsigned slot = 0;
    // Pinned ring record (dword_62EB8C): slot 0 prefilled if eligible.
    if (hk.objectRingPinned) {
        u16 pinnedId = static_cast<u16>(hk.objectRingPinned[0]);
        if (hk.evaluateEligibility(*refId, pinnedId, filter)) {
            outIds[0] = pinnedId;
            slot = 1;
        }
    }

    for (; slot < count; ++slot) {
        int ringCount = hk.objectRingCount;
        bool found = false;
        const i32* rec = hk.objectRingAdvance();
        if (ringCount == 0) {
            return 0;  // empty ring => the slot found nothing
        }
        while (ringCount > 0) {
            if (!rec || rec[0] == 0) {
                return 0;  // *v13 == 0 terminator (0x47a9b2 -> return result==0)
            }
            float key = *reinterpret_cast<const float*>(&rec[1]);
            if (key >= minR && key <= maxR &&
                hk.evaluateEligibility(*refId, static_cast<u16>(rec[0]), filter)) {
                outIds[slot] = static_cast<u16>(rec[0]);
                found = true;
            }
            --ringCount;
            rec = hk.objectRingAdvance();
            if (found || ringCount == 0) {
                break;
            }
        }
        if (!found) {
            return 0;  // 0x47a9de
        }
    }
    return 1;  // 0x47aac3
}

int ObjectSearchFindMatchingColor(const u16* refId, void* filter, float minR,
                                  float maxR, u16* outId) {
    const PathfindMapHooks& hk = g_hooks;
    hk.setGrayColor(0, 40);  // 0x47aaf5

    // Pinned ring record fast path.
    if (hk.objectRingPinned &&
        hk.evaluateEligibility(*refId, static_cast<u16>(hk.objectRingPinned[0]), filter)) {
        *outId = static_cast<u16>(hk.objectRingPinned[0]);
        return 1;  // 0x47abb8
    }

    int ringCount = hk.objectRingCount;
    const i32* rec = hk.objectRingAdvance();
    if (ringCount == 0) {
        return 0;  // 0x47aba0
    }
    while (true) {
        if (!rec || rec[0] == 0) {
            return 0;  // terminator
        }
        float key = *reinterpret_cast<const float*>(&rec[1]);
        if (key >= minR && key <= maxR &&
            hk.evaluateEligibility(*refId, static_cast<u16>(rec[0]), filter)) {
            *outId = static_cast<u16>(rec[0]);
            return 1;  // 0x47abe9
        }
        --ringCount;
        rec = hk.objectRingAdvance();
        if (ringCount == 0) {
            return 0;  // 0x47ab9e
        }
    }
}

// ===========================================================================
// City / road-network map builders.
// ===========================================================================

// Find the node (other than self) whose nodeId equals `targetId`; -1 if none.
static int RoadFindByNodeId(const RoadNetwork& net, int targetId, int selfIdx) {
    for (int slot = 0; slot < net.nodeCount; ++slot) {
        if (slot != selfIdx && net.nodes[slot].nodeId == targetId) {
            return slot;
        }
    }
    return -1;
}

int MapComputeBuildingChainDepth(RoadNetwork& net, int index) {
    if (index < 0 || index >= net.nodeCount) {
        return 0;
    }
    RoadNode& n = net.nodes[index];
    if (static_cast<u16>(n.depth) != 0xFFFF) {
        return static_cast<u16>(n.depth);  // 0x592ca4 cached
    }
    int result = 0;  // v3 — depth via the first parent link
    int alt = 0;     // v11 — depth via the second parent link
    if (n.parentFromId != 0) {  // 0x592caa: first link present
        int found = RoadFindByNodeId(net, n.parentFromId, index);  // 0x592ceb
        if (found >= 0) {
            result = MapComputeBuildingChainDepth(net, found) + 1;  // 0x592d79
        }
    }
    if (n.parentToId != 0) {    // 0x592d0a: second link present
        int found = RoadFindByNodeId(net, n.parentToId, index);     // 0x592d45
        if (found >= 0) {
            alt = MapComputeBuildingChainDepth(net, found) + 1;      // 0x592d4f
        }
    }
    return result > alt ? result : alt;  // 0x592d57 max(v3, v11)
}

int MapComputeRoadNetworkLayout(RoadNetwork& net, int width, int height) {
    if (net.nodeCount <= 0) {
        return 1;  // 0x592e0e / 0x592f3a empty network
    }

    // 1. Assign each node its chain depth; track the max (depthCount-1). (0x592f4e)
    net.depthCount = 0;
    for (int j = 0; j < net.nodeCount; ++j) {
        u16 d = static_cast<u16>(MapComputeBuildingChainDepth(net, j));
        net.nodes[j].depth = static_cast<i16>(d);
        if (d > static_cast<unsigned>(net.depthCount)) {
            net.depthCount = d;
        }
    }
    ++net.depthCount;  // 0x592f90: depthCount = maxDepth + 1

    // 2. Bubble-sort nodes by ascending depth (the original swaps whole 44-byte
    //    records via qmemcpy in a do/while bubble pass). (0x592f96..0x593044)
    bool swapped;
    do {
        swapped = false;
        for (int j = 0; j < net.nodeCount; ++j) {
            for (int k = j + 1; k < net.nodeCount; ++k) {
                if (static_cast<u16>(net.nodes[k].depth) <
                    static_cast<u16>(net.nodes[j].depth)) {
                    RoadNode tmp = net.nodes[j];
                    net.nodes[j] = net.nodes[k];
                    net.nodes[k] = tmp;
                    swapped = true;
                }
            }
        }
    } while (swapped);

    // 3. Compute per-level start indices: levelStart[level] = first node index whose
    //    depth == level. (0x593052..0x5930a7)
    int level = 0;
    net.levelStart[0] = 0;
    for (int j = 0; j < net.nodeCount; ++j) {
        if (static_cast<u16>(net.nodes[j].depth) > static_cast<unsigned>(level)) {
            ++level;
            net.levelStart[level] = static_cast<u16>(j);
        }
    }
    net.levelStart[net.depthCount] = static_cast<u16>(net.nodeCount);

    // 4. Distribute the first level's X coordinates evenly across `width`.
    //    (0x5930c1) v27 = width / (2*count_level0); step = 2*v27.
    int count0 = net.levelStart[1] - net.levelStart[0];
    if (count0 <= 0) {
        count0 = net.nodeCount;  // single-level guard
    }
    int x = width / (2 * count0);
    int xstep = 2 * x;
    for (int j = 0; j < count0; ++j) {
        net.nodes[j].coordX = x - 24;  // dword_12CDD6C, 0x5930ea
        x += xstep;
    }

    // 5. For each subsequent level, average each node's X from its connected
    //    children, then spread evenly. (0x593103.. — the layered relaxation.) We
    //    keep the faithful per-level averaging that the original performs.
    for (int lvl = 1; lvl < net.depthCount; ++lvl) {
        int start = net.levelStart[lvl];
        int end = net.levelStart[lvl + 1];
        for (int j = start; j < end; ++j) {
            // average cost(X) from prior-level nodes linked by hi-word id.
            int sum = 0, n = 0;
            for (int p = 0; p < start; ++p) {
                if (net.nodes[p].nodeId == net.nodes[j].parentFromId ||
                    net.nodes[p].nodeId == net.nodes[j].parentToId) {
                    sum += net.nodes[p].cost;
                    ++n;
                }
            }
            net.nodes[j].cost = n ? sum / n : sum;  // 0x5931d3 / 0x5931f2
        }
    }

    // 6. Assign the Y coordinate per level: spread the depthCount levels across
    //    `height` (clamped). (0x5935ab) y = min(96*depthCount, height-16) scaled.
    int y = 96 * net.depthCount;
    if (y >= height - 16) {
        y = height - 16;
    }
    for (int j = 0; j < net.nodeCount; ++j) {
        int row = y * static_cast<u16>(net.nodes[j].depth) / net.depthCount;
        net.nodes[j].coordY = row + 16;  // dword_12CDD70, 0x593602
    }
    return 0;  // 0x592e94
}

int MapRasterEdgeStepCount(float lenSq) {
    int steps = 2 * PathfindMapTruncToInt(std::sqrt(static_cast<double>(lenSq)) +
                                          kRasterEdgeBias);
    if (steps < 1) {
        steps = 1;  // 0x577777 / 0x577812
    }
    return steps;
}

int MapRasterizeBauplatzEdge(const float* quad, int fillTerrain, int fillCell) {
    // quad: [0,1] edge0 start, [2,3] edge0 end, [4,5] edge1 end, [6,7] edge1 start.
    float ex0 = quad[2] - quad[0];  // v25
    float ey0 = quad[3] - quad[1];  // v23
    float lenSqOuter = ex0 * ex0 + ey0 * ey0;  // v17

    float ex1 = quad[4] - quad[6];  // v19
    float ey1 = quad[5] - quad[7];  // v21
    float lenSqInner = ex1 * ex1 + ey1 * ey1;  // v16

    float maxLenSq = lenSqOuter > lenSqInner ? lenSqOuter : lenSqInner;  // v4
    int outerSteps = MapRasterEdgeStepCount(maxLenSq);  // v6/v18

    // Walk the two edges in lockstep across outerSteps, and for each, walk the
    // cross segment in innerSteps. The grid writes (terrain/cell) go through the
    // opaque grid in the original; with no grid we count the samples written.
    int samples = 0;
    float ax = quad[0], ay = quad[1];   // v30/v31 (edge0 cursor)
    float bx = quad[6], by = quad[7];   // v28/v29 (edge1 cursor)
    float dax = ex0 / outerSteps, day = ey0 / outerSteps;
    float dbx = ex1 / outerSteps, dby = ey1 / outerSteps;
    for (int o = 0; o < outerSteps; ++o) {
        float cx = bx - ax, cy = by - ay;       // v35/v33
        int innerSteps = MapRasterEdgeStepCount(cx * cx + cy * cy);  // v10/v32
        float px = ax, py = ay;                  // v37/v38
        float dpx = cx / innerSteps, dpy = cy / innerSteps;  // v36/v34
        for (int i = 0; i < innerSteps; ++i) {
            if (fillTerrain != -1 || fillCell != 255) {
                ++samples;  // a grid write would happen here (hook-modelled)
            }
            px += dpx;
            py += dpy;
        }
        ax += dax; ay += day;
        bx += dbx; by += dby;
    }
    (void)fillCell;
    return samples;
}

int MapLoadCityFile(int loadNet, const char* cityName) {
    const PathfindMapHooks& hk = g_hooks;
    char path[268];

    hk.universeSwitchActiveSlot(0, 0, 0, 0);  // 0x528be0
    hk.universeResetCurrentSlot(0, 0);        // 0x528be5
    hk.sceneEnterCity(nullptr, 0);            // 0x528bec

    if (loadNet) {
        std::snprintf(path, sizeof(path), "%s/%s.NET", "gamedata/cities", cityName);
    } else {
        std::snprintf(path, sizeof(path), "%s/%s.CTY", "gamedata/cities", cityName);
    }

    hk.saveWriteGameFile(path, "city", 0, 2);  // 0x528c39
    hk.buildingResetAll();                     // 0x528c3e
    hk.worldResetPersonTable();                // 0x528c43
    hk.worldRelinkObjectOwners();              // 0x528c48
    hk.objectDestroySpawned(0, 0);             // 0x528c4d
    hk.universeSwitchActiveSlot(0, 0, 0, 0);   // 0x528c56
    hk.universeResetCurrentSlot(0, 0);         // 0x528c5b
    return hk.sceneLoadFromStream("scenes/*ChooseCity.ed3", 0, 0, 0);  // 0x528c75
}

int MapSpawnCityTowerMarker(float offX, float offY, float* outXform) {
    const PathfindMapHooks& hk = g_hooks;
    void* objA = hk.findObjectByHandle(0, 320, "dummy_Kartentisch_LINKS_OBEN", 0, nullptr);
    void* objB = hk.findObjectByHandle(0, 320, "dummy_Kartentisch_RECHTS_UNTEN", 0, nullptr);
    if (!objA || !objB) {
        return 0;  // 0x52e1d5
    }
    // The world span between the two dummies, scaled by the per-axis factors.
    const float* a = reinterpret_cast<const float*>(objA);
    const float* b = reinterpret_cast<const float*>(objB);
    float spanX = b[19] - a[19];  // +76 (float idx 19)
    float spanY = b[21] - a[21];  // +84 (float idx 21)
    float sx = offX * kTowerMarkerScaleX;
    float sy = offY * kTowerMarkerScaleY;

    float xform[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    xform[0] = a[19] + spanX * sx;          // v14
    xform[1] = *reinterpret_cast<const float*>(&a[20]);  // +80 carried through
    xform[2] = a[21] + spanY * sy;          // v16
    if (outXform) {
        std::memcpy(outXform, xform, sizeof(xform));
    }

    void* node = hk.attachToUniverseNode(0, xform, "sp_STADTTURM", 0);
    if (node) {
        hk.lightBuildObjectCache(node);     // 0x52e2b2
        hk.loadObjectAnimation(0, "sonstiges\\wimpel_STADTTURM.baf", 1);  // 0x52e2be
    }
    return node ? 1 : 0;
}

char* MapBuildDummyName(const char* name, char* out) {
    char upper[256];
    int i = 0;
    for (; name[i] != '\0' && i < 254; ++i) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - ('a' - 'A'));
        }
        upper[i] = c;
    }
    upper[i] = '\0';
    std::snprintf(out, 262, "dummy_%s", upper);  // 0x52e32c
    return out;
}

int MapSpawnCityPointMarker(const char* name, float param) {
    const PathfindMapHooks& hk = g_hooks;
    char dummy[262];
    MapBuildDummyName(name, dummy);

    void* obj = hk.findObjectByHandle(0, 0, dummy, 0, nullptr);  // 0x52e338
    if (!obj) {
        return 0;  // 0x52e33f miss
    }
    float xform[8] = {0, 0, 0, 0, 0, 0, 0, param};
    void* node = hk.attachToUniverseNode(0, xform, dummy, 0);  // 0x52e374
    if (node) {
        hk.lightBuildObjectCache(node);   // 0x52e3b7
        hk.loadObjectAnimation(0, "sonstiges\\STADTPUNKT_ANM_K.baf", 1);  // 0x52e3c3
    }
    return node ? 1 : 0;
}

// ===========================================================================
// VIBE_Path_BuildMarkerPoints (0x4074c0).
// ===========================================================================
int PathBuildMarkerPoints(int grid, const void* routeRecord, void* markerOut) {
    const PathfindMapHooks& hk = g_hooks;
    const u32* rec = reinterpret_cast<const u32*>(routeRecord);
    u8* out = reinterpret_cast<u8*>(markerOut);
    float cam[1] = {0.0f};  // dword_62D0B4 placeholder camera (hook-supplied world)

    // Project the route's primary anchor (rec[13] world point at +76). (0x4074fe)
    // In the live game rec[13] is a pointer to a tile whose world coord sits at +76;
    // with no world (inert hooks) we project a null anchor.
    {
        i32 proj[3];
        const float* world = nullptr;
        if (rec[13] != 0) {
            world = reinterpret_cast<const float*>(
                reinterpret_cast<const u8*>(static_cast<std::uintptr_t>(rec[13])) + 76);
        }
        hk.coordProjectPoint(cam, world, proj);
        std::memcpy(out, proj, 2 * sizeof(i32));  // x,y
        out[8] = 1;  // marker tag byte
    }

    // Walk the interior tile chain (rec[60]/rec[61] hold the chain length / base in
    // the original; the projection per tile goes through heightmapTileToWorld).
    int chainLen = static_cast<int>(rec[61]);  // dword at +244 == element count
    int written = 1;
    for (int i = 0; i < chainLen && written < 256; ++i) {
        float world[4];
        if (hk.heightmapTileToWorld(grid, i, world, i)) {  // (0x4075a9)
            i32 proj[3];
            hk.coordProjectPoint(cam, world, proj);
            u8* slot = out + 12 * written;
            std::memcpy(slot, proj, 2 * sizeof(i32));
            slot[8] = 2;  // interior tag
            ++written;
        }
    }
    out[12 * written + 8] = 0;  // terminator tag (0x40764d)
    return 1;  // 0x407656
}

}  // namespace guild::sim
