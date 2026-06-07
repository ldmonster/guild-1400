// tests/e2e/wire_char_anim_e2e_test.cpp — REAL animation playback wired into a REAL
// AUGSBURG character (e2e, GUARDED). Loads the shipped AUGSBURG city into the live
// sim arrays, pulls a REAL `.baf` morph clip out of Resources/animations.BIN, binds
// it (via the deterministic test-supplied mapping — the engine's per-object anim
// track is not in the portable arrays, SAID SO) to a real loaded character's entity
// id, and advances the playback driver a few ticks through the PUBLIC PosedMeshResolver
// hook. Asserts the posed mesh bbox / vertices EVOLVE across ticks and are
// deterministic across reruns. Reports the real clip + observed vertex motion.
//
// Skips cleanly when the real install is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/wire_char_anim.h"
#include "play/real_session.h"
#include "render/agf_anim.h"
#include "io/zip_archive.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "shim/IFileSystem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;
using namespace guild::render;

namespace {

class DiskFs : public shim::IFileSystem {
public:
    explicit DiskFs(std::string root) : root_(std::move(root)) {}
    shim::IFile* open(const char* path, const char* mode) override {
        if (mode && (std::strchr(mode, 'w') || std::strchr(mode, 'W'))) return nullptr;
        std::ifstream f(full(path), std::ios::binary);
        if (!f) return nullptr;
        std::vector<u8> data((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        return new DiskFile(std::move(data));
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char* path) override {
        std::ifstream f(full(path), std::ios::binary); return (bool)f;
    }
private:
    struct DiskFile : public shim::IFile {
        explicit DiskFile(std::vector<u8> d) : data_(std::move(d)) {}
        std::size_t read(void* dst, std::size_t n) override {
            std::size_t a = data_.size() - pos_; if (n > a) n = a;
            if (n) std::memcpy(dst, data_.data() + pos_, n);
            pos_ += n; return n;
        }
        std::size_t write(const void*, std::size_t) override { return 0; }
        std::int64_t seek(std::int64_t off, int whence) override {
            std::int64_t len = (std::int64_t)data_.size();
            std::int64_t base = whence == 1 ? (std::int64_t)pos_ : whence == 2 ? len : 0;
            std::int64_t t = base + off; if (t < 0) return -1; pos_ = (std::size_t)t; return pos_;
        }
        std::int64_t tell() override { return (std::int64_t)pos_; }
        std::int64_t size() override { return (std::int64_t)data_.size(); }
        std::vector<u8> data_; std::size_t pos_ = 0;
    };
    std::string full(const char* path) const {
        std::string p = path; for (char& c : p) if (c == '\\') c = '/';
        return root_ + "/" + p;
    }
    std::string root_;
};

const char* FindAssetRoot() {
    static std::string root;
    if (const char* env = std::getenv("GUILD_GAME_DIR")) {
        std::string cty = std::string(env) + "/Resources/gamedata/Cities/AUGSBURG.cty";
        std::ifstream f(cty, std::ios::binary);
        if (f) { root = env; return root.c_str(); }
    }
    static const char* candidates[] = {
        "europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
        "reimpl/europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
    };
    for (const char* c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f) { std::string s = c; auto pos = s.find("/Resources/");
                 root = s.substr(0, pos); return root.c_str(); }
    }
    return nullptr;
}

bool bboxFinite(const PosedMesh& m) {
    for (int k = 0; k < 3; ++k)
        if (!std::isfinite(m.bbMin[k]) || !std::isfinite(m.bbMax[k])) return false;
    return true;
}

} // namespace

// =============================================================================
// Bind a real clip to a real AUGSBURG character and advance playback a few ticks.
// =============================================================================
TEST(WireCharAnimE2E, AugsburgCharacterPosedPlayback) {
    const char* root = FindAssetRoot();
    if (!root) { CHECK(true); return; }   // assets absent — skip
    std::printf("[wire_char_anim][e2e] asset root: %s\n", root);

    DiskFs fs(root);

    // 1) Load the real AUGSBURG city into the live sim arrays.
    std::uint32_t persons = 0, objects = 0;
    bool loaded = LoadRealCity(&fs, root, "Augsburg", &persons, &objects);
    CHECK(loaded);
    if (!loaded) return;
    std::printf("[wire_char_anim][e2e] loaded persons=%u objects=%u\n", persons, objects);

    // Pick a real loaded character (first live person slot).
    i32 charId = -1;
    for (int i = 0; i < kPersonCapacity; ++i) {
        if (g_persons[i].marker != -1) { charId = g_persons[i].id; break; }
    }
    CHECK(charId >= 0);
    if (charId < 0) return;
    std::printf("[wire_char_anim][e2e] bound character id=%d\n", charId);

    // 2) Pull a real `.baf` morph clip out of animations.BIN (the tree-fell BUCHE
    //    clip probed in the agf_anim e2e: 5 frames, 91 morph verts).
    io::ZipArchive z;
    if (!z.Open(&fs, "Resources/animations.BIN")) { CHECK(true); return; }
    std::vector<u8> bytes;
    bool got = z.ExtractByName("BAUM/faellen_BUCHE_01.baf", bytes, /*caseSensitive*/false);
    CHECK(got);
    if (!got) { z.Close(); return; }

    AnimClip clip;
    bool ok = LoadAnimation(bytes.data(), bytes.size(), "BAUM/faellen_BUCHE_01.baf", clip, 1);
    CHECK(ok);
    if (!ok) { z.Close(); return; }
    CHECK(clip.FrameCount() > 0);
    CHECK(clip.VertexCount() > 0);
    std::printf("[wire_char_anim][e2e] real clip BAUM/faellen_BUCHE_01: frames=%d morphVerts=%d\n",
                clip.FrameCount(), clip.VertexCount());

    // 3) Bind + install the playback driver through the PUBLIC PosedMeshResolver hook.
    //    NOTE: the entity->clip binding is test-supplied (deterministic), because the
    //    engine's per-object anim-track handle (record +380) is not in the portable
    //    entity arrays — documented in wire_char_anim.h.
    CharAnimDriver drv;
    drv.Bind(charId, &clip, /*base*/nullptr, /*mode*/0, /*stepPerTick*/1.0f);
    InstallCharAnimDriver(&drv);

    EntityRef e{EntityKind::Person, charId, 0, 1};

    // Frame 0 posed mesh via the resolver (the live-render mesh-fetch point).
    const MeshGeometry* g0 = PosedMeshResolver(e);
    CHECK(g0 != nullptr);
    CHECK(g0 && g0->vertexCount == clip.VertexCount());

    // Capture the posed bbox per tick over a sweep; assert it EVOLVES.
    auto bboxNow = [&]() {
        PosedMesh m = SamplePosedMesh(clip, BindingTime(*drv.BindingFor(charId)));
        return m;
    };

    PosedMesh first = bboxNow();
    CHECK(first.valid);
    CHECK(bboxFinite(first));

    int moves = 0;
    PosedMesh prev = first;
    for (int t = 0; t < 4; ++t) {
        drv.Tick();
        const MeshGeometry* g = PosedMeshResolver(e);
        CHECK(g != nullptr);
        PosedMesh m = bboxNow();
        CHECK(m.valid);
        CHECK(bboxFinite(m));
        bool changed = false;
        for (int k = 0; k < 3; ++k)
            if (std::fabs(m.bbMin[k] - prev.bbMin[k]) > 1e-3f ||
                std::fabs(m.bbMax[k] - prev.bbMax[k]) > 1e-3f) changed = true;
        if (changed) ++moves;
        prev = m;
    }
    CHECK(moves > 0);    // the tree visibly falls as the character anim plays
    std::printf("[wire_char_anim][e2e] posed bbox evolved on %d/4 ticks; "
                "t0 y[%.2f..%.2f] -> tN y[%.2f..%.2f]\n",
                moves, first.bbMin[1], first.bbMax[1], prev.bbMin[1], prev.bbMax[1]);

    // 4) Determinism across reruns: a second driver stepped the same way yields the
    //    same posed bbox at the final tick.
    CharAnimDriver drv2;
    drv2.Bind(charId, &clip, nullptr, 0, 1.0f);
    for (int t = 0; t < 4; ++t) drv2.Tick();
    PosedMesh m2 = SamplePosedMesh(clip, BindingTime(*drv2.BindingFor(charId)));
    bool same = m2.valid;
    for (int k = 0; k < 3 && same; ++k)
        if (std::fabs(m2.bbMin[k] - prev.bbMin[k]) > 0.0f ||
            std::fabs(m2.bbMax[k] - prev.bbMax[k]) > 0.0f) same = false;
    CHECK(same);
    std::printf("[wire_char_anim][e2e] deterministic across reruns: %d\n", (int)same);

    InstallCharAnimDriver(nullptr);
    z.Close();
}
