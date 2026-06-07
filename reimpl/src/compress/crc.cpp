#include "compress/crc.h"

namespace guild::compress {

namespace {

// --- CRC-32 table (dword_5EE990) -------------------------------------------
// Precomputed global in the binary; regenerated here with the standard reflected
// poly 0xEDB88320. table[1] == 0x77073096, matching the bytes read from the IDB.
struct Crc32Table {
    u32 t[256];
    Crc32Table() {
        for (u32 n = 0; n < 256; ++n) {
            u32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[n] = c;
        }
    }
};
const Crc32Table g_crc32;

// --- CRC-16 table (word_1460A60) -------------------------------------------
// Built exactly as VIBE_Crc16_InitTable @0x1418a40 does at runtime:
//   for each i: v = 0xC0C1; for j=1; j<256; j*=2: if(j&i) tab[i]^=v; v=(2*v)^0x4003
// This is the reflected 0x8005 (CRC-16/ARC) generator.
struct Crc16Table {
    u16 t[256];
    Crc16Table() {
        for (int i = 0; i < 256; ++i) {
            u16 entry = 0;
            u16 v = 0xC0C1;
            for (int j = 1; j < 256; j *= 2) {
                if (j & i)
                    entry ^= v;
                v = static_cast<u16>((2 * v) ^ 0x4003);
            }
            t[i] = entry;
        }
    }
};
const Crc16Table g_crc16;

} // namespace

// VIBE_Crc_GetTable @0x5eed90
const u32* CrcGetTable() {
    return g_crc32.t;
}

// VIBE_Crc_Compute @0x5eed98 — __usercall(eax=crc, edx=data, ebx=len).
// The binary unrolls the inner loop by 8; the result is byte-identical to a plain
// byte-at-a-time loop, so we translate to the readable form. Entry/exit one's-
// complement preserved (init ~crc, return ~i).
u32 CrcCompute(u32 crc, const u8* data, u32 len) {
    if (!data)
        return 0;
    const u32* table = g_crc32.t;
    u32 i = ~crc;
    for (; len; --len) {
        i = table[static_cast<u8>(i ^ *data++)] ^ (i >> 8);
    }
    return ~i;
}

// VIBE_Crc16_Update @0x1418ae0
u16 Crc16Update(u16 crc, const u8* data, int len) {
    const u16* table = g_crc16.t;
    for (const u8* p = data; p < data + len; ++p)
        crc = static_cast<u16>(table[*p ^ static_cast<u8>(crc)] ^ (crc >> 8));
    return crc;
}

} // namespace guild::compress
