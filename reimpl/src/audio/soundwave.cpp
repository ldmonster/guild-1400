#include "audio/soundwave.h"

#include <cmath>
#include <cstring>

namespace guild::audio {

namespace {

u32 ReadLe32(const u8* p, std::size_t off) {
    return static_cast<u32>(p[off])
         | (static_cast<u32>(p[off + 1]) << 8)
         | (static_cast<u32>(p[off + 2]) << 16)
         | (static_cast<u32>(p[off + 3]) << 24);
}
u16 ReadLe16(const u8* p, std::size_t off) {
    return static_cast<u16>(p[off]) | (static_cast<u16>(p[off + 1]) << 8);
}
bool TagEq(const u8* p, std::size_t off, const char* tag) {
    return std::memcmp(p + off, tag, 4) == 0;
}

// flt_611588 = 0x40C90FDB = 6.2831855f (2*pi) — the period the engine divides by N.
constexpr float kTwoPi = 6.2831855f;

} // namespace

// gilde.exe 0x424d40 — VIBE_SoundWave_InitSineTables (d3sndw_Init)
// (__usercall, eax = init(a1=n@ax)); returns 1 on success / 0 when n<=4.
SineTables InitSineTables(u16 n) {
    SineTables t;
    // if ((u16)a1 <= 4) return 0;
    if (n <= 4)
        return t;

    t.count = n;                  // word_62D430 = a1
    t.sine.assign(n, 0.0f);       // dword_62D424
    t.aux1.assign(n, 0.0f);       // dword_62D428 (zeroed)
    t.aux2.assign(n, 0.0f);       // dword_62D42C (zeroed)

    // v18 = 2*pi / N; v19 = 0; for k in [0,N): table[k] = sin(v19); v19 += step.
    float step = kTwoPi / static_cast<float>(n);
    float phase = 0.0f;
    for (u16 k = 0; k < n; ++k) {
        t.sine[k] = std::sin(phase);
        phase += step;
    }
    return t;
}

// Canonical RIFF/WAVE header parse (the byte layout Miles consumes for .wav).
WavHeader ParseWavHeader(const u8* data, std::size_t size) {
    WavHeader h;
    if (!data || size < 0x2C)
        return h;
    if (!TagEq(data, 0x00, "RIFF") || !TagEq(data, 0x08, "WAVE"))
        return h;
    if (!TagEq(data, 0x0C, "fmt "))
        return h;

    u32 fmtSize = ReadLe32(data, 0x10);
    if (fmtSize < 16 || 0x14 + fmtSize > size)
        return h;

    h.audioFormat   = ReadLe16(data, 0x14);
    h.numChannels   = ReadLe16(data, 0x16);
    h.sampleRate    = ReadLe32(data, 0x18);
    h.byteRate      = ReadLe32(data, 0x1C);
    h.blockAlign    = ReadLe16(data, 0x20);
    h.bitsPerSample = ReadLe16(data, 0x22);

    // Scan chunks after the fmt chunk for "data" (skipping fact/LIST/etc.).
    std::size_t pos = 0x14 + fmtSize;
    while (pos + 8 <= size) {
        u32 chunkSize = ReadLe32(data, pos + 4);
        if (TagEq(data, pos, "data")) {
            std::size_t off = pos + 8;
            if (off > size)
                return h; // ok stays false
            h.dataSize = chunkSize;
            h.dataOffset = off;
            h.ok = true;
            return h;
        }
        // Advance past this chunk (chunks are word-aligned: pad odd sizes).
        std::size_t advance = 8 + chunkSize + (chunkSize & 1);
        if (advance == 0)
            break;
        pos += advance;
    }
    return h;
}

} // namespace guild::audio
