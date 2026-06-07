#pragma once
#include "guild/common/types.h"

// Checksum routines embedded in gilde.exe. CRC-32 and CRC-16 live together here
// since they are small and conceptually paired.
//
//   VIBE_Crc_GetTable    @0x5eed90   (returns the precomputed CRC-32 table)
//   VIBE_Crc_Compute     @0x5eed98
//   VIBE_Crc16_InitTable @0x1418a40
//   VIBE_Crc16_Update    @0x1418ae0
//   VIBE_Crc16_HashFile  @0x1418b50  (file I/O wrapper — not reimplemented here)
//
// CRC-32: standard reflected CRC-32 (poly 0xEDB88320). The table is a precomputed
// global (dword_5EE990) in the binary; we regenerate the identical table at first
// use. Init = ~0, final XOR = ~result. CRC32("123456789") == 0xCBF43926.
//
// CRC-16: CRC-16/ARC (reflected poly 0xA001, init 0, no final XOR). The original
// builds its table at runtime in VIBE_Crc16_InitTable using the seed 0xC0C1 and
// the step v = (2*v) ^ 0x4003; that is the bit-reflected 0x8005 generator.
// CRC16("123456789") == 0xBB3D.
namespace guild::compress {

// VIBE_Crc_GetTable @0x5eed90 — pointer to the 256-entry CRC-32 table.
const u32* CrcGetTable();

// VIBE_Crc_Compute @0x5eed98 — CRC-32 of `len` bytes.
// `crc` is the running value (use 0 for a fresh hash; the function applies ~ on
// entry and exit, so chaining requires passing back the previous return value).
u32 CrcCompute(u32 crc, const u8* data, u32 len);

// VIBE_Crc16_Update @0x1418ae0 — fold `len` bytes into the running CRC-16.
u16 Crc16Update(u16 crc, const u8* data, int len);

} // namespace guild::compress
