// tests/e2e/agf_real_mesh_e2e_test.cpp — GUARDED real-asset AGF mesh decode.
//
// Mounts the REAL Resources/Objects.BIN, opens a known .bgf member, decompresses
// (DEFLATE via ArchiveMount), decodes the shipped AGF token-script format
// (render::LoadAgfModel), and asserts sane geometry (verts/polys/materials > 0,
// finite bbox, in-range poly indices). Also resolves through play::RealMeshSource
// and asserts a MeshGeometry comes back.
//
// GUARDED: the real game directory is not in the repo. If absent the test records
// ZERO checks and returns (clean skip). Override with GUILD_GAME_DIR.
#include "test.h"

#include "play/real_mesh_source.h"
#include "render/agf_loader.h"
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

bool AssetsPresent(shim::IFileSystem& fs) {
    return fs.exists("Resources/Objects.BIN");
}

bool Finite(float f) { return f == f && std::fabs(f) < 1e30f; }

} // namespace

TEST(AgfRealMesh, DecodeBuchBgf) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!AssetsPresent(fs)) {
        std::printf("  [skip] AgfRealMesh.DecodeBuchBgf: real game dir absent (%s)\n",
                    dir.c_str());
        return;
    }

    io::ArchiveMount mount;
    CHECK(mount.Mount(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/false));
    if (!mount.isMounted()) return;
    std::printf("  Objects.BIN members: %zu\n", mount.memberCount());

    const char* member = "_DYNAMIC/Buch/Buch.bgf";
    std::vector<u8> bytes;
    CHECK(mount.OpenMember(member, bytes));
    CHECK(!bytes.empty());
    if (bytes.size() < 4) return;
    // Confirm the shipped AGF magic + that the fast-chunk magic is absent.
    CHECK(bytes[0] == 'B' && bytes[1] == 'G' && bytes[2] == 'F' && bytes[3] == 0);

    render::BgfModel m;
    CHECK(render::LoadAgfModel(bytes.data(), bytes.size(), m));

    std::printf("  Buch.bgf -> %u verts / %u polys / %u materials / %u dummies\n",
                m.vertexCount, m.polyCount, m.materialCount, m.dummyCount);

    CHECK(m.vertexCount > 0);
    CHECK(m.polyCount > 0);
    CHECK(m.materialCount > 0);

    // No polygon may reference a vertex outside the array.
    int oob = 0;
    for (const auto& p : m.polygons)
        if (p.vtx[0] >= m.vertices.size() || p.vtx[1] >= m.vertices.size() ||
            p.vtx[2] >= m.vertices.size())
            ++oob;
    CHECK_EQ(oob, 0);

    // Finite bounding box.
    render::BgfBounds b = render::ComputeBoundingExtents(m);
    for (int k = 0; k < 3; ++k) { CHECK(Finite(b.min[k])); CHECK(Finite(b.max[k])); }
    CHECK(Finite(b.radius));
    CHECK(b.radius > 0.0f);
    std::printf("  bbox x[%.3f,%.3f] y[%.3f,%.3f] z[%.3f,%.3f] r=%.3f\n",
                b.min[0], b.max[0], b.min[1], b.max[1], b.min[2], b.max[2], b.radius);

    // First material name parsed cleanly (non-empty, ext stripped -> no '.').
    if (!m.materials.empty()) {
        const std::string& n = m.materials[0].name0;
        CHECK(!n.empty());
        CHECK(n.find('.') == std::string::npos);
        std::printf("  material[0].name = '%s'\n", n.c_str());
    }

    // Geometry build succeeds and yields a usable MeshGeometry view.
    render::BgfGeometry g;
    CHECK(render::BuildGeometry(m, g));
    render::MeshGeometry* mg = g.View();
    CHECK(mg != nullptr);
    if (mg) {
        CHECK_EQ(mg->polyCount, (int)m.polyCount);
        CHECK_EQ(mg->vertexCount, (int)m.vertices.size());
    }
}

TEST(AgfRealMesh, ResolveThroughSource) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!AssetsPresent(fs)) {
        std::printf("  [skip] AgfRealMesh.ResolveThroughSource: real game dir absent\n");
        return;
    }

    play::RealMeshSource src;
    CHECK(src.MountArchive(&fs, "Resources/Objects.BIN", /*caseInsensitive=*/false));
    if (!src.mounted()) return;

    render::BgfBounds bounds;
    render::MeshGeometry* mg = src.ResolveWithBounds("_DYNAMIC/Buch/Buch.bgf", &bounds);
    CHECK(mg != nullptr);
    if (mg) {
        CHECK(mg->polyCount > 0);
        CHECK(mg->vertexCount > 0);
    }
    CHECK(bounds.radius > 0.0f);

    // A second known member parses too (the low-LOD book).
    render::MeshGeometry* mg2 = src.Resolve("_DYNAMIC/Buch/ob_BUCH_Low.bgf");
    CHECK(mg2 != nullptr);
    if (mg2) CHECK(mg2->polyCount > 0);

    // A missing member resolves to null (no crash).
    CHECK(src.Resolve("does/not/Exist.bgf") == nullptr);

    // Install as the active MeshResolver source + a fixed name resolver, then
    // exercise the object_mesh_render-compatible adapter.
    play::InstallRealMeshSource(&src);
    play::InstallMeshNameResolver([](const play::EntityRef&) -> std::string {
        return "_DYNAMIC/Buch/Buch.bgf";
    });
    play::EntityRef e{play::EntityKind::Object, 1, 0, 1};
    const render::MeshGeometry* viaHook = play::RealMeshResolver(e);
    CHECK(viaHook != nullptr);
    play::InstallRealMeshSource(nullptr);
    play::InstallMeshNameResolver(nullptr);
}
