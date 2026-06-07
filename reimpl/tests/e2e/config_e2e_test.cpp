#include "test.h"

#include "config/cmdline.h"
#include "config/config.h"
#include "config/ini.h"
#include "config/registry.h"

#include <string>

using namespace guild;
using namespace guild::config;

// A full sample gilde.INI covering every section the original reads.
static const char* kFullIni =
    "[General]\n"
    "Bildmodus=WINDOWED\n"
    "GfxPath=\\custom\\gfx\\\n"
    "GamePath=\\custom\\game\\\n"
    "MoviePath=\\custom\\movie\\\n"
    "Language=english\n"
    "show_intro=1\n"
    "[Network]\n"
    "Server=Server\\Server.dll\n"
    "Host=192.168.0.5\n"
    "Port=8080\n"
    "[Gfx]\n"
    "texture_scale=3\n"
    "details=2\n"
    "character_detail=1\n"
    "cur_res=2\n"
    "gfx_set=4\n"
    "brightness_r=10\n"
    "brightness_a=90\n"
    "contrast_g=150\n"
    "gamma_b=200\n"
    "[Sound]\n"
    "master_vol=15\n"
    "sfx_vol=12\n"
    "msx_vol=8\n"
    "speech_vol=5\n"
    "msx_freq=44\n"
    "[Game]\n"
    "speed=4\n"
    "mouse_speed=6\n"
    "scroll_speed=9\n"
    "stadt=Hamburg\n"
    "historie=1\n"
    "mission=2\n"
    "net_mission=3\n"
    "difficulty=2\n"
    "hints=0\n";

// e2e: feed a full sample gilde.INI plus a full command line and verify the
// consolidated StartupConfig, including the command-line overrides.
TEST(ConfigE2E, FullIniPlusCmdLine) {
    IniFile ini(kFullIni);

    LaunchOptions opt =
        ParseLaunchOptions("STADT=\"Foo\" BERUF=\"3\" IP=\"1.2.3.4\" PORT=\"9000\"");

    StartupConfig cfg = BuildStartupConfig(ini, opt);

    // --- [General] ---
    CHECK_EQ(cfg.general.bildmodus, std::string("WINDOWED"));
    CHECK_EQ(cfg.general.displayMode, 1); // non-FULLSCREEN -> windowed mode 1
    CHECK_EQ(cfg.general.gfxPath, std::string("\\custom\\gfx\\"));
    CHECK_EQ(cfg.general.language, std::string("english"));
    CHECK_EQ(cfg.general.showIntro, 1);

    // --- [Network] (overridden by cmdline IP/PORT) ---
    CHECK_EQ(cfg.network.server, std::string("Server\\Server.dll"));
    CHECK_EQ(cfg.network.host, std::string("1.2.3.4")); // IP= override
    CHECK_EQ(cfg.network.port, 9000);                   // PORT= override
    CHECK(cfg.networkClient);                            // IP= sets client flag

    // --- [Gfx] ---
    CHECK_EQ((int)cfg.gfx.textureScale, 3);
    CHECK_EQ((int)cfg.gfx.details, 2);
    CHECK_EQ((int)cfg.gfx.gfxSet, 4);
    CHECK_EQ((int)cfg.gfx.curRes, 2);
    // cur_res=2 -> table (1152,864): resHeight=1152, resWidth=864.
    CHECK_EQ(cfg.gfx.resHeight, 1152);
    CHECK_EQ(cfg.gfx.resWidth, 864);
    // Scaled floats.
    CHECK(cfg.gfx.brightness[0] > 0.099f && cfg.gfx.brightness[0] < 0.101f);  // 10*0.01
    CHECK(cfg.gfx.brightness[3] > 0.899f && cfg.gfx.brightness[3] < 0.901f);  // 90*0.01
    CHECK(cfg.gfx.contrast[1] > 1.499f && cfg.gfx.contrast[1] < 1.501f);      // 150*0.01
    CHECK(cfg.gfx.gamma[2] > 1.999f && cfg.gfx.gamma[2] < 2.001f);            // 200*0.01

    // --- [Sound] ---
    CHECK_EQ((int)cfg.sound.masterVol, 15);
    CHECK_EQ((int)cfg.sound.sfxVol, 12);
    CHECK_EQ((int)cfg.sound.msxFreq, 44);

    // --- [Game] (stadt overridden by cmdline) ---
    CHECK_EQ(cfg.game.speed, 4);
    CHECK_EQ(cfg.game.scrollSpeed, 9);
    CHECK_EQ(cfg.game.mission, 2);
    CHECK_EQ(cfg.game.netMission, 3);
    CHECK_EQ((int)cfg.game.difficulty, 2);
    CHECK_EQ((int)cfg.game.hints, 0);
    CHECK_EQ(cfg.game.stadt, std::string("FOO")); // STADT= override (uppercased)
    CHECK_EQ(cfg.beruf, std::string("3"));        // BERUF= override
}

// e2e: with NO command line, the consolidated config keeps INI values/defaults.
TEST(ConfigE2E, IniOnlyNoOverrides) {
    IniFile ini(kFullIni);
    LaunchOptions opt = ParseLaunchOptions(""); // nothing supplied
    StartupConfig cfg = BuildStartupConfig(ini, opt);

    CHECK_EQ(cfg.network.host, std::string("192.168.0.5")); // from INI
    CHECK_EQ(cfg.network.port, 8080);                       // from INI
    CHECK(!cfg.networkClient);
    CHECK_EQ(cfg.game.stadt, std::string("Hamburg"));       // from INI
    CHECK_EQ(cfg.beruf, std::string(""));                   // never set
}

// e2e: an empty INI yields every documented default, then cmdline overrides.
TEST(ConfigE2E, EmptyIniUsesDefaults) {
    IniFile ini(""); // empty
    LaunchOptions opt = ParseLaunchOptions("PORT=\"1234\"");
    StartupConfig cfg = BuildStartupConfig(ini, opt);

    CHECK_EQ(cfg.general.bildmodus, std::string("FULLSCREEN"));
    CHECK_EQ(cfg.general.displayMode, 3);
    CHECK_EQ(cfg.general.language, std::string("german"));
    CHECK_EQ(cfg.network.host, std::string("128.0.0.1"));
    CHECK_EQ(cfg.network.port, 1234); // PORT override over default 7531
    CHECK_EQ(cfg.game.stadt, std::string("Augsburg"));
    CHECK_EQ((int)cfg.game.nachtwaechter, 1);
    CHECK_EQ((int)cfg.gfx.characterDetail, 1);
}

// e2e: registry persistence flow — write a settings key, reopen, read back.
TEST(ConfigE2E, RegistrySettingsRoundtrip) {
    MemRegistry reg;
    int h = OpenKey(reg, "Settings", OpenMode::Create);
    CHECK(h >= 0);
    SetDwordValue(reg, h, "resolution", 2);
    SetStringValue(reg, h, "city", "Augsburg");
    SetFloatValue(reg, h, "gamma", 1.25f);
    CloseKey(reg, h);

    // Verify the full prefixed path was used.
    int h2 = OpenKey(reg, "Settings", OpenMode::Open);
    CHECK(h2 >= 0);
    CHECK_EQ(QueryDwordValue(reg, h2, "resolution"), 2u);
    std::string city;
    CHECK_EQ(QueryStringValue(reg, h2, "city", &city), 1);
    CHECK_EQ(city, std::string("Augsburg"));
    float g = QueryFloatValue(reg, h2, "gamma");
    CHECK(g > 1.24f && g < 1.26f);
    CloseKey(reg, h2);
}
