// guild::render — gilde.gfx archive loader + depth-2 shape decoder. See header.
#include "render/gfx_archive.h"

#include "shim/IFileSystem.h"

#include <cstring>

namespace guild::render {
namespace {

inline u16 RdU16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 RdU32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// Record layout (84 bytes).
constexpr std::size_t kRecSize    = 84;
constexpr std::size_t kRecName    = 0;
constexpr std::size_t kRecDataOff = 48;
constexpr std::size_t kRecDataSz  = 56;
constexpr std::size_t kRecWidth   = 80;
constexpr std::size_t kRecHeight  = 82;

// SHAPBANK header offsets used here.
constexpr std::size_t kBankShapeCount = 0x2A;  // u16
constexpr std::size_t kBankOffTable   = 0x45;  // u32[] (relative to blob)

// Shape header offsets used here.
constexpr std::size_t kShWidth   = 6;     // u16
constexpr std::size_t kShHeight  = 0x0A;  // u16
constexpr std::size_t kShFullFlag = 0x26; // u32: 0xFFFFFFFF for a FULL (uncompressed)
                                          // RGB bitmap shape; else an RLE shape.
                                          // (VIBE_Shape_ConvertRgbTo16 @0x5d7c0c
                                          // branches on *(shape+38) == -1.)
constexpr std::size_t kShRowTab  = 0x2A;  // u32 byte offset (rel to shape) of the
                                          // per-row offset table (a3[21] in the
                                          // disasm, read as a full 32-bit field).
constexpr std::size_t kShPixels  = 0x32;  // first pixel/row-stream byte (offset 50).

} // namespace

bool DecodeShapeBlob(const u8* blob, std::size_t blobLen, int shapeNr,
                     DecodedShape& out) {
    out = DecodedShape{};
    if (!blob || blobLen < kBankOffTable + 4) return false;

    const u16 shapeCount = RdU16(blob + kBankShapeCount);
    if (shapeNr < 0 || shapeNr >= (int)shapeCount) return false;

    const std::size_t offEntry = kBankOffTable + (std::size_t)shapeNr * 4;
    if (offEntry + 4 > blobLen) return false;
    const u32 shapeOff = RdU32(blob + offEntry);
    if (shapeOff + 0x32 > blobLen) return false;

    const u8* shape = blob + shapeOff;
    const std::size_t shapeMax = blobLen - shapeOff;  // bytes available from shape

    const int w = (int)RdU16(shape + kShWidth);
    const int h = (int)RdU16(shape + kShHeight);
    if (w <= 0 || h <= 0 || (long long)w * h > (1LL << 28)) return false;

    // FULL bitmap shapes (e.g. _MOUSE_CURSOR): w*h packed 3-byte RGB pixels from
    // offset 50, row-major, no RLE. Pure black (0,0,0) is the transparent key (the
    // engine's ConvertRgbTo16 full-bitmap branch does no black->(5,5,5) remap, so
    // black maps to the 16bpp 0 == transparent slot).
    if (RdU32(shape + kShFullFlag) == 0xFFFFFFFFu) {
        const std::size_t need = kShPixels + (std::size_t)w * h * 3;
        if (need > shapeMax) return false;
        const u8* px = shape + kShPixels;
        out.width = w;
        out.height = h;
        out.argb.assign((std::size_t)w * h, 0u);
        int op = 0;
        for (int i = 0; i < w * h; ++i) {
            const u8 R = px[i * 3], G = px[i * 3 + 1], B = px[i * 3 + 2];
            if (R | G | B) {
                out.argb[(std::size_t)i] = 0xFF000000u | ((u32)R << 16) | ((u32)G << 8) | B;
                ++op;
            }
        }
        out.opaque = op;
        return true;
    }

    const u32 rowTabRel = RdU32(shape + kShRowTab);
    // The row table holds h u32 entries (byte offsets relative to the shape).
    if ((std::size_t)rowTabRel + (std::size_t)h * 4 > shapeMax) return false;

    out.width = w;
    out.height = h;
    out.argb.assign((std::size_t)w * h, 0u);  // 0 == transparent
    int opaque = 0;

    for (int row = 0; row < h; ++row) {
        const u32 rowOff = RdU32(shape + rowTabRel + (std::size_t)row * 4);
        // Each row's stream: u32 runCount, then runs.
        std::size_t p = rowOff;
        if (p + 4 > shapeMax) return false;
        const u32 runCount = RdU32(shape + p);
        p += 4;

        int x = 0;
        u32* dstRow = out.argb.data() + (std::size_t)row * w;
        for (u32 r = 0; r < runCount; ++r) {
            if (p + 8 > shapeMax) return false;
            const u32 skipBytes = RdU32(shape + p);
            const u32 lenPixels = RdU32(shape + p + 4);
            p += 8;
            // The on-disk gap and pixels are both 3-byte (24bpp) units, so the
            // transparent advance is skipBytes/3 pixels.
            x += (int)(skipBytes / 3);
            for (u32 k = 0; k < lenPixels; ++k) {
                if (p + 3 > shapeMax) return false;
                const u8 R = shape[p], G = shape[p + 1], B = shape[p + 2];
                p += 3;
                if (x >= 0 && x < w) {
                    dstRow[x] = 0xFF000000u | ((u32)R << 16) | ((u32)G << 8) | B;
                    ++opaque;
                }
                ++x;
            }
        }
    }

    out.opaque = opaque;
    return true;
}

bool GfxArchive::LoadFromMemory(std::vector<u8> bytes) {
    ok_ = false;
    records_.clear();
    bytes_ = std::move(bytes);
    if (bytes_.size() < 4) return false;

    const u32 count = RdU32(bytes_.data());
    // Sanity: the header must fit. (Reject absurd counts that would overflow.)
    if (count > 1000000u) return false;
    const std::size_t headerEnd = 4 + (std::size_t)count * kRecSize;
    if (headerEnd > bytes_.size()) return false;

    records_.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const std::size_t base = 4 + (std::size_t)i * kRecSize;
        GfxRecord rec;
        const char* nm = (const char*)(bytes_.data() + base + kRecName);
        std::size_t nlen = 0;
        while (nlen < 48 && nm[nlen] != '\0') ++nlen;
        rec.name.assign(nm, nlen);
        rec.dataOffset = RdU32(bytes_.data() + base + kRecDataOff);
        rec.dataSize   = RdU32(bytes_.data() + base + kRecDataSz);
        rec.width      = RdU16(bytes_.data() + base + kRecWidth);
        rec.height     = RdU16(bytes_.data() + base + kRecHeight);
        records_.push_back(std::move(rec));
    }
    ok_ = true;
    return true;
}

bool GfxArchive::LoadFromFile(shim::IFileSystem& fs, const char* path) {
    ok_ = false;
    records_.clear();
    bytes_.clear();
    if (!path) return false;
    shim::IFile* f = fs.open(path, "rb");
    if (!f) return false;
    const std::int64_t sz = f->size();
    if (sz <= 0) { fs.close(f); return false; }
    std::vector<u8> data((std::size_t)sz);
    f->seek(0, 0 /*SEEK_SET*/);
    const std::size_t got = f->read(data.data(), (std::size_t)sz);
    fs.close(f);
    if (got != (std::size_t)sz) return false;
    return LoadFromMemory(std::move(data));
}

int GfxArchive::FindByName(const char* name) const {
    if (!name) return -1;
    for (std::size_t i = 0; i < records_.size(); ++i)
        if (records_[i].name == name) return (int)i;
    return -1;
}

int GfxArchive::ShapeCount(int index) const {
    if (!ok_ || index < 0 || index >= (int)records_.size()) return 0;
    const GfxRecord& rec = records_[index];
    if ((std::size_t)rec.dataOffset + kBankShapeCount + 2 > bytes_.size()) return 0;
    if (rec.dataSize < kBankShapeCount + 2) return 0;
    return (int)RdU16(bytes_.data() + rec.dataOffset + kBankShapeCount);
}

bool GfxArchive::DecodeShape(int index, int shapeNr, DecodedShape& out) const {
    out = DecodedShape{};
    if (!ok_ || index < 0 || index >= (int)records_.size()) return false;
    const GfxRecord& rec = records_[index];
    if ((std::size_t)rec.dataOffset >= bytes_.size()) return false;
    std::size_t avail = bytes_.size() - rec.dataOffset;
    std::size_t blobLen = rec.dataSize ? (std::size_t)rec.dataSize : avail;
    if (blobLen > avail) blobLen = avail;
    return DecodeShapeBlob(bytes_.data() + rec.dataOffset, blobLen, shapeNr, out);
}

bool GfxArchive::DecodeShapeByName(const char* name, int shapeNr,
                                   DecodedShape& out) const {
    return DecodeShape(FindByName(name), shapeNr, out);
}

} // namespace guild::render
