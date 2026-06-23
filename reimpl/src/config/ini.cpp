#include "config/ini.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace guild::config {

namespace {

std::string lower(const std::string& s) {
    std::string r = s;
    for (char& c : r)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

} // namespace

// GetPrivateProfileIntA value parse: skip leading whitespace, optional '-',
// then base-10 digits, stop at first non-digit. Non-numeric -> 0.
int ParseProfileInt(const std::string& value) {
    std::size_t i = 0;
    while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i])))
        ++i;
    bool neg = false;
    if (i < value.size() && (value[i] == '-' || value[i] == '+')) {
        neg = (value[i] == '-');
        ++i;
    }
    // Accumulate in an UNSIGNED type: signed overflow on a pathologically long
    // digit run (untrusted .INI) is C++ UB, whereas unsigned wraparound is well
    // defined. For every value in the int range the final truncation is
    // bit-identical to the prior signed accumulation, so valid inputs are
    // unchanged; only out-of-range garbage (which was UB before) differs.
    unsigned long acc = 0;
    bool any = false;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
        acc = acc * 10u + static_cast<unsigned long>(value[i] - '0');
        any = true;
        ++i;
    }
    if (!any)
        return 0;
    unsigned long mag = neg ? (0ul - acc) : acc; // two's-complement negate, no UB
    return static_cast<int>(static_cast<unsigned int>(mag));
}

void IniFile::parse(const std::string& text) {
    sections_.clear();
    std::istringstream in(text);
    std::string line;
    std::string cur; // lowercased current section name
    while (std::getline(in, line)) {
        // Strip a trailing CR (CRLF files).
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#')
            continue;
        if (t.front() == '[') {
            std::size_t end = t.find(']');
            if (end != std::string::npos)
                cur = lower(trim(t.substr(1, end - 1)));
            continue;
        }
        std::size_t eq = t.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = lower(trim(t.substr(0, eq)));
        std::string val = trim(t.substr(eq + 1));
        sections_[cur][key] = val; // last assignment wins
    }
}

bool IniFile::loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    parse(ss.str());
    return true;
}

std::string IniFile::getString(const std::string& section, const std::string& key,
                               const std::string& def) const {
    auto si = sections_.find(lower(section));
    if (si == sections_.end())
        return def;
    auto ki = si->second.find(lower(key));
    if (ki == si->second.end())
        return def;
    return ki->second;
}

int IniFile::getInt(const std::string& section, const std::string& key, int def) const {
    auto si = sections_.find(lower(section));
    if (si == sections_.end())
        return def;
    auto ki = si->second.find(lower(key));
    if (ki == si->second.end())
        return def;
    return ParseProfileInt(ki->second);
}

// Resolution table recovered from gilde.exe @0x63D70C (u32 array, interleaved).
// The original computes:  dword_63D728 = tab[2*idx];  dword_63D72C = tab[2*idx+1];
// so each logical entry is the pair (tab[2*idx], tab[2*idx+1]).
namespace {
const int kResTable[] = {
    800, 600,   // idx 0
    1024, 768,  // idx 1
    1152, 864,  // idx 2
    0, 0,       // idx 3
    800, 600,   // idx 4
    0, 0,       // idx 5
};
const int kResEntries = static_cast<int>(sizeof(kResTable) / sizeof(kResTable[0]) / 2);
} // namespace

void ResolutionForIndex(u8 index, int* width, int* height) {
    // *width  mirrors dword_63D72C (= tab[2*idx+1])
    // *height mirrors dword_63D728 (= tab[2*idx])
    if (index >= kResEntries) {
        *width = 0;
        *height = 0;
        return;
    }
    *height = kResTable[2 * index];
    *width = kResTable[2 * index + 1];
}

// gilde.exe 0x56b834 — VIBE_Config_ReadGfxAndSoundSettings
void ReadGfxAndSoundSettings(const IProfileProvider& ini,
                             GfxSettings& gfx, SoundSettings& snd, GameSettings& game) {
    const float scale = 0.01f; // gilde.exe flt_625200

    // [Gfx]
    gfx.textureScale    = static_cast<u8>(ini.getInt("Gfx", "texture_scale", 0));
    gfx.details         = static_cast<u8>(ini.getInt("Gfx", "details", 0));
    gfx.lodHandling     = static_cast<u8>(ini.getInt("Gfx", "lod_handling", 0));
    gfx.shadowDetail    = static_cast<u8>(ini.getInt("Gfx", "shadow_detail", 0));
    gfx.floorMipmapping = static_cast<u8>(ini.getInt("Gfx", "floor_mipmapping", 0));
    gfx.cameraLimits    = static_cast<u8>(ini.getInt("Gfx", "camera_limits", 0));
    gfx.floorLod        = static_cast<u8>(ini.getInt("Gfx", "floor_lod", 0));
    gfx.characterDetail = static_cast<u8>(ini.getInt("Gfx", "character_detail", 1));
    gfx.fogPlane        = static_cast<u8>(ini.getInt("Gfx", "fog_plane", 0));

    gfx.brightness[0] = ini.getInt("Gfx", "brightness_r", 0) * scale;
    gfx.brightness[1] = ini.getInt("Gfx", "brightness_g", 0) * scale;
    gfx.brightness[2] = ini.getInt("Gfx", "brightness_b", 0) * scale;
    gfx.brightness[3] = ini.getInt("Gfx", "brightness_a", 50) * scale;
    gfx.contrast[0]   = ini.getInt("Gfx", "contrast_r", 100) * scale;
    gfx.contrast[1]   = ini.getInt("Gfx", "contrast_g", 100) * scale;
    gfx.contrast[2]   = ini.getInt("Gfx", "contrast_b", 100) * scale;
    gfx.contrast[3]   = ini.getInt("Gfx", "contrast_a", 100) * scale;
    gfx.gamma[0]      = ini.getInt("Gfx", "gamma_r", 100) * scale;
    gfx.gamma[1]      = ini.getInt("Gfx", "gamma_g", 100) * scale;
    gfx.gamma[2]      = ini.getInt("Gfx", "gamma_b", 100) * scale;
    gfx.gamma[3]      = ini.getInt("Gfx", "gamma_a", 100) * scale;

    gfx.gfxSet = static_cast<u8>(ini.getInt("Gfx", "gfx_set", 0));
    gfx.curRes = static_cast<u8>(ini.getInt("Gfx", "cur_res", 0));
    ResolutionForIndex(gfx.curRes, &gfx.resWidth, &gfx.resHeight);

    // [Sound]
    snd.masterVol = static_cast<u8>(ini.getInt("Sound", "master_vol", 0));
    snd.sfxVol    = static_cast<u8>(ini.getInt("Sound", "sfx_vol", 0));
    snd.msxVol    = static_cast<u8>(ini.getInt("Sound", "msx_vol", 0));
    snd.speechVol = static_cast<u8>(ini.getInt("Sound", "speech_vol", 0));
    snd.msxFreq   = static_cast<u8>(ini.getInt("Sound", "msx_freq", 0));

    // [Game]
    game.speed         = ini.getInt("Game", "speed", 0);
    game.mouseSpeed    = ini.getInt("Game", "mouse_speed", 0);
    game.cameraSpeed   = static_cast<u8>(ini.getInt("Game", "camera_speed", 0));
    game.scrollSpeed   = ini.getInt("Game", "scroll_speed", 0);
    game.invertMouse   = static_cast<u8>(ini.getInt("Game", "invert_mouse", 0));
    game.nachtwaechter = static_cast<u8>(ini.getInt("Game", "nachtwaechter", 1));
    game.showCursorTxt = static_cast<u8>(ini.getInt("Game", "show_cursor_txt", 1));
    game.showGebInfo   = static_cast<u8>(ini.getInt("Game", "show_geb_info", 1));
    // The original forces invert_mouse back to 0 immediately after reading it.
    game.invertMouse   = 0;
    game.stadt         = ini.getString("Game", "stadt", "Augsburg");
    game.historie      = ini.getInt("Game", "historie", 0);
    game.mission       = ini.getInt("Game", "mission", 1);
    game.netMission    = ini.getInt("Game", "net_mission", 1);
    game.panelMode     = static_cast<u8>(ini.getInt("Game", "panel_mode", 1));
    game.helpEvents    = static_cast<u8>(ini.getInt("Game", "help_events", 1));
    game.difficulty    = static_cast<u8>(ini.getInt("Game", "difficulty", 1));
    game.hints         = static_cast<u8>(ini.getInt("Game", "hints", 1));
    game.panelHelp     = static_cast<u8>(ini.getInt("Game", "panel_help", 1));
}

} // namespace guild::config
