// tests/e2e/play_interact_building_e2e_test.cpp — GUARDED real-asset building
// interaction. Load the REAL shipped AUGSBURG.cty into the live arrays, pick a REAL
// live building object, project it into a city view, click it, drive the building
// dialog FSM to an action, build+enqueue a real opcode-26 building command through
// the REAL CommandQueue codec, apply it, and assert the live building record changed
// deterministically (same across reruns). Skips cleanly when assets are absent.
#include "test.h"

#include "play/interact_building.h"
#include "play/scene_pick.h"
#include "play/real_session.h"
#include "sim/command.h"
#include "sim/entity.h"
#include "sim/building_types.h"
#include "io/vfs.h"
#include "shim/IFileSystem.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// A read-only disk-backed filesystem rooted at the real game directory.
class DiskFile : public shim::IFile {
public:
    explicit DiskFile(std::vector<u8> d) : data_(std::move(d)) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = data_.size() - pos_;
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, data_.data() + pos_, n);
        pos_ += n; return n;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t len = (std::int64_t)data_.size();
        std::int64_t base = whence == 1 ? (std::int64_t)pos_ : whence == 2 ? len : 0;
        std::int64_t t = base + off; if (t < 0) return -1;
        pos_ = (std::size_t)t; return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)data_.size(); }
private:
    std::vector<u8> data_;
    std::size_t pos_ = 0;
};

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
    std::string full(const char* path) const {
        std::string p = path; for (char& c : p) if (c == '\\') c = '/';
        return root_ + "/" + p;
    }
    std::string root_;
};

const char* FindAssetRoot() {
    static const char* candidates[] = {
        "europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
        "reimpl/europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
    };
    static std::string root;
    for (const char* c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f) { std::string s = c; auto pos = s.find("/Resources/");
                 root = s.substr(0, pos); return root.c_str(); }
    }
    return nullptr;
}

// Find the first live building in g_objects after a real load; report its id.
i32 FirstLiveBuildingId() {
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive != 0)
            return sim::g_objects[i].id;
    return 0;
}

// Read the BuildingRec price scalar (+122) of the building with this id.
bool ReadPrice(i32 id, i32* out) {
    sim::ObjectRec* o = sim::BuildingFindById(id);
    if (!o) return false;
    auto* b = reinterpret_cast<sim::BuildingRec*>(o);
    std::memcpy(out, &b->qualityScalar, sizeof *out);
    return true;
}

// Run one full real-asset building click->dialog->command->apply cycle. Returns the
// click result; *priceBefore/*priceAfter capture the mutated field.
BuildingClickResult RunRealClick(const char* root, i32* outId,
                                 i32* priceBefore, i32* priceAfter) {
    DiskFs fs(root);
    u32 pc = 0, oc = 0;
    bool loaded = LoadRealCity(&fs, root, "Augsburg", &pc, &oc);
    BuildingClickResult r;
    if (!loaded) { io::VfsShutdown(); return r; }

    sim::g_sceneArrayLoaded = true;           // resolver bail guard

    i32 id = FirstLiveBuildingId();
    *outId = id;
    if (id == 0) { io::VfsShutdown(); return r; }
    ReadPrice(id, priceBefore);

    // Project this real building at a fixed world position into a city view, then
    // click exactly on its projected screen point.
    float eye[3] = {0.0f, 0.0f, 0.0f};
    CityViewCamera cam = MakeCityViewCamera(eye, /*pixelsPerUnit=*/1.0f, 320, 240);
    ScenePickObject objs[1];
    objs[0].id = id;
    objs[0].pos[0] = 12.0f; objs[0].pos[1] = 0.0f; objs[0].pos[2] = 7.0f;
    float sx = 0.0f, sy = 0.0f;
    ProjectWorldToScreen(cam, objs[0].pos, &sx, &sy);

    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    InstallBuildingCommandHandler(q);
    SetBuildingDialogHooks(nullptr);          // inert defaults -> real-record apply

    // Drive a carpenter-production dialog (kind 133) so a real building command issues.
    auto kindOf = [](i32) { return 133; };
    r = IssueBuildingClick(q, cam, sx, sy, objs, 1, /*pickRadius=*/4.0f, kindOf,
                           BuildingMenuItem::kRaisePrice);
    ReadPrice(id, priceAfter);

    io::VfsShutdown();
    return r;
}

} // namespace

TEST(PlayInteractBuildingE2E, AugsburgClickBuildingAppliesCommand) {
    const char* root = FindAssetRoot();
    if (!root) { CHECK(true); return; }       // assets absent — skip cleanly
    std::printf("[interact_building][augsburg] asset root: %s\n", root);

    i32 id = 0, priceBefore = 0, priceAfter = 0;
    BuildingClickResult r = RunRealClick(root, &id, &priceBefore, &priceAfter);

    std::printf("[interact_building][augsburg] buildingId=%d resolveKind=%d opened=%d "
                "op=%d field=%d ring=%d applied=%d price %d->%d\n",
                id, r.resolveKind, (int)r.opened, (int)r.command.opcode,
                r.command.field, r.ringSlot, (int)r.applied, priceBefore, priceAfter);

    CHECK(id != 0);                            // a real building was found
    if (id == 0) return;

    // the click resolved the real building, the dialog reached the action, and a real
    // opcode-26 command was enqueued + applied.
    CHECK_EQ(r.pickId, id);
    CHECK_EQ(r.resolveKind, 1);                // object/building
    CHECK(r.opened);
    CHECK(r.finalState == BuildingDialogState::kAction);
    CHECK(r.command.issued);
    CHECK_EQ((int)r.command.opcode, 26);
    CHECK_EQ(r.command.field, (i32)kFieldPrice);
    CHECK(r.enqueued);
    CHECK(r.applied);

    // the world changed: the real building's price scalar incremented by 1.
    CHECK_EQ(priceAfter, priceBefore + 1);
}

TEST(PlayInteractBuildingE2E, AugsburgClickDeterministicAcrossReruns) {
    const char* root = FindAssetRoot();
    if (!root) { CHECK(true); return; }

    i32 id1 = 0, b1 = 0, a1 = 0;
    i32 id2 = 0, b2 = 0, a2 = 0;
    BuildingClickResult r1 = RunRealClick(root, &id1, &b1, &a1);
    BuildingClickResult r2 = RunRealClick(root, &id2, &b2, &a2);

    std::printf("[interact_building][augsburg] rerun1 id=%d %d->%d  rerun2 id=%d %d->%d\n",
                id1, b1, a1, id2, b2, a2);

    if (id1 == 0 || id2 == 0) { CHECK(id1 != 0); return; }

    // Same building, same before/after price, same command each run (deterministic).
    CHECK_EQ(id1, id2);
    CHECK_EQ(b1, b2);
    CHECK_EQ(a1, a2);
    CHECK_EQ(a1, b1 + 1);
    CHECK_EQ((int)r1.command.opcode, (int)r2.command.opcode);
    CHECK_EQ(r1.command.field, r2.command.field);
    CHECK(r1.applied && r2.applied);
}
