#include "compress/gzip.h"
#include "compress/inflate.h"

namespace guild::compress {

namespace {
// dword_64A7BC — gzip magic bytes.
constexpr u8 kMagic0 = 0x1f;
constexpr u8 kMagic1 = 0x8b;

// gzip flag bits (VIBE_Gzip_CheckHeader).
constexpr u8 kFhcrc    = 0x02;
constexpr u8 kFextra   = 0x04;
constexpr u8 kFname    = 0x08;
constexpr u8 kFcomment = 0x10;
constexpr u8 kReserved = 0xE0;
} // namespace

// In-memory equivalent of VIBE_Gzip_CheckHeader @0x5ebdac followed by a raw
// inflate of the deflate payload. Header parsing matches the original byte for
// byte (magic, method==8, reserved bits, skip FEXTRA/FNAME/FCOMMENT/FHCRC).
bool Gunzip(const u8* in, std::size_t in_len, std::vector<u8>& out) {
    std::size_t i = 0;
    auto get = [&]() -> int { return i < in_len ? in[i++] : -1; };

    if (get() != kMagic0) return false;
    if (get() != kMagic1) return false;

    int method = get();
    int flags = get();
    if (method != 8 || flags < 0 || (flags & kReserved) != 0)
        return false;

    // skip MTIME(4) + XFL(1) + OS(1) = 6 bytes
    for (int j = 0; j < 6; ++j) {
        if (get() < 0) return false;
    }

    if (flags & kFextra) {              // skip the extra field
        int lo = get();
        int hi = get();
        if (lo < 0 || hi < 0) return false;
        int len = (hi << 8) + lo;
        while (len-- && get() != -1) {}
    }
    if (flags & kFname) {               // skip original file name
        int c;
        do { c = get(); } while (c != 0 && c != -1);
    }
    if (flags & kFcomment) {            // skip the .gz file comment
        int c;
        do { c = get(); } while (c != 0 && c != -1);
    }
    if (flags & kFhcrc) {               // skip the header crc
        if (get() < 0 || get() < 0) return false;
    }

    // The remainder (minus the 8-byte CRC32+ISIZE trailer) is raw deflate.
    if (i > in_len) return false;
    std::size_t payload = in_len - i;
    if (payload < 8) return false;
    payload -= 8;                       // trailer: CRC32(4) + ISIZE(4)
    return InflateRaw(in + i, payload, out);
}

} // namespace guild::compress
