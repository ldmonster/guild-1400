// tests/unit/app_config_write_test.cpp — unit tests for the settings serializer
// VIBE_Config_WriteGfxSettings (0x56af54) + its itoa helper
// VIBE_AnimationState_Update (0x5d92ec), both translated in src/app/config_write.
//
// Verifies: the itoa helper formats signed/unsigned radices like the original;
// the serializer emits the exact key set/order; and a read -> write -> read of
// the settings round-trips bit-for-bit through guild::config (the write scale
// undoes the read scale).
#include "test.h"

#include "app/config_write.h"
#include "config/ini.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;

namespace {

// Record sink capturing every (section,key,value) the serializer emits, in order.
struct Recorder {
    struct Line { std::string section, key, value; };
    std::vector<Line> lines;
    app::ProfileWriteSink sink() {
        return [this](const std::string& s, const std::string& k,
                      const std::string& v) {
            lines.push_back({s, k, v});
        };
    }
    const std::string* find(const std::string& section, const std::string& key) const {
        for (const auto& l : lines)
            if (l.section == section && l.key == key) return &l.value;
        return nullptr;
    }
};

// A profile provider backed by the recorded (section,key,value) lines so a
// write -> read round-trip can run through the real ReadGfxAndSoundSettings.
class RecorderProvider : public config::IProfileProvider {
public:
    explicit RecorderProvider(const Recorder& r) {
        for (const auto& l : r.lines) m_[l.section][l.key] = l.value;
    }
    std::string getString(const std::string& s, const std::string& k,
                          const std::string& def) const override {
        auto si = m_.find(s);
        if (si == m_.end()) return def;
        auto ki = si->second.find(k);
        return ki == si->second.end() ? def : ki->second;
    }
    int getInt(const std::string& s, const std::string& k, int def) const override {
        auto si = m_.find(s);
        if (si == m_.end()) return def;
        auto ki = si->second.find(k);
        if (ki == si->second.end()) return def;
        return config::ParseProfileInt(ki->second);
    }
private:
    std::map<std::string, std::map<std::string, std::string>> m_;
};

} // namespace

// ---- VIBE_AnimationState_Update (itoa) -----------------------------------
TEST(AppConfigWrite, AnimationStateUpdateSignedRadix10) {
    char buf[32];
    CHECK(std::strcmp(app::AnimationState_Update(0, buf, 10u), "0") == 0);
    CHECK(std::strcmp(app::AnimationState_Update(12345, buf, 10u), "12345") == 0);
    CHECK(std::strcmp(app::AnimationState_Update(-1, buf, 10u), "-1") == 0);
    CHECK(std::strcmp(app::AnimationState_Update(-987, buf, 10u), "-987") == 0);
    CHECK(std::strcmp(app::AnimationState_Update(255, buf, 10u), "255") == 0);
}

TEST(AppConfigWrite, AnimationStateUpdateUnsignedRadices) {
    char buf[40];
    // For radix != 10 the value is formatted UNSIGNED (no '-' branch).
    CHECK(std::strcmp(app::AnimationState_Update(255, buf, 16u), "ff") == 0);
    CHECK(std::strcmp(app::AnimationState_Update(8, buf, 2u), "1000") == 0);
    // -1 as unsigned base 16 == 0xffffffff (matches the original's unsigned core).
    CHECK(std::strcmp(app::AnimationState_Update(-1, buf, 16u), "ffffffff") == 0);
}

// ---- key set / order ------------------------------------------------------
TEST(AppConfigWrite, EmitsExpectedKeyCountAndSections) {
    config::GfxSettings gfx;
    config::SoundSettings snd;
    config::GameSettings game;
    Recorder rec;
    app::ConfigWriteGfxSettings(gfx, snd, game, rec.sink());

    // 11 [Gfx] byte keys + 12 [Gfx] float keys + 5 [Sound] + 17 [Game] == 45.
    CHECK_EQ(static_cast<int>(rec.lines.size()), 45);

    int gfxN = 0, soundN = 0, gameN = 0;
    for (const auto& l : rec.lines) {
        if (l.section == "Gfx") ++gfxN;
        else if (l.section == "Sound") ++soundN;
        else if (l.section == "Game") ++gameN;
    }
    CHECK_EQ(gfxN, 23);
    CHECK_EQ(soundN, 5);
    CHECK_EQ(gameN, 17);

    // The first key written is texture_scale (the original's order), the stadt
    // value is written verbatim as text (not itoa'd).
    CHECK(rec.lines.front().section == "Gfx");
    CHECK(rec.lines.front().key == "texture_scale");
    const std::string* stadt = rec.find("Game", "stadt");
    CHECK(stadt != nullptr);
    CHECK(*stadt == "Augsburg"); // GameSettings default
}

// ---- value formatting -----------------------------------------------------
TEST(AppConfigWrite, FloatFieldsScaledByHundred) {
    config::GfxSettings gfx;
    gfx.brightness[3] = 0.5f;   // -> 50
    gfx.contrast[0]   = 1.0f;   // -> 100
    gfx.gamma[2]      = 1.0f;   // -> 100
    config::SoundSettings snd;
    config::GameSettings game;
    Recorder rec;
    app::ConfigWriteGfxSettings(gfx, snd, game, rec.sink());

    // (int)(0.5f * 100.0f) == 50 ; (int)(1.0f * 100.0f) == 100.
    const std::string* ba = rec.find("Gfx", "brightness_a");
    CHECK(ba && *ba == "50");
    const std::string* cr = rec.find("Gfx", "contrast_r");
    CHECK(cr && *cr == "100");
    const std::string* gb = rec.find("Gfx", "gamma_b");
    CHECK(gb && *gb == "100");
}

// ---- read -> write -> read round-trip (the inverse-of-Read property) ------
TEST(AppConfigWrite, RoundTripThroughReadGfxAndSound) {
    // 1. Author a non-default INI and read it via the real reader.
    const char* iniText =
        "[Gfx]\n"
        "texture_scale=2\n"
        "details=3\n"
        "character_detail=1\n"
        "cur_res=4\n"
        "brightness_a=50\n"
        "contrast_r=100\n"
        "gamma_g=100\n"
        "[Sound]\n"
        "master_vol=80\n"
        "sfx_vol=70\n"
        "[Game]\n"
        "speed=5\n"
        "mouse_speed=9\n"
        "stadt=Cologne\n"
        "mission=2\n"
        "difficulty=3\n";
    config::IniFile ini(iniText);
    config::GfxSettings g1; config::SoundSettings s1; config::GameSettings m1;
    config::ReadGfxAndSoundSettings(ini, g1, s1, m1);

    // 2. Write the read-back settings out to a recorder.
    Recorder rec;
    app::ConfigWriteGfxSettings(g1, s1, m1, rec.sink());

    // 3. Read the serialized output back and compare the structs field-for-field.
    RecorderProvider prov(rec);
    config::GfxSettings g2; config::SoundSettings s2; config::GameSettings m2;
    config::ReadGfxAndSoundSettings(prov, g2, s2, m2);

    CHECK_EQ((int)g2.textureScale, (int)g1.textureScale);
    CHECK_EQ((int)g2.details, (int)g1.details);
    CHECK_EQ((int)g2.characterDetail, (int)g1.characterDetail);
    CHECK_EQ((int)g2.curRes, (int)g1.curRes);
    // Float fields round-trip to the same value (50/100 truncate cleanly).
    CHECK(g2.brightness[3] == g1.brightness[3]);
    CHECK(g2.contrast[0] == g1.contrast[0]);
    CHECK(g2.gamma[1] == g1.gamma[1]);

    CHECK_EQ((int)s2.masterVol, (int)s1.masterVol);
    CHECK_EQ((int)s2.sfxVol, (int)s1.sfxVol);

    CHECK_EQ(m2.speed, m1.speed);
    CHECK_EQ(m2.mouseSpeed, m1.mouseSpeed);
    CHECK(m2.stadt == m1.stadt);          // "Cologne" survives the text round-trip
    CHECK_EQ(m2.mission, m1.mission);
    CHECK_EQ((int)m2.difficulty, (int)m1.difficulty);
}
