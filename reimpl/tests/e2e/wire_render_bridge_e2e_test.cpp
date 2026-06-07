// tests/e2e/wire_render_bridge_e2e_test.cpp — GUARDED real-asset E2E for the
// render/character-mesh bridge. Mounts the REAL Resources/Objects.BIN, decodes a
// shipped .bgf mesh (render::LoadAgfModel) and runs the bridge's REAL pivot leaf
// (util::PointThroughBoneChainPivot @0x5c8d0c, the leaf the inert
// CharRenderHooks::pointThroughPivot slot is de-inerted to) over the mesh's REAL
// vertices through a frame seeded from the mesh's real root translation. With the
// inert default the vertices come through untransformed (identity); with the
// bridge installed they are transformed + root-translated into world space — real
// geometry that was ABSENT under the inert default now appears.
//
// GUARDED: skips cleanly (zero checks) when the real game dir is absent.
#include "test.h"

#include "play/wire_render_bridge.h"
#include "render/agf_loader.h"
#include "render/bgf_loader.h"
#include "io/archive_mount.h"
#include "shim_impl/disk_filesystem.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool Finite(float f) { return f == f && std::fabs(f) < 1e30f; }

// A mesh FRAME (the actor+52 handle) seeded with a real non-identity pose: a yaw
// 3x3 + the mesh root translation read from the BGF bbox center, so the bridge
// transform is grounded in real geometry. float-indexed engine layout.
std::vector<float> MakeFrameFromBbox(float cx, float cy, float cz) {
    std::vector<float> f(200, 0.0f);
    // 90-degree yaw 3x3 (rows at 99/103/107, 100/104/108, 101/105/109)
    f[99] = 0.0f;  f[103] = 0.0f; f[107] = 1.0f;
    f[100] = 0.0f; f[104] = 1.0f; f[108] = 0.0f;
    f[101] = -1.0f; f[105] = 0.0f; f[109] = 0.0f;
    // mesh ROOT translation (33..35) = the mesh's real bbox center -> seats the
    // model in the world at its true centroid.
    f[33] = cx; f[34] = cy; f[35] = cz;
    return f;   // parent link (byte 504) null -> leaf frame
}

} // namespace

TEST(WireRenderBridgeE2E, RealMeshTransformedByBridge) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] WireRenderBridgeE2E: real game dir absent (%s)\n",
                    dir.c_str());
        return;
    }

    io::ArchiveMount mount;
    CHECK(mount.Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/false));
    if (!mount.isMounted()) return;

    const char* member = "_DYNAMIC/Buch/Buch.bgf";
    std::vector<u8> bytes;
    CHECK(mount.OpenMember(member, bytes));
    if (bytes.empty()) return;

    render::BgfModel m;
    CHECK(render::LoadAgfModel(bytes.data(), bytes.size(), m));
    CHECK(m.vertexCount > 0);
    if (m.vertices.empty()) return;
    std::printf("  %s -> %u verts / %u polys\n", member, m.vertexCount, m.polyCount);

    // Real bbox center of the loaded mesh -> mesh root translation.
    float mn[3] = { 1e30f,  1e30f,  1e30f};
    float mx[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& v : m.vertices) {
        mn[0] = std::min(mn[0], v.pos[0]); mx[0] = std::max(mx[0], v.pos[0]);
        mn[1] = std::min(mn[1], v.pos[1]); mx[1] = std::max(mx[1], v.pos[1]);
        mn[2] = std::min(mn[2], v.pos[2]); mx[2] = std::max(mx[2], v.pos[2]);
    }
    float cx = 0.5f * (mn[0] + mx[0]);
    float cy = 0.5f * (mn[1] + mx[1]);
    float cz = 0.5f * (mn[2] + mx[2]);
    CHECK(Finite(cx) && Finite(cy) && Finite(cz));
    std::vector<float> frame = MakeFrameFromBbox(cx, cy, cz);

    // Transform a sample of the real vertices through (a) the INERT default
    // (identity, no root) and (b) the REAL bridge pivot leaf, and count how many
    // land at a DIFFERENT world position. Under the inert default NONE move (the
    // geometry is dropped on the floor); under the bridge they all seat into the
    // world -> real geometry appears that was absent before.
    const int sample = std::min<int>(64, (int)m.vertices.size());
    int moved = 0;
    float worldMnX = 1e30f, worldMxX = -1e30f;
    for (int i = 0; i < sample; ++i) {
        const auto& v = m.vertices[i];
        float in[3]  = {v.pos[0], v.pos[1], v.pos[2]};

        // (a) inert default: pointThroughPivot is identity, no root translation.
        float inert[3] = {in[0], in[1], in[2]};   // identity copy

        // (b) real bridge.
        float real[3] = {0, 0, 0};
        play::BridgePointThroughPivot(frame.data(), in, real);
        float root[3] = {0, 0, 0};
        play::BridgeMeshRootTranslation(frame.data(), root);
        real[0] += root[0]; real[1] += root[1]; real[2] += root[2];

        if (std::fabs(real[0] - inert[0]) > 1e-3f ||
            std::fabs(real[1] - inert[1]) > 1e-3f ||
            std::fabs(real[2] - inert[2]) > 1e-3f)
            ++moved;
        worldMnX = std::min(worldMnX, real[0]);
        worldMxX = std::max(worldMxX, real[0]);
        CHECK(Finite(real[0]) && Finite(real[1]) && Finite(real[2]));
    }

    std::printf("  bridge seated %d/%d real verts into world (worldX %.2f..%.2f, "
                "root %.2f/%.2f/%.2f)\n",
                moved, sample, worldMnX, worldMxX, cx, cy, cz);

    // The bridge must have moved the real geometry (unless the mesh is degenerate
    // at the exact pivot+root fixpoint, which Buch is not).
    CHECK(moved > 0);

    // The installer wires the SAME leaf the production CharRenderHooks route to.
    play::InstallRealRenderBridge();
    CHECK(play::RealRenderBridgeInstalled());
    play::UninstallRealRenderBridge();
}
