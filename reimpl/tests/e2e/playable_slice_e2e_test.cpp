// tests/e2e/playable_slice_e2e_test.cpp — GUARDED real-asset PLAYABLE SLICE on
// AUGSBURG. The full end-to-end proof:
//
//   load AUGSBURG -> render frame 1 (BMP) -> scripted world click as a unit ORDER
//   (mutates a live object) -> advance one game-day (real economy passes) -> render
//   frame 2 (BMP) -> assert the two frames DIFFER (the city changed) AND the whole
//   run is byte-identical on rerun (deterministic).
//
// Skips cleanly when the shipped AUGSBURG.cty asset is absent (honors
// GUILD_GAME_DIR).
#include "test.h"

#include "play/playable_slice.h"
#include "play/input_command.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/filedump_graphics.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

SliceClick AugsburgClick() {
    SliceClick c;
    c.sx = 0.0f; c.sy = 0.0f;            // aim at the first live object
    c.pickRadius = 64.0f;
    c.mode = CursorMode::kConquer;        // WARE conquer order (kind 6): enqueues +
                                          // applies, mutating the picked object record
    c.attackAllowed = false;
    return c;
}

// Run the full slice once into two FileDump devices dumping with `prefix`.
SliceResult RunOnce(const char* prefix1, const char* prefix2,
                    std::vector<std::uint8_t>& frame1Bytes,
                    std::vector<std::uint8_t>& frame2Bytes) {
    const int W = 128, H = 96;
    shim::DiskFileSystem fs(GameDir());

    shim::FileDumpGraphicsDevice dev1, dev2;
    dev1.init(W, H, 16, false);
    dev2.init(W, H, 16, false);
    dev1.configureDump("/tmp", prefix1, shim::FileDumpGraphicsDevice::kBmp);
    dev2.configureDump("/tmp", prefix2, shim::FileDumpGraphicsDevice::kBmp);

    SliceResult r = RunPlayableSlice(&fs, GameDir(), "Augsburg", AugsburgClick(),
                                     /*econSeed=*/0xA065B, W, H, &dev1, &dev2);
    frame1Bytes = dev1.lastPresented();
    frame2Bytes = dev2.lastPresented();
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// The full slice on AUGSBURG: two BMPs, frames differ, deterministic on rerun.
// ---------------------------------------------------------------------------
TEST(PlayableSliceE2E, AugsburgFullSliceFramesDifferDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] PlayableSliceE2E: real game dir absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }
    std::printf("[slice-e2e] asset dir: %s\n", GameDir().c_str());

    std::vector<std::uint8_t> f1a, f2a;
    SliceResult r = RunOnce("guild_slice_f1", "guild_slice_f2", f1a, f2a);

    std::printf("[slice-e2e] loaded=%d persons=%u objects=%u | frame1 obj=%d nonclear=%d "
                "| cmd issued=%d enqueued=%d kind=%d target=%d | econPasses=%d "
                "| frame2 obj=%d nonclear=%d\n",
                (int)r.loaded, r.personCount, r.objectCount,
                r.frame1Objects, r.frame1NonClear,
                (int)r.commandIssued, (int)r.commandEnqueued, r.commandKind,
                r.commandTarget, r.economyPasses,
                r.frame2Objects, r.frame2NonClear);
    std::printf("[slice-e2e] hashAfterLoad=%llu hashAfterCommand=%llu hashAfterDay=%llu\n",
                (unsigned long long)r.hashAfterLoad,
                (unsigned long long)r.hashAfterCommand,
                (unsigned long long)r.hashAfterDay);
    std::printf("[slice-e2e] frame1 BMP -> %s\n", r.frame1Path.c_str());
    std::printf("[slice-e2e] frame2 BMP -> %s\n", r.frame2Path.c_str());

    CHECK(r.loaded);
    if (!r.loaded) { io::VfsShutdown(); return; }

    // The shipped Augsburg seed (stable across the asset).
    CHECK_EQ(r.personCount, (u32)1);
    CHECK_EQ(r.objectCount, (u32)55);

    // Both frames rendered the real world.
    CHECK(r.frame1Rendered);
    CHECK(r.frame2Rendered);
    CHECK(r.frame1Objects > 0);
    CHECK(r.frame1NonClear > 0);
    CHECK(r.frame2Objects > 0);
    CHECK(r.frame2NonClear > 0);
    // The conquered building despawned, so the painted object footprint changes.
    CHECK(r.frame2NonClear != r.frame1NonClear);

    // The click issued a real order onto a live object, and it mutated the world.
    CHECK(r.commandIssued);
    CHECK(r.commandChangedWorld());

    // The game-day ran the real economy passes.
    CHECK(r.economyPasses > 0);

    // THE CENTRAL PROOF: the world changed between frame 1 and frame 2.
    CHECK(r.worldChanged());
    CHECK(r.hashAfterLoad != 0u);
    CHECK(r.ok());

    // The two frames differ at the pixel level (the city changed).
    bool framesDiffer = (f1a != f2a);
    std::printf("[slice-e2e] frame1 vs frame2 differ = %d (f1=%zu f2=%zu bytes)\n",
                (int)framesDiffer, f1a.size(), f2a.size());
    CHECK(framesDiffer);

    // Determinism: a full rerun reproduces identical hashes + identical frames.
    std::vector<std::uint8_t> f1b, f2b;
    SliceResult r2 = RunOnce("guild_slice2_f1", "guild_slice2_f2", f1b, f2b);
    std::printf("[slice-e2e] rerun hashAfterLoad=%llu hashAfterDay=%llu\n",
                (unsigned long long)r2.hashAfterLoad,
                (unsigned long long)r2.hashAfterDay);
    CHECK_EQ(r.hashAfterLoad, r2.hashAfterLoad);
    CHECK_EQ(r.hashAfterCommand, r2.hashAfterCommand);
    CHECK_EQ(r.hashAfterDay, r2.hashAfterDay);
    CHECK(f1a == f1b);    // byte-identical frame 1 across reruns
    CHECK(f2a == f2b);    // byte-identical frame 2 across reruns

    io::VfsShutdown();
}
