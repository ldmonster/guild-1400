#pragma once
// guild::audio — SoundWave helpers of gilde.exe (the d3sndw_* waveform layer)
// plus a faithful RIFF/WAVE header parser.
//
// Two concerns live here:
//
// 1) Sine/wave lookup tables (VIBE_SoundWave_InitSineTables @0x424d40,
//    VIBE_SoundWave_FreeTables @0x424eb4 / "d3sndw_Init"). The engine builds
//    three N-entry float tables; table[0] is filled with sin(k * 2*pi/N) for
//    k in [0,N), the other two zeroed. N must be > 4.
//
// 2) The WAV container the samples are stored in. The sample bank's entries name
//    ".wav" files (VIBE_Audio_PlayVoiceSample @0x44737c sets ".wav"/".mp3" and
//    calls AIL_set_named_sample_file); Miles parses the RIFF header. We recover
//    the canonical RIFF/WAVE/fmt /data header byte-for-byte so the replacement
//    backend can decode the same files: sample rate, channels, bits, data span.
//
// RIFF WAVE header (little-endian) — the bytes Miles/our backend consume:
//   +0x00  'R''I''F''F'           ChunkID
//   +0x04  u32  riffSize          file size - 8
//   +0x08  'W''A''V''E'           Format
//   +0x0C  'f''m''t'' '           Subchunk1ID  ("fmt ")
//   +0x10  u32  fmtSize           16 for PCM
//   +0x14  u16  audioFormat       1 = PCM
//   +0x16  u16  numChannels       1=mono, 2=stereo
//   +0x18  u32  sampleRate        e.g. 44100
//   +0x1C  u32  byteRate          sampleRate*channels*bits/8
//   +0x20  u16  blockAlign        channels*bits/8
//   +0x22  u16  bitsPerSample     8/16
//   +0x24  'd''a''t''a'           Subchunk2ID   (may follow extra chunks)
//   +0x28  u32  dataSize          bytes of PCM
//   +0x2C  ...  PCM data
// (Extra chunks before "data" — e.g. "fact"/"LIST" — are skipped by chunk size.)
#include "guild/common/types.h"
#include <cstddef>
#include <vector>

namespace guild::audio {

// VIBE_SoundWave_InitSineTables @0x424d40 builds tables sized by this count.
// Three tables of N floats; the engine passes the table length in ax.
struct SineTables {
    std::vector<float> sine;  // dword_62D424: sin(k * 2pi/N)
    std::vector<float> aux1;  // dword_62D428: zeroed
    std::vector<float> aux2;  // dword_62D42C: zeroed
    u16 count = 0;            // word_62D430
    bool valid() const { return count > 4 && !sine.empty(); }
};

// VIBE_SoundWave_InitSineTables @0x424d40 — allocate three N-float tables and
// fill table0 with sin(k * step), step = 2*pi/N. Returns a SineTables with
// valid()==false when n <= 4 (original returns 0 in that case).
SineTables InitSineTables(u16 n);

// Parsed RIFF/WAVE header fields (the subset the engine/backend needs).
struct WavHeader {
    bool ok = false;
    u16  audioFormat = 0;   // 1 = PCM
    u16  numChannels = 0;
    u32  sampleRate  = 0;
    u32  byteRate    = 0;
    u16  blockAlign  = 0;
    u16  bitsPerSample = 0;
    u32  dataSize    = 0;       // bytes of PCM
    std::size_t dataOffset = 0; // byte offset of the PCM payload in the buffer
};

// Parse a canonical RIFF/WAVE header from a byte buffer. Validates the "RIFF",
// "WAVE" and "fmt " tags, reads the fmt chunk, then scans chunks (skipping any
// non-"data" chunk by its size) to locate "data". Returns ok=false on any
// malformed/truncated header. Mirrors the layout Miles consumes for .wav files.
WavHeader ParseWavHeader(const u8* data, std::size_t size);

} // namespace guild::audio
