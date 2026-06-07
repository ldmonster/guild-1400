// tests/e2e/agf_postprocess_e2e_test.cpp — GUARDED real-asset AGF post-process.
//
// Extracts the REAL Buch.bgf from Resources/Objects.BIN (DEFLATE via ArchiveMount),
// decodes the shipped AGF token-script (render::LoadAgfModel), then runs the
// reconstructed post-process stages (render::PostProcessModel) and asserts the
// vertex + material counts DROP vs the raw parse, indices stay in range, and the
// bbox stays finite. Reports the real before/after counts for Buch.bgf.
//
// GUARDED: skips cleanly (zero checks) if the real game dir is absent. Override
// with GUILD_GAME_DIR.
#include "test.h"

#include "render/agf_loader.h"
#include "render/agf_postprocess.h"
#include "render/bgf_loader.h"
#include "io/archive_mount.h"

#include "shim_impl/disk_filesystem.h"

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

} // namespace

TEST(AgfPostProcessE2E, BuchBeforeAfter) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] AgfPostProcessE2E.BuchBeforeAfter: real game dir absent (%s)\n",
                    dir.c_str());
        return;
    }

    io::ArchiveMount mount;
    CHECK(mount.Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/false));
    if (!mount.isMounted()) return;

    const char* member = "_DYNAMIC/Buch/Buch.bgf";
    std::vector<u8> bytes;
    CHECK(mount.OpenMember(member, bytes));
    CHECK(!bytes.empty());
    if (bytes.size() < 4) return;
    CHECK(bytes[0] == 'B' && bytes[1] == 'G' && bytes[2] == 'F' && bytes[3] == 0);

    render::BgfModel m;
    CHECK(render::LoadAgfModel(bytes.data(), bytes.size(), m));

    const u32 rawV = m.vertexCount;
    const u32 rawP = m.polyCount;
    const u32 rawM = m.materialCount;
    std::printf("  Buch.bgf RAW  : %u verts / %u polys / %u materials\n", rawV, rawP, rawM);
    CHECK(rawV > 0);
    CHECK(rawP > 0);
    CHECK(rawM > 0);

    render::PostProcessModel(m);

    std::printf("  Buch.bgf POST : %u verts / %u polys / %u materials\n",
                m.vertexCount, m.polyCount, m.materialCount);
    std::printf("  delta: verts -%u, materials -%u\n",
                rawV - m.vertexCount, rawM - m.materialCount);

    // Post-process must not GROW counts; for a real mesh with welded seams and
    // shared/duplicated materials we expect at least one of them to actually drop.
    CHECK(m.vertexCount <= rawV);
    CHECK(m.materialCount <= rawM);
    CHECK(m.polyCount == rawP);                 // poly count is preserved
    CHECK(m.vertexCount < rawV || m.materialCount < rawM);  // something collapsed

    // No out-of-range vertex indices after the remaps.
    int vOob = 0;
    for (const auto& p : m.polygons)
        for (int k = 0; k < 3; ++k)
            if (p.vtx[k] >= m.vertices.size()) ++vOob;
    CHECK_EQ(vOob, 0);

    // Every poly material index in range (or -1).
    int mOob = 0;
    for (const auto& p : m.polygons)
        if (p.matIndex >= (i32)m.materials.size()) ++mOob;
    CHECK_EQ(mOob, 0);

    // Finite bbox after the morph bake.
    render::BgfBounds bb = render::ComputeBoundingExtents(m);
    for (int k = 0; k < 3; ++k) { CHECK(Finite(bb.min[k])); CHECK(Finite(bb.max[k])); }
    CHECK(Finite(bb.radius));
    CHECK(bb.radius > 0.0f);
    std::printf("  POST bbox x[%.3f,%.3f] y[%.3f,%.3f] z[%.3f,%.3f] r=%.3f\n",
                bb.min[0], bb.max[0], bb.min[1], bb.max[1], bb.min[2], bb.max[2], bb.radius);
}
