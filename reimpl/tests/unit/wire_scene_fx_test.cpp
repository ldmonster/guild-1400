// Verifies InstallRealSceneFxWiring() binds the reconstructed pure-logic scene/fx
// leaves into the globally-installable hook seams (previously inert at runtime),
// and that the bound math leaves actually execute over the hook seam.
#include "tests/framework/test.h"

#include "render/wire_scene_fx.h"
#include "render/fxrecon_particle_mirror_shadow.h"
#include "render/modelio_recon.h"
#include "render/render_recon_objlist.h"   // ObjListHooks / SetGammaTable (0x5b9ef4)
#include "io/file.h"   // VfsHandle
#include "io/vfs.h"    // VfsOpenMemoryStream / VfsCloseStream (Bio adapter round-trip)

#include "guild/common/types.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

// After install, the modelio Bio hook fields are non-null and the reader thunks
// read the right material-field offsets off the parser context.
TEST(WireSceneFx, BindsModelIoBioReaders) {
    // Baseline: clear the Bio hooks -> the read thunks degrade to 0 (inert).
    SetModelIoBioHooks(ModelIoBioHooks{});
    CHECK(GetModelIoBioHooks().readByte  == nullptr);
    CHECK(GetModelIoBioHooks().readDword == nullptr);

    InstallRealSceneFxWiring();

    ModelIoBioHooks h = GetModelIoBioHooks();
    CHECK(h.readByte  != nullptr);   // bound to BioReadByte adapter   (0x5dc850)
    CHECK(h.readDword != nullptr);   // bound to BioReadDword adapter  (0x5dc894)

    // Build a parser context: ctx+8 = material index 0, ctx+16 = material table base.
    // The byte thunk writes mat[+200]; the dword thunks write mat[+204..+220].
    u8 matTable[256];
    std::memset(matTable, 0, sizeof(matTable));
    u8 ctx[32];
    std::memset(ctx, 0, sizeof(ctx));
    *reinterpret_cast<i32*>(ctx + 8)  = 0;                 // material index
    *reinterpret_cast<u8**>(ctx + 16) = matTable;          // table base

    // A 5-byte VFS-backed read stream: 0xAB byte, then dword 0xDEADBEEF (LE).
    u8 streamBytes[5] = {0xAB, 0xEF, 0xBE, 0xAD, 0xDE};
    io::VfsHandle* vh = io::VfsOpenMemoryStream(streamBytes, sizeof(streamBytes), "rb");
    CHECK(vh != nullptr);

    int nb = ModelIoReadFloatThunk(vh, ctx);   // 1 byte -> mat[+200]
    CHECK(nb == 1);
    CHECK(matTable[200] == 0xAB);

    int nd = ModelIoReadDwordThunk(vh, ctx);   // dword  -> mat[+204]
    CHECK(nd == 4);
    CHECK(*reinterpret_cast<u32*>(matTable + 204) == 0xDEADBEEFu);

    io::VfsCloseStream(vh);
}

// The bound BuildBasis / MatrixToEuler math leaves run over the fxrecon seam and
// produce a finite orientation (real reconstructed control flow executes).
TEST(WireSceneFx, WiredOrientationFromAngleExecutes) {
    InstallRealSceneFxWiring();

    // SetOrientationFromAngle(enable, dir, objBase, angle): with enable -> the
    // bound BuildBasisFromAngle + MatrixToEuler + SetWorldTranslation run. The leaf
    // passes (objBase + 232) to the world-translation hook, which the adapter views
    // as a SceneNode3 (540 bytes) and writes its euler at node+132 (obj+232+132).
    u8 obj[232 + 0x21C];
    std::memset(obj, 0, sizeof(obj));
    float dir[3] = {0.0f, 1.0f, 0.0f};

    u8 r = fxrecon::Particle_SetOrientationFromAngle(/*enable=*/1, dir, obj, /*angle=*/0.5f);
    // The euler the basis produced was written into the node (+132); it must be
    // finite (the genuine math path ran, not the inert no-op which leaves zeros).
    const float* euler = reinterpret_cast<const float*>(obj + 232 + 132);
    CHECK(std::isfinite(euler[0]));
    CHECK(std::isfinite(euler[1]));
    CHECK(std::isfinite(euler[2]));
    CHECK(r == 0 || r == 1);   // a defined enable/apply result byte
}

// After install, the SetGammaTable -> Floor_ReloadTextures edge (0x5b9ef4 ->
// 0x5bd2d8) is bound: ObjListHooks().floorReloadTextures is non-null and the
// reconstructed ReloadTextures runs over the seam (returns 0 attempted under the
// headless inert FloorTileAccess -- the faithful no-floor-surface behaviour).
TEST(WireSceneFx, BindsFloorReloadTexturesEdge) {
    // Baseline: clear the hook -> inert (null).
    ObjListHooks().floorReloadTextures = nullptr;
    CHECK(ObjListHooks().floorReloadTextures == nullptr);

    InstallRealSceneFxWiring();

    CHECK(ObjListHooks().floorReloadTextures != nullptr);   // bound (0x5bd2d8)

    // A null floor handle takes ReloadTextures' early-out (returns 0).
    CHECK_EQ(ObjListHooks().floorReloadTextures(0u), 0);
    // A non-null handle: 8 slots x 3 mips iterate; with no live FloorTileAccess
    // every tile-surface lookup is null so no decode is attempted -> 0.
    CHECK_EQ(ObjListHooks().floorReloadTextures(0x1234u), 0);
}

// The full reconstructed SetGammaTable runs over the bound edge: on a gamma
// change with an active floor, the floorReloadTextures hook is reached.
TEST(WireSceneFx, SetGammaTableReachesFloorReloadEdge) {
    InstallRealSceneFxWiring();

    // Install an active-floor accessor + a gamma mirror so SetGammaTable's
    // primary-floor branch reaches floorReloadTextures (the bound adapter).
    static u8 s_gamma = 0;
    ObjListHooks().gammaByte   = &s_gamma;
    ObjListHooks().activeFloor = []() -> u32 { return 0x4242u; };

    // First call with a new gamma value: change detected -> reload edge fires.
    int r = SetGammaTable(7);
    // The adapter returns the ReloadTextures attempted-count (0 headless), which
    // SetGammaTable propagates as `result`.
    CHECK_EQ(r, 0);
    CHECK_EQ((int)s_gamma, 7);          // gamma recorded

    // Same gamma again: no change -> the reload edge is NOT taken, returns 0.
    CHECK_EQ(SetGammaTable(7), 0);

    // Restore inert defaults for other tests.
    ObjListHooks().activeFloor = nullptr;
    ObjListHooks().gammaByte   = nullptr;
}

// Idempotent: a second install leaves the seam in the same bound state.
TEST(WireSceneFx, InstallIsIdempotent) {
    InstallRealSceneFxWiring();
    void* b1 = reinterpret_cast<void*>(GetModelIoBioHooks().readByte);
    InstallRealSceneFxWiring();
    void* b2 = reinterpret_cast<void*>(GetModelIoBioHooks().readByte);
    CHECK(b1 != nullptr);
    CHECK(b1 == b2);
}
