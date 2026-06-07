#include "util/util_misc.h"
#include "crt/rand.h"

namespace guild::util {

// gilde.exe 0x4029c0 — VIBE_Util_RetZero
int RetZero() { return 0; }

// gilde.exe 0x527d9c — VIBE_Util_RetOne
int RetOne() { return 1; }

// gilde.exe 0x140b000 — VIBE_Util_MemFindPattern (__stdcall(a1,a2,a3,a4))
//   a1=haystack, a2=needle, a3=hayLen, a4=needleLen.
//   v6 walks the haystack, v5 walks the needle; on mismatch v5 resets to 0.
//   When v5 reaches needleLen the match end is at v6, so the start is a1+v6-a4.
const void* MemFindPattern(const void* haystack, const void* needle,
                           int hayLen, int needleLen) {
    const u8* h = static_cast<const u8*>(haystack);
    const u8* n = static_cast<const u8*>(needle);
    int v6 = 0; // haystack cursor
    int v5 = 0; // needle cursor
    while (v6 < hayLen && v5 < needleLen) {
        if (h[v6] == n[v5])
            ++v5;
        else
            v5 = 0;
        ++v6;
    }
    if (v5 == needleLen)               // matched the whole needle
        return h + v6 - needleLen;     // a1 + v6 - a4
    return nullptr;                    // return 0
}

// ---- MSVC 214013 LCG (VIBE_Rand_Next @0x142214e / SetSeed @0x1422144) ---------
namespace {
i32 g_msvcRandState = 0; // dword_1452BD0
} // namespace

// gilde.exe 0x142214e — VIBE_Rand_Next
int MsvcRandNext() {
    g_msvcRandState = 214013 * g_msvcRandState + 2531011;
    return (g_msvcRandState >> 16) & 0x7FFF;
}

// gilde.exe 0x1422144 — VIBE_Rand_SetSeed
void SetMsvcRandSeed(i32 seed) { g_msvcRandState = seed; }

UtilMiscHooks& GetUtilMiscHooks() {
    static UtilMiscHooks hooks{ &MsvcRandNext };
    return hooks;
}

// gilde.exe 0x140b530 — VIBE_Util_RandomMod (__thiscall(this, a2))
//   v3 = VIBE_Rand_Next() << 16;  return (VIBE_Rand_Next() | v3) % a2;
u32 RandomMod(u32 mod) {
    int (*rng)() = GetUtilMiscHooks().randNext;
    u32 hi = static_cast<u32>(rng()) << 16;          // v3
    u32 v = static_cast<u32>(rng()) | hi;            // VIBE_Rand_Next() | v3
    return v % mod;
}

// gilde.exe 0x1412290 — VIBE_Util_BuildCrc16Table (poly 0x1021, MSB-first)
void BuildCrc16Table(u16* table16) {
    for (int i = 0; i < 256; ++i) {
        i16 v2 = static_cast<i16>(static_cast<u16>(i) << 8); // (_WORD)i << 8
        for (int j = 0; j < 8; ++j) {
            if (v2 < 0)                                       // high bit set
                v2 = static_cast<i16>((2 * v2) ^ 0x1021);
            else
                v2 = static_cast<i16>(2 * v2);
        }
        table16[i] = static_cast<u16>(v2);
    }
}

// gilde.exe 0x14155b0 — VIBE_Util_RotateByte (__stdcall(a1,a2))
//   return (u8)(a1 << (a2 % 8)) | (((a1 << (a2 % 8)) & 0xFF00) >> 8);
int RotateByte(int value, int shift) {
    int s = shift % 8;
    int shifted = value << s;
    return static_cast<u8>(shifted) | ((shifted & 0xFF00) >> 8);
}

// gilde.exe 0x14155f0 — VIBE_Util_AlignTo8 (__stdcall(a1,a2))
int AlignTo8(int value, int shift) {
    return RotateByte(value, 8 - shift % 8);
}

// gilde.exe 0x58b9c4 — VIBE_Util_InitAndShuffleByteArray (__usercall a1@eax, a2@edx)
//   for i in [0,count): arr[i]=i
//   if count >= 2: do ((count>>2 clamp>=1) + (count>>1)) successful swaps using
//   two RandNext()%count draws; a swap counts only when the indices differ.
void InitAndShuffleByteArray(u8 count, u8* arr) {
    for (u8 i = 0; i < count; ++i)
        arr[i] = i;
    if (count < 2)
        return;
    int quarter = static_cast<int>(count) >> 2;
    if (quarter <= 1) quarter = 1;
    int swaps = quarter + (static_cast<int>(count) >> 1); // v10
    int done = 0;                                         // v6
    while (done < swaps) {
        u8 a = 0, b = 0;
        if (count) a = static_cast<u8>(crt::RandNext() % count); // v11
        if (count) b = static_cast<u8>(crt::RandNext() % count); // v8
        if (a != b) {
            u8 t = arr[a];
            arr[a] = arr[b];
            arr[b] = t;
            ++done;
        }
    }
}

// gilde.exe 0x58ba98 — VIBE_Util_InitAndShuffleDwordArray (__usercall a1@al, a2@edx)
void InitAndShuffleDwordArray(u8 count, u32* arr) {
    for (int i = 0; i < count; ++i)
        arr[i] = static_cast<u32>(i);
    if (count < 2)
        return;
    int quarter = static_cast<int>(count) >> 2;
    if (quarter <= 1) quarter = 1;
    int swaps = quarter + (static_cast<int>(count) >> 1); // v13
    int done = 0;                                         // v9
    do {
        int a = static_cast<u16>(crt::RandNext() % count); // v7
        int b = static_cast<u16>(crt::RandNext() % count); // v11
        if (a != b) {
            u32 t = arr[a];
            arr[a] = arr[b];
            arr[b] = t;
            ++done;
        }
    } while (done < swaps);
}

// gilde.exe 0x58bb74 — VIBE_Util_ShuffleDwordArray (__usercall count@eax, arr@edx)
//   No init fill; same swap-count formula on the existing contents.
void ShuffleDwordArray(u32 count, u32* arr) {
    if (count < 2)
        return;
    u32 quarter = count >> 2;
    if (quarter <= 1) quarter = 1;
    int swaps = static_cast<int>(quarter + (count >> 1)); // v13
    int done = 0;                                         // v4
    if (swaps <= 0)
        return;
    do {
        u16 a = 0, b = 0;
        if (static_cast<u16>(count))
            a = static_cast<u16>(crt::RandNext() % static_cast<u16>(count)); // v5
        if (static_cast<u16>(count))
            b = static_cast<u16>(crt::RandNext() % static_cast<u16>(count)); // v7
        if (a != b) {
            u32 ta = arr[a];
            u32 tb = arr[b];
            arr[a] = tb;
            arr[b] = ta;
            ++done;
        }
    } while (done < swaps);
}

// gilde.exe 0x43fc58 — VIBE_Util_StrCopyChecked (__usercall a1@eax(dst), a2@edx(src))
//   if (!dst) return 0; else strcpy(dst, src); return 1; (the original unrolls 2/step)
int StrCopyChecked(char* dst, const char* src) {
    if (!dst)
        return 0;
    while ((*dst++ = *src++) != '\0') {
    }
    return 1;
}

} // namespace guild::util
