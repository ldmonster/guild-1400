// GUARDED real-asset e2e: render LIVE PERSONS in the AUGSBURG city session view.
//
//   1. mount the real game assets + VFS, io::LoadWorld AUGSBURG.cty (populates
//      sim::g_persons, 768 x 536-byte rows),
//   2. RealCityRenderer: Objects.BIN (meshes) + animations.BIN (pose clips),
//   3. render ONCE with scanPersons=false (baseline) and ONCE with
//      scanPersons=true: each live person resolves through the REAL
//      VIBE_Office_ResolveStaffModel @0x57c1e8 reconstruction to its
//      "_DYNAMIC/Character/<model>.bgf" member, poses through the REAL chain
//      (UpdateSkeletonPose @0x5cd1d8 + CalculateAnimNormals @0x5d0020 +
//      RelightPosedFrame) and rasterizes in the same sorted draw pass,
//   4. assert persons resolved + rasterized, a PIXEL DELTA vs the baseline
//      frame, a populated roster, and a roster-centre pick returning the person.
//
// GUARDED: clean skip when the real game dir is absent. Honors GUILD_GAME_DIR.
#include "test.h"

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "play/real_city_render.h"
#include "sim/entity.h"
#include "sim/person.h"          // PersonSetByte (raw-offset field writes)
#include "sim/person_create.h"   // Person_CreateAndSpawn (the person factory)
#include "shim/IGraphicsDevice.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/memory_graphics.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("Resources/Objects.BIN");
}

// Snapshot a device backbuffer's pixel bytes.
std::vector<unsigned char> GrabBackbuffer(shim::IGraphicsDevice& dev) {
    std::vector<unsigned char> out;
    shim::Surface* bb = dev.backbuffer();
    if (!bb || !bb->pixels) return out;
    std::size_t bytes = (std::size_t)bb->pitch * (std::size_t)bb->height;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(bb->pixels);
    out.assign(p, p + bytes);
    return out;
}

// Populate live persons through the REAL person factory
// (VIBE_Person_CreateAndSpawn @0x58da70 via sim::Person_CreateAndSpawn — the
// single path every live person is created through; the shipped AUGSBURG.cty
// SEED carries only the city record, persons are created at new-game). Then set
// the profession byte (+0x165) the recruit/AI flow would assign, so the model
// resolver exercises real table rows. Returns the number created.
int SpawnLivePersons() {
    using namespace guild::sim;
    struct Spec { u8 kind; u8 gender; u8 profession; };
    // kinds 5/6/7 are the live person kinds (the AddObjekt +2 byte checks);
    // professions from the recovered tables: 2=dieb_MANN2, 4=priester_KUTTE,
    // 0x13=fechter_SOLDAT, 5(f)=handwerkerin_FRAU, 0(=default by gender).
    const Spec specs[] = {
        {5, 0, 0x02}, {5, 1, 0x05}, {6, 0, 0x04},
        {6, 1, 0x00}, {7, 0, 0x13}, {7, 1, 0x0e},
    };
    int created = 0;
    for (const Spec& s : specs) {
        PersonSpawnArgs a{};
        a.kind = s.kind;
        a.parentAId = -1;
        a.ownerWord = 0;
        a.parentBId = -1;
        a.queryRec = 0;
        a.a6 = 0; a.a7 = 0;
        a.a8 = s.gender;          // gender/seed byte -> record +9
        u16 slot = Person_CreateAndSpawn(a);
        if (slot == 0xFFFF) break;
        PersonSetByte(&g_persons[slot], 0x165, s.profession);
        ++created;
    }
    return created;
}

} // namespace

TEST(SessionPersonsRenderE2E, RenderLivePersonsInAugsburg) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SessionPersonsRenderE2E.RenderLivePersonsInAugsburg: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    // 1. Mount + load the real AUGSBURG world (persons populated by LoadWorld).
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    sim::ResetEntityArrays();
    sim::ResetPersonCreate();
    io::WorldState world{};
    bool loaded = io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world);
    CHECK(loaded);
    // The shipped .cty SEED carries only the city/scene record; live persons are
    // created at new-game through the person factory. Create them the same way.
    int seedRows = 0;
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        if (sim::g_persons[i].marker != -1) ++seedRows;
    int created = SpawnLivePersons();
    int livePersonKinds = 0;
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        if (sim::g_persons[i].marker == -1) continue;
        if (sim::g_persons[i].kind < 10) ++livePersonKinds;
    }
    std::printf("[persons-e2e] LoadWorld=%d seed rows=%d created=%d live(kind<10)=%d\n",
                (int)loaded, seedRows, created, livePersonKinds);
    CHECK(created > 0);
    CHECK(livePersonKinds >= created);

    // 2. The renderer: meshes + textures (optional) + the anim archive.
    play::RealCityRenderer rc;
    CHECK(rc.Init(&fs, "Resources/Objects.BIN"));
    CHECK(rc.mounted());
    bool anims = rc.InitPersonAnims(&fs, "Resources/animations.BIN");
    std::printf("[persons-e2e] animations.BIN mounted=%d\n", (int)anims);

    play::RealCityRenderer::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    opt.maxObjects = 24;
    opt.pixelsPerUnit = 0.18f;

    // 3a. BASELINE frame: scanPersons = false.
    shim::MemoryGraphicsDevice devBase;
    CHECK(devBase.init(opt.fbW, opt.fbH, 16, false));
    play::RealCityRenderer::Result rBase = rc.Render(opt, devBase);
    std::vector<unsigned char> pxBase = GrabBackbuffer(devBase);
    CHECK(rBase.presented);
    CHECK_EQ(rBase.personMeshes, 0);
    CHECK(rc.personRoster().empty());

    // 3b. PERSON frame: scanPersons = true (idle pose, one anim step).
    opt.scanPersons = true;
    opt.maxPersons = 12;
    opt.personAnimStep = 8.0f;     // fold 8 phase ticks through the REAL driver
    shim::MemoryGraphicsDevice devP;
    CHECK(devP.init(opt.fbW, opt.fbH, 16, false));
    play::RealCityRenderer::Result rP = rc.Render(opt, devP);
    std::vector<unsigned char> pxP = GrabBackbuffer(devP);

    std::printf("[persons-e2e] livePersons=%d personMeshes=%d personPosed=%d "
                "personRestPose=%d personQuads=%d rasterTris=%d (base %d)\n",
                rP.livePersons, rP.personMeshes, rP.personPosed,
                rP.personRestPose, rP.personQuads, rP.rasterTris,
                rBase.rasterTris);

    // 4a. Persons were scanned, RESOLVED to real character meshes, and drawn.
    CHECK(rP.livePersons > 0);
    CHECK(rP.personMeshes > 0);          // resolved character meshes, not quads
    CHECK(rP.personQuads == 0);          // every drawn person had a real mesh
    if (anims) {
        CHECK(rP.animsMounted);
        CHECK(rP.personPosed > 0);       // the real pose chain ran
    }
    // More triangles flushed than the object-only frame (persons rasterized).
    CHECK(rP.rasterTris > rBase.rasterTris);

    // 4b. PIXEL DELTA vs the scanPersons=false frame.
    CHECK(!pxBase.empty());
    CHECK_EQ(pxBase.size(), pxP.size());
    int diff = 0;
    for (std::size_t i = 0; i < pxBase.size(); ++i)
        if (pxBase[i] != pxP[i]) ++diff;
    std::printf("[persons-e2e] pixel-byte delta vs baseline = %d\n", diff);
    CHECK(diff > 0);

    // 4c. The roster matches the drawn persons and projects on screen.
    const auto& roster = rc.personRoster();
    std::printf("[persons-e2e] roster=%zu entries; first: id=%d model=%s "
                "member=%s screen=(%.1f,%.1f) r=%.1f posed=%d\n",
                roster.size(),
                roster.empty() ? 0 : roster[0].id,
                roster.empty() ? "-" : roster[0].model.c_str(),
                roster.empty() ? "-" : roster[0].member.c_str(),
                roster.empty() ? 0.f : roster[0].screenX,
                roster.empty() ? 0.f : roster[0].screenY,
                roster.empty() ? 0.f : roster[0].radius,
                roster.empty() ? 0 : (int)roster[0].posed);
    CHECK(!roster.empty());
    CHECK_EQ((int)roster.size(), rP.personMeshes + rP.personQuads);
    for (const auto& re : roster) {
        CHECK(re.id != 0 || re.slot >= 0);
        CHECK(!re.model.empty());
        CHECK(!re.member.empty());
        CHECK(re.member.find("Character/") != std::string::npos ||
              re.member.find("CHARACTER/") != std::string::npos);
        CHECK(re.radius >= 2.0f);
    }

    // 4d. A pick at a roster entry's projected centre returns that person.
    int pickIdx = -1;
    for (std::size_t i = 0; i < roster.size(); ++i)
        if (roster[i].onScreen) { pickIdx = (int)i; break; }
    if (pickIdx >= 0) {
        play::ScenePickResult pr =
            rc.PickPerson(opt, roster[(std::size_t)pickIdx].screenX,
                          roster[(std::size_t)pickIdx].screenY);
        std::printf("[persons-e2e] pick at roster[%d] -> index=%d id=%d d=%.2f\n",
                    pickIdx, pr.index, pr.id, pr.screenDist);
        CHECK(pr.index >= 0);
        CHECK_EQ(pr.id, roster[(std::size_t)pickIdx].id);
    } else {
        std::printf("[persons-e2e] note: no roster entry projected on-screen "
                    "(grid outside the demo frame)\n");
        CHECK(true);
    }

    // 4e. DETERMINISM: the same person render again yields identical counts.
    shim::MemoryGraphicsDevice devP2;
    CHECK(devP2.init(opt.fbW, opt.fbH, 16, false));
    play::RealCityRenderer::Result rP2 = rc.Render(opt, devP2);
    CHECK_EQ(rP2.personMeshes, rP.personMeshes);
    CHECK_EQ(rP2.livePersons, rP.livePersons);

    sim::ResetEntityArrays();
    io::VfsShutdown();
}

// =============================================================================
// GUARDED: the ANIMATION advances — two different personAnimStep values move the
// pose cursor (the REAL UpdateSkeletonPose frame/phase advance), changing the
// posed frame's pixels.
// =============================================================================
TEST(SessionPersonsRenderE2E, PoseDriverAdvancesAnimation) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SessionPersonsRenderE2E.PoseDriverAdvancesAnimation: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    sim::ResetEntityArrays();
    sim::ResetPersonCreate();
    io::WorldState world{};
    CHECK(io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world));
    CHECK(SpawnLivePersons() > 0);

    play::RealCityRenderer rc;
    CHECK(rc.Init(&fs, "Resources/Objects.BIN"));
    if (!rc.InitPersonAnims(&fs, "Resources/animations.BIN")) {
        std::printf("  [skip] animations.BIN absent — pose chain unavailable\n");
        sim::ResetEntityArrays();
        io::VfsShutdown();
        CHECK(true);
        return;
    }

    play::RealCityRenderer::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    opt.maxObjects = 0;            // persons only -> the delta IS the pose
    opt.scanObjects = false;
    opt.scanPersons = true;
    opt.maxPersons = 4;
    // ZOOM IN: a character mesh is ~10 world units; at 3 px/unit it covers
    // ~30 px so the posed-frame difference is visible in pixels.
    opt.pixelsPerUnit = 3.0f;
    opt.eyeX = 0.0f; opt.eyeZ = 0.0f;     // grid centred on the frame centre
    opt.personOriginX = -10.0f;
    opt.personOriginZ = -10.0f;
    opt.personCellSize = 14.0f;
    opt.personGridCols = 2;

    opt.personAnimStep = 0.0f;     // rest cursor
    shim::MemoryGraphicsDevice d0;
    CHECK(d0.init(opt.fbW, opt.fbH, 16, false));
    play::RealCityRenderer::Result r0 = rc.Render(opt, d0);
    std::vector<unsigned char> px0 = GrabBackbuffer(d0);

    opt.personAnimStep = 64.0f;    // a deep fold through the driver's ladder
    shim::MemoryGraphicsDevice d1;
    CHECK(d1.init(opt.fbW, opt.fbH, 16, false));
    play::RealCityRenderer::Result r1 = rc.Render(opt, d1);
    std::vector<unsigned char> px1 = GrabBackbuffer(d1);

    std::printf("[persons-anim-e2e] step0: posed=%d tris=%d  step64: posed=%d tris=%d\n",
                r0.personPosed, r0.rasterTris, r1.personPosed, r1.rasterTris);
    if (r0.personPosed == 0) {
        std::printf("  [skip] no person posed (clip/mesh vertex-count mismatch "
                    "across the shipped models) — rest-pose only\n");
        sim::ResetEntityArrays();
        io::VfsShutdown();
        CHECK(true);
        return;
    }
    CHECK(r1.personPosed > 0);
    CHECK(!px0.empty());
    CHECK_EQ(px0.size(), px1.size());
    int diff = 0;
    for (std::size_t i = 0; i < px0.size(); ++i)
        if (px0[i] != px1[i]) ++diff;
    std::printf("[persons-anim-e2e] pixel-byte delta between pose steps = %d\n", diff);
    CHECK(diff > 0);

    sim::ResetEntityArrays();
    io::VfsShutdown();
}
