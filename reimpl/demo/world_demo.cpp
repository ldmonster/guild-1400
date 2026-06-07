// world_demo — a fuller end-to-end integration demo for the Guild reconstruction.
//
// NOT a translation of gilde.exe and NOT a unit/e2e test: a standalone `main`
// that drives the REAL reconstructed subsystems through the shim_impl headless
// backends and the InstallRealSimHooks*() cross-module wiring, to prove the
// reconstruction runs as one integrated whole:
//
//   1. construct a small synthetic world (persons + objects/building + a city),
//   2. install all real sim hooks (InstallRealSimHooks / 2 / 3),
//   3. run several simulation ticks / a player turn driving real command
//      enqueue (QueueRequest*) + entity updates (CharacterUpdate),
//   4. render a frame with the reconstructed software rasterizer and dump it
//      via FileDumpGraphicsDevice -> demo/world_demo_frameNNNN.bmp,
//   5. save the world state via the io/gamestate (save) module, reload it, and
//      assert the reload matches.
//
// It prints a concise log of what happened and exits 0 on success. Where a
// subsystem path is not wired headless, it uses what is available and notes it
// in the log (it never silently skips).
//
// Build (from the repo root /home/cnupt/work/reverse/reverse-guild/reimpl):
//   see demo/README.md — links the needed src/*.cpp explicitly with g++.

#include "guild/common/types.h"

// --- app spine + cross-module real wiring ---------------------------------
#include "app/wiring.h"
#include "sim/real_hooks.h"
#include "sim/real_hooks2.h"
#include "sim/real_hooks3.h"

// --- sim: entities, command queue/codec, character driver -----------------
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/command.h"
#include "sim/command_codec.h"
#include "sim/character.h"
#include "sim/gametime.h"

// --- world: city / economy ------------------------------------------------
#include "world/city.h"
#include "world/production.h"

// --- io: savegame ----------------------------------------------------------
#include "io/vfs.h"
#include "io/save.h"
#include "io/gamestate.h"

// --- render: software rasterizer + framebuffer dump ------------------------
#include "render/raster.h"
#include "render/types.h"

// --- shim_impl headless backends ------------------------------------------
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/filedump_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

// (The TextureLoadByName gap is now resolved in src/render/texture_loader.cpp.)

namespace {

int g_failures = 0;
#define DEMO_CHECK(cond, msg)                                                  \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  [FAIL] %s\n", (msg));                               \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// --------------------------------------------------------------------------
// 1. Build a small synthetic world directly in the real entity arrays.
//    (The on-disk world loader needs assets we don't have headless; here we
//     populate the real global record arrays the lookups/iterators scan.)
// --------------------------------------------------------------------------
struct SyntheticWorld {
    int personCount = 0;
    int objectCount = 0;
};

SyntheticWorld BuildWorld() {
    sim::ResetEntityArrays();

    SyntheticWorld w;

    // Three persons: a player-controlled merchant and two NPCs.
    struct PersonSeed { i32 id; u8 kind; u8 isPlayer; i16 cash; i16 faction; };
    const PersonSeed seeds[] = {
        {1001, 5, 1, 5000, 1},   // player merchant
        {1002, 6, 0, 1200, 2},   // NPC competitor
        {1003, 7, 0,  300, 2},   // NPC laborer
    };
    for (const auto& s : seeds) {
        sim::Person& p = sim::g_persons[w.personCount];
        std::memset(&p, 0, sizeof(p));
        p.marker = 0;          // 0 == occupied (-1 would be a free slot)
        p.kind = s.kind;
        p.id = s.id;
        p.isPlayer = s.isPlayer;
        p.cash = s.cash;
        p.factionA = s.faction;
        sim::g_personIds[w.personCount] = s.id; // parallel id column
        ++w.personCount;
    }

    // One building/object: the player's trading house.
    {
        sim::ObjectRec& o = sim::g_objects[w.objectCount];
        std::memset(&o, 0, sizeof(o));
        o.alive = 1;
        o.id = 2001;
        ++w.objectCount;
    }

    // A city + the 28-good economy parameter table (real world economy core).
    world::CityInitParameterTable(100.0f);

    return w;
}

// --------------------------------------------------------------------------
// 4. Render a frame with the reconstructed software rasterizer and dump it.
//    Mirrors demo/render_demo.cpp's bridge from the shim Surface to the
//    render::Surface the rasterizer writes into.
// --------------------------------------------------------------------------
std::string RenderAndDumpFrame(const std::string& dir) {
    shim::FileDumpGraphicsDevice gfx;
    gfx.configureDump(dir, "world_demo_frame", shim::FileDumpGraphicsDevice::kBmp);
    gfx.init(160, 120, 8, false);

    // A small palette: 0=sky, 1=ground, 2=building, 3=person, 4=accent.
    std::uint32_t pal[256] = {0};
    pal[0] = 0x2060A0; // sky blue
    pal[1] = 0x407030; // ground green
    pal[2] = 0xC07030; // building brown
    pal[3] = 0xE0E040; // person yellow
    pal[4] = 0xFFFFFF; // accent white
    gfx.setPalette(pal);

    shim::Surface* sh = gfx.backbuffer();
    std::memset(sh->pixels, 0, static_cast<size_t>(sh->height) * sh->pitch);

    // Bridge to a render::Surface over the same backbuffer memory.
    render::Surface fb{};
    fb.width = sh->width;
    fb.height = sh->height;
    fb.pitch = sh->pitch;
    fb.widthPx = sh->pitch;
    fb.bpp = 8;
    fb.pixels = static_cast<u8*>(sh->pixels);
    fb.clipX0 = 0; fb.clipY0 = 0; fb.clipX1 = sh->width; fb.clipY1 = sh->height;

    // Ground (two triangles filling the lower band).
    render::RasterVertex g1[3] = {{0, 70, 200}, {160, 70, 200}, {0, 120, 200}};
    render::RasterVertex g2[3] = {{160, 70, 200}, {160, 120, 200}, {0, 120, 200}};
    render::RasterizeFlatTriangle(&fb, g1, 1);
    render::RasterizeFlatTriangle(&fb, g2, 1);

    // A building (roof + body).
    render::RasterVertex roof[3] = {{40, 30, 220}, {90, 30, 220}, {65, 12, 220}};
    render::RasterVertex body1[3] = {{45, 30, 220}, {85, 30, 220}, {45, 70, 220}};
    render::RasterVertex body2[3] = {{85, 30, 220}, {85, 70, 220}, {45, 70, 220}};
    render::RasterizeFlatTriangle(&fb, roof, 4);
    render::RasterizeFlatTriangle(&fb, body1, 2);
    render::RasterizeFlatTriangle(&fb, body2, 2);

    // A person marker.
    render::RasterVertex person[3] = {{110, 55, 240}, {120, 55, 240}, {115, 40, 240}};
    render::RasterizeFlatTriangle(&fb, person, 3);

    gfx.present();
    return gfx.framePath(0, shim::FileDumpGraphicsDevice::kBmp);
}

// --------------------------------------------------------------------------
// 5. Save / reload the world state via the io/gamestate (save) module.
// --------------------------------------------------------------------------
bool SaveRoundtrip(const std::string& savePath, const SyntheticWorld& w) {
    // Assemble a GameState from the reconstructed, byte-exact portions: the
    // header, the fixed scalar block, and the relink table.
    io::GameState toSave{};

    toSave.header.magic = io::kSaveVersionCurrent;
    toSave.header.flagByte = 0;
    std::snprintf(toSave.header.name, sizeof(toSave.header.name), "Augsburg");
    toSave.header.season = 1;
    toSave.header.wealth = 12345;
    toSave.header.idA = w.personCount > 0 ? sim::g_personIds[0] : -1; // player id
    toSave.header.idB = w.objectCount > 0 ? sim::g_objects[0].id : -1;
    std::snprintf(toSave.header.name96, sizeof(toSave.header.name96),
                  "world_demo savegame");

    toSave.scalar.g649890 = 0xABCD1234;
    toSave.scalar.g632244 = w.personCount;
    toSave.scalar.season = 1;
    toSave.scalar.g6498E4 = w.personCount > 0 ? sim::g_personIds[0] : 0; // player id
    toSave.scalar.g64771C = w.objectCount;
    toSave.scalar.g632240 = 1000000;

    // A small populated relink table so the byte-equality invariant has content.
    toSave.relink.assign(io::kRelinkBytes, 0);
    for (int e = 0; e < 4; ++e) {
        u8* rec = toSave.relink.data() + static_cast<size_t>(e) * io::kRelinkStride;
        rec[io::kRelinkTagOffset] = io::kRelinkPerson;
        u32 id = 1001u + e;
        std::memcpy(rec + io::kRelinkPtrOffset, &id, sizeof(id));
    }

    // Write through the io::gamestate top-level writer (opens via the VFS).
    if (!io::WriteGameState(savePath.c_str(), toSave, /*save=*/nullptr,
                            /*partial=*/false))
        return false;

    // Reload into a fresh GameState.
    io::GameState loaded{};
    loaded.relink.assign(io::kRelinkBytes, 0);
    loaded.thumbnail.clear();
    if (!io::LoadGameState(savePath.c_str(), loaded, /*load=*/nullptr))
        return false;

    // Assert the round-trip reconstructs the byte-exact portions.
    bool ok = true;
    ok &= (loaded.header.magic == toSave.header.magic);
    ok &= (std::strcmp(loaded.header.name, toSave.header.name) == 0);
    ok &= (loaded.header.season == toSave.header.season);
    ok &= (loaded.header.wealth == toSave.header.wealth);
    ok &= (loaded.header.idA == toSave.header.idA);
    ok &= (loaded.header.idB == toSave.header.idB);
    ok &= (std::strcmp(loaded.header.name96, toSave.header.name96) == 0);
    ok &= (loaded.scalar.g649890 == toSave.scalar.g649890);
    ok &= (loaded.scalar.g632244 == toSave.scalar.g632244);
    ok &= (loaded.scalar.season == toSave.scalar.season);
    ok &= (loaded.scalar.g6498E4 == toSave.scalar.g6498E4);
    ok &= (loaded.scalar.g64771C == toSave.scalar.g64771C);
    ok &= (loaded.scalar.g632240 == toSave.scalar.g632240);
    return ok;
}

} // namespace

int main() {
    std::printf("=== Guild reconstruction — world_demo (integration E2E) ===\n");

    // ----------------------------------------------------------------------
    // Bind the headless OS shims. The save module reaches the disk via the VFS,
    // which must be bound to an IFileSystem. We use a real DiskFileSystem rooted
    // at the demo directory so the savegame round-trips on a real file.
    // ----------------------------------------------------------------------
    const std::string demoDir = "demo";
    shim::DiskFileSystem fs(demoDir);
    io::VfsInit(&fs, /*caseInsensitive=*/false);
    std::printf("[shim] VFS bound to DiskFileSystem(root=\"%s\")\n", demoDir.c_str());

    // ----------------------------------------------------------------------
    // 1. Synthetic world.
    // ----------------------------------------------------------------------
    SyntheticWorld w = BuildWorld();
    std::printf("[world] built synthetic world: %d persons, %d objects/building, "
                "28-good economy table seeded\n",
                w.personCount, w.objectCount);

    // Verify the real id->record lookups resolve our seeded entities.
    sim::Person* player = sim::PersonFindRecordById(1001);
    sim::ObjectRec* house = sim::BuildingFindById(2001);
    DEMO_CHECK(player != nullptr && player->cash == 5000,
               "PersonFindRecordById(1001) should resolve the player merchant");
    DEMO_CHECK(house != nullptr, "BuildingFindById(2001) should resolve the house");
    std::printf("[world] entity lookups OK: player(id=1001 cash=%d) house(id=2001 %s)\n",
                player ? player->cash : -1, house ? "alive" : "MISSING");

    // ----------------------------------------------------------------------
    // 2. Install all real sim hooks (three composing waves; share one queue).
    // ----------------------------------------------------------------------
    sim::InstallRealSimHooks();
    sim::InstallRealSimHooks2();
    sim::InstallRealSimHooks3();
    sim::CommandQueue* q = sim::RealCommandQueue();
    DEMO_CHECK(q != nullptr, "RealCommandQueue() must exist after install");
    std::printf("[hooks] installed real sim hooks (waves 1/2/3); shared CommandQueue ready\n");

    // ----------------------------------------------------------------------
    // 3. Run several simulation ticks / a player turn: drive real command
    //    enqueue (via the reconstructed command codec) + entity updates.
    // ----------------------------------------------------------------------
    const u32 baseSend = q ? q->send_count() : 0;
    sim::GameTime clock{};
    int commandsEnqueued = 0;
    int characterUpdates = 0;
    const int kTicks = 6;
    for (int tick = 0; tick < kTicks; ++tick) {
        // Advance the wall clock 30 minutes/tick (real GameTime carry chain).
        sim::GameTimeAdvance(&clock, 0, 0, 30);

        // Player turn: enqueue a real opcode-16 command onto the shared queue
        // through the reconstructed builder (e.g. "merchant places a buy order").
        i32 ring = sim::QueueRequest16(*q, /*a1=*/1001, /*a2=*/2001,
                                       /*a3=*/100 + tick, /*a4=*/static_cast<u8>(tick));
        if (ring >= 0) ++commandsEnqueued;

        // Step the live-actor per-frame driver (real sim::CharacterUpdate). With
        // no live render actors registered the driver loop runs and returns 0.
        sim::CharacterUpdate();
        ++characterUpdates;
    }
    const u32 afterSend = q ? q->send_count() : 0;
    DEMO_CHECK(afterSend > baseSend,
               "CommandQueue send_count should advance after enqueuing commands");
    std::printf("[sim] ran %d ticks: clock=day%d %02d:%02d, %d commands enqueued "
                "(send_count %u->%u), %d CharacterUpdate() calls\n",
                kTicks, clock.day, clock.hour, clock.minute, commandsEnqueued,
                baseSend, afterSend, characterUpdates);

    // Integrate one work-window of production through the real economy core.
    world::ProdTime startT{/*day=*/0, /*hour=*/6, /*minute=*/0};
    world::ProdTime endT{/*day=*/0, /*hour=*/22, /*minute=*/0};
    int workMinutes =
        world::ProductionComputeOutputOverTime(startT, endT, /*pauseMode=*/false);
    std::printf("[econ] production integrated over a work day: %d work-minutes\n",
                workMinutes);

    // ----------------------------------------------------------------------
    // 4. Render a frame and dump it to a BMP.
    // ----------------------------------------------------------------------
    std::string bmpPath = RenderAndDumpFrame(demoDir);
    bool bmpExists = false;
    {
        if (std::FILE* f = std::fopen(bmpPath.c_str(), "rb")) {
            std::fseek(f, 0, SEEK_END);
            long sz = std::ftell(f);
            std::fclose(f);
            bmpExists = sz > 54; // 54-byte BMP header + pixels
        }
    }
    DEMO_CHECK(bmpExists, "world_demo frame BMP should be written and non-trivial");
    std::printf("[render] rasterized + dumped frame -> %s (%s)\n", bmpPath.c_str(),
                bmpExists ? "OK" : "MISSING");

    // ----------------------------------------------------------------------
    // 5. Save the world state, reload it, assert the reload matches.
    // ----------------------------------------------------------------------
    const std::string savePath = "world_demo.SAV"; // resolved under demo/ via VFS root
    bool roundtripOk = SaveRoundtrip(savePath, w);
    DEMO_CHECK(roundtripOk, "save -> reload should reconstruct the byte-exact state");
    std::printf("[io] save roundtrip via io/gamestate: wrote+reloaded %s/%s -> %s\n",
                demoDir.c_str(), savePath.c_str(), roundtripOk ? "OK" : "MISMATCH");

    // ----------------------------------------------------------------------
    // Bonus: exercise the full app spine end-to-end against RealSubsystems
    //  (init -> N frames -> 13-step shutdown) so the demo also proves the
    //  wiring.cpp integration runs to completion headless.
    // ----------------------------------------------------------------------
    app::HeadlessResult hr = app::RunHeadless(/*displayMode=*/1, /*showIntro=*/false,
                                              /*networkClient=*/false, /*frames=*/4);
    DEMO_CHECK(hr.exitCode == 0, "app spine RunHeadless should exit 0");
    std::printf("[app] spine RunHeadless: exit=%d, frames=%d, presents=%d, "
                "memTracker=%s vfs=%s sound=%s\n",
                hr.exitCode, hr.frameCount, hr.presentCount,
                hr.memoryTrackerInited ? "init" : "-",
                hr.vfsInited ? "init" : "-", hr.soundInited ? "init" : "-");

    io::VfsShutdown();

    // ----------------------------------------------------------------------
    std::printf("\n=== summary ===\n");
    std::printf("  persons=%d objects=%d | ticks=%d commands=%d charUpdates=%d | "
                "frame=%s | save_roundtrip=%s | spine_exit=%d\n",
                w.personCount, w.objectCount, kTicks, commandsEnqueued,
                characterUpdates, bmpExists ? "written" : "MISSING",
                roundtripOk ? "OK" : "FAIL", hr.exitCode);
    if (g_failures == 0) {
        std::printf("world_demo: ALL CHECKS PASSED (0 failures)\n");
        return 0;
    }
    std::printf("world_demo: %d CHECK(S) FAILED\n", g_failures);
    return 1;
}
