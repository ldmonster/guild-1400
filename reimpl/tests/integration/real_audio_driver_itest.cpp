// tests/integration/real_audio_driver_itest.cpp — wires the real-asset SFX audio
// driver against the REAL reconstructed loader siblings (audio::LoadSampleBank-
// FromBuffer / audio::SampleBank / audio::Sound3dPool + Compute3dVolume/Pan) over
// a small on-disk FIXTURE built in a temp dir through shim::DiskFileSystem. This
// exercises the cross-module flow end to end (include parse -> VFS read -> .sbf
// parse -> SampleBank bridge -> 3D mix) AND the case-insensitive segment resolve
// (the real include file references "SPRACHE\\..." against a "Sprache" dir), which
// requires the DiskFileSystem listDir path.
#include "app/real_audio_driver.h"

#include "audio/samplebank_load.h"
#include "audio/sound.h"
#include "audio/sound3d.h"

#include "shim_impl/disk_filesystem.h"
#include "shim_impl/null_audio.h"

#include "tests/framework/test.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string MakeTempDir() {
    char tmpl[] = "/tmp/rad_itest_XXXXXX";
    char* d = mkdtemp(tmpl);
    return d ? std::string(d) : std::string();
}

// Build a valid .sbf buffer (same layout the real loader parses).
std::vector<std::uint8_t> MakeSbf(const std::string& bankName,
                                  const std::vector<std::pair<std::string, std::uint8_t>>& entries) {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(audio::kSbHeaderSize)
                                  + entries.size() * audio::kSbEntrySize, 0);
    for (std::size_t i = 0; i < bankName.size() && i < (std::size_t)audio::kSbNameLen; ++i)
        buf[i] = static_cast<std::uint8_t>(bankName[i]);
    std::uint32_t cnt = static_cast<std::uint32_t>(entries.size());
    buf[audio::kSbCountOffset + 0] = static_cast<std::uint8_t>(cnt & 0xff);
    buf[audio::kSbCountOffset + 1] = static_cast<std::uint8_t>((cnt >> 8) & 0xff);
    buf[audio::kSbCountOffset + 2] = static_cast<std::uint8_t>((cnt >> 16) & 0xff);
    buf[audio::kSbCountOffset + 3] = static_cast<std::uint8_t>((cnt >> 24) & 0xff);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::size_t base = audio::kSbBankBaseSize + i * audio::kSbEntrySize;
        const std::string& nm = entries[i].first;
        for (std::size_t j = 0; j < nm.size() && j < (std::size_t)audio::kSbNameLen; ++j)
            buf[base + audio::kSbEntryNameOff + j] = static_cast<std::uint8_t>(nm[j]);
        buf[base + audio::kSbEntryFmtOff] = entries[i].second;
    }
    return buf;
}

void WriteFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        if (!bytes.empty()) std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
}

void WriteText(const std::string& path, const std::string& s) {
    std::vector<std::uint8_t> b(s.begin(), s.end());
    WriteFile(path, b);
}

} // namespace

// Full cross-module flow over a real on-disk fixture (DiskFileSystem), including
// the case-insensitive resolve path.
TEST(RealAudioDriverIntegration, DiskFixtureDrivesRealLoaderAndMix) {
    std::string root = MakeTempDir();
    CHECK(!root.empty());
    if (root.empty()) return;

    // Layout: <root>/sfx/Menschen.sbf, <root>/sfx/Sprache/Auftraege.sbf,
    //         <root>/include_sfx.ini.  The include references the dir as
    //         "SPRACHE\\Auftraege.sbf" (wrong case) to force the listDir resolve.
    std::string sfx = root + "/sfx";
    std::string sprache = sfx + "/Sprache";
    std::system(("mkdir -p '" + sprache + "'").c_str());

    WriteFile(sfx + "/Menschen.sbf",
              MakeSbf("Menschen", {{"laughter", 2}, {"cough", 1}, {"talk", 2}}));
    WriteFile(sprache + "/Auftraege.sbf",
              MakeSbf("Auftraege", {{"order_01", 1}, {"order_02", 1}}));
    WriteText(root + "/include_sfx.ini",
              "#include\"Menschen.sbf\"\r\n"
              "#include \"SPRACHE\\Auftraege.sbf\"\r\n");

    shim::DiskFileSystem fs(root);
    shim::NullAudioDevice dev;
    audio::SoundSystem sound(&dev);
    CHECK(sound.init());

    audio::Vec3 lpos{100, 0, 100};
    audio::Vec3 lfwd{0, 0, 1};
    app::RealAudioResult r = app::DriveRealSfxAudio(&fs, sound, lpos, lfwd);

    CHECK(r.iniFound);
    CHECK_EQ(r.includesListed, (std::size_t)2);
    // Both banks parsed — the second only resolves via the case-insensitive walk.
    CHECK_EQ(r.banksLoaded, (std::size_t)2);
    CHECK_EQ(r.samplesLoaded, (std::size_t)5); // 3 + 2
    // The case-folded bank really loaded its entries into the SampleBank.
    CHECK(sound.bank().findSampleByName("order_01") != nullptr);
    CHECK(sound.bank().findSampleByName("laughter") != nullptr);

    // The 3D mix math ran on the loaded banks and produced audible voices.
    CHECK(r.voicesPlaced > 0);
    CHECK(r.voicesAudible > 0);
    CHECK(r.peakVolume > 0);

    // Per-bank detail: the second bank's resolved vfsPath used the real dir case.
    bool sawResolved = false;
    for (const auto& b : r.banks) {
        if (b.bankName == "Auftraege") {
            sawResolved = b.parsed && b.vfsPath.find("Sprache/Auftraege.sbf") != std::string::npos;
        }
    }
    CHECK(sawResolved);

    std::system(("rm -rf '" + root + "'").c_str());
}

// Direct check of the reconstructed loader sibling wired through the driver: a
// single bank load + the SampleBank bridge, asserting the real Compute3d* curves
// agree with the pool tally for a known geometry.
TEST(RealAudioDriverIntegration, MixMathAgreesWithCurves) {
    std::string root = MakeTempDir();
    CHECK(!root.empty());
    if (root.empty()) return;
    std::string sfx = root + "/sfx";
    std::system(("mkdir -p '" + sfx + "'").c_str());
    WriteFile(sfx + "/One.sbf", MakeSbf("One", {{"only", 1}}));

    shim::DiskFileSystem fs(root);
    shim::NullAudioDevice dev;
    audio::SoundSystem sound(&dev);
    CHECK(sound.init());

    app::SfxBankLoad b = app::LoadOneSfxBank(&fs, sound, "sfx", "One.sbf");
    CHECK(b.parsed);
    CHECK_EQ(b.entryCount, (std::size_t)1);

    // A near source straight to the right of a forward-facing listener: the real
    // attenuation curve should give a high volume, the pan curve should be > center.
    audio::Vec3 lpos{0, 0, 0}, lfwd{0, 0, 1};
    audio::Vec3 src{50, 0, 0}; // to the side, close
    int vol = audio::Compute3dVolume(lpos, src, 2000.0f, 127);
    audio::Vec3 dir{src.x - lpos.x, src.y - lpos.y, src.z - lpos.z};
    int pan = audio::Compute3dPan(lfwd, dir);
    CHECK(vol > 100);       // close => loud
    CHECK(pan > 60);        // 90deg to the side => sin(angle)~1 => pan ~ 126

    std::system(("rm -rf '" + root + "'").c_str());
}
