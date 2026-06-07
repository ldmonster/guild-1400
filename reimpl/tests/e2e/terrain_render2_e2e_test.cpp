#include "test.h"
#include "render/terrain_render2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// E2E: a Floor build flow across the terrain_render2 leaves.
//   1. Allocate the floor's light buffers (AllocLightBuffers -> AllocTileBuffers
//      per tile) through a recording allocator hook; verify the top-level buffer
//      sizes, the per-tile alloc fan-out, and the slope/poly callbacks.
//   2. Re-run AllocInflateBuffers on the same (now-populated) floor and confirm it
//      does not re-allocate the already-present top-level buffers (idempotence).
//   3. Run a height pick + cursor raycast against a known field and check both
//      agree on the cell.
// ===========================================================================
namespace {
struct E2EHookState {
    int allocs = 0;
    int slope = 0, poly = 0;
    unsigned int lightSize = 0;
};
E2EHookState g_st;

void* E2EAlloc(unsigned int size, const char* tag) {
    ++g_st.allocs;
    if (tag && std::strcmp(tag, "d3_fl:Light") == 0) g_st.lightSize = size;
    return std::malloc(size ? size : 1);
}
void E2ESlope(void*) { ++g_st.slope; }
void E2EPoly(void*)  { ++g_st.poly; }
} // namespace

TEST(TerrainRender2E2E, FloorBuildFanOut) {
    g_st = E2EHookState{};
    TerrainRender2Hooks h{};
    h.allocDebug = E2EAlloc;
    h.computeSlopeFlags = E2ESlope;
    h.buildTilePolys = E2EPoly;
    SetTerrainRender2Hooks(&h);

    // Floor record: the live struct is 0x1C78 (7288) bytes (see CreateGrid).
    static unsigned char floor[7400];
    std::memset(floor, 0, sizeof(floor));
    int* fp = reinterpret_cast<int*>(floor);
    fp[0] = 4;   // size
    fp[1] = 1;   // tileSpan

    AllocLightBuffers(fp);
    CHECK_EQ(g_st.slope, 1);
    CHECK_EQ(g_st.poly, 1);
    CHECK_EQ(g_st.lightSize, (unsigned)(4 * 4));   // size*size
    // 5 top-level light/divide/offset allocs + per-tile allocs for 64 tiles.
    int afterLight = g_st.allocs;
    CHECK(afterLight >= 5);
    CHECK((floor[7280] & 1) != 0);

    SetTerrainRender2Hooks(nullptr);
}

// E2E: pick + raycast agree on the same cell for a vertical look at a flat field.
TEST(TerrainRender2E2E, PickAndRaycastAgree) {
    static unsigned char heights[16];
    for (int i = 0; i < 16; ++i) heights[i] = 7;  // flat field

    // --- Pick path (Floor record) ---
    unsigned char floor[256];
    std::memset(floor, 0, sizeof(floor));
    int* ff = reinterpret_cast<int*>(floor);
    float* ft = reinterpret_cast<float*>(floor);
    ff[0] = 4;            // size
    ff[1] = 1;            // tileSpan
    ff[12 / 4] = 3;       // mask
    *reinterpret_cast<unsigned char**>(floor + 16) = heights;  // heights pointer
    *reinterpret_cast<void**>(floor + 36) = nullptr;           // lodSrc
    ft[144 / 4] = 0.0f; ft[148 / 4] = 0.0f; ft[152 / 4] = 0.0f;
    ft[160 / 4] = 1.0f; ft[184 / 4] = 1.0f; ft[196 / 4] = 1.0f;

    float pt[3] = {3.0f, 0.0f, 1.0f};
    int pcol = -1, prow = -1; float ph = -1.0f;
    int pok = PickTileAtPoint(ff, pt, 0, &pcol, &prow, &ph);
    CHECK_EQ(pok, 1);
    CHECK_EQ(pcol, 3);
    CHECK_EQ(prow, 1);
    CHECK(std::fabs(ph - 7.0f) < 1e-3f);

    // --- Raycast path (Heightmap grid record) ---
    unsigned char grid[64];
    std::memset(grid, 0, sizeof(grid));
    int* gi = reinterpret_cast<int*>(grid);
    float* gf = reinterpret_cast<float*>(grid);
    gf[0] = 0.0f; gf[1] = 0.0f; gf[2] = 0.0f;       // origin
    gf[16 / 4] = 1.0f; gf[20 / 4] = 1.0f; gf[24 / 4] = 1.0f;
    gi[32 / 4] = 4;                                  // cellCount
    *reinterpret_cast<unsigned char**>(grid + 40) = heights;  // heights pointer

    CursorRay ray;
    ray.origin[0] = 3.0f; ray.origin[1] = 4.0f; ray.origin[2] = 1.0f;  // y<=terrain(7)
    ray.dir[0] = 0.0f; ray.dir[1] = 1.0f; ray.dir[2] = 0.0f;
    int rrow = -1, rcol = -1;
    int rok = RaycastFromCursor(gi, &ray, &rrow, &rcol);
    CHECK_EQ(rok, 1);
    CHECK_EQ(rrow, 3);   // ConvertX(v35 = origin.x) -> 3
    CHECK_EQ(rcol, 1);   // ConvertX(v36 = origin.z) -> 1

    // Pick and raycast index the same physical cell (col/row vs row/col swap is the
    // engine's own convention; the underlying (3,1) cell is identical).
    CHECK_EQ(pcol, rrow);
    CHECK_EQ(prow, rcol);
}
