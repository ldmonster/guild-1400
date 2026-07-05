#include "play/real_audio.h"

#include "audio/soundwave.h" // ParseWavHeader (RIFF/WAVE) — reused, not modified

#include <cstring>

namespace guild::play {

namespace {

u32 ReadLe32(const u8* p, std::size_t off) {
    return static_cast<u32>(p[off])
         | (static_cast<u32>(p[off + 1]) << 8)
         | (static_cast<u32>(p[off + 2]) << 16)
         | (static_cast<u32>(p[off + 3]) << 24);
}

// On-disk strides recovered from VIBE_Sound_LoadSampleBank @0x446b2c and
// VIBE_Sound_LoadEntry @0x446830 (verified against a real
// europe_guild_1400_original/sfx/*.sbf hexdump):
//   header 0x144 bytes (entryCount u32 @0x134), then count 0x40-byte entries.
//   Entry record (LoadEntry): +0 u32 data-block file offset (the
//   VIBE_File_Seek(*(_DWORD*)entry, 0) target @0x44688c), +4 name (50),
//   +54 format byte (1 single / 2 variation, @0x44689d), +60 runtime scratch
//   (written 13*dword_62EB38 @0x44697a — NOT persisted).
constexpr std::size_t kHeaderSize   = 0x144; // 324
constexpr std::size_t kEntryBase    = 0x144; // entry table starts right after the header
constexpr std::size_t kEntrySize    = 0x40;  // 64
constexpr std::size_t kCountOff     = 0x134; // entryCount within the header
constexpr std::size_t kNameLen      = 50;
constexpr std::size_t kEntryNameOff = 0x04;  // name follows the data-offset dword
constexpr std::size_t kEntryFmtOff  = 0x36;  // format (byte @entry+54)
constexpr std::size_t kEntryDataOff = 0x00;  // u32 file offset of the data block
constexpr std::size_t kBlockHdr     = 12;    // 12-byte data-block / sub-entry header

std::string DecodeName(const u8* p, std::size_t avail, std::size_t max) {
    std::size_t n = avail < max ? avail : max;
    std::size_t len = 0;
    while (len < n && p[len] != 0)
        ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }

// Classify a RIFF/WAVE audioFormat tag (or a raw MP3 frame) into SbCodec.
SbCodec CodecFromAudioFormat(u16 audioFormat) {
    switch (audioFormat) {
        case 1:    return SbCodec::kPcm;
        case 17:   return SbCodec::kImaAdpcm;
        case 0x55: return SbCodec::kMp3;
        default:   return SbCodec::kUnknown;
    }
}

// Locate the first RIFF/WAVE payload of an entry's data block and fill the audio
// metadata (codec/channels/rate/bits/pcm span). `blockOff` is the file offset of
// the 12-byte data-block header. Returns true if a RIFF/WAVE was located.
bool ResolveBlock(const u8* data, std::size_t size, u32 format,
                  std::size_t blockOff, RealSbEntry& e) {
    if (blockOff + kBlockHdr > size)
        return false;

    std::size_t riffOff;
    if (format == 2) {
        // variation: u32 subCount; u32 ptr; u32 totalSize; then subCount 12-byte
        // sub-entries; then the concatenated RIFF payloads.
        u32 subCount = ReadLe32(data, blockOff);
        if (subCount == 0 || subCount > 4096)
            return false; // implausible — treat as malformed
        e.subCount = subCount;
        riffOff = blockOff + kBlockHdr +
                  static_cast<std::size_t>(subCount) * kBlockHdr;
    } else {
        // single: u8 fmt; u8 pad[3]; u32 size; u32 ptr; then the RIFF payload.
        e.subCount = 1;
        riffOff = blockOff + kBlockHdr;
    }

    if (riffOff + 4 > size || std::memcmp(data + riffOff, "RIFF", 4) != 0) {
        // Not a RIFF container — could be raw MP3 (0xFF 0xFx frame sync).
        if (riffOff + 2 <= size && data[riffOff] == 0xFF &&
            (data[riffOff + 1] & 0xE0) == 0xE0) {
            e.codec = SbCodec::kMp3;
        }
        return false;
    }

    audio::WavHeader h = audio::ParseWavHeader(data + riffOff, size - riffOff);
    if (!h.ok)
        return false;
    e.codec         = CodecFromAudioFormat(h.audioFormat);
    e.channels      = h.numChannels;
    e.sampleRate    = h.sampleRate;
    e.bitsPerSample = h.bitsPerSample;
    e.pcmBytes      = h.dataSize;
    e.pcmFileOffset = riffOff + h.dataOffset;
    // Clamp the PCM span to the buffer (guards against a corrupt data chunk size).
    if (e.pcmFileOffset > size) {
        e.pcmBytes = 0;
    } else if (e.pcmFileOffset + e.pcmBytes > size) {
        e.pcmBytes = static_cast<u32>(size - e.pcmFileOffset);
    }
    return true;
}

} // namespace

bool ParseRealSampleBank(const u8* data, std::size_t size, RealSbBank& out) {
    out = RealSbBank{};
    if (!data || size < kHeaderSize)
        return false;

    out.name = DecodeName(data, size, kNameLen);
    u32 count = ReadLe32(data, kCountOff);

    // The whole entry array must be present (VIBE_File_Read(...,0x40,count)==count).
    std::size_t entriesEnd = kEntryBase + static_cast<std::size_t>(count) * kEntrySize;
    if (count > 100000 || entriesEnd > size)
        return false;

    out.entries.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const u8* rec = data + kEntryBase + static_cast<std::size_t>(i) * kEntrySize;
        RealSbEntry e;
        e.name       = DecodeName(rec + kEntryNameOff, kNameLen, kNameLen);
        e.format     = rec[kEntryFmtOff];               // byte @entry+54 (0x44689d)
        e.dataOffset = ReadLe32(rec, kEntryDataOff);    // seek target (0x44688c)
        // Resolve the first audio block (best-effort; leaves codec=kUnknown if the
        // offset is bogus, as happens for a couple of stray entry slots on disk).
        if (e.dataOffset >= kHeaderSize && e.dataOffset < size)
            ResolveBlock(data, size, e.format, e.dataOffset, e);
        out.entries.push_back(std::move(e));
    }
    out.ok = true;
    return true;
}

bool LoadRealSampleBank(shim::IFileSystem& fs, const char* path,
                        std::vector<u8>& fileBytes, RealSbBank& out) {
    out = RealSbBank{};
    fileBytes.clear();
    shim::IFile* f = fs.open(path, "rb");
    if (!f)
        return false;
    std::int64_t sz = f->size();
    if (sz <= 0) { fs.close(f); return false; }
    fileBytes.resize(static_cast<std::size_t>(sz));
    std::size_t got = f->read(fileBytes.data(), fileBytes.size());
    fs.close(f);
    if (got != fileBytes.size())
        return false;
    return ParseRealSampleBank(fileBytes.data(), fileBytes.size(), out);
}

bool DecodeEntryToPcm(const std::vector<u8>& fileBytes, const RealSbEntry& entry,
                      std::vector<i16>& pcm, int& outRate, int& outChannels) {
    pcm.clear();
    outRate = 0;
    outChannels = 0;
    if (entry.codec != SbCodec::kPcm)
        return false; // ADPCM / MP3 / unknown — not decoded here (SAY SO)
    if (entry.bitsPerSample != 16)
        return false; // only 16-bit PCM is handed straight to IAudioDevice (S16)
    if (entry.pcmBytes == 0 || entry.channels == 0)
        return false;
    if (entry.pcmFileOffset + entry.pcmBytes > fileBytes.size())
        return false;

    std::size_t nSamples = entry.pcmBytes / sizeof(i16);
    pcm.resize(nSamples);
    // The .wav payload is little-endian S16; on a little-endian host this is a
    // straight copy. Read byte-wise so it is endian-correct everywhere.
    const u8* src = fileBytes.data() + entry.pcmFileOffset;
    for (std::size_t i = 0; i < nSamples; ++i) {
        pcm[i] = static_cast<i16>(static_cast<u16>(src[2 * i]) |
                                  (static_cast<u16>(src[2 * i + 1]) << 8));
    }
    outRate = static_cast<int>(entry.sampleRate);
    outChannels = static_cast<int>(entry.channels);
    return true;
}

const RealSbEntry* FindEntry(const RealSbBank& bank, const std::string& name) {
    for (const auto& e : bank.entries) {
        if (e.name.size() != name.size())
            continue;
        bool eq = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (LowerAscii(e.name[i]) != LowerAscii(name[i])) { eq = false; break; }
        }
        if (eq)
            return &e;
    }
    return nullptr;
}

PlaySfxResult PlaySfxByName(shim::IAudioDevice& dev, const std::vector<u8>& fileBytes,
                            const RealSbBank& bank, const std::string& name,
                            int loops) {
    PlaySfxResult r;
    const RealSbEntry* e = FindEntry(bank, name);
    if (!e)
        return r;
    r.entryName = e->name;
    r.codec     = e->codec;

    std::vector<i16> pcm;
    int rate = 0, ch = 0;
    if (!DecodeEntryToPcm(fileBytes, *e, pcm, rate, ch))
        return r; // unsupported codec or empty — ok stays false

    r.sampleRate = rate;
    r.channels   = ch;
    r.pcmFrames  = ch > 0 ? pcm.size() / static_cast<std::size_t>(ch) : 0;
    r.pcmBytes   = pcm.size() * sizeof(i16);
    for (i16 s : pcm) {
        if (s != 0) { r.nonSilent = true; break; }
    }

    shim::VoiceHandle v = dev.allocVoice();
    if (v < 0)
        return r; // no free voice
    r.voice = v;
    dev.setVolume(v, 127);
    dev.playSample(v, pcm.data(), r.pcmBytes, rate, loops);
    r.ok = true;
    return r;
}

} // namespace guild::play
