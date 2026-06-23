#include "world/statistic_recon_dump.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace guild::world {

namespace {

// Little-endian field reads off the raw 536-byte person record (the originals index
// the record as u16*/u8*/dword* at fixed byte offsets; we mirror those reads).
inline u16 RdU16(const u8* p, std::size_t off) {
    return static_cast<u16>(p[off]) | (static_cast<u16>(p[off + 1]) << 8);
}
inline u32 RdU32(const u8* p, std::size_t off) {
    return static_cast<u32>(p[off]) | (static_cast<u32>(p[off + 1]) << 8) |
           (static_cast<u32>(p[off + 2]) << 16) |
           (static_cast<u32>(p[off + 3]) << 24);
}
inline i32 RdI32(const u8* p, std::size_t off) {
    return static_cast<i32>(RdU32(p, off));
}
inline float RdF32(const u8* p, std::size_t off) {
    float f;
    std::uint32_t bits = RdU32(p, off);
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// VIBE_Crt_Sprintf_0 @0x5cba00 — the original is a thin MSVCRT vsprintf wrapper.
// Reproduced as a forwarding sprintf so the produced text matches byte-for-byte.
template <typename... Args>
inline int CrtSprintf(char* out, const char* fmt, Args... args) {
    return std::sprintf(out, fmt, args...);
}

}  // namespace

// gilde.exe 0x594fd0
int StatDumpRoundHeader(char* out, const StatDumpEnv& env) {
    return CrtSprintf(out, "++++++++++ Begin statistic dump in round %i +++++++++",
                      env.CurrentRound());
}

// gilde.exe 0x5953bc
int StatDumpRoundFooter(char* out, const StatDumpEnv& env) {
    return CrtSprintf(out, "++++++++++ End statistic dump in round %i +++++++++",
                      env.CurrentRound());
}

// gilde.exe 0x594ff8
int StatDumpNpcIdentity(const u8* record, char* out, const StatDumpEnv& env) {
    // typeByte = record[+2]; <10 -> kind table; else dynamic name pointer.
    u8 typeByte = record[2];
    const char* kind = env.IdentityKindName(typeByte);

    // Bitfields packed as the high byte of a dword read at a byte offset:
    //   class    : *(int*)(record+6)   >> 24  == record[+9]
    //   origin   : *(int*)(record+9)   >> 24  == record[+12]
    //   religion : record[+13]
    //   status   : *(int*)(record+356) >> 24  == record[+359]   (a1+177 word = +354; +356 dword>>24 -> +359)
    //   location : *(int*)(record+354) >> 24  == record[+357]
    // The decompile expresses these as `*(int*)addr >> 24`; on little-endian that
    // selects the byte at addr+3 (arithmetic shift, but the value is a u8 index here).
    u8 classIdx = static_cast<u8>(RdI32(record, 6) >> 24);
    u8 originIdx = static_cast<u8>(RdI32(record, 9) >> 24);
    u8 religionIdx = record[13];
    u8 statusIdx = static_cast<u8>(RdI32(record, 356) >> 24);
    u8 locationIdx = static_cast<u8>(RdI32(record, 354) >> 24);

    return CrtSprintf(
        out, "%i\t%i\t%i\t%s\t%s\t%i\t%s\t%i\t%s\t%s\t%s\t%s\t",
        env.CurrentRound(),
        RdI32(record, 4),                       // id  (*((_DWORD*)a1 + 1))
        static_cast<int>(RdU16(record, 0)),     // marker (*a1)
        kind,                                   // identity kind name
        reinterpret_cast<const char*>(record) + 48,  // name (a1 + 48)
        static_cast<int>(record[8]),            // byte +8
        env.ClassName(classIdx),
        static_cast<int>(RdU16(record, 10)),    // word +10 (a1[5])
        env.OriginName(originIdx),
        env.ReligionName(religionIdx),
        env.StatusName(statusIdx),
        env.LocationName(locationIdx));
}

// gilde.exe 0x5950a4
int StatDumpNpcAttributes(const u8* record, char* out, const StatDumpEnv& env) {
    (void)env;
    // The assembled 6th double: lo = dword@+36, hi = (i32)(dword@+38) >> 16  (arith).
    // Reproduce the exact raw double the original places on the stack.
    std::uint64_t lo = RdU32(record, 36);
    std::uint64_t hi = static_cast<std::uint64_t>(
        static_cast<std::uint32_t>(RdI32(record, 38) >> 16));
    std::uint64_t rawDouble = lo | (hi << 32);
    double assembled;
    std::memcpy(&assembled, &rawDouble, sizeof(assembled));

    // v6 == [ebp-Ch] is uninitialized in the original; the %i prints stack garbage.
    // We cannot reproduce indeterminate stack contents faithfully, so emit 0 for the
    // 7th (junk) field — documented divergence, value is never read downstream.
    int v6 = 0;

    CrtSprintf(out, "%5.2f\t %5.2f\t %5.2f\t %5.2f\t %5.2f\t %5.2f\t %i\t",
               static_cast<double>(RdF32(record, 16)),
               static_cast<double>(RdF32(record, 20)),
               static_cast<double>(RdF32(record, 24)),
               static_cast<double>(RdF32(record, 28)),
               static_cast<double>(RdF32(record, 32)),
               assembled,
               v6);

    // Second write appends nine packed bitfields of the dword at +44.
    // Order matches the original push sequence (format-arg order):
    //   f0 = field & 0xF
    //   f1 = (u32)(field << 0x18) >> 0x1C
    //   f2 = (u32)(field << 0x14) >> 0x1C
    //   f3 = (u32)(field << 0x12) >> 0x1E
    //   f4 = (u32)(field << 0x0F) >> 0x1D
    //   f5 = (u32)(field << 0x0C) >> 0x1D
    //   f6 = (u32)(field << 0x09) >> 0x1D
    //   f7 = (u32)(field << 0x07) >> 0x1E
    //   f8 = (u32)(field << 0x03) >> 0x1C
    std::uint32_t field = RdU32(record, 44);
    int f0 = static_cast<int>(field & 0xFu);
    int f1 = static_cast<int>((field << 0x18) >> 0x1C);
    int f2 = static_cast<int>((field << 0x14) >> 0x1C);
    int f3 = static_cast<int>((field << 0x12) >> 0x1E);
    int f4 = static_cast<int>((field << 0x0F) >> 0x1D);
    int f5 = static_cast<int>((field << 0x0C) >> 0x1D);
    int f6 = static_cast<int>((field << 0x09) >> 0x1D);
    int f7 = static_cast<int>((field << 0x07) >> 0x1E);
    int f8 = static_cast<int>((field << 0x03) >> 0x1C);

    return CrtSprintf(out + std::strlen(out),
                      "%i\t %i\t %i\t %i\t %i\t %i\t %i\t %i\t %i\t",
                      f0, f1, f2, f3, f4, f5, f6, f7, f8);
}

// gilde.exe 0x595168
int StatDumpNpcNeeds(const u8* record, char* out, const StatDumpEnv& env) {
    (void)env;
    return CrtSprintf(out, "%i\t%i\t%i\t%i\t%i\t",
                      static_cast<int>(record[128]),
                      static_cast<int>(record[129]),
                      static_cast<int>(record[130]),
                      static_cast<int>(record[131]),
                      static_cast<int>(record[132]));
}

// gilde.exe 0x5951ac
int StatDumpNpcTraits(const u8* record, char* out, const StatDumpEnv& env) {
    return CrtSprintf(out, "%s\t%s\t%s\t%s\t",
                      env.TraitName(record[358]),
                      env.TraitName(record[359]),
                      env.TraitName(record[360]),
                      env.TraitName(record[361]));
}

// gilde.exe 0x595208
int StatDumpNpcSkills(const u8* record, char* out, const StatDumpEnv& env) {
    // ecx == record; ax == record[+0] (the u16 marker) for ComputeTotalWealth.
    // The original computes wealth first (eax=ax), then currency (eax=record):
    //   v7 = ComputeTotalWealth(*a1, record);
    //   v3 = SumCurrencyHeld(record);
    int wealth = env.ComputeTotalWealth(RdU16(record, 0), record);
    int currency = env.SumCurrencyHeld(record);

    // Six skill dwords at +0x190 (=400), +0x194, +0x198, +0x19C, +0x1A0, +0x1A4.
    return CrtSprintf(out, "%i\t%i\t%i\t%i\t%i\t%i\t%i\t%i\t",
                      RdI32(record, 0x190),
                      RdI32(record, 0x194),
                      RdI32(record, 0x198),
                      RdI32(record, 0x19C),
                      RdI32(record, 0x1A0),
                      RdI32(record, 0x1A4),
                      currency,
                      wealth);
}

// gilde.exe 0x595260
int StatDumpNpcInventory(const u8* record, char* out, const StatDumpEnv& env) {
    // Walk the 8 equipment-slot ids: id_k at +92 + 4*k for k = 0..7 (loop end +124).
    char scratch[276];
    char result = 0;
    for (int k = 0; k < 8; ++k) {
        i32 id = RdI32(record, 92 + 4 * k);
        const u8* found = env.FindRecordById(id);
        if (found) {
            CrtSprintf(scratch, "%i\t%s", RdI32(found, 4),
                       reinterpret_cast<const char*>(found) + 48);
        } else {
            CrtSprintf(scratch, "%i\t%s", -1, "NIEMAND");
        }
        // strcat(out, scratch) done two bytes at a time exactly as the original; the
        // returned `result` is the last byte copied (the original's `al`).
        char* dst = out + std::strlen(out);
        char* src = scratch;
        for (;;) {
            result = *src;
            dst[0] = src[0];
            if (!result) break;
            result = src[1];
            dst[1] = result;
            src += 2;
            dst += 2;
            if (!result) break;
        }
    }
    return static_cast<unsigned char>(result);
}

// gilde.exe 0x5952dc
int StatDumpNpcRecord(const u8* record, char* out, const StatDumpEnv& env) {
    if (record && RdU16(record, 0) != 0xFFFF) {
        // VIBE_Light_SetGrayColorThunk(0, 8120, out) zero-fills out[0..8120).
        std::memset(out, 0, 8120);
        StatDumpNpcIdentity(record, out + std::strlen(out), env);
        StatDumpNpcAttributes(record, out + std::strlen(out), env);
        StatDumpNpcNeeds(record, out + std::strlen(out), env);
        StatDumpNpcTraits(record, out + std::strlen(out), env);
        (void)std::strlen(out);  // the original's stray strlen (result discarded)
        StatDumpNpcSkills(record, out + std::strlen(out), env);
        return StatDumpNpcInventory(record, out + std::strlen(out), env);
    }
    // record==nullptr -> LOBYTE(a1)==0; record!=nullptr but marker==0xFFFF -> low
    // byte of the pointer (indeterminate). We return 0 for the not-dumped case.
    return 0;
}

}  // namespace guild::world
