#pragma once
#include "guild/common/types.h"

// Vendored zlib 1.1.x checksum + shared return codes used by the inflate/gzip
// codec embedded in gilde.exe. The library version byte stored in the binary is
// '1' (0x31, byte_62BF7C), matching zlib 1.1.4 ("1.1.4").
//
//   VIBE_Zlib_Adler32 @0x5ffaf0  (the classic unrolled-by-16 Adler-32)
//
// Adler-32: two 16-bit sums modulo 65521 (BASE), processed in blocks of 5552
// bytes (NMAX) so the inner accumulation cannot overflow a 32-bit register.
namespace guild::compress {

// zlib return / flush codes (from zlib.h). Only the ones the binary's inflate
// path actually produces are documented; values are the canonical zlib ones.
enum ZlibReturn : int {
    kZOk           = 0,
    kZStreamEnd    = 1,
    kZNeedDict     = 2,
    kZErrno        = -1,
    kZStreamError  = -2,
    kZDataError    = -3,
    kZMemError     = -4,
    kZBufError     = -5,
    kZVersionError = -6,
};

enum ZlibFlush : int {
    kZNoFlush = 0,
    kZSyncFlush = 2,
    kZFullFlush = 3,
    kZFinish  = 4,
};

// VIBE_Zlib_Adler32 @0x5ffaf0 — fold `len` bytes into the running Adler-32.
// Pass 1 as the initial value (adler of the empty string). `data == nullptr`
// resets to 1, matching the original (used by the library to query the seed).
u32 Adler32(u32 adler, const u8* data, u32 len);

} // namespace guild::compress
