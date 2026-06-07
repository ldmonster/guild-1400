#pragma once
#include "guild/common/types.h"
#include <cstddef>
#include <vector>

// gzip framing for the vendored zlib in gilde.exe. In the original this is the
// gzio.c FILE wrapper (VIBE_Gzip_* @0x5eb970..0x5fea88): a buffered reader/writer
// around a fd that strips the gzip header (VIBE_Gzip_CheckHeader @0x5ebdac),
// inflates with a raw inflate_blocks stream, and verifies the trailing CRC-32 +
// ISIZE (VIBE_Gzip_GetLong @0x5ec668). Those functions are file-I/O bound and
// not reimplemented here; what IS reconstructed is the framing logic, exposed as
// an in-memory helper (equivalent to inflate with windowBits=31).
//
// gzip magic: dword_64A7BC = {0x1f, 0x8b}. Header flag bits parsed exactly as
// VIBE_Gzip_CheckHeader: FHCRC(2) FEXTRA(4) FNAME(8) FCOMMENT(16); reserved bits
// 0xE0 must be clear and the method byte must be 8 (deflate).
namespace guild::compress {

// GunzipMember: parse a single gzip member, inflate the deflate payload into
// `out`. Returns true on success (header valid, raw-inflate ok). The trailing
// CRC-32/ISIZE are read but not verified against the CRC module here (the game's
// gzio computes CRC via the same VIBE_Crc path; left to the caller).
bool Gunzip(const u8* in, std::size_t in_len, std::vector<u8>& out);

} // namespace guild::compress
