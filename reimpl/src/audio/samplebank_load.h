#pragma once
// guild::audio — sample-bank (.sbf) FILE LOADER of gilde.exe.
//
// A ".sbf" sample bank is a tiny binary index file read by
//   VIBE_Sound_LoadSampleBank @0x446b2c  (sb_LoadSampleBank)
//   VIBE_Sound_LoadEntry      @0x446830  (sb_LoadEntry)
// It does NOT contain PCM; it indexes named ".wav"/".mp3" sub-samples whose audio
// payload is faulted in on demand from companion files. The original reads it
// field-by-field through VIBE_File_Read; here we parse the same bytes from an
// in-memory buffer so the format is testable without the OS file layer.
//
// ----------------------------------------------------------------------------
// Recovered byte format (little-endian, 32-bit x86, MSVC):
//
// BANK HEADER  — 0x144 (324) bytes, read in one VIBE_File_Read(buf, 0x144, 1):
//   +0x000  char name[50]   bank name (StrNCopyPad to 50 bytes into bank+0)
//   ...                      (padding / reserved header fields, not interpreted)
//   +0x134  u32  entryCount  number of 0x40-byte entry records that follow
//   (everything else in the 324-byte header is opaque and discarded)
//
// In the live bank object the loader keeps:
//   bank+0x000 char name[50]      (from header+0)
//   bank+0x032 char path[256]     full path "<sfxDir><name>" (StrNCopyPad 256)
//   bank+0x134 u32  entryCount    (= header+0x134)
//   bank+0x138 Entry entries[]    (bank+324, contiguous)
//   bank+0x13C u32  loaded flag   (=1 once entries read; bank+0x140 = +316 next)
//   bank+0x140 ptr  nextBank      (bank[79], +0x13C linked list, here a vector)
//
// ENTRY RECORD — 0x40 (64) bytes each, read in one VIBE_File_Read(buf, 0x40, count):
//   +0x00  ... (opaque leading bytes)
//   +0x04  char name[50]   sub-sample name (matched via StrCmpNoCaseN, 50 bytes)
//   +0x36  u8   format      54: 1 = single .wav sample, 2 = .mp3 variation group
//   +0x38  ptr  data        56: faulted PCM/variation block (0 until LoadEntry)
//   +0x3C  u32  timestamp   60: last-touch tick (13 * timer); 0 here
// (offsets +0x36/+0x38/+0x3C are bytes 54/56/60 as used by the original.)
//
// ENTRY DATA (faulted by VIBE_Sound_LoadEntry, format-specific 12-byte header):
//   format 1 (.wav single):  u8 fmt; u8 pad[3]; u32 size; ptr payload; then `size`
//                            bytes of PCM (here we keep size + payload offset).
//   format 2 (.mp3/variation): u32 subCount; ptr table; u32 sizeAccum; then
//                            `subCount` 12-byte sub-entries {u8 fmt; u32 size;
//                            ptr data} with cumulative payload offsets.
// We model the faulted side as SbEntryData; the on-disk index parse is the focus.
// ----------------------------------------------------------------------------
#include "guild/common/types.h"
#include <cstddef>
#include <string>
#include <vector>

namespace guild::audio {

// Strides/offsets recovered from the loader (see header comment).
constexpr int kSbHeaderSize   = 0x144; // 324 — bank header record on disk
constexpr int kSbNameLen      = 50;    // name fields (StrNCopyPad to 50)
constexpr int kSbCountOffset  = 0x134; // 308 — entryCount within the header
constexpr int kSbEntrySize    = 0x40;  // 64  — one entry record
constexpr int kSbEntryNameOff = 0x04;  // 4   — entry name field
constexpr int kSbEntryFmtOff  = 0x36;  // 54  — entry format byte
constexpr int kSbBankBaseSize = 0x144; // 324 — bank object base before entries

// Entry format byte values (entry+0x36 / +54).
enum SbFormat : u8 {
    kSbFormatWav = 1, // single .wav sample
    kSbFormatMp3 = 2, // .mp3 / variation group (multiple alternatives)
};

// One parsed index entry (the 64-byte on-disk record, decoded).
struct SbEntry {
    std::string name;          // +0x04  matched name (NUL-trimmed, <=50)
    u8          format = 0;    // +0x36  1=wav, 2=mp3/variation
    // The faulted data side stays null until LoadEntry; loaders may populate it.
    bool        loaded = false; // bank entry+0x38 != 0
};

// A parsed sample bank: name + path + the entry index.
struct SbBank {
    std::string         name;        // +0x000  bank name (from header)
    std::string         path;        // +0x032  full path on disk
    std::vector<SbEntry> entries;    // +0x138  index records
    bool                loaded = false; // +0x13C  set once entries are read
};

// VIBE_Sound_LoadSampleBank @0x446b2c — parse a .sbf bank from a raw byte buffer.
// Mirrors the original's two reads: a 0x144-byte header (entry count at +0x134)
// then `count` x 0x40-byte entry records. Returns false (and leaves `out` empty)
// if the buffer is too short for the header or for the declared entry array —
// matching the original, which fails the load when VIBE_File_Read returns short.
// `name`/`path` seed the bank's identity fields (header+0 and the composed path).
bool LoadSampleBankFromBuffer(const u8* data, std::size_t size,
                              const std::string& path, SbBank& out);

// VIBE_Sound_FindSampleInBank @0x446288 — first entry whose name matches `name`
// case-insensitively (StrCmpNoCaseN over 50 bytes). Returns nullptr if absent.
const SbEntry* FindSampleInBank(const SbBank& bank, const std::string& name);

// VIBE_Audio_SampleIsMp3 @0x4471f0 — true iff the named entry exists and its
// format byte is kSbFormatMp3 (2). Original returns 1 when audio is uninitialized
// (treated as "not a resolvable wav"); here `audioReady=false` reproduces that.
bool SampleIsMp3(const SbBank& bank, const std::string& name, bool audioReady = true);

} // namespace guild::audio
