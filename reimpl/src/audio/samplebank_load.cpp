#include "audio/samplebank_load.h"

#include <cstring>

namespace guild::audio {

namespace {

// Read a little-endian u32 at `off` (the original is 32-bit x86 LE).
u32 ReadLe32(const u8* p, std::size_t off) {
    return static_cast<u32>(p[off])
         | (static_cast<u32>(p[off + 1]) << 8)
         | (static_cast<u32>(p[off + 2]) << 16)
         | (static_cast<u32>(p[off + 3]) << 24);
}

// VIBE_Util_StrNCopyPad-style decode: take up to `max` bytes, stop at the first
// NUL, return the resulting string. The original zero-pads to `max`; we only
// need the logical string for comparisons.
std::string DecodeName(const u8* p, std::size_t avail, std::size_t max) {
    std::size_t n = avail < max ? avail : max;
    std::size_t len = 0;
    while (len < n && p[len] != 0)
        ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

} // namespace

// gilde.exe 0x446b2c — VIBE_Sound_LoadSampleBank (sb_LoadSampleBank)
// (__usercall, eax = parse(a1=path@eax, a2=loadEntries@edx); here from a buffer)
bool LoadSampleBankFromBuffer(const u8* data, std::size_t size,
                              const std::string& path, SbBank& out) {
    out = SbBank{};
    out.path = path;

    // VIBE_File_Read(buf, 0x144, 1) == 1 — must have the full header.
    if (!data || size < static_cast<std::size_t>(kSbHeaderSize))
        return false;

    // bank name = StrNCopyPad(bank+0, header+0, 50).
    out.name = DecodeName(data, size, kSbNameLen);

    // entryCount at header+0x134 (stored to bank+0x308 in the original).
    u32 count = ReadLe32(data, kSbCountOffset);

    // VIBE_File_Read(bank+324, 0x40, count) == count — the whole array must be
    // present. (The original allocs (count<<6)+324 and reads count entries.)
    std::size_t entriesBytes = static_cast<std::size_t>(count) * kSbEntrySize;
    if (size < static_cast<std::size_t>(kSbBankBaseSize) + entriesBytes)
        return false;

    out.entries.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const u8* rec = data + kSbBankBaseSize + static_cast<std::size_t>(i) * kSbEntrySize;
        SbEntry e;
        e.name   = DecodeName(rec + kSbEntryNameOff, kSbEntrySize - kSbEntryNameOff, kSbNameLen);
        e.format = rec[kSbEntryFmtOff];
        e.loaded = false;
        out.entries.push_back(std::move(e));
    }

    out.loaded = true; // bank+0x13C = 1
    return true;
}

// gilde.exe 0x446288 — VIBE_Sound_FindSampleInBank
// (__usercall, eax = find(a1=bank@eax, a2=name@edx))
const SbEntry* FindSampleInBank(const SbBank& bank, const std::string& name) {
    // Original walks bank+312 entries, comparing the +4 name field with
    // VIBE_Util_StrCmpNoCaseN(..., 50). First case-insensitive match wins.
    for (const auto& e : bank.entries) {
        if (e.name.size() == name.size()) {
            bool eq = true;
            for (std::size_t i = 0; i < e.name.size(); ++i) {
                char a = e.name[i], b = name[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                if (a != b) { eq = false; break; }
            }
            if (eq)
                return &e;
        }
    }
    return nullptr;
}

// gilde.exe 0x4471f0 — VIBE_Audio_SampleIsMp3
// (__usercall, eax = isMp3(a1=bank@eax, a2=name@edx))
bool SampleIsMp3(const SbBank& bank, const std::string& name, bool audioReady) {
    // Original: result=1; if(dword_62E8FC){ s=FindSampleInBank; if(!s||s[54]!=2)
    // return 0; } return result;  -> only true when entry exists AND format==2.
    if (!audioReady)
        return true; // mirrors the "audio uninitialized => return 1" branch
    const SbEntry* e = FindSampleInBank(bank, name);
    if (!e || e->format != kSbFormatMp3)
        return false;
    return true;
}

} // namespace guild::audio
