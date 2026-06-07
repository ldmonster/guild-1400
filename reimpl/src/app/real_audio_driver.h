#pragma once
// gilde.exe — REAL-asset AUDIO driver (guild::app).
//
// INTEGRATION GLUE, not a translation. This module closes the gap left open in
// wiring.cpp (RealSubsystems::soundLoadSampleBank / soundPreloadIncludeFile note
// "the on-disk .sbf parse is deferred"): it drives the SFX sample-bank preload
// end to end over the REAL shipped bytes:
//
//   1. parse the real "include_sfx.ini" (the listing VIBE_Sound_PreloadFromInclude-
//      File @0x52f154 reads): a flat list of `#include "<rel>.sbf"` directives,
//   2. resolve each `<rel>` under the sfx/ root over the bound VFS (the real
//      include file uses Windows backslash paths + mixed case; we normalize +
//      case-fold to match the on-disk names),
//   3. load each .sbf through the reconstructed loader
//      (audio::LoadSampleBankFromBuffer == VIBE_Sound_LoadSampleBank @0x446b2c),
//      then bridge its index entries into the SoundSystem's live SampleBank
//      (audio::SampleBank::addSample / addVariation) so name lookups resolve,
//   4. drive the audio tick / 3D mix math (audio::Sound3dPool::updateAll == the
//      Compute3dVolume/Compute3dPan curves) on the loaded banks against a listener
//      pose, through a NullAudioDevice shim (no real OS audio).
//
// Everything reached is real reconstructed code over real bytes; the only OS
// boundary is shim::IFileSystem, and the device leaf is the inert NullAudioDevice.
#include "audio/sound.h"
#include "audio/sound3d.h"
#include "shim/IFileSystem.h"

#include <string>
#include <vector>

namespace guild::app {

// One parsed `#include` directive resolved (or not) against the sfx tree.
struct SfxBankLoad {
    std::string include;         // the raw rel path from the .ini (e.g. "Locations\\gb_Kirche.sbf")
    std::string vfsPath;         // the resolved VFS path actually opened (e.g. "sfx/Locations/gb_Kirche.sbf")
    bool        opened = false;  // the file was found + read through the VFS
    bool        parsed = false;  // LoadSampleBankFromBuffer succeeded
    std::string bankName;        // SbBank.name (header+0)
    std::size_t entryCount = 0;  // number of index entries parsed
};

// The aggregate result of driving the SFX preload + mix.
struct RealAudioResult {
    bool        iniFound = false;     // the include file existed + was read
    std::size_t includesListed = 0;   // `#include` directives seen in the .ini
    std::size_t banksLoaded = 0;      // banks that parsed cleanly
    std::size_t samplesLoaded = 0;    // total index entries across all banks
    std::size_t voicesPlaced = 0;     // 3D voices attached to the pool for the mix
    std::size_t voicesAudible = 0;    // voices the mix math made audible (vol > 0)
    int         peakVolume = 0;       // max per-voice volume the mix produced (0..127)
    std::vector<SfxBankLoad> banks;   // per-bank detail (in include order)
};

// Parse an include-listing buffer (the include_sfx.ini bytes): return the rel
// paths from every `#include "<path>"` directive, in file order. Whitespace
// between `#include` and the quote is tolerated (the real file has both forms).
// Mirrors the directive scan in VIBE_Sound_PreloadFromIncludeFile @0x52f154.
std::vector<std::string> ParseSfxIncludeList(const std::string& text);

// Normalize an include rel-path to a VFS path under `sfxRoot`: backslashes ->
// forward slashes, collapse doubled separators, and join. (Case is handled at
// resolve time, not here.) e.g. ("sfx", "Locations\\gb_Kirche.sbf") ->
// "sfx/Locations/gb_Kirche.sbf".
std::string NormalizeSfxPath(const std::string& sfxRoot, const std::string& include);

// Drive the full real-asset SFX path through `fs`:
//   * read `includeName` (default "include_sfx.ini") off the VFS-bound `fs`,
//   * resolve + load each listed .sbf under `sfxRoot` (default "sfx"),
//   * bridge the parsed entries into `sound`'s SampleBank,
//   * attach up to `maxVoices` of the loaded samples as positioned 3D emitters
//     and run one Sound3dPool::updateAll(listener) so the real pan/vol math runs.
// `fs` must already be VFS-resolvable for loose files (io::VfsInit done by the
// caller, or a plain shim::IFileSystem rooted at the game dir — this driver reads
// through `fs` directly so it works either way). Returns the aggregate counts.
RealAudioResult DriveRealSfxAudio(shim::IFileSystem* fs, audio::SoundSystem& sound,
                                  const audio::Vec3& listenerPos,
                                  const audio::Vec3& listenerForward,
                                  const std::string& includeName = "include_sfx.ini",
                                  const std::string& sfxRoot = "sfx",
                                  std::size_t maxVoices = 8);

// Lower-level helper: resolve `include` under `sfxRoot` over `fs` (case-insensitive
// segment match against the directory listing when an exact open fails), read its
// bytes, parse via LoadSampleBankFromBuffer, and append its entries to `sound`'s
// SampleBank. Returns the per-bank load record. Exposed for the integration test.
SfxBankLoad LoadOneSfxBank(shim::IFileSystem* fs, audio::SoundSystem& sound,
                           const std::string& sfxRoot, const std::string& include);

} // namespace guild::app
