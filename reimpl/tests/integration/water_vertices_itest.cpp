// Integration tests: drive the water-animation driver against its REAL sibling
// modules (render/floorwater AnimateWaterWaveGrid + FindRegionOffset, render/
// water_anim WaterTextureFrameIndex speed table) and render/terrain_mesh tile
// math — no mocks for the math leaves, only the texture-bank callback is injected.
#include "test.h"

#include "render/water_vertices.h"
#include "render/water_anim.h"     // kWaterAnimSpeedTable, WaterTextureFrameIndex
#include "render/floorwater.h"     // AnimateWaterWaveGrid, FindRegionOffset
#include "render/terrain_mesh.h"   // SummarizeTileElevations (real sibling)

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;

namespace {

constexpr double kTwoPi = 6.283185307179586;

bool nearF(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

// A real-ish texture bank: groups of frames, returns a stable member id.
struct Bank {
    u32 lookups = 0;
    i32 lastGroup = -1;
    u8  lastFrame = 0;
};
u32 BankFind(i32 groupId, u8 frameByte, void* ctx) {
    Bank* b = static_cast<Bank*>(ctx);
    b->lookups++;
    b->lastGroup = groupId;
    b->lastFrame = frameByte;
    return (u32)(1000 + groupId * 10 + frameByte);
}

} // namespace

// ---------------------------------------------------------------------------
// Driver vs the REAL speed table: the texture frame the driver selects must match
// WaterTextureFrameIndex computed directly from render/water_anim's table.
// ---------------------------------------------------------------------------
TEST(WaterVerticesITest, DriverTextureMatchesSpeedTable) {
    Bank bank;
    render::WaterMesh m;
    std::memset(&m, 0, sizeof(m));
    m.hasTexture = true;
    m.groupId = 3;
    m.texMemberCount = 6;
    m.texSpeedNibble = 5;      // table[5] == 8
    m.lastTime = 0;

    i32 time = 50;
    render::AnimateWaterVertices(&m, 1, time, BankFind, &bank);

    i32 expectFrame = render::WaterTextureFrameIndex((u32)time, 5, 6); // (50/8)%6
    CHECK_EQ(bank.lastFrame, (u8)expectFrame);
    CHECK_EQ(bank.lastGroup, 3);
    CHECK_EQ(m.activeMember, (u32)(1000 + 3 * 10 + expectFrame));
    CHECK_EQ(render::kWaterAnimSpeedTable[5], (u32)8);
}

// ---------------------------------------------------------------------------
// Driver's wave grid must equal a direct AnimateWaterWaveGrid call on the
// PROPAGATED phases — i.e. the driver wires the real sibling correctly.
// ---------------------------------------------------------------------------
TEST(WaterVerticesITest, DriverGridEqualsSiblingOnPropagatedPhase) {
    render::WaterMesh m;
    std::memset(&m, 0, sizeof(m));
    m.hasTexture = false;
    m.lastTime = 0;
    for (int k = 0; k < 4; ++k) {
        m.waveSpeed[k] = 0.25f * (k + 1);
        m.amp[k] = 0.5f + 0.5f * k;
        m.phase[k] = 0.05f * (k + 1);
    }

    // Reproduce the propagation the driver applies (phase[0..2]<-prop[1..3]).
    float prop[4];
    render::PropagatePhases(m.waveSpeed, m.phase, 7.0, prop);
    float expectPhase[4] = {prop[1], prop[2], prop[3], m.phase[3]};
    float expectGrid[64];
    render::AnimateWaterWaveGrid(expectGrid, m.amp, expectPhase, kTwoPi);

    render::AnimateWaterVertices(&m, 1, 7, BankFind, nullptr);

    for (int i = 0; i < 64; ++i)
        CHECK(nearF(m.waveOut[i], expectGrid[i]));
    for (int k = 0; k < 3; ++k)
        CHECK(nearF(m.phase[k], expectPhase[k]));
    CHECK(nearF(m.phase[3], expectPhase[3]));
}

// ---------------------------------------------------------------------------
// Multiple meshes in one array, mixed dt: independent advance per record.
// ---------------------------------------------------------------------------
TEST(WaterVerticesITest, MultiMeshIndependentAdvance) {
    Bank bank;
    std::vector<render::WaterMesh> meshes(3);
    std::memset(meshes.data(), 0, meshes.size() * sizeof(render::WaterMesh));
    for (int i = 0; i < 3; ++i) {
        meshes[i].hasTexture = true;
        meshes[i].groupId = i;
        meshes[i].texMemberCount = 4;
        meshes[i].texSpeedNibble = 1;
        meshes[i].amp[0] = 1.0f;
        meshes[i].waveSpeed[0] = 1.0f;
    }
    meshes[0].lastTime = 0;   // dt = 20 -> animates
    meshes[1].lastTime = 20;  // dt = 0  -> texture only
    meshes[2].lastTime = 5;   // dt = 15 -> animates

    render::AnimateWaterVertices(meshes.data(), 3, 20, BankFind, &bank);

    CHECK_EQ(meshes[0].lastTime, 20);     // advanced
    CHECK_EQ(meshes[1].lastTime, 20);     // unchanged (dt==0)
    CHECK_EQ(meshes[2].lastTime, 20);     // advanced
    CHECK_EQ(bank.lookups, (u32)3);       // texture advanced on all three
}

// ---------------------------------------------------------------------------
// FindRegionOffset feeds an 80-byte-strided water buffer that a terrain pass would
// index; cross-check the offset is cell-aligned and inside the buffer the real
// terrain_mesh summary would also walk. (Uses real FindRegionOffset.)
// ---------------------------------------------------------------------------
TEST(WaterVerticesITest, RegionOffsetCellAlignedForTerrainBuffer) {
    render::WaterRegionSpan spans[2];
    std::memset(spans, 0, sizeof(spans));
    spans[0].base = 0; spans[0].type = 1; spans[0].lo = 0; spans[0].hi = 7;
    spans[0].marker = 0xFF;
    spans[1].type = -1;   // terminator

    const i32 bufBase = 4096;
    for (i32 q = 0; q <= 7; ++q) {
        i32 off = render::FindRegionOffset(spans, 2, bufBase, q, 0x10, 1);
        CHECK_EQ(off, bufBase + 80 * q);
        CHECK_EQ((off - bufBase) % 80, 0);    // 80-byte cell stride
    }
    // Marker stamped once (first hit), survives subsequent hits:
    CHECK_EQ(spans[0].marker, (u8)0x10);

    // The real terrain_mesh elevation summary runs unchanged on its own grid (a
    // sanity wire that the sibling links into the same binary).
    const i32 size = 4, mask = 3, span = 1;
    u8 heights[16];
    for (int i = 0; i < 16; ++i) heights[i] = (u8)(i * 4);
    render::TileElevationSummary sum;
    render::SummarizeTileElevations(heights, nullptr, size, mask, span, &sum);
    CHECK(sum.maxHeight[0] >= sum.minHeight[0]);
}
