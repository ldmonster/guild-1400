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

// ---- EXACT 45-key serialization order (1:1 pin) ---------------------------
// gilde.exe 0x56af54 — VIBE_Config_WriteGfxSettings emits its keys in ONE fixed
// order (the order of the WritePrivateProfileStringA calls in the binary). The
// other tests pin the count (45), the first key and the per-section counts, but
// not the precise ordered (section,key) sequence — the load-bearing 1:1 value.
// This golden pins it verbatim from the source's documented recovery
// (src/app/config_write.cpp lines 60..111). Any reorder/insert/drop is caught.
TEST(AppConfigWrite, EmitsExactKeyOrder) {
    config::GfxSettings gfx;
    config::SoundSettings snd;
    config::GameSettings game;
    Recorder rec;
    app::ConfigWriteGfxSettings(gfx, snd, game, rec.sink());

    // The exact (section,key) sequence the original serializer writes.
    static const struct { const char* section; const char* key; } kOrder[] = {
        // [Gfx] byte fields
        {"Gfx", "texture_scale"},    {"Gfx", "details"},
        {"Gfx", "lod_handling"},     {"Gfx", "shadow_detail"},
        {"Gfx", "floor_mipmapping"}, {"Gfx", "gfx_set"},
        {"Gfx", "camera_limits"},    {"Gfx", "floor_lod"},
        {"Gfx", "character_detail"}, {"Gfx", "fog_plane"},
        {"Gfx", "cur_res"},
        // [Gfx] float fields (*100 truncate)
        {"Gfx", "brightness_r"}, {"Gfx", "brightness_g"},
        {"Gfx", "brightness_b"}, {"Gfx", "brightness_a"},
        {"Gfx", "contrast_r"},   {"Gfx", "contrast_g"},
        {"Gfx", "contrast_b"},   {"Gfx", "contrast_a"},
        {"Gfx", "gamma_r"},      {"Gfx", "gamma_g"},
        {"Gfx", "gamma_b"},      {"Gfx", "gamma_a"},
        // [Sound]
        {"Sound", "master_vol"}, {"Sound", "sfx_vol"},
        {"Sound", "msx_vol"},    {"Sound", "speech_vol"},
        {"Sound", "msx_freq"},
        // [Game]
        {"Game", "speed"},          {"Game", "mouse_speed"},
        {"Game", "scroll_speed"},   {"Game", "camera_speed"},
        {"Game", "invert_mouse"},   {"Game", "nachtwaechter"},
        {"Game", "stadt"},          {"Game", "historie"},
        {"Game", "mission"},        {"Game", "net_mission"},
        {"Game", "show_cursor_txt"},{"Game", "show_geb_info"},
        {"Game", "panel_mode"},     {"Game", "help_events"},
        {"Game", "difficulty"},     {"Game", "hints"},
        {"Game", "panel_help"},
    };
    const int kCount = static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));
    CHECK_EQ(kCount, 45);
    CHECK_EQ(static_cast<int>(rec.lines.size()), kCount);
    for (int i = 0; i < kCount && i < static_cast<int>(rec.lines.size()); ++i) {
        CHECK(rec.lines[i].section == kOrder[i].section);
        CHECK(rec.lines[i].key == kOrder[i].key);
    }
}

// ---- cold-default emitted values (1:1 pin) --------------------------------
// Serializing the all-default settings (the cold struct image, == the reader's
// defaults from gilde.exe 0x56b834) emits a fixed set of value strings: the
// float defaults truncate (brightness_a 0.5 -> "50", contrast/gamma 1.0 ->
// "100"), and the int defaults (character_detail/nachtwaechter/.../panel_help
// == 1, mission/net_mission == 1) format verbatim. Values traced to ini.h
// struct defaults (the documented 0x56b834 read defaults). Not previously pinned
// on the WRITE side.
TEST(AppConfigWrite, ColdDefaultEmittedValues) {
    config::GfxSettings gfx;
    config::SoundSettings snd;
    config::GameSettings game;
    Recorder rec;
    app::ConfigWriteGfxSettings(gfx, snd, game, rec.sink());
    auto val = [&](const char* s, const char* k) -> std::string {
        const std::string* v = rec.find(s, k);
        return v ? *v : std::string("<MISSING>");
    };
    // [Gfx] byte defaults (all 0 except character_detail = 1).
    CHECK(val("Gfx", "texture_scale") == "0");
    CHECK(val("Gfx", "character_detail") == "1");
    CHECK(val("Gfx", "cur_res") == "0");
    // [Gfx] float defaults: brightness rgb=0 a=50; contrast/gamma rgba=100.
    CHECK(val("Gfx", "brightness_r") == "0");
    CHECK(val("Gfx", "brightness_a") == "50");
    CHECK(val("Gfx", "contrast_r") == "100");
    CHECK(val("Gfx", "contrast_a") == "100");
    CHECK(val("Gfx", "gamma_r") == "100");
    CHECK(val("Gfx", "gamma_a") == "100");
    // [Sound] defaults (all 0).
    CHECK(val("Sound", "master_vol") == "0");
    CHECK(val("Sound", "msx_freq") == "0");
    // [Game] defaults.
    CHECK(val("Game", "speed") == "0");
    CHECK(val("Game", "invert_mouse") == "0");   // reader forces 0
    CHECK(val("Game", "nachtwaechter") == "1");
    CHECK(val("Game", "stadt") == "Augsburg");
    CHECK(val("Game", "mission") == "1");
    CHECK(val("Game", "net_mission") == "1");
    CHECK(val("Game", "show_cursor_txt") == "1");
    CHECK(val("Game", "show_geb_info") == "1");
    CHECK(val("Game", "panel_mode") == "1");
    CHECK(val("Game", "help_events") == "1");
    CHECK(val("Game", "difficulty") == "1");
    CHECK(val("Game", "hints") == "1");
    CHECK(val("Game", "panel_help") == "1");
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

// ====================================================================
// WAVE-11 HARDENING EDGE TESTS (ASAN/UBSAN bounds on the itoa scratch
// buffer + the per-key sink). Goldens above are unchanged.
// ====================================================================

// The itoa helper formats into the caller's buffer; verify the in-bounds
// radix-10 path for the extreme signed values the original handles, into the
// same CHAR String[32] scratch the serializer uses. "-2147483648" is 11 chars
// + NUL == 12, comfortably inside 32 (no scratch overflow).
TEST(AppConfigWrite, AnimationStateUpdateExtremeRadix10) {
    char buf[32];
    CHECK(std::strcmp(app::AnimationState_Update(2147483647, buf, 10u),
                      "2147483647") == 0);
    // INT_MIN: value < 0 so '-' is emitted, then value = -value wraps back to
    // INT_MIN, then StringUIntToString((u32)INT_MIN == 2147483648) appends the
    // magnitude -> "-2147483648" (11 chars + NUL == 12, inside buf[32]).
    // This matches the original's negate-in-place behavior exactly.
    CHECK(std::strcmp(app::AnimationState_Update(-2147483647 - 1, buf, 10u),
                      "-2147483648") == 0);
    CHECK_EQ(std::strlen(buf), static_cast<std::size_t>(11));
}

// A radix-2 format of a full 32-bit value needs up to 32 digits + NUL == 33;
// feed it a 40-byte buffer (as the public unit tests do) and verify the longest
// case stays in-bounds. This pins that AnimationState_Update never writes past
// the digits it produces for the value it is given.
TEST(AppConfigWrite, AnimationStateUpdateRadix2WidthBound) {
    char buf[40] = {};
    // -1 as unsigned base 2 == 32 set bits.
    const char* r = app::AnimationState_Update(-1, buf, 2u);
    CHECK_EQ(std::strlen(r), static_cast<std::size_t>(32));
    for (int i = 0; i < 32; ++i)
        CHECK(buf[i] == '1');
    CHECK_EQ(buf[32], '\0');
}

// The serializer must drive ANY conforming sink without assuming it stores or
// inspects the strings; a sink that captures only lengths (and copies the value
// into a fixed small buffer) must see well-formed NUL-terminated C-strings for
// every one of the 45 keys (ASAN catches an over-read of the value buffer).
TEST(AppConfigWrite, SerializerEmitsNulTerminatedValuesToOddSink) {
    config::GfxSettings gfx;
    config::SoundSettings snd;
    config::GameSettings game;
    // Push values to extremes so the formatted strings are as long as possible.
    game.speed = 2147483647;
    game.mouseSpeed = -2147483647 - 1; // INT_MIN
    game.historie = -123456789;
    gfx.brightness[0] = 99.0f;         // -> 9900
    gfx.contrast[3] = -50.0f;          // -> -5000

    int count = 0;
    std::size_t totalLen = 0;
    auto sink = [&](const std::string& s, const std::string& k,
                    const std::string& v) {
        // Copy through a fixed C-buffer sized like WritePrivateProfileStringA's
        // value scratch would be; a non-terminated value would over-read here.
        char tmp[64];
        CHECK(s.size() < sizeof(tmp));
        CHECK(k.size() < sizeof(tmp));
        CHECK(v.size() < sizeof(tmp));
        std::strncpy(tmp, v.c_str(), sizeof(tmp));
        tmp[sizeof(tmp) - 1] = '\0';
        totalLen += std::strlen(tmp);
        ++count;
    };
    app::ConfigWriteGfxSettings(gfx, snd, game, sink);
    CHECK_EQ(count, 45);
    CHECK(totalLen > 0);
}

// stadt is written verbatim as text. An empty stadt and a long stadt must pass
// through unchanged (no itoa, no truncation in the serializer itself).
TEST(AppConfigWrite, StadtVerbatimEmptyAndLong) {
    Recorder rec1;
    {
        config::GfxSettings gfx; config::SoundSettings snd; config::GameSettings game;
        game.stadt = "";                       // empty
        app::ConfigWriteGfxSettings(gfx, snd, game, rec1.sink());
    }
    const std::string* s1 = rec1.find("Game", "stadt");
    CHECK(s1 != nullptr);
    CHECK(*s1 == "");

    Recorder rec2;
    {
        config::GfxSettings gfx; config::SoundSettings snd; config::GameSettings game;
        game.stadt = std::string(50, 'Z');     // long-ish city name
        app::ConfigWriteGfxSettings(gfx, snd, game, rec2.sink());
    }
    const std::string* s2 = rec2.find("Game", "stadt");
    CHECK(s2 != nullptr);
    CHECK(*s2 == std::string(50, 'Z'));        // verbatim, no truncation
}
