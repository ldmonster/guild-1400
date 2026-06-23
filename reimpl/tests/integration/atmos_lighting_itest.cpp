#include "test.h"

// INTEGRATION: the DAY/NIGHT BRIGHTNESS actually changes RENDERED PIXELS via
// the reconstructed lighting-table rebuild — the renderer-consumption link the
// session-atmos slice left pending. Full chain under test (all reconstructed):
//
//   SessionAtmos::Frame (VIBE_DayCycle_UpdateBrightness @0x4b2504)
//     -> band/blend + ambient (VIBE_SkyColor_BlendBandLighting @0x5b85e4 math)
//   play::ApplyAtmosLightingFrame (wire_atmos_bridge)
//     -> render::LightAtmosStoreAmbient   (the flt_64A074/78/7C store half)
//     -> render::LightAtmosRefreshAllObjects @0x5c886c (invalidate + serial)
//   RealCityRenderer::Render(opt.atmosRelight)
//     -> render::LightAtmosEnsureNodeLit  (the 0x5add1c on-screen rebuild arm)
//     -> render::LightAtmosBuildObjectCache @0x5c8218 (ambient -> lightIdx)
//     -> the flat/shaded raster interpolates the rebuilt lightIdx into the fb.
//
// Asset-free: a synthetic stored-PKZIP Resources/Objects.BIN (one real-format
// AGF octahedron) in a MemFileSystem — the SAME mount/decode path the live
// game uses. Asserts: noon and midnight frames DIFFER; re-rendering the same
// state is byte-identical (deterministic); a frame with no brightness rebuild
// applies nothing.
#include "compress/crc.h"
#include "play/real_city_render.h"
#include "play/session_atmos.h"
#include "play/wire_atmos_bridge.h"
#include "render/light_atmos.h"
#include "render/sky.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "sim/entity.h"
#include "sim/types.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

// ---- minimal AGF builder (same grammar the real_city_render itest uses) ----
struct AgfBuilder {
    std::vector<u8> b;
    void byte(u8 v) { b.push_back(v); }
    void u32v(u32 v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
                       b.push_back((v >> 16) & 0xff); b.push_back((v >> 24) & 0xff); }
    void f32v(float f) { u32 bits; std::memcpy(&bits, &f, 4); u32v(bits); }
    void str(const char* s) { while (*s) b.push_back((u8)*s++); b.push_back(0); }
    void magic() { byte('B'); byte('G'); byte('F'); byte(0); }
};

std::vector<u8> BuildOcta(float S) {
    AgfBuilder w;
    w.magic();
    w.byte(0x2e); w.u32v(1);
    w.byte(0x03);
      w.byte(0x04); w.u32v(1);
      w.byte(0x05); w.byte(0x07); w.str("diffuse.tga"); w.byte(0x28);
    w.byte(0x27);
    w.byte(0x14);
      w.byte(0x17);
        w.byte(0x18); w.u32v(6);
        w.byte(0x19); w.u32v(6);
        w.byte(0x1a); w.u32v(8);
        w.byte(0x1b);
          w.f32v(S);  w.f32v(0);  w.f32v(0);
          w.f32v(-S); w.f32v(0);  w.f32v(0);
          w.f32v(0);  w.f32v(S);  w.f32v(0);
          w.f32v(0);  w.f32v(-S); w.f32v(0);
          w.f32v(0);  w.f32v(0);  w.f32v(S);
          w.f32v(0);  w.f32v(0);  w.f32v(-S);
        w.byte(0x1c);
          int tri[8][3] = {{0,2,4},{2,1,4},{1,3,4},{3,0,4},{2,0,5},{1,2,5},{3,1,5},{0,3,5}};
          for (int i = 0; i < 8; ++i) {
            w.byte(0x1d); w.u32v(tri[i][0]); w.u32v(tri[i][1]); w.u32v(tri[i][2]);
            w.byte(0x1e); w.f32v(0); w.f32v(0); w.f32v(0); w.f32v(1); w.f32v(1); w.f32v(0);
                          w.u32v(0); w.u32v(0); w.u32v(0);
            w.byte(0x20); w.byte(0);
          }
        w.byte(0x27);
      w.byte(0x27);
    w.byte(0x27);
    w.byte(0x2b);
    return w.b;
}

// ---- minimal STORED PKZIP writer (the reconstructed ZipArchive accepts it) --
void Put16(std::vector<u8>& b, u32 v) {
    b.push_back((u8)(v & 0xFF)); b.push_back((u8)((v >> 8) & 0xFF));
}
void Put32(std::vector<u8>& b, u32 v) {
    Put16(b, v & 0xFFFF); Put16(b, (v >> 16) & 0xFFFF);
}

std::vector<u8> BuildStoredZip(const std::string& name, const std::vector<u8>& data) {
    std::vector<u8> out;
    u32 crc = compress::CrcCompute(0, data.data(), (u32)data.size());
    u32 sz = (u32)data.size();
    Put32(out, 0x04034b50); Put16(out, 20); Put16(out, 0); Put16(out, 0);
    Put16(out, 0); Put16(out, 0); Put32(out, crc); Put32(out, sz); Put32(out, sz);
    Put16(out, (u32)name.size()); Put16(out, 0);
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), data.begin(), data.end());
    u32 cdStart = (u32)out.size();
    Put32(out, 0x02014b50); Put16(out, 20); Put16(out, 20); Put16(out, 0);
    Put16(out, 0); Put16(out, 0); Put16(out, 0); Put32(out, crc);
    Put32(out, sz); Put32(out, sz); Put16(out, (u32)name.size()); Put16(out, 0);
    Put16(out, 0); Put16(out, 0); Put16(out, 0); Put32(out, 0); Put32(out, 0);
    out.insert(out.end(), name.begin(), name.end());
    u32 cdSize = (u32)out.size() - cdStart;
    Put32(out, 0x06054b50); Put16(out, 0); Put16(out, 0);
    Put16(out, 1); Put16(out, 1); Put32(out, cdSize); Put32(out, cdStart);
    Put16(out, 0);
    return out;
}

// The 7-band sky rig: band 0 (deep night) dark, band 6 (full day) bright, the
// in-between bands a ramp — distinct per band so any band change moves pixels.
void DayNightRig(SessionAtmos& atmos) {
    const float ramp[render::kSkyBands] = {40.0f, 70.0f, 100.0f, 130.0f,
                                           160.0f, 180.0f, 200.0f};
    for (int i = 0; i < render::kSkyBands; ++i)
        atmos.skyBands[i] = render::SkyBandColor{ramp[i], ramp[i], ramp[i]};
    atmos.hasSkyBands = true;
}

std::vector<u8> Snapshot(shim::MemoryGraphicsDevice& dev) {
    return dev.lastPresented();
}

} // namespace

TEST(AtmosLightingItest, BrightnessChangesRenderedFrameDeterministically) {
    // --- a real-format Objects.BIN (stored zip, one octa.bgf) in memory -----
    shim::MemFileSystem fs;
    fs.put("Resources/Objects.BIN", BuildStoredZip("octa.bgf", BuildOcta(20.0f)));

    // --- a small live world --------------------------------------------------
    ResetEntityArrays();
    for (int i = 0; i < 4; ++i) { g_objects[i].alive = 1; g_objects[i].id = 100 + i; }

    render::LightAtmosResetAll();

    RealCityRenderer rc;
    CHECK(rc.Init(&fs));
    CHECK(rc.mounted());

    RealCityRenderer::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    opt.pixelsPerUnit = 0.6f;
    opt.eyeX = opt.originX + 1.5f * opt.cellSize;
    opt.eyeZ = opt.originZ;
    opt.maxObjects = 8;
    opt.atmosRelight = true;          // the consumption gate under test

    // --- the brightness driver ----------------------------------------------
    SessionAtmos atmos;
    DayNightRig(atmos);
    int applied = 0;                  // the cursor over atmos.lightRebuilds

    // FRAME A: NOON. The new-day init's brightness step rebuilds once (the
    // Sky_InitScene tail; lastBrightness inits -100 -> |delta| >= 100).
    sim::GameTime noon{};
    noon.day = 1; noon.hour = 12; noon.minute = 0; noon.second = 0;
    atmos.Frame(noon, /*seed=*/1234u, /*nowMs=*/1000u);
    CHECK_EQ(atmos.lightRebuilds, 1);
    AtmosLightingApplyResult apA = ApplyAtmosLightingFrame(atmos, 1000, applied);
    CHECK(apA.ambientStored);
    CHECK(apA.refreshed);
    CHECK_EQ(apA.rebuilds, 1);
    CHECK_EQ(apA.serial, 1u);

    shim::MemoryGraphicsDevice devA;
    CHECK(devA.init(opt.fbW, opt.fbH, 16, false));
    RealCityRenderer::Result rA = rc.Render(opt, devA);
    CHECK(rA.mounted);
    CHECK(rA.meshObjects > 0);
    CHECK(rA.relitMeshes > 0);        // the lazy rebuild arm fired
    CHECK_EQ(rA.lightSerial, 1u);
    CHECK(rA.nonClearPixels > 0);
    std::vector<u8> frameA = Snapshot(devA);
    CHECK(!frameA.empty());

    // FRAME B: MIDNIGHT. Big delta -> rebuild again with the night ambient.
    sim::GameTime night{};
    night.day = 1; night.hour = 0; night.minute = 30; night.second = 0;
    atmos.Frame(night, 1234u, 2000u);
    CHECK(atmos.lightRebuilds > 1);
    CHECK(atmos.brightness != 600 || atmos.band != 6);  // moved off full day
    AtmosLightingApplyResult apB = ApplyAtmosLightingFrame(atmos, 2000, applied);
    CHECK(apB.refreshed);
    CHECK(apB.serial >= 2u);
    const u32 serialB = apB.serial;

    shim::MemoryGraphicsDevice devB;
    CHECK(devB.init(opt.fbW, opt.fbH, 16, false));
    RealCityRenderer::Result rB = rc.Render(opt, devB);
    CHECK(rB.relitMeshes > 0);        // relit AGAIN against the new ambient
    CHECK_EQ(rB.lightSerial, serialB);
    std::vector<u8> frameB = Snapshot(devB);

    // THE CLAIM: two brightness levels -> two DIFFERENT deterministic frames.
    CHECK_EQ(frameA.size(), frameB.size());
    CHECK(frameA != frameB);

    // DETERMINISM: same lighting state again -> byte-identical frame; and no
    // further rebuild runs (serial already current on every mesh).
    shim::MemoryGraphicsDevice devB2;
    CHECK(devB2.init(opt.fbW, opt.fbH, 16, false));
    RealCityRenderer::Result rB2 = rc.Render(opt, devB2);
    CHECK_EQ(rB2.relitMeshes, 0);
    std::vector<u8> frameB2 = Snapshot(devB2);
    CHECK(frameB == frameB2);

    ResetEntityArrays();
    render::LightAtmosResetAll();
}

// A frame whose brightness step skipped the rebuild (hysteresis) applies
// NOTHING to the lighting table — exactly the original's skip arm.
TEST(AtmosLightingItest, NoRebuildFrameAppliesNothing) {
    render::LightAtmosResetAll();
    SessionAtmos atmos;
    DayNightRig(atmos);
    int applied = 0;
    sim::GameTime t{};
    t.day = 1; t.hour = 12; t.minute = 0; t.second = 0;
    atmos.Frame(t, 99u, 100u);              // first frame: the init rebuild
    ApplyAtmosLightingFrame(atmos, 100, applied);
    u32 serial = render::LightAtmos().rebuildSerial;
    CHECK_EQ(serial, 1u);

    t.second = 30;                          // same minute: settled, |delta| < 10
    atmos.Frame(t, 99u, 200u);
    CHECK(!atmos.lightingRebuilt);
    AtmosLightingApplyResult ap = ApplyAtmosLightingFrame(atmos, 200, applied);
    CHECK(!ap.refreshed);
    CHECK(!ap.ambientStored);
    CHECK_EQ(render::LightAtmos().rebuildSerial, serial);   // untouched
    render::LightAtmosResetAll();
}

// The OFF gate: an identical render without atmosRelight leaves the lighting
// registry untouched (additive wiring; existing behaviour unchanged).
TEST(AtmosLightingItest, GateOffTouchesNothing) {
    shim::MemFileSystem fs;
    fs.put("Resources/Objects.BIN", BuildStoredZip("octa.bgf", BuildOcta(20.0f)));
    ResetEntityArrays();
    g_objects[0].alive = 1; g_objects[0].id = 7;
    render::LightAtmosResetAll();

    RealCityRenderer rc;
    CHECK(rc.Init(&fs));
    RealCityRenderer::Options opt;
    opt.fbW = 96; opt.fbH = 64;
    opt.pixelsPerUnit = 0.6f;
    opt.maxObjects = 4;
    // atmosRelight stays false.
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(opt.fbW, opt.fbH, 16, false));
    RealCityRenderer::Result r = rc.Render(opt, dev);
    CHECK(r.mounted);
    CHECK_EQ(r.relitMeshes, 0);
    CHECK_EQ(r.lightSerial, 0u);
    CHECK_EQ(render::LightAtmosRegisteredCount(), 0);
    ResetEntityArrays();
    render::LightAtmosResetAll();
}
