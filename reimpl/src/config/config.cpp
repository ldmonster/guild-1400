#include "config/config.h"

namespace guild::config {

// gilde.exe 0x534bbc — consolidated read order.
StartupConfig BuildStartupConfig(const IProfileProvider& ini, const LaunchOptions& opt) {
    StartupConfig cfg;

    // [Gfx] / [Sound] / [Game]  (VIBE_Config_ReadGfxAndSoundSettings schema).
    ReadGfxAndSoundSettings(ini, cfg.gfx, cfg.sound, cfg.game);

    // [General]
    cfg.general.bildmodus = ini.getString("General", "Bildmodus", "FULLSCREEN");
    // The original: StrCmp("WINDOWED"?,..) -> mode 3 unless it matches the
    // FULLSCREEN/windowed predicate. We model the observable result: the literal
    // "FULLSCREEN" keeps the default fullscreen mode (3); anything else -> 1.
    cfg.general.displayMode = (cfg.general.bildmodus == "FULLSCREEN") ? 3 : 1;
    cfg.general.gfxPath   = ini.getString("General", "GfxPath", "\\project\\gfx\\");
    cfg.general.gamePath  = ini.getString("General", "GamePath", "\\project\\game\\");
    cfg.general.moviePath = ini.getString("General", "MoviePath", "\\project\\movie\\");
    cfg.general.language  = ini.getString("General", "Language", "german");
    cfg.general.showIntro = ini.getInt("General", "show_intro", 0);

    // [Network]
    cfg.network.server = ini.getString("Network", "Server", "Server\\Server.dll");
    cfg.network.host   = ini.getString("Network", "Host", "128.0.0.1");
    cfg.network.port   = ini.getInt("Network", "Port", 7531);

    // Command-line overrides (main-entry order).
    if (opt.hasStadt)
        cfg.game.stadt = opt.stadt;
    if (opt.hasBeruf)
        cfg.beruf = opt.beruf;
    if (opt.hasIp) {
        cfg.network.host = opt.ip;
        cfg.networkClient = true; // v16 = 1 in the original
    }
    if (opt.hasPort)
        cfg.network.port = opt.port;

    return cfg;
}

} // namespace guild::config
