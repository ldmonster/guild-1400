#pragma once
// gilde.exe profile (.INI) configuration.
//
// The original reads "gilde.INI" (next to the executable) via the Win32
// GetPrivateProfileStringA / GetPrivateProfileIntA APIs across the sections
// [Gfx], [Sound], [Game], [General], [Network]. This module provides:
//   * IniFile  — a file/text-backed IProfileProvider that reproduces the
//                Get*ProfileString/Int parsing semantics (the Win32 substitute).
//   * ReadGfxAndSoundSettings / ReadGeneralAndNetworkSettings — faithful
//     translations of the original's key lookups, defaults and value scaling.
#include "config/provider.h"
#include "guild/common/types.h"
#include <map>
#include <string>

namespace guild::config {

// File/text-backed profile provider implementing GetPrivateProfile* semantics.
//
// Format: classic Windows .INI — "[Section]" headers, "key=value" lines,
// ';' line comments, surrounding whitespace trimmed from section/key/value.
// Section and key matching is case-insensitive. The LAST assignment of a key in
// a section wins (matches Win32). getString returns the default if absent;
// getInt parses the value with ParseProfileInt().
class IniFile : public IProfileProvider {
public:
    IniFile() = default;
    explicit IniFile(const std::string& text) { parse(text); }

    // Parse INI text (replaces current contents).
    void parse(const std::string& text);
    // Load INI text from a file on the host filesystem. Returns false if the
    // file can't be read (the provider is then empty -> all defaults).
    bool loadFile(const std::string& path);

    std::string getString(const std::string& section, const std::string& key,
                          const std::string& def) const override;
    int getInt(const std::string& section, const std::string& key, int def) const override;

private:
    // sections_[lower(section)][lower(key)] = raw value text
    std::map<std::string, std::map<std::string, std::string>> sections_;
};

// GetPrivateProfileIntA integer parse: optional leading whitespace, optional
// '-' sign, then decimal digits, stopping at the first non-digit. An empty /
// non-numeric value yields 0. Exposed for testing.
int ParseProfileInt(const std::string& value);

// --- Consolidated settings -------------------------------------------------
// These structs hold the values the original scatters across many globals,
// grouped by INI section. Field comments carry the INI key + default so the
// schema stays 1:1 with VIBE_Config_ReadGfxAndSoundSettings (0x56b834) and the
// [General]/[Network] reads in VIBE_GameLogic_MainEntryAndShutdown (0x534bbc).

struct GfxSettings {
    u8 textureScale = 0;   // texture_scale   def 0
    u8 details = 0;        // details         def 0
    u8 lodHandling = 0;    // lod_handling    def 0
    u8 shadowDetail = 0;   // shadow_detail   def 0
    u8 floorMipmapping = 0;// floor_mipmapping def 0
    u8 cameraLimits = 0;   // camera_limits   def 0
    u8 floorLod = 0;       // floor_lod       def 0
    u8 characterDetail = 1;// character_detail def 1
    u8 fogPlane = 0;       // fog_plane       def 0
    u8 gfxSet = 0;         // gfx_set         def 0
    u8 curRes = 0;         // cur_res         def 0
    // brightness/contrast/gamma are read as ints then scaled by flt_625200
    // (1/100). Defaults: brightness rgb=0 a=50; contrast/gamma rgba=100.
    float brightness[4] = {0, 0, 0, 0.5f};
    float contrast[4] = {1, 1, 1, 1};
    float gamma[4] = {1, 1, 1, 1};
    // Derived from the cur_res lookup table (gilde.exe dword_63D70C/710).
    int resWidth = 0;      // dword_63D72C
    int resHeight = 0;     // dword_63D728
};

struct SoundSettings {
    u8 masterVol = 0;  // master_vol  def 0
    u8 sfxVol = 0;     // sfx_vol     def 0
    u8 msxVol = 0;     // msx_vol     def 0
    u8 speechVol = 0;  // speech_vol  def 0
    u8 msxFreq = 0;    // msx_freq    def 0
};

struct GameSettings {
    int speed = 0;          // speed         def 0
    int mouseSpeed = 0;     // mouse_speed   def 0
    u8  cameraSpeed = 0;    // camera_speed  def 0
    int scrollSpeed = 0;    // scroll_speed  def 0
    u8  invertMouse = 0;    // invert_mouse  def 0 (original then forces 0)
    u8  nachtwaechter = 1;  // nachtwaechter def 1
    u8  showCursorTxt = 1;  // show_cursor_txt def 1
    u8  showGebInfo = 1;    // show_geb_info def 1
    std::string stadt = "Augsburg"; // stadt  def "Augsburg" (max 0x40)
    int historie = 0;       // historie    def 0
    int mission = 1;        // mission     def 1
    int netMission = 1;     // net_mission def 1
    u8  panelMode = 1;      // panel_mode  def 1
    u8  helpEvents = 1;     // help_events def 1
    u8  difficulty = 1;     // difficulty  def 1
    u8  hints = 1;          // hints       def 1
    u8  panelHelp = 1;      // panel_help  def 1
};

// gilde.exe 0x56b834 — VIBE_Config_ReadGfxAndSoundSettings
// Reads the [Gfx], [Sound] and [Game] sections in one pass, applying the same
// keys, defaults and (for brightness/contrast/gamma) the *0.01 scaling.
void ReadGfxAndSoundSettings(const IProfileProvider& ini,
                             GfxSettings& gfx, SoundSettings& snd, GameSettings& game);

// Resolution lookup table recovered from gilde.exe (dword_63D70C / dword_63D710,
// interleaved height,width pairs). Index by `cur_res`. Out-of-range -> {0,0}.
void ResolutionForIndex(u8 index, int* width, int* height);

} // namespace guild::config
