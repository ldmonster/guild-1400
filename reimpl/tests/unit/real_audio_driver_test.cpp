// tests/unit/real_audio_driver_test.cpp — drives the real-asset SFX audio driver
// over small SYNTHETIC in-format bytes (no real assets): a hand-built include
// listing + hand-built .sbf banks injected into a MemFileSystem. Asserts the
// include parse, the path normalize, the per-bank load (LoadSampleBankFromBuffer),
// the SampleBank bridge, and the 3D mix tally are all deterministic.
#include "app/real_audio_driver.h"

#include "audio/samplebank_load.h"
#include "audio/sound.h"

#include "shim_impl/mem_filesystem.h"
#include "shim_impl/null_audio.h"

#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild;

namespace {

// Build a minimal valid .sbf buffer: a 0x144-byte header (name@0, entryCount@0x134)
// followed by `names.size()` x 0x40-byte entry records (name@+4, format@+0x36).
std::vector<u8> MakeSbf(const std::string& bankName,
                        const std::vector<std::pair<std::string, u8>>& entries) {
    std::vector<u8> buf(static_cast<std::size_t>(audio::kSbHeaderSize)
                        + entries.size() * audio::kSbEntrySize, 0);
    // bank name at +0
    for (std::size_t i = 0; i < bankName.size() && i < (std::size_t)audio::kSbNameLen; ++i)
        buf[i] = static_cast<u8>(bankName[i]);
    // entryCount at +0x134 (LE32)
    u32 cnt = static_cast<u32>(entries.size());
    buf[audio::kSbCountOffset + 0] = static_cast<u8>(cnt & 0xff);
    buf[audio::kSbCountOffset + 1] = static_cast<u8>((cnt >> 8) & 0xff);
    buf[audio::kSbCountOffset + 2] = static_cast<u8>((cnt >> 16) & 0xff);
    buf[audio::kSbCountOffset + 3] = static_cast<u8>((cnt >> 24) & 0xff);
    // entries
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::size_t base = audio::kSbBankBaseSize + i * audio::kSbEntrySize;
        const std::string& nm = entries[i].first;
        for (std::size_t j = 0; j < nm.size() && j < (std::size_t)audio::kSbNameLen; ++j)
            buf[base + audio::kSbEntryNameOff + j] = static_cast<u8>(nm[j]);
        buf[base + audio::kSbEntryFmtOff] = entries[i].second;
    }
    return buf;
}

} // namespace

TEST(RealAudioDriverUnit, ParseIncludeListBothForms) {
    // The real file mixes `#include"x"` (no space) and `#include "x"` (space),
    // plus blank/garbage lines. We must capture exactly the quoted rel paths.
    std::string text =
        "#include\"Lebewesen\\Menschen.sbf\"\r\n"
        "#include \"Locations\\gb_Kirche.sbf\"\n"
        "; a comment line with no include\n"
        "#include \"fanfaren.sbf\"\n"
        "garbage #include with no quote\n";
    auto v = app::ParseSfxIncludeList(text);
    CHECK_EQ(v.size(), (std::size_t)3);
    if (v.size() == 3) {
        CHECK(v[0] == "Lebewesen\\Menschen.sbf");
        CHECK(v[1] == "Locations\\gb_Kirche.sbf");
        CHECK(v[2] == "fanfaren.sbf");
    }
}

TEST(RealAudioDriverUnit, NormalizeSfxPath) {
    CHECK(app::NormalizeSfxPath("sfx", "Locations\\gb_Kirche.sbf")
          == "sfx/Locations/gb_Kirche.sbf");
    CHECK(app::NormalizeSfxPath("sfx", "fanfaren.sbf") == "sfx/fanfaren.sbf");
    // Doubled backslash (the real "SPRACHE\\AUFTRAEGE.sbf") collapses.
    CHECK(app::NormalizeSfxPath("sfx", "SPRACHE\\\\AUFTRAEGE.sbf")
          == "sfx/SPRACHE/AUFTRAEGE.sbf");
    // Trailing-slash root is not doubled.
    CHECK(app::NormalizeSfxPath("sfx/", "a.sbf") == "sfx/a.sbf");
}

TEST(RealAudioDriverUnit, LoadOneSyntheticBankBridgesEntries) {
    shim::MemFileSystem fs;
    fs.put("sfx/Test.sbf",
           MakeSbf("TestBank", {{"klick", 1}, {"markt", 2}, {"step", 1}}));

    shim::NullAudioDevice dev;
    audio::SoundSystem sound(&dev);
    CHECK(sound.init());

    app::SfxBankLoad b = app::LoadOneSfxBank(&fs, sound, "sfx", "Test.sbf");
    CHECK(b.opened);
    CHECK(b.parsed);
    CHECK(b.bankName == "TestBank");
    CHECK_EQ(b.entryCount, (std::size_t)3);
    // Entries bridged into the live SampleBank; name lookups resolve.
    CHECK_EQ(sound.bank().samples().size(), (std::size_t)3);
    CHECK(sound.bank().findSampleByName("markt") != nullptr);
    audio::SampleRecord* mk = sound.bank().findSampleByName("markt");
    if (mk) CHECK_EQ(mk->format, 2);
}

TEST(RealAudioDriverUnit, MissingBankReportsUnopened) {
    shim::MemFileSystem fs; // empty
    shim::NullAudioDevice dev;
    audio::SoundSystem sound(&dev);
    CHECK(sound.init());
    app::SfxBankLoad b = app::LoadOneSfxBank(&fs, sound, "sfx", "Nope.sbf");
    CHECK(!b.opened);
    CHECK(!b.parsed);
    CHECK_EQ(b.entryCount, (std::size_t)0);
}

TEST(RealAudioDriverUnit, DriveFullSyntheticPathAndMix) {
    shim::MemFileSystem fs;
    // Two synthetic banks + an include listing referencing them.
    fs.put("sfx/A.sbf", MakeSbf("BankA", {{"a0", 1}, {"a1", 2}}));
    fs.put("sfx/B.sbf", MakeSbf("BankB", {{"b0", 1}, {"b1", 1}, {"b2", 2}}));
    fs.put("include_sfx.ini",
           shim::MemFileSystem::Blob([] {
               std::string s = "#include\"A.sbf\"\n#include \"B.sbf\"\n";
               return std::vector<std::uint8_t>(s.begin(), s.end());
           }()));

    shim::NullAudioDevice dev;
    audio::SoundSystem sound(&dev);
    CHECK(sound.init());

    audio::Vec3 lpos{0, 0, 0};
    audio::Vec3 lfwd{0, 0, 1};
    app::RealAudioResult r = app::DriveRealSfxAudio(&fs, sound, lpos, lfwd);

    CHECK(r.iniFound);
    CHECK_EQ(r.includesListed, (std::size_t)2);
    CHECK_EQ(r.banksLoaded, (std::size_t)2);
    CHECK_EQ(r.samplesLoaded, (std::size_t)5); // 2 + 3
    CHECK(r.voicesPlaced > 0);
    CHECK(r.voicesAudible > 0);  // near sources within radius are audible
    CHECK(r.peakVolume > 0);
    CHECK(r.peakVolume <= 127);
    // The mix really hit the device (NullAudioDevice recorded play calls).
    bool sawPlay = false;
    for (const auto& c : dev.calls())
        if (c.kind == shim::NullAudioDevice::CallKind::Play) { sawPlay = true; break; }
    CHECK(sawPlay);
}

TEST(RealAudioDriverUnit, NoIncludeFileReportsNotFound) {
    shim::MemFileSystem fs; // no include_sfx.ini
    shim::NullAudioDevice dev;
    audio::SoundSystem sound(&dev);
    CHECK(sound.init());
    app::RealAudioResult r = app::DriveRealSfxAudio(&fs, sound, {}, {0, 0, 1});
    CHECK(!r.iniFound);
    CHECK_EQ(r.banksLoaded, (std::size_t)0);
}
