#pragma once
// gilde.exe 0x56af54 — VIBE_Config_WriteGfxSettings  (guild::app).
//
// The settings serializer the spine runs as shutdown teardown STEP 3
// (VIBE_GameLogic_MainEntryAndShutdown @0x534bbc -> 0x56af54). It writes the
// effective [Gfx] / [Sound] / [Game] settings back out, in a fixed key order,
// via WritePrivateProfileStringA(section, key, valueText, iniPath).
//
// The OS WritePrivateProfileStringA is the genuine leaf here (no portable INI
// *writer* exists in the reconstructed config layer — IProfileProvider is
// read-only). The reconstructed, platform-neutral half — the EXACT key order,
// the int/float value formatting (signed itoa for ints, *100.0f truncate for
// the brightness/contrast/gamma floats), and the section/key strings — is
// translated 1:1 here against an injectable sink. A real host would back the
// sink with WritePrivateProfileStringA; tests record (section,key,value) tuples.
//
// This is the exact inverse of guild::config::ReadGfxAndSoundSettings
// (0x56b834): every key it reads, this writes, with the *0.01 read scaling
// undone by the *100.0f write scaling (flt_6251F0 == 100.0, flt_625200 == 0.01).
#include "config/ini.h"        // GfxSettings / SoundSettings / GameSettings

#include <functional>
#include <string>

namespace guild::app {

// The per-key output sink. Mirrors WritePrivateProfileStringA's (section, key,
// value) triple (the INI path is fixed per process, so it is not threaded here).
using ProfileWriteSink =
    std::function<void(const std::string& section, const std::string& key,
                       const std::string& value)>;

// gilde.exe 0x5d92ec — VIBE_AnimationState_Update(value@eax, out@edx, radix@ebx).
// itoa: for radix 10 a negative value emits a leading '-' then formats |value|;
// any other radix formats `value` unsigned. Writes a NUL-terminated string into
// `out` (which must have room) and returns `out`. (Despite the symbol name, this
// is the generic int->string helper the config writer uses for every numeric
// field; reused verbatim from the binary.)
char* AnimationState_Update(int value, char* out, unsigned radix);

// gilde.exe 0x56af54 — VIBE_Config_WriteGfxSettings.
// Serialize the [Gfx]/[Sound]/[Game] settings to `sink` in the original's exact
// key order. The brightness/contrast/gamma floats are written as
// (int)(value * 100.0f), reproducing the original's flt_6251F0 scale + truncate
// (and its float-precision rounding) so a read->write->read round-trips bit-for
// bit through guild::config::ReadGfxAndSoundSettings.
void ConfigWriteGfxSettings(const config::GfxSettings& gfx,
                            const config::SoundSettings& snd,
                            const config::GameSettings& game,
                            const ProfileWriteSink& sink);

} // namespace guild::app
