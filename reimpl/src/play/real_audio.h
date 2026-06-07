#pragma once
// guild::play::real_audio — wire a REAL sample bank (sfx/*.sbf) through the decode
// pipeline to a host IAudioDevice (SdlAudioDevice on the host; NullAudioDevice for
// headless tests). This is the "a game event actually plays a sound" integrator,
// the audio counterpart of play::RealCityRenderer for the render path.
//
// It is ADDITIVE: it does not touch src/audio/* or the shim audio backends. It
// re-parses the on-disk .sbf binary format (recovered below from the gilde.exe
// loader + a real-file hexdump) into self-contained PCM, then submits that PCM to
// an IAudioDevice via the same allocVoice()/playSample() contract the real
// digital_output facade uses.
//
// ----------------------------------------------------------------------------
// On-disk .sbf binary format (verified against europe_guild_1400_original/sfx/*)
// ----------------------------------------------------------------------------
// Read by VIBE_Sound_LoadSampleBank @0x446b2c (header + entry index) and
// VIBE_Sound_LoadEntry @0x446830 (the per-entry data block, faulted on demand
// by seeking to the entry's data offset). Little-endian, 32-bit x86, MSVC.
//
// BANK HEADER — 0x144 (324) bytes (VIBE_File_Read(buf, 0x144, 1)):
//   +0x000  char name[50]    bank name (e.g. "Allgemein")
//   +0x134  u32  entryCount   number of 0x40-byte entry records
//   (the rest of the 324-byte header is opaque / reserved)
//
// ENTRY RECORD — 0x40 (64) bytes each, contiguous from file offset 0x148:
//   +0x00  char name[50]    sub-sample name ("SabotageExplosionGross"), NUL-padded
//   +0x32  u32  format       1 = single .wav block, 2 = variation group
//   +0x36  (runtime data ptr, 0 on disk)
//   +0x3C  u32  dataOffset   file offset of this entry's DATA BLOCK
//   (NB: the original's runtime entry shuffles these — entry+0 holds the seek
//    offset and entry+4 the name at run time — but the on-disk bytes are as above;
//    we parse the on-disk bytes directly, which is what ships in sfx/*.sbf.)
//
// DATA BLOCK (at dataOffset) — format-specific 12-byte header then payload:
//   format 1 (single .wav):  u8 fmt; u8 pad[3]; u32 size; u32 ptr; then `size`
//                            bytes of a complete RIFF/WAVE file.
//   format 2 (variation):    u32 subCount; u32 ptr; u32 totalSize; then `subCount`
//                            x 12-byte sub-entries {u8 fmt; u8 pad[3]; u32 size;
//                            u32 ptr}; then each sub-entry's `size` bytes of a
//                            complete RIFF/WAVE file, concatenated in order.
//
// The embedded RIFF/WAVE payloads are real audio: PCM (audioFormat 1, 8/16-bit),
// IMA ADPCM (audioFormat 17), or MP3-in-RIFF / raw MP3. We DECODE the common 16-bit
// PCM case to interleaved S16 (what IAudioDevice consumes); ADPCM and MP3 payloads
// are detected and surfaced (codec field) but NOT decoded here — SAY SO: a full
// IMA-ADPCM / MP3 decoder is out of scope for this slice (the shipped UI/event SFX
// that matter for "a click plays a sound" are PCM in allgemein/duell banks).
// ----------------------------------------------------------------------------
#include "guild/common/types.h"
#include "shim/IAudioDevice.h"
#include "shim/IFileSystem.h"

#include <cstddef>
#include <string>
#include <vector>

namespace guild::play {

// Audio codec of an embedded RIFF/WAVE payload (audioFormat tag / sniffed).
enum class SbCodec {
    kUnknown = 0,
    kPcm     = 1,   // WAVE_FORMAT_PCM (audioFormat 1)
    kImaAdpcm = 17, // WAVE_FORMAT_IMA_ADPCM (audioFormat 17) — not decoded here
    kMp3     = 0x55,// MP3 (RIFF tag 0x55 or raw 0xFFFx frame sync) — not decoded
};

// One parsed bank entry (the 0x40-byte on-disk record, decoded), with the
// metadata of its FIRST audio block (the variation's first sub-entry for fmt 2).
struct RealSbEntry {
    std::string name;            // +0x00 sub-sample name
    u32         format = 0;      // +0x32 1 = single, 2 = variation
    u32         dataOffset = 0;  // +0x3C file offset of the data block
    u32         subCount = 1;    // variation sub-count (1 for fmt 1)
    // First playable block's RIFF/WAVE header fields (0 if not located):
    SbCodec     codec = SbCodec::kUnknown;
    u16         channels = 0;
    u32         sampleRate = 0;
    u16         bitsPerSample = 0;
    u32         pcmBytes = 0;     // bytes of PCM in the data chunk
    std::size_t pcmFileOffset = 0; // file offset of the PCM payload
};

// A fully parsed real sample bank.
struct RealSbBank {
    std::string              name;     // header +0x000
    std::vector<RealSbEntry> entries;  // index records
    bool                     ok = false;
};

// Parse a real .sbf bank from an in-memory buffer (the whole file). Mirrors the
// header + 0x40-entry index read by VIBE_Sound_LoadSampleBank, then resolves each
// entry's first audio block (the variation's first sub-entry for fmt 2) so callers
// can decode without a second pass. Returns false (out.ok=false) on a truncated
// header / entry array. The PCM itself is NOT copied — only located.
bool ParseRealSampleBank(const u8* data, std::size_t size, RealSbBank& out);

// Convenience: read a .sbf through the VFS/filesystem and parse it. `path` is a
// VFS path (e.g. "sfx/allgemein.sbf"). Returns false if the file is missing or
// malformed. Keeps the raw file bytes in `fileBytes` (the PCM lives there).
bool LoadRealSampleBank(shim::IFileSystem& fs, const char* path,
                        std::vector<u8>& fileBytes, RealSbBank& out);

// Decode one entry's first audio block to interleaved SIGNED 16-BIT PCM (what
// IAudioDevice::playSample consumes). `fileBytes` is the full .sbf buffer the
// entry's offsets index into. Only 16-bit PCM (codec kPcm) is decoded; for
// ADPCM/MP3 this returns false (and leaves `pcm` empty) — SAY SO.
//   On success: pcm = interleaved S16 (entry.channels), and outRate/outChannels
//   describe it.
bool DecodeEntryToPcm(const std::vector<u8>& fileBytes, const RealSbEntry& entry,
                      std::vector<i16>& pcm, int& outRate, int& outChannels);

// Find an entry by name (case-insensitive, like VIBE_Sound_FindSampleInBank).
const RealSbEntry* FindEntry(const RealSbBank& bank, const std::string& name);

// Result of a one-shot "play a sound" submission.
struct PlaySfxResult {
    bool        ok = false;     // sample found, decoded, and submitted
    std::string entryName;      // the entry that was played
    SbCodec     codec = SbCodec::kUnknown;
    int         sampleRate = 0;
    int         channels = 0;
    std::size_t pcmFrames = 0;  // decoded frames (per channel)
    std::size_t pcmBytes = 0;   // bytes handed to playSample
    shim::VoiceHandle voice = -1;
    bool        nonSilent = false; // decoded PCM had at least one non-zero sample
};

// "Play sfx by name": find `name` in `bank`, decode its first PCM block, alloc a
// voice on `dev`, and playSample() it. `dev` may be a real SdlAudioDevice or a
// capturing NullAudioDevice (headless). The device must already be init()'d at a
// rate/channel count; the PCM is submitted at its native rate (the SDL backend
// resamples). Returns a PlaySfxResult; ok=false if the sample is absent or its
// codec is not decodable here (ADPCM/MP3).
PlaySfxResult PlaySfxByName(shim::IAudioDevice& dev, const std::vector<u8>& fileBytes,
                            const RealSbBank& bank, const std::string& name,
                            int loops = 0);

} // namespace guild::play
