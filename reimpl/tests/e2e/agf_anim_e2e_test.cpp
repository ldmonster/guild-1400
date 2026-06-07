// tests/e2e/agf_anim_e2e_test.cpp — REAL animations.BIN decode + posed-mesh sampler
// (e2e, GUARDED). Opens the shipped Resources/animations.BIN PKZIP archive, extracts
// a REAL `.baf` member, parses it with LoadAnimation, and asserts a sane frame /
// vertex count and a finite, ordered posed bbox at t=0 and t=mid. Skips cleanly when
// the asset folder is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "render/agf_anim.h"
#include "io/zip_archive.h"
#include "shim_impl/disk_filesystem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

std::string gameDir() {
    if (const char* e = std::getenv("GUILD_GAME_DIR")) return e;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool assetPresent(const std::string& dir) {
    std::string p = dir + "/Resources/animations.BIN";
    if (std::FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
}

bool bboxSane(const PosedMesh& m) {
    if (!m.valid) return false;
    for (int k = 0; k < 3; ++k) {
        if (!std::isfinite(m.bbMin[k]) || !std::isfinite(m.bbMax[k])) return false;
        if (m.bbMin[k] > m.bbMax[k] + 1e-3f) return false;
    }
    return true;
}

} // namespace

// =============================================================================
// Open the real archive, walk it for `.baf` members, and drive the first few
// through the decode + posed-mesh sampler. Asserts real counts and finite poses.
// =============================================================================
TEST(AgfAnimE2E, RealAnimationsBin) {
    std::string dir = gameDir();
    if (!assetPresent(dir)) { CHECK(true); return; }   // skipped: no assets

    shim::DiskFileSystem fs(dir);
    io::ZipArchive z;
    bool opened = z.Open(&fs, "Resources/animations.BIN");
    CHECK(opened);
    if (!opened) return;

    // The archive must carry the real member population (953 .baf + .ini siblings).
    CHECK(z.numberEntry() > 1000u);

    // Walk the central directory; drive the first few `.baf` members.
    int drove = 0, totalFrames = 0;
    std::string firstName;
    int firstFrames = 0, firstVerts = 0;

    int rc = z.GoToFirstFile();
    CHECK_EQ(rc, io::kZipOk);
    while (rc == io::kZipOk && drove < 6) {
        char name[260] = {0};
        io::ZipFileInfo info{};
        if (z.GetCurrentFileInfo(&info, name, sizeof(name)) != io::kZipOk) break;

        std::string nm(name);
        bool isBaf = nm.size() >= 4 &&
                     (nm.compare(nm.size() - 4, 4, ".baf") == 0 ||
                      nm.compare(nm.size() - 4, 4, ".BAF") == 0);
        if (isBaf) {
            std::vector<u8> bytes;
            if (z.ExtractCurrentFile(bytes) && !bytes.empty()) {
                // Real entries begin with the "BGF\0" magic.
                CHECK(bytes.size() >= 4);
                CHECK(bytes[0] == 'B' && bytes[1] == 'G' && bytes[2] == 'F' && bytes[3] == 0);

                AnimClip clip;
                bool ok = LoadAnimation(bytes.data(), bytes.size(), nm.c_str(), clip, /*loadFlag*/1);
                CHECK(ok);
                if (ok) {
                    CHECK(clip.FrameCount() > 0);
                    CHECK(clip.VertexCount() >= 0);
                    CHECK(clip.StartFrame() >= 0);
                    CHECK(clip.EndFrame() < clip.FrameCount());

                    // Posed mesh at t=0 and t=mid: finite, ordered bbox, right vtx count.
                    PosedMesh m0 = SamplePosedMesh(clip, 0.0f);
                    float midT = (float)(clip.FrameCount() - 1) * 0.5f;
                    PosedMesh mm = SamplePosedMesh(clip, midT);

                    if (clip.VertexCount() > 0) {
                        CHECK(m0.valid);
                        CHECK(mm.valid);
                        CHECK_EQ(m0.vertexCount, clip.VertexCount());
                        CHECK_EQ(mm.vertexCount, clip.VertexCount());
                        CHECK(bboxSane(m0));
                        CHECK(bboxSane(mm));
                        for (float c : m0.points) CHECK(std::isfinite(c));
                    }

                    if (drove == 0) {
                        firstName = nm;
                        firstFrames = clip.FrameCount();
                        firstVerts = clip.VertexCount();
                    }
                    totalFrames += clip.FrameCount();
                    ++drove;
                }
            }
        }
        rc = z.GoToNextFile();
    }

    CHECK(drove > 0);
    CHECK(totalFrames > 0);
    std::printf("[agf_anim e2e] drove %d real .baf clips; first=%s frames=%d morphVerts=%d totalFrames=%d\n",
                drove, firstName.c_str(), firstFrames, firstVerts, totalFrames);

    z.Close();
}

// =============================================================================
// Pull a specific known real clip (the tree-fell BUCHE animation) and assert the
// counts probed offline (5 frames, 91 morph verts) plus that the posed mesh moves
// between t=0 and t=mid (the tree visibly falls).
// =============================================================================
TEST(AgfAnimE2E, RealTreeFellClip) {
    std::string dir = gameDir();
    if (!assetPresent(dir)) { CHECK(true); return; }   // skipped

    shim::DiskFileSystem fs(dir);
    io::ZipArchive z;
    if (!z.Open(&fs, "Resources/animations.BIN")) { CHECK(true); return; }

    std::vector<u8> bytes;
    bool got = z.ExtractByName("BAUM/faellen_BUCHE_01.baf", bytes, /*caseSensitive*/false);
    CHECK(got);
    if (!got) { z.Close(); return; }

    AnimClip clip;
    bool ok = LoadAnimation(bytes.data(), bytes.size(), "BAUM/faellen_BUCHE_01.baf", clip, 1);
    CHECK(ok);
    if (ok) {
        CHECK_EQ(clip.FrameCount(), 5);
        CHECK_EQ(clip.VertexCount(), 91);

        PosedMesh m0 = SamplePosedMesh(clip, 0.0f);
        PosedMesh mm = SamplePosedMesh(clip, 2.0f);
        CHECK(m0.valid);
        CHECK(mm.valid);
        CHECK(bboxSane(m0));
        CHECK(bboxSane(mm));

        // The tree falls: the posed point cloud must change between t=0 and t=mid.
        bool moved = false;
        if (m0.points.size() == mm.points.size()) {
            for (size_t k = 0; k < m0.points.size(); ++k)
                if (std::fabs(m0.points[k] - mm.points[k]) > 1e-3f) { moved = true; break; }
        }
        CHECK(moved);
        std::printf("[agf_anim e2e] BUCHE fell: t0 bbox x[%.1f..%.1f] -> tmid x[%.1f..%.1f] moved=%d\n",
                    m0.bbMin[0], m0.bbMax[0], mm.bbMin[0], mm.bbMax[0], (int)moved);
    }
    z.Close();
}
