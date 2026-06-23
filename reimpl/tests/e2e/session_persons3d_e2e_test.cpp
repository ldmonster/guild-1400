// GUARDED real-asset e2e: LIVE PERSONS in the REAL 3D city view (wave 3).
//
//   1. mount the real game assets, io::LoadWorldEx AUGSBURG.cty (live world +
//      the embedded city scene blob — the VIBE_Save_PostLoadInitScene @0x5a7ef8
//      stream),
//   2. create live persons through the REAL person factory
//      (VIBE_Person_CreateAndSpawn @0x58da70) and anchor them to REAL buildings
//      by writing the homeBld column (+0x16C, dword_12CEA7C — the column
//      VIBE_NpcAction_DailyRoutineStep @0x4e7e88 dispatches to),
//   3. CityView3D: real placements (LoadCityFromWorld + BindWorldObjects), then
//      WireSessionPersons3D: every anchored person resolves through the REAL
//      chain — VIBE_Office_ResolveStaffModel @0x57c1e8 ->
//      VIBE_Character_CreateFromModel @0x402d10 (the factory, mesh attach into
//      Objects.BIN) -> the entrance dummy seat ("dummy_TUER" subtree node, the
//      0x57c8f0/0x4b0ee8 resolution) -> the pose driver
//      (VIBE_Anim_UpdateSkeletonPose @0x5cd1d8),
//   4. assert persons rendered (> 0 instances), the frame DIFFERS from the
//      persons-off frame, pose steps change pixels, determinism, and dump
//      /tmp/guild_w3b_persons.ppm.
//
// GUARDED: clean skip when the real game dir is absent. Honors GUILD_GAME_DIR.
#include "test.h"

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "play/city_view3d.h"
#include "play/session_persons3d.h"
#include "render/surface.h"
#include "sim/character_query.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/person_create.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using play::CityView3D;

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
           fs.exists("Resources/Objects.BIN") &&
           fs.exists("Resources/scenes.BIN");
}

// Snapshot the view surface's RGB bytes.
std::vector<u8> GrabSurface(const CityView3D& view, int w, int h) {
    std::vector<u8> out;
    render::Surface* fb = view.surface();
    if (!fb)
        return out;
    out.reserve((std::size_t)w * (std::size_t)h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(fb, x, y, px);
            out.push_back(px[0]);
            out.push_back(px[1]);
            out.push_back(px[2]);
        }
    return out;
}

int PixelDelta(const std::vector<u8>& a, const std::vector<u8>& b) {
    if (a.size() != b.size())
        return -1;
    int d = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i])
            ++d;
    return d;
}

bool DumpPpm(const char* path, const std::vector<u8>& rgb, int w, int h) {
    if ((int)rgb.size() != w * h * 3)
        return false;
    std::FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
    return true;
}

// Create live persons through the REAL factory (the same path the new-game
// commit populates g_persons through) and anchor each to a real building id
// via the homeBld column. Returns the created slots.
std::vector<int> SpawnAnchoredPersons(const std::vector<i32>& buildingIds) {
    using namespace guild::sim;
    struct Spec { u8 kind; u8 gender; u8 profession; };
    const Spec specs[] = {
        {5, 0, 0x02}, {5, 1, 0x05}, {6, 0, 0x04},
        {6, 1, 0x00}, {7, 0, 0x13}, {7, 1, 0x0e},
    };
    std::vector<int> slots;
    if (buildingIds.empty())
        return slots;
    int bi = 0;
    for (const Spec& s : specs) {
        PersonSpawnArgs a{};
        a.kind = s.kind;
        a.parentAId = -1;
        a.ownerWord = 0;
        a.parentBId = -1;
        a.a8 = s.gender;
        u16 slot = Person_CreateAndSpawn(a);
        if (slot == 0xFFFF)
            break;
        PersonSetByte(&g_persons[slot], 0x165, s.profession);
        // The evidenced anchor column: homeBld dword_12CEA7C (+0x16C).
        PersonSetDword(&g_persons[slot], 0x16C,
                       buildingIds[(std::size_t)(bi++ % (int)buildingIds.size())]);
        slots.push_back((int)slot);
    }
    return slots;
}

} // namespace

TEST(SessionPersons3DE2E, PersonsRenderAtBuildingDoorsInAugsburg) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SessionPersons3DE2E.PersonsRenderAtBuildingDoorsInAugsburg: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    sim::ResetEntityArrays();
    sim::ResetCharacterQuery();
    sim::ResetPersonCreate();

    // 1. The live world + the embedded city scene (real node placements + the
    //    +512 owner-object ids).
    io::WorldState world{};
    std::vector<u8> sceneBlob;
    CHECK(io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &sceneBlob));
    CHECK(!sceneBlob.empty());

    // 2. The view over the real archives + the real placements.
    CityView3D view;
    CHECK(view.Init(&fs));
    CHECK(view.LoadCityFromWorld(sceneBlob));
    int boundObjects = view.BindWorldObjects();
    CHECK(boundObjects > 0);

    // 3. Live persons anchored to REAL owner-linked buildings.
    std::vector<i32> buildingIds;
    for (const CityView3D::BoundObject& bo : view.boundObjects())
        if (bo.sceneIndex >= 0)
            buildingIds.push_back(bo.id);
    CHECK(buildingIds.size() >= 3u);
    std::vector<int> slots = SpawnAnchoredPersons(buildingIds);
    std::printf("[persons3d-e2e] buildings=%d anchored persons=%d\n",
                (int)buildingIds.size(), (int)slots.size());
    CHECK((int)slots.size() == 6);

    // 4a. BASELINE whole-city frame (no persons wired).
    CityView3D::Options opt;
    opt.fbW = 320; opt.fbH = 240;
    play::CityCamera3D cam = view.OverviewCamera();
    CityView3D::Result rBase = view.RenderFrame(cam, opt);
    std::vector<u8> pxBase = GrabSurface(view, opt.fbW, opt.fbH);
    CHECK(rBase.instancesDrawn > 0);
    CHECK_EQ(0, rBase.personInstances);

    // 4b. Wire the persons through the session entry point.
    play::SessionPersons3DOptions po;
    play::SessionPersons3DStatus st = play::WireSessionPersons3D(view, &fs, po);
    std::printf("[persons3d-e2e] wired: bound=%d posed=%d unplaced=%d "
                "modelUnresolved=%d anims=%d\n",
                st.bound, st.posed, st.unplaced, st.modelUnresolved,
                (int)st.animsMounted);
    CHECK_EQ(6, st.bound);
    CHECK_EQ(0, st.unplaced);
    CHECK_EQ(0, st.modelUnresolved);   // every model ships in Objects.BIN
    if (st.animsMounted)
        CHECK(st.posed > 0);           // the real pose chain bound clips

    // 4c. The placement evidence: every bound person sits on its anchor
    //     building's entrance dummy ("dummy_TUER" in the embedded city scene;
    //     the gb_ model's dummy_EINGANG is the parallel module's subtree) or on
    //     the building node itself (the 0x4b0ee8 fallback), at the node's
    //     composed world position.
    int atDummies = 0;
    for (const CityView3D::BoundPerson& bp : view.boundPersons()) {
        CHECK(bp.anchorBuildingId != 0);
        CHECK(!bp.model.empty());
        CHECK(!bp.member.empty());
        CHECK(bp.member.find("Character") != std::string::npos ||
              bp.member.find("CHARACTER") != std::string::npos);
        if (bp.dummySceneIndex >= 0) {
            ++atDummies;
            const play::SceneObjectInst& d =
                view.scene()[(std::size_t)bp.dummySceneIndex];
            std::string upper = d.name;
            for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
            CHECK(upper == "DUMMY_TUER" || upper == "DUMMY_EINGANG");
        }
    }
    std::printf("[persons3d-e2e] persons at entrance dummies: %d/%d\n",
                atDummies, st.bound);
    CHECK(atDummies > 0);

    // 4d. Persons frame: advance the poses, render, compare to the baseline.
    play::UpdateSessionPersons3D(view, po.animStepPerFrame);
    CityView3D::Result rP = view.RenderFrame(cam, opt);
    std::vector<u8> pxP = GrabSurface(view, opt.fbW, opt.fbH);
    std::printf("[persons3d-e2e] frame: personInstances=%d posed=%d rest=%d "
                "rasterTris=%d (base %d)\n",
                rP.personInstances, rP.personPosed, rP.personRestPose,
                rP.rasterTris, rBase.rasterTris);
    CHECK(rP.personInstances > 0);
    CHECK_EQ(rP.personInstances, rP.personPosed + rP.personRestPose);
    if (st.animsMounted && st.posed > 0)
        CHECK(rP.personPosed > 0);
    CHECK(rP.meshPolysIn > rBase.meshPolysIn);
    int deltaOverview = PixelDelta(pxBase, pxP);
    std::printf("[persons3d-e2e] overview pixel delta vs baseline = %d\n",
                deltaOverview);

    // 4e. ZOOMED pair at the first bound person (the overview can render a
    //     person sub-pixel; the zoom makes the delta decisive).
    const CityView3D::BoundPerson& first = view.boundPersons()[0];
    float eye[3] = {first.place.pos[0] + 60.0f, first.place.pos[1] + 45.0f,
                    first.place.pos[2] - 60.0f};
    play::CityCamera3D zoomCam;
    play::AimCamera(zoomCam, eye, first.place.pos);

    CityView3D::Result rZoomP = view.RenderFrame(zoomCam, opt);
    std::vector<u8> pxZoomP = GrabSurface(view, opt.fbW, opt.fbH);
    CHECK(rZoomP.personInstances > 0);

    view.UnbindPersons();   // persons off, hooks intact
    CityView3D::Result rZoom0 = view.RenderFrame(zoomCam, opt);
    std::vector<u8> pxZoom0 = GrabSurface(view, opt.fbW, opt.fbH);
    CHECK_EQ(0, rZoom0.personInstances);
    int deltaZoom = PixelDelta(pxZoom0, pxZoomP);
    std::printf("[persons3d-e2e] zoomed pixel delta persons-on vs off = %d "
                "(tris %d vs %d)\n", deltaZoom, rZoomP.rasterTris, rZoom0.rasterTris);
    CHECK(deltaZoom > 0);

    // 4f. DETERMINISM: rebind + the same advance + the same camera reproduces
    //     the persons frame byte-identically.
    play::SessionPersons3DStatus st2 = play::RebindSessionPersons3D(view, po);
    CHECK_EQ(st.bound, st2.bound);
    CHECK_EQ(st.posed, st2.posed);
    play::UpdateSessionPersons3D(view, po.animStepPerFrame);
    CityView3D::Result rZoomP2 = view.RenderFrame(zoomCam, opt);
    std::vector<u8> pxZoomP2 = GrabSurface(view, opt.fbW, opt.fbH);
    CHECK_EQ(rZoomP.personInstances, rZoomP2.personInstances);
    CHECK_EQ(rZoomP.rasterTris, rZoomP2.rasterTris);
    CHECK_EQ(0, PixelDelta(pxZoomP, pxZoomP2));

    // 4g. Pose steps move pixels (the REAL 0x5cd1d8 advance ladder).
    if (st.posed > 0) {
        play::UpdateSessionPersons3D(view, 64.0f);
        CityView3D::Result rZoomAdv = view.RenderFrame(zoomCam, opt);
        std::vector<u8> pxZoomAdv = GrabSurface(view, opt.fbW, opt.fbH);
        int poseDelta = PixelDelta(pxZoomP2, pxZoomAdv);
        std::printf("[persons3d-e2e] pose-advance pixel delta = %d (posed %d)\n",
                    poseDelta, rZoomAdv.personPosed);
        CHECK(rZoomAdv.personPosed > 0);
        CHECK(poseDelta > 0);
    }

    // 5. Artifact.
    bool dumped = DumpPpm("/tmp/guild_w3b_persons.ppm", pxZoomP, opt.fbW, opt.fbH);
    std::printf("[persons3d-e2e] dumped /tmp/guild_w3b_persons.ppm (%d)\n",
                (int)dumped);
    CHECK(dumped);

    play::UnwireSessionPersons3D(view);
    sim::ResetEntityArrays();
    sim::ResetCharacterQuery();
    io::VfsShutdown();
}

// =============================================================================
// GUARDED: the BYTE-IDENTICAL default — a view that never binds persons renders
// exactly the same frame as before this wave (the determinism the city_view3d
// e2e suite pins, re-asserted here against the person-capable build).
// =============================================================================
TEST(SessionPersons3DE2E, NoPersonsBoundKeepsFrameByteIdentical) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SessionPersons3DE2E.NoPersonsBoundKeepsFrameByteIdentical: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    sim::ResetEntityArrays();
    io::WorldState world{};
    std::vector<u8> sceneBlob;
    CHECK(io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &sceneBlob));

    CityView3D view;
    CHECK(view.Init(&fs));
    CHECK(view.LoadCityFromWorld(sceneBlob));
    view.BindWorldObjects();

    CityView3D::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    play::CityCamera3D cam = view.OverviewCamera();

    CityView3D::Result r1 = view.RenderFrame(cam, opt);
    std::vector<u8> px1 = GrabSurface(view, opt.fbW, opt.fbH);
    CityView3D::Result r2 = view.RenderFrame(cam, opt);
    std::vector<u8> px2 = GrabSurface(view, opt.fbW, opt.fbH);

    CHECK(r1.instancesDrawn > 0);
    CHECK_EQ(r1.rasterTris, r2.rasterTris);
    CHECK_EQ(0, r1.personInstances);
    CHECK_EQ(0, r2.personInstances);
    CHECK_EQ(0, PixelDelta(px1, px2));

    sim::ResetEntityArrays();
    io::VfsShutdown();
}
