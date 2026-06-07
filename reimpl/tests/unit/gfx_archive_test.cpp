// Unit tests for guild::render::GfxArchive + DecodeShapeBlob (the depth-2
// gilde.gfx shape codec reconstructed from VIBE_FrameTable_Index @0x5fbb24).
//
// Builds a tiny synthetic SHAPBANK buffer in the REAL on-disk run format that the
// codec nailed:  per row { u32 runCount; runs of { u32 skipBytes; u32 lenPixels;
//   lenPixels * {u8 R,u8 G,u8 B} } }, rows located via the u32 row-offset table at
//   shape + (u32 @ shape+0x2A).  skipBytes/3 = transparent pixels.
#include "test.h"

#include "render/gfx_archive.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using guild::render::GfxArchive;
using guild::render::DecodedShape;
using guild::render::DecodeShapeBlob;
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u16 = std::uint16_t;

namespace {

void PutU16(std::vector<u8>& b, std::size_t at, u16 v) {
    if (at + 2 > b.size()) b.resize(at + 2);
    b[at] = (u8)(v & 0xFF); b[at + 1] = (u8)(v >> 8);
}
void PutU32(std::vector<u8>& b, std::size_t at, u32 v) {
    if (at + 4 > b.size()) b.resize(at + 4);
    b[at] = (u8)(v); b[at + 1] = (u8)(v >> 8);
    b[at + 2] = (u8)(v >> 16); b[at + 3] = (u8)(v >> 24);
}

// Build a SHAPBANK blob with ONE shape of size (w,h). `rows` is per-row run list;
// each run is (skipPixels, vector<{R,G,B}>).
struct Run { int skipPixels; std::vector<std::array<u8,3>> px; };
std::vector<u8> BuildBlob(int w, int h, const std::vector<std::vector<Run>>& rows) {
    // Bank header: shapeCount @0x2A, offset table @0x45.  Put the shape right after
    // a small header region (offset 0x80).
    const u32 shapeOff = 0x80;
    std::vector<u8> blob(shapeOff, 0);
    PutU16(blob, 0x2A, 1);                 // shapeCount = 1
    PutU32(blob, 0x45, shapeOff);          // offsetTable[0] = shapeOff

    // Shape header (>= 0x32 bytes). width @6, height @0xA, rowtab u32 @0x2A.
    std::size_t shBase = blob.size();
    blob.resize(shBase + 0x32, 0);
    PutU16(blob, shBase + 6, (u16)w);
    PutU16(blob, shBase + 0x0A, (u16)h);

    // Emit each row's RLE; record its byte offset (relative to the shape).
    std::vector<u32> rowOffsets(h, 0);
    for (int r = 0; r < h; ++r) {
        rowOffsets[r] = (u32)(blob.size() - shBase);
        const std::vector<Run>& runs = (r < (int)rows.size()) ? rows[r]
                                                              : std::vector<Run>{};
        u32 at = (u32)blob.size();
        PutU32(blob, at, (u32)runs.size()); at += 4;
        for (const Run& run : runs) {
            PutU32(blob, at, (u32)(run.skipPixels * 3)); at += 4;  // skip in bytes
            PutU32(blob, at, (u32)run.px.size());        at += 4;  // len in pixels
            for (const auto& p : run.px) {
                blob.resize(at + 3);
                blob[at] = p[0]; blob[at + 1] = p[1]; blob[at + 2] = p[2];
                at += 3;
            }
        }
    }

    // Emit the row-offset table and point the shape header at it.
    u32 rowTabRel = (u32)(blob.size() - shBase);
    PutU32(blob, shBase + 0x2A, rowTabRel);
    for (int r = 0; r < h; ++r) PutU32(blob, blob.size(), rowOffsets[r]);

    return blob;
}

} // namespace

TEST(GfxArchive, DecodeSolidRow) {
    // 4x2: row0 = 4 opaque pixels (red ramp); row1 = skip 2, then 2 pixels.
    std::vector<std::vector<Run>> rows = {
        { Run{0, {{10,0,0},{20,0,0},{30,0,0},{40,0,0}}} },
        { Run{2, {{0,0,50},{0,0,60}}} },
    };
    std::vector<u8> blob = BuildBlob(4, 2, rows);

    DecodedShape sh;
    CHECK(DecodeShapeBlob(blob.data(), blob.size(), 0, sh));
    CHECK_EQ(sh.width, 4);
    CHECK_EQ(sh.height, 2);
    CHECK_EQ((int)sh.argb.size(), 8);

    // row0 fully opaque red ramp.
    CHECK_EQ(sh.argb[0], 0xFF0A0000u);
    CHECK_EQ(sh.argb[1], 0xFF140000u);
    CHECK_EQ(sh.argb[2], 0xFF1E0000u);
    CHECK_EQ(sh.argb[3], 0xFF280000u);
    // row1: first 2 transparent (skip), then 2 blue pixels.
    CHECK_EQ(sh.argb[4], 0x00000000u);
    CHECK_EQ(sh.argb[5], 0x00000000u);
    CHECK_EQ(sh.argb[6], 0xFF000032u);
    CHECK_EQ(sh.argb[7], 0xFF00003Cu);
    CHECK_EQ(sh.opaque, 6);
}

TEST(GfxArchive, MultiRunRow) {
    // 6x1: run0 = 2 px at x0; gap 2; run1 = 2 px at x4.
    std::vector<std::vector<Run>> rows = {
        { Run{0, {{1,1,1},{2,2,2}}}, Run{2, {{3,3,3},{4,4,4}}} },
    };
    std::vector<u8> blob = BuildBlob(6, 1, rows);
    DecodedShape sh;
    CHECK(DecodeShapeBlob(blob.data(), blob.size(), 0, sh));
    CHECK_EQ(sh.argb[0], 0xFF010101u);
    CHECK_EQ(sh.argb[1], 0xFF020202u);
    CHECK_EQ(sh.argb[2], 0x00000000u);
    CHECK_EQ(sh.argb[3], 0x00000000u);
    CHECK_EQ(sh.argb[4], 0xFF030303u);
    CHECK_EQ(sh.argb[5], 0xFF040404u);
    CHECK_EQ(sh.opaque, 4);
}

TEST(GfxArchive, OutOfRangeShapeRejected) {
    std::vector<u8> blob = BuildBlob(2, 1, {{ Run{0, {{9,9,9},{8,8,8}}} }});
    DecodedShape sh;
    CHECK(!DecodeShapeBlob(blob.data(), blob.size(), 1, sh));   // only shape 0 exists
    CHECK(!DecodeShapeBlob(blob.data(), blob.size(), -1, sh));
    CHECK(!DecodeShapeBlob(nullptr, 0, 0, sh));
}

TEST(GfxArchive, ParseSyntheticArchive) {
    // Build a 2-record archive: a SHAPBANK blob each.
    std::vector<u8> blobA = BuildBlob(3, 1, {{ Run{0, {{1,2,3},{4,5,6},{7,8,9}}} }});
    std::vector<u8> blobB = BuildBlob(1, 1, {{ Run{0, {{99,99,99}}} }});

    const u32 count = 2;
    std::vector<u8> file;
    file.resize(4 + count * 84, 0);
    PutU32(file, 0, count);

    auto writeRec = [&](u32 idx, const char* name, u32 off, u32 sz, u16 w, u16 h) {
        std::size_t base = 4 + idx * 84;
        std::memcpy(file.data() + base, name, std::strlen(name));
        PutU32(file, base + 48, off);
        PutU32(file, base + 56, sz);
        PutU16(file, base + 80, w);
        PutU16(file, base + 82, h);
    };
    u32 offA = (u32)file.size();
    file.insert(file.end(), blobA.begin(), blobA.end());
    u32 offB = (u32)file.size();
    file.insert(file.end(), blobB.begin(), blobB.end());
    writeRec(0, "_TEST_A", offA, (u32)blobA.size(), 3, 1);
    writeRec(1, "_TEST_B", offB, (u32)blobB.size(), 1, 1);

    GfxArchive arc;
    CHECK(arc.LoadFromMemory(file));
    CHECK(arc.ok());
    CHECK_EQ((int)arc.recordCount(), 2);
    CHECK_EQ(arc.FindByName("_TEST_A"), 0);
    CHECK_EQ(arc.FindByName("_TEST_B"), 1);
    CHECK_EQ(arc.FindByName("_NOPE"), -1);
    CHECK_EQ(arc.ShapeCount(0), 1);

    DecodedShape a;
    CHECK(arc.DecodeShapeByName("_TEST_A", 0, a));
    CHECK_EQ(a.width, 3);
    CHECK_EQ(a.argb[0], 0xFF010203u);
    CHECK_EQ(a.argb[2], 0xFF070809u);

    DecodedShape b;
    CHECK(arc.DecodeShape(1, 0, b));
    CHECK_EQ(b.argb[0], 0xFF636363u);
}

TEST(GfxArchive, MalformedHeaderRejected) {
    std::vector<u8> tiny = {1, 0};         // too short for even the count
    GfxArchive arc;
    CHECK(!arc.LoadFromMemory(tiny));
    CHECK(!arc.ok());

    std::vector<u8> bigcount(4, 0);
    PutU32(bigcount, 0, 5);                 // 5 records but no record bytes follow
    GfxArchive arc2;
    CHECK(!arc2.LoadFromMemory(bigcount));
}
