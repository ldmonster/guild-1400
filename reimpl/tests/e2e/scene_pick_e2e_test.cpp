// E2E (GUARDED on real assets): load the REAL shipped AUGSBURG.cty through the full
// world loader, collect the live object/building records the loader scattered into
// sim::g_objects, lay them out on the city ground plane, project them through the
// REAL city-view camera transform (scene_pick + render::ProjectVerticesToScreen),
// click at a chosen cursor over one object, and assert a VALID live object id is
// picked (and resolves to a live entity via the REAL resolver). A click on empty
// space picks nothing.
//
// Skips cleanly (trivial pass) when the original assets are absent. Honors
// GUILD_GAME_DIR; otherwise probes the in-repo europe_guild_1400_original tree.
//
// NOTE ON SCOPE: the reconstructed ObjectRec does not yet decode each record's
// world transform (its +5..+168 block is still opaque pad), so the projected
// positions are derived deterministically from each REAL object's array index. The
// drive is real end to end otherwise: real .cty load, real object COUNT and real
// object IDS, the real projection pipeline, and the real id->entity resolver.
#include "test.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "play/scene_pick.h"
#include "sim/entity.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "shim/IFileSystem.h"

using namespace guild;

namespace {

// A passthrough host filesystem serving real files from disk (so the VFS's
// transparent gunzip path runs over the actual AUGSBURG.cty bytes).
class DiskFile : public shim::IFile {
public:
    explicit DiskFile(std::vector<u8> data) : data_(std::move(data)) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = data_.size() - pos_;
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, data_.data() + pos_, n);
        pos_ += n;
        return n;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)data_.size() : 0;
        std::int64_t t = base + off;
        if (t < 0) return -1;
        pos_ = (std::size_t)t;
        return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)data_.size(); }
private:
    std::vector<u8> data_;
    std::size_t     pos_ = 0;
};

class DiskFs : public shim::IFileSystem {
public:
    explicit DiskFs(std::string root) : root_(std::move(root)) {}
    shim::IFile* open(const char* path, const char* mode) override {
        if (mode && (std::strchr(mode, 'w') || std::strchr(mode, 'W')))
            return nullptr;
        std::ifstream f(full(path), std::ios::binary);
        if (!f) return nullptr;
        std::vector<u8> data((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        return new DiskFile(std::move(data));
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char* path) override {
        std::ifstream f(full(path), std::ios::binary);
        return (bool)f;
    }
private:
    std::string full(const char* path) const {
        std::string p = path;
        for (char& c : p) if (c == '\\') c = '/';
        return root_ + "/" + p;
    }
    std::string root_;
};

// Locate the asset root (GUILD_GAME_DIR first, then in-repo candidates).
const char* FindAssetRoot() {
    static std::string root;
    const char* env = std::getenv("GUILD_GAME_DIR");
    if (env && *env) {
        std::string probe = std::string(env) +
                            "/Resources/gamedata/Cities/AUGSBURG.cty";
        std::ifstream f(probe, std::ios::binary);
        if (f) { root = env; return root.c_str(); }
    }
    static const char* candidates[] = {
        "europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
        "reimpl/europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
    };
    for (const char* c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f) {
            std::string s = c;
            auto pos = s.find("/Resources/");
            root = s.substr(0, pos);
            return root.c_str();
        }
    }
    return nullptr;
}

} // namespace

TEST(ScenePickE2E, AugsburgProjectRealObjectsAndPick) {
    const char* root = FindAssetRoot();
    if (!root) {
        std::printf("[scene_pick] assets absent -> skip (trivial pass)\n");
        CHECK(true);
        return;
    }
    std::printf("[scene_pick] asset root: %s\n", root);

    DiskFs fs(root);
    io::VfsInit(&fs, /*caseInsensitive=*/false);
    sim::ResetEntityArrays();

    io::WorldState world{};
    bool ok = io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world);
    std::printf("[scene_pick] LoadWorld -> %s, objectCount=%u\n",
                ok ? "true" : "false", world.objectCount);
    CHECK(ok);

    // Collect the REAL live object/building records the loader populated.
    std::vector<play::ScenePickObject> objs;
    for (int i = 0; i < sim::kObjectCapacity; ++i) {
        if (sim::g_objects[i].alive) {
            play::ScenePickObject o;
            o.id = sim::g_objects[i].id;
            // Deterministic ground-plane layout derived from the real array index:
            // a 16-wide grid, 40 world-units spacing, centered on the camera eye.
            const int gx = i % 16;
            const int gz = i / 16;
            o.pos[0] = 100.0f + gx * 40.0f;
            o.pos[1] = 0.0f;
            o.pos[2] = 100.0f + gz * 40.0f;
            objs.push_back(o);
        }
    }
    const int liveCount = (int)objs.size();
    std::printf("[scene_pick] live objects laid out = %d\n", liveCount);
    CHECK(liveCount > 0);
    CHECK((u32)liveCount == world.objectCount);

    // The id-resolver short-circuits unless the array-base guard flags are set
    // (they mirror dword_13CE27C / dword_13CE294, runtime-bound by the engine boot
    // we don't run here). The arrays ARE populated by LoadWorld, so enable them.
    sim::g_sceneArrayLoaded = true;
    sim::g_personArrayLoaded = true;

    // City-view camera centered on the grid origin; 4 px / world-unit.
    float eye[3] = {100, 0, 100};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, /*ppu=*/4.0f, 1024, 1024);

    // Choose a target object well inside the grid (index 17 if present, else mid).
    const int target = liveCount > 17 ? 17 : liveCount / 2;
    float tx = 0, ty = 0;
    bool onScreen = play::ProjectWorldToScreen(cam, objs[target].pos, &tx, &ty);
    std::printf("[scene_pick] target idx=%d id=%d -> screen (%.3f, %.3f) on=%d\n",
                target, objs[target].id, tx, ty, onScreen ? 1 : 0);
    CHECK(onScreen);

    // Click right on the target object's projected center.
    int kind = -99;
    play::ScenePickResult r =
        play::PickAndResolveSceneEntity(cam, tx, ty, objs.data(), liveCount,
                                        /*pickRadius=*/15.0f, &kind);
    std::printf("[scene_pick] picked index=%d id=%d dist=%.3f kind=%d\n",
                r.index, r.id, r.screenDist, kind);

    // A valid object was picked under the cursor.
    CHECK(r.index >= 0);
    CHECK(r.index < liveCount);
    CHECK(r.id != 0);
    CHECK(r.screenDist < 2.0f);
    // The picked id is one of the REAL live object ids and resolves to a live entity.
    CHECK_EQ(kind, 1);
    bool idIsReal = false;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive && sim::g_objects[i].id == r.id) { idIsReal = true; break; }
    CHECK(idIsReal);

    // A click far off any object's projection picks nothing.
    play::ScenePickResult miss =
        play::PickSceneObject(cam, 5.0f, 1000.0f, objs.data(), liveCount, 6.0f);
    std::printf("[scene_pick] empty-space click -> index=%d\n", miss.index);
    CHECK_EQ(miss.index, -1);
    CHECK_EQ(miss.id, 0);

    io::VfsShutdown();
}
