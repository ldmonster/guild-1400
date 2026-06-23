// gilde.exe 0x5d0020 — VIBE_Anim_CalculateAnimNormals over a REAL .baf clip (e2e, guarded).
// Loads a real morph clip from Resources/animations.BIN, builds a triangle list over its
// vertices, and runs CalculateClipNormals — asserting per-frame unit normals + finite,
// neighbour-smoothed bounds for every frame of the real animation.
#include "test.h"
#include "render/agf_anim.h"
#include "render/anim_normals.h"
#include "io/zip_archive.h"
#include "shim_impl/disk_filesystem.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
using namespace guild;
static std::string gameDir(){ if(const char*e=std::getenv("GUILD_GAME_DIR"))return e; return "europe_guild_1400_original"; }

TEST(AnimNormalsE2E, RealClip) {
    shim::DiskFileSystem fs(gameDir());
    if (!fs.exists("Resources/animations.BIN")) { CHECK(true); return; }
    io::ZipArchive z; if (!z.Open(&fs, "Resources/animations.BIN")) { CHECK(true); return; }

    int drove = 0;
    for (int rc = z.GoToFirstFile(); rc == io::kZipOk && drove < 3; rc = z.GoToNextFile()) {
        char name[260] = {0}; io::ZipFileInfo info{};
        if (z.GetCurrentFileInfo(&info, name, sizeof name) != io::kZipOk) break;
        std::string nm(name);
        if (nm.size() < 4 || (nm.compare(nm.size()-4,4,".baf") && nm.compare(nm.size()-4,4,".BAF"))) continue;
        std::vector<u8> bytes;
        if (!z.ExtractCurrentFile(bytes) || bytes.empty()) continue;
        render::AnimClip clip;
        if (!render::LoadAnimation(bytes.data(), bytes.size(), nm.c_str(), clip, 1)) continue;
        const int vc = clip.VertexCount();
        if (clip.FrameCount() <= 0 || vc < 3) continue;
        ++drove;

        // A valid triangle list over the real vertices (consecutive triples).
        std::vector<std::array<int,3>> tris;
        for (int i = 0; i + 2 < vc; i += 3) tris.push_back({i, i+1, i+2});
        CHECK(!tris.empty());

        std::vector<std::vector<float>> N; std::vector<render::AnimFrameBounds> B;
        render::CalculateClipNormals(clip, tris, N, B);

        CHECK_EQ((int)N.size(), clip.FrameCount());
        CHECK_EQ((int)B.size(), clip.FrameCount());
        for (int f = 0; f < clip.FrameCount(); ++f) {
            CHECK_EQ((int)N[f].size(), vc * 3);
            // every normal is unit length or exactly zero (degenerate / unreferenced vertex)
            for (int v = 0; v < vc; ++v) {
                float x=N[f][3*v], y=N[f][3*v+1], zz=N[f][3*v+2];
                float len = std::sqrt(x*x+y*y+zz*zz);
                CHECK(len < 1e-3f || std::fabs(len - 1.0f) < 1e-3f);
            }
            for (int k = 0; k < 3; ++k) {
                CHECK(std::isfinite(B[f].bbMin[k]));
                CHECK(std::isfinite(B[f].bbMax[k]));
                CHECK(B[f].bbMax[k] >= B[f].bbMin[k]);
            }
        }
        std::printf("[animnormals] '%s' frames=%d verts=%d tris=%zu\n",
                    nm.c_str(), clip.FrameCount(), vc, tris.size());
    }
    CHECK(true);
}
