#pragma once
// gilde.exe consolidated startup configuration.
//
// Mirrors the order in which VIBE_GameLogic_MainEntryAndShutdown (0x534bbc)
// builds the effective config: read the [General]/[Network] keys and the
// [Gfx]/[Sound]/[Game] block from gilde.INI, then let the command-line options
// STADT/BERUF/IP/PORT override the matching fields.
#include "config/cmdline.h"
#include "config/ini.h"
#include "guild/common/types.h"
#include <string>

namespace guild::config {

// [General] / [Network] values read directly in the main entry function.
struct GeneralSettings {
    std::string bildmodus = "FULLSCREEN"; // [General] Bildmodus def "FULLSCREEN"
    int displayMode = 3;     // 1 = windowed (==FULLSCREEN match), else 3
    std::string gfxPath = "\\project\\gfx\\";   // [General] GfxPath
    std::string gamePath = "\\project\\game\\"; // [General] GamePath
    std::string moviePath = "\\project\\movie\\"; // [General] MoviePath
    std::string language = "german";  // [General] Language def "german"
    int showIntro = 0;       // [General] show_intro def 0
};

struct NetworkSettings {
    std::string server = "Server\\Server.dll"; // [Network] Server
    std::string host = "128.0.0.1";            // [Network] Host def "128.0.0.1"
    int port = 7531;                           // [Network] Port def 7531
};

// The fully-resolved configuration after INI load + command-line overrides.
struct StartupConfig {
    GfxSettings gfx;
    SoundSettings sound;
    GameSettings game;
    GeneralSettings general;
    NetworkSettings network;

    std::string beruf;       // profession (set only via BERUF= cmdline)
    bool networkClient = false; // set true when IP= was supplied on the cmdline
};

// Build the consolidated config: read every section from `ini`, then apply the
// parsed launch options. Reproduces the main-entry override order:
//   STADT -> game.stadt,  BERUF -> beruf,  IP -> network.host (+networkClient),
//   PORT -> network.port.
StartupConfig BuildStartupConfig(const IProfileProvider& ini, const LaunchOptions& opt);

} // namespace guild::config
