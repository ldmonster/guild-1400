// tests/e2e/real_audio_driver_e2e_test.cpp — full run of the real-asset SFX audio
// driver over the REAL "Die Gilde" install. GUARDED: if the asset dir is absent
// it prints a [skip] line and returns cleanly (honors GUILD_GAME_DIR).
//
// Drives the real include_sfx.ini + sfx/ tree: parses the real `#include` listing,
// resolves + loads every real .sbf through audio::LoadSampleBankFromBuffer over a
// shim::DiskFileSystem, bridges the indexed entries into a live audio::SampleBank,
// then runs the reconstructed 3D pan/vol mix math on the loaded banks through a
// NullAudioDevice shim. Asserts real banks/samples loaded + audible mix output and
// reports the real counts (banks, samples, voices) observed.
#include "app/real_audio_driver.h"

#include "audio/sound.h"
#include "audio/sound3d.h"

#include "shim_impl/disk_filesystem.h"
#include "shim_impl/null_audio.h"

#include "tests/framework/test.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealSfxAssetsPresent(shim::IFileSystem& fs) {
    return fs.exists("include_sfx.ini") && fs.exists("sfx/fanfaren.sbf");
}

} // namespace

TEST(RealAudioDriverE2E, RealIncludeSfxDrivesBanksAndMix) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!RealSfxAssetsPresent(fs)) {
        std::printf("  [skip] RealAudioDriverE2E.RealIncludeSfxDrivesBanksAndMix: "
                    "real game dir absent (%s)\n", dir.c_str());
        return; // clean skip — no checks recorded
    }

    shim::NullAudioDevice dev;
    audio::SoundSystem sound(&dev);
    CHECK(sound.init());

    // Listener at the world origin facing +Z; the driver spreads SFX emitters
    // around it so the real pan/vol curves run over the loaded banks.
    audio::Vec3 lpos{0, 0, 0};
    audio::Vec3 lfwd{0, 0, 1};
    app::RealAudioResult r =
        app::DriveRealSfxAudio(&fs, sound, lpos, lfwd, "include_sfx.ini", "sfx", /*maxVoices=*/8);

    CHECK(r.iniFound);
    CHECK(r.includesListed > 0);
    CHECK(r.banksLoaded > 0);
    CHECK(r.samplesLoaded > 0);
    // Every listed include should resolve to a real bank (the case-insensitive
    // resolve handles the "SPRACHE\\" / mixed-case directives).
    CHECK_EQ(r.banksLoaded, r.includesListed);
    // The bridged entries landed in the live SampleBank index.
    CHECK_EQ(sound.bank().samples().size(), r.samplesLoaded);

    // The 3D mix math ran for real on the loaded banks.
    CHECK(r.voicesPlaced > 0);
    CHECK(r.voicesAudible > 0);
    CHECK(r.peakVolume > 0);
    CHECK(r.peakVolume <= 127);
    // The mix really reached the device (NullAudioDevice recorded play calls).
    bool sawPlay = false;
    for (const auto& c : dev.calls())
        if (c.kind == shim::NullAudioDevice::CallKind::Play) { sawPlay = true; break; }
    CHECK(sawPlay);

    std::printf("  [real] include_sfx.ini -> %zu includes, %zu banks loaded, "
                "%zu samples indexed; mix: %zu voices placed, %zu audible, "
                "peak vol %d\n",
                r.includesListed, r.banksLoaded, r.samplesLoaded,
                r.voicesPlaced, r.voicesAudible, r.peakVolume);
}
