// Golden tests for VIBE_Coord_WorldToTile (coord_worldtile_misc_recon).
#include "tests/framework/test.h"
#include "util/coord_worldtile_misc_recon.h"

using namespace guild;
using guild::util::Coord_WorldToTile;
using guild::util::CoordMapRecord;

namespace {
// Identity-ish transform that copies worldPt into out (the pivot just passes
// through here so the projection math is exercised deterministically).
void PassThrough(const float* /*pivot*/, const float* world, float* out3) {
    out3[0] = world[0];
    out3[1] = world[1];
    out3[2] = world[2];
}
} // namespace

TEST(MiscReconCoordTile, ProjectsXandZByMapOriginAndScale) {
    CoordMapRecord map{};
    map.originX = 100.0f; // +0x90
    map.originZ = 50.0f;  // +0x98
    map.scaleU = 10.0f;   // +0xA0
    map.scaleV = 5.0f;    // +0xB8
    float world[3] = {130.0f, 0.0f, 65.0f};
    float pivot[1] = {0.0f};
    float u = -1, v = -1;
    Coord_WorldToTile(&u, &v, world, pivot, map, PassThrough);
    // u = (130 - 100) / 10 = 3 ; v = (65 - 50) / 5 = 3
    CHECK_EQ(u, 3.0f);
    CHECK_EQ(v, 3.0f);
}

TEST(MiscReconCoordTile, UsesXandZNotY) {
    CoordMapRecord map{};
    map.originX = 0.0f;
    map.originZ = 0.0f;
    map.scaleU = 2.0f;
    map.scaleV = 4.0f;
    float world[3] = {8.0f, 999.0f, 16.0f}; // Y must be ignored
    float pivot[1] = {0.0f};
    float u = 0, v = 0;
    Coord_WorldToTile(&u, &v, world, pivot, map, PassThrough);
    CHECK_EQ(u, 4.0f);  // 8/2
    CHECK_EQ(v, 4.0f);  // 16/4
}
