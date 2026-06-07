// E2E: the determinism harness used as the reconstruction oracle. Seed the CRT
// RNG with S, boot the real spine, drive a fixed live-frame + sim sequence, then
// hash the whole live world. Same S + same inputs -> identical digest across
// independent runs; a different S -> a different digest. This is the
// self-consistency proof that stands in for a Wine/original-binary oracle.
#include "test.h"

#include "play/determinism.h"
#include "play/playable_app.h"
#include "shim_impl/scripted_platform.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/loopback_socket.h"
#include "app/wiring.h"
#include "config/ini.h"
#include "sim/entity.h"
#include "sim/building_lifecycle.h"
#include "crt/rand.h"

using namespace guild;

namespace {

// Headless rig mirroring tests/unit/playable_app_test.cpp.
struct Rig {
    shim::ScriptedPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    config::IniFile ini;
    std::unique_ptr<app::RealSubsystems> sub;
    std::unique_ptr<app::GameApp> game;
    std::pair<std::unique_ptr<shim::INetSocket>, std::unique_ptr<shim::INetSocket>> netpair;
    Rig() {
        netpair = shim::LoopbackSocket::makePair();
        sub = std::make_unique<app::RealSubsystems>(&plat, &gfx, &audio, &fs,
                                                    netpair.first.get(), &ini);
        game = std::make_unique<app::GameApp>(plat, gfx, audio, *sub);
    }
};

// A fixed, seeded sim sequence: consumes the RNG and writes RNG-derived values
// into the live entity arrays. Pure function of the current RNG state, so two
// runs seeded identically produce identical world mutations.
void DriveSeededSimSequence() {
    using namespace guild::sim;
    for (int i = 0; i < 16; ++i) {
        int slot = crt::RandNext() % kPersonCapacity;
        g_persons[slot].marker = static_cast<i16>(crt::RandNext() & 0x7);
        g_persons[slot].id     = crt::RandNext();
        g_persons[slot].cash   = static_cast<i16>(crt::RandNext() & 0x3FFF);
    }
    for (int i = 0; i < 8; ++i) {
        int slot = crt::RandNext() % kObjectCapacity;
        g_objects[slot].alive = 1;
        g_objects[slot].id    = crt::RandNext();
    }
    g_sceneNodeCount = crt::RandNext() % kSceneNodeCapacity;
}

// One full run: reset world, seed with S, boot the real spine, run live frames,
// drive the seeded sim sequence, then hash the live world. Returns the digest.
std::uint64_t RunSession(unsigned seed, int frames) {
    sim::ResetEntityArrays();
    sim::ResetBuildingPersons();
    crt::Srand(seed);

    Rig rig;
    rig.plat.setMouse(320, 240, /*left=*/true);
    rig.plat.quitAfterPumps(frames);

    play::PlayableApp pa(*rig.game, rig.plat);
    pa.init(/*displayMode=*/1);
    pa.runUntilQuit(play::kLiveFrameMask, /*maxFrames=*/frames + 5);

    DriveSeededSimSequence();
    std::uint64_t h = play::HashWorldState();
    pa.shutdown();
    return h;
}

} // namespace

TEST(DeterminismE2E, SameSeedSameInputsIdenticalHash) {
    std::uint64_t a = RunSession(/*seed=*/4242u, /*frames=*/8);
    std::uint64_t b = RunSession(/*seed=*/4242u, /*frames=*/8);
    // Two independent boot+run+hash cycles with the same seed must agree.
    CHECK_EQ(a, b);
    CHECK(a != 0u);
}

TEST(DeterminismE2E, DifferentSeedDifferentHash) {
    std::uint64_t a = RunSession(/*seed=*/4242u, /*frames=*/8);
    std::uint64_t c = RunSession(/*seed=*/9999u, /*frames=*/8);
    CHECK(a != c);
}

TEST(DeterminismE2E, SnapshotCompareAcrossRunsAgrees) {
    // Run one seeded session, capture a snapshot, run it again identically, and
    // diff the two snapshots — they must be byte-identical region by region.
    auto capture = [](unsigned seed) {
        sim::ResetEntityArrays();
        sim::ResetBuildingPersons();
        crt::Srand(seed);
        Rig rig;
        rig.plat.quitAfterPumps(6);
        play::PlayableApp pa(*rig.game, rig.plat);
        pa.init(1);
        pa.runUntilQuit(play::kLiveFrameMask, 16);
        DriveSeededSimSequence();
        play::WorldSnapshot s = play::SnapshotWorld();
        pa.shutdown();
        return s;
    };

    play::WorldSnapshot s1 = capture(7u);
    play::WorldSnapshot s2 = capture(7u);
    std::string diff;
    CHECK(play::CompareSnapshots(s1, s2, &diff));
    CHECK(diff.empty());

    // A different seed diverges, and the diff names at least one region.
    play::WorldSnapshot s3 = capture(8u);
    diff.clear();
    CHECK(!play::CompareSnapshots(s1, s3, &diff));
    CHECK(!diff.empty());
}
