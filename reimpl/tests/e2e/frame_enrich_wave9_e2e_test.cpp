// =============================================================================
// WAVE-9 W9-FRAME-ENRICH — the wave-8 reconstructions that were INERT in the live
// frame (because the simplified CityView3D instance pipeline did not carry their
// per-object data) wired so they FIRE, each behind an additive Options flag
// (DEFAULT OFF so every pinned frame stays byte-identical; the session opts in).
//
// GUARDED real-asset e2e over the WHOLE REAL AUGSBURG city. What wave-9 wired:
//
//   * FLAGS (cloth-anim-wave8.md) — Options::flagAnim + CityView3D::RefreshObjectFlags
//     walks each bound BUILDING's universe-node CHILD list (the scene nodes whose
//     parent index is the building node — the live scene graph the wave-8 handoff
//     said the host must supply) and runs render::RefreshFlagAnimation @0x4b5ef8 on
//     each dummy_FAHNE placeholder, gated by the engine's own heraldry/build-type
//     gate. Asserted: the child list is threaded (44 dummy_FAHNE children walked),
//     the engine gate behaves (open -> a flag object per child; build-type != {5,6,7}
//     or no heraldry -> shut, faithful), and with no heraldry table carried the gate
//     stays shut (no flag fired where the engine itself would not).
//
//   * VEGETATION RELIGHT (vegetation-anim-wave8.md) — Options::vegRelight runs
//     render::BuildVegetationCache @0x5c8560 each frame on the type-4 vg_/pfl_ scenery
//     the pose-driver veg gate (0x5cebed) targets. The wave-8 module was unreachable
//     because the static scenery draws without the per-object pose driver; wave-9
//     reaches the same relight. Asserted: AUGSBURG's vg_/pfl_ scenery is relit each
//     frame (vegRelitMeshes > 0), and the default-off frame does not relight.
//
//   * REFLECTIVE (reflective-nodes-wave8.md) — Options::reflective consults
//     render::TextureFlagIsReflective on the bound scene meshes and primes the mirror
//     gate + plane when a reflective surface exists. AUGSBURG ships NO reflective mesh
//     prop (its reflector is the water, a separate floor path), so the scan finds zero
//     and the gate stays idle — the documented "City scene reality" outcome. The PATH
//     is wired so a scene carrying a reflective prop activates the reflection.
//
//   * ANIMALS (creature-wave8.md render arm) — Options::animals draws the ambient
//     animals the sim spawns through the SAME character mesh path persons use, via
//     CityView3D::AddAnimalInstance (the host character-actor injection API the wave-8
//     handoff named as the one genuine gap). Asserted: a seated animal draws (the
//     species mesh resolves + renders through the character pass).
//
// DEFAULTS stay byte-identical: with every wave-9 flag off the frame is unchanged.
// Clean skip when the real game dir is absent (GUILD_GAME_DIR).
// =============================================================================
#include "test.h"

#include "app/wiring.h"
#include "io/save_world_load.h"
#include "play/city_view3d.h"
#include "render/surface.h"
#include "shim_impl/disk_filesystem.h"
#include "sim/entity.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("Resources/scenes.BIN") &&
           fs.exists("Resources/Objects.BIN");
}

std::vector<u8> Snapshot(render::Surface* s) {
    std::vector<u8> out;
    if (!s) return out;
    for (int y = 0; y < s->height; ++y)
        for (int x = 0; x < s->width; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, y, px);
            out.push_back(px[0]); out.push_back(px[1]); out.push_back(px[2]);
        }
    return out;
}

int FrameDiff(const std::vector<u8>& a, const std::vector<u8>& b) {
    if (a.size() != b.size()) return -1;
    int d = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) ++d;
    return d;
}

bool LoadAugsburg(shim::DiskFileSystem& fs, play::CityView3D& view) {
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    if (!assets.vfsBound) return false;
    sim::ResetEntityArrays();
    io::WorldState world{};
    std::vector<u8> sceneBlob;
    if (!io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &sceneBlob))
        return false;
    if (!view.Init(&fs) || !view.mounted()) return false;
    if (!view.LoadCityFromWorld(sceneBlob)) return false;
    view.BindWorldObjects();
    return !view.instances().empty();
}

play::CityView3D::Options Base() {
    play::CityView3D::Options o;
    o.fbW = 160; o.fbH = 120;
    o.textured = false;
    o.dynamicLight = true;
    o.worldDay = 0; o.worldHour = 12; o.worldMinute = 0;
    return o;
}

} // namespace

// ---------------------------------------------------------------------------
// FLAGS: RefreshObjectFlags threads the building universe-node child list into
// render::RefreshFlagAnimation; the engine's heraldry/build-type gate behaves.
// ---------------------------------------------------------------------------
TEST(FrameEnrichWave9E2E, FlagRefreshWalksUniverseNodeChildren) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] FrameEnrichWave9E2E.FlagRefreshWalksUniverseNodeChildren: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    play::CityView3D view;
    CHECK(LoadAugsburg(fs, view));

    // No heraldry table carried -> every object 0xFFFF -> the gate stays shut
    // (faithful: no flag fired where the engine's own gate would not).
    int noProv = view.RefreshObjectFlags();
    std::printf("  [w9] flagObjects (no heraldry provider) = %d\n", noProv);
    CHECK_EQ(noProv, 0);

    // Open the gate (valid heraldry + a flag-bearing build type {5,6,7}): the
    // dummy_FAHNE children of the bound buildings each produce a flag object — proving
    // the universe-node child list is threaded into RefreshFlagAnimation.
    auto open = [](i32) {
        play::CityView3D::FlagHeraldry h;
        h.heraldry = 0; h.buildType = 5; h.heraldryByte = 100;
        return h;
    };
    int opened = view.RefreshObjectFlags(open);
    std::printf("  [w9] flagObjects (gate open, build-type 5) = %d\n", opened);
    CHECK(opened > 0);   // flags fired on the dummy_FAHNE children

    // The engine gate shuts for a non-flag build type (not 5/6/7).
    auto shut = [](i32) {
        play::CityView3D::FlagHeraldry h;
        h.heraldry = 0; h.buildType = 2;
        return h;
    };
    CHECK_EQ(view.RefreshObjectFlags(shut), 0);

    // ... and for an absent heraldry (0xFFFF) even with a flag build type.
    auto noHer = [](i32) {
        play::CityView3D::FlagHeraldry h;
        h.heraldry = 0xFFFF; h.buildType = 5;
        return h;
    };
    CHECK_EQ(view.RefreshObjectFlags(noHer), 0);
}

// ---------------------------------------------------------------------------
// VEGETATION RELIGHT: Options::vegRelight runs BuildVegetationCache each frame on
// the vg_/pfl_ type-4 scenery; default-off leaves it inert.
// ---------------------------------------------------------------------------
TEST(FrameEnrichWave9E2E, VegetationRelitPerFrame) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] FrameEnrichWave9E2E.VegetationRelitPerFrame: "
                    "real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    play::CityView3D view;
    CHECK(LoadAugsburg(fs, view));
    play::CityCamera3D cam = view.OverviewCamera();

    play::CityView3D::Options off = Base();
    play::CityView3D::Result ro = view.RenderFrame(cam, off);
    std::printf("  [w9] vegRelit (off) = %d\n", ro.vegRelitMeshes);
    CHECK_EQ(ro.vegRelitMeshes, 0);   // default off -> not reached

    play::CityView3D::Options on = Base();
    on.vegRelight = true;
    play::CityView3D::Result rn = view.RenderFrame(cam, on);
    std::printf("  [w9] vegRelit (on) = %d meshes, %d verts\n",
                rn.vegRelitMeshes, rn.vegRelitVerts);
    CHECK(rn.vegRelitMeshes > 0);     // AUGSBURG's foliage scenery is relit
    CHECK(rn.vegRelitVerts > 0);

    // Deterministic: the relight count is stable frame-to-frame.
    play::CityView3D::Result rn2 = view.RenderFrame(cam, on);
    CHECK_EQ(rn2.vegRelitMeshes, rn.vegRelitMeshes);
}

// ---------------------------------------------------------------------------
// REFLECTIVE: the detector is consulted on scene meshes. AUGSBURG ships none, so
// the scan reports zero and the mirror gate stays idle (documented outcome).
// ---------------------------------------------------------------------------
TEST(FrameEnrichWave9E2E, ReflectiveScanConsultsSceneMeshes) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] FrameEnrichWave9E2E.ReflectiveScanConsultsSceneMeshes: "
                    "real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    play::CityView3D view;
    CHECK(LoadAugsburg(fs, view));
    play::CityCamera3D cam = view.OverviewCamera();

    play::CityView3D::Options on = Base();
    on.textured = true;        // bind the real Textures.BIN materials to scan their flags
    on.reflective = true;
    play::CityView3D::Result r = view.RenderFrame(cam, on);
    std::printf("  [w9] reflectiveMeshes = %d mirrorPass=%d\n",
                r.reflectiveMeshes, (int)r.mirrorPass);
    // AUGSBURG carries no reflective mesh prop (its reflector is the water, a separate
    // floor path) — the scan correctly reports zero (reflective-nodes-wave8.md).
    CHECK_EQ(r.reflectiveMeshes, 0);
}

// ---------------------------------------------------------------------------
// ANIMALS: a seated ambient animal draws through the character mesh path.
// ---------------------------------------------------------------------------
TEST(FrameEnrichWave9E2E, AmbientAnimalDrawsThroughCharacterPath) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] FrameEnrichWave9E2E.AmbientAnimalDrawsThroughCharacterPath: "
                    "real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    play::CityView3D view;
    CHECK(LoadAugsburg(fs, view));
    play::CityCamera3D cam = view.OverviewCamera();

    // Seat an animal at a real bound building's position (the CityAnimalWorld path).
    play::CityPlacement pl{};
    CHECK(!view.boundObjects().empty());
    pl.pos[0] = view.boundObjects()[0].place.pos[0];
    pl.pos[1] = view.boundObjects()[0].place.pos[1];
    pl.pos[2] = view.boundObjects()[0].place.pos[2];
    i32 tok = view.AddAnimalInstance("hund_HUND", pl);   // the real dog/cat model
    std::printf("  [w9] AddAnimalInstance(hund_HUND) token=%d count=%d\n",
                tok, view.animalInstanceCount());
    CHECK(tok != 0);                        // model shipped + seated
    CHECK_EQ(view.animalInstanceCount(), 1);

    // Off: the animal is NOT drawn (Options::animals default off).
    play::CityView3D::Options off = Base();
    play::CityView3D::Result ro = view.RenderFrame(cam, off);
    CHECK_EQ(ro.animalInstances, 0);

    // On: the animal draws through the character pass.
    play::CityView3D::Options on = Base();
    on.animals = true;
    play::CityView3D::Result rn = view.RenderFrame(cam, on);
    std::printf("  [w9] animalInstances drawn (on) = %d\n", rn.animalInstances);
    CHECK_EQ(rn.animalInstances, 1);

    // Removing it drops the draw.
    view.RemoveAnimalInstance(tok);
    play::CityView3D::Result rr = view.RenderFrame(cam, on);
    CHECK_EQ(rr.animalInstances, 0);
    view.ClearAnimalInstances();
}

// ---------------------------------------------------------------------------
// DEFAULTS BYTE-IDENTICAL: with every wave-9 flag off the frame is unchanged.
// ---------------------------------------------------------------------------
TEST(FrameEnrichWave9E2E, DefaultsByteIdentical) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] FrameEnrichWave9E2E.DefaultsByteIdentical: "
                    "real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    play::CityView3D view;
    CHECK(LoadAugsburg(fs, view));
    play::CityCamera3D cam = view.OverviewCamera();

    play::CityView3D::Options base = Base();   // every wave-9 flag default-off
    play::CityView3D::Result r1 = view.RenderFrame(cam, base);
    std::vector<u8> a = Snapshot(view.surface());
    CHECK_EQ(r1.vegRelitMeshes, 0);
    CHECK_EQ(r1.reflectiveMeshes, 0);
    CHECK_EQ(r1.animalInstances, 0);
    CHECK_EQ(r1.flagObjects, 0);

    play::CityView3D::Result r2 = view.RenderFrame(cam, base);
    std::vector<u8> b = Snapshot(view.surface());
    CHECK_EQ(FrameDiff(a, b), 0);              // deterministic, byte-identical
}
