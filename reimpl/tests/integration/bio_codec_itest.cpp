// Integration test for guild::io bio_codec — compose the Bio compound codecs into
// a small record pipeline (write a mesh-like record, read it back) and prove the
// serialized byte stream round-trips losslessly. Exercises the real VFS memory
// stream (write -> reopen for read) end to end, mixing the existing scalar Bio
// primitives (io/worldio) with the new compound codecs.
#include "test.h"
#include "io/bio_codec.h"
#include "io/worldio.h"   // BioWriteDword / BioReadDword / BioWriteByte / BioReadByte
#include "io/vfs.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::io;

namespace {

// A synthetic "mesh chunk": a magic dword, a flag byte, a 4-vector of fixed
// attributes, a variable-length name block, and an array of vertices.
struct MeshChunk {
    u32 magic;
    u8  flags;
    u32 vec[4];
    std::vector<u8> name;
    std::vector<u32> verts;   // stride-4 elements
};

bool WriteChunk(VfsHandle* h, const MeshChunk& c) {
    bool ok = BioWriteDword(h, c.magic);
    ok = BioWriteByte(h, c.flags) && ok;
    ok = BioWriteVec4(h, c.vec) && ok;
    ok = BioWriteBlock(h, c.name.data(), (u32)c.name.size()) && ok;
    ok = BioWriteArray(h, c.verts.data(), 4, (u32)c.verts.size()) && ok;
    return ok;
}

bool ReadChunk(VfsHandle* h, MeshChunk& c) {
    bool ok = BioReadDword(h, &c.magic);
    ok = BioReadByte(h, &c.flags) && ok;
    ok = BioReadVec4(h, c.vec) && ok;

    void* nameBuf = nullptr;
    u32 nameLen = BioReadBlockAlloc(h, &nameBuf, "itest:name");
    c.name.assign((u8*)nameBuf, (u8*)nameBuf + nameLen);
    if (nameBuf) GetBioCodecHooks().free(nameBuf);

    void* vbuf = nullptr;
    u32 vcount = BioReadArrayDebug(h, /*expectStride*/4, "itest:verts", &vbuf);
    c.verts.assign((u32*)vbuf, (u32*)vbuf + vcount);
    if (vbuf) GetBioCodecHooks().free(vbuf);
    return ok;
}

} // namespace

TEST(BioCodecPipeline, MeshChunk_RoundTrip) {
    MeshChunk src;
    src.magic = 0x4853454Du;   // 'MESH'
    src.flags = 0x2A;
    src.vec[0] = 100; src.vec[1] = 200; src.vec[2] = 300; src.vec[3] = 0xFFFFFFFFu;
    const char* nm = "tower_roof";
    src.name.assign((const u8*)nm, (const u8*)nm + std::strlen(nm));
    src.verts = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u, 0x55555555u};

    std::vector<u8> buf(4096, 0);
    VfsHandle* w = VfsOpenMemoryStream(buf.data(), (u32)buf.size(), "wb");
    CHECK(w != nullptr);
    long written = 0;
    if (w) { CHECK(WriteChunk(w, src)); written = VfsTell(w); VfsCloseStream(w); }

    // Expected size: 4 (magic) + 1 (flag) + 16 (vec4) + (4+10) name + (4+4+20) array.
    CHECK_EQ(written, (long)(4 + 1 + 16 + (4 + 10) + (4 + 4 + 20)));

    VfsHandle* r = VfsOpenMemoryStream(buf.data(), (u32)buf.size(), "rb");
    CHECK(r != nullptr);
    if (!r) return;
    MeshChunk got;
    CHECK(ReadChunk(r, got));
    VfsCloseStream(r);

    CHECK_EQ(got.magic, src.magic);
    CHECK_EQ(got.flags, src.flags);
    CHECK_EQ(std::memcmp(got.vec, src.vec, sizeof(src.vec)), 0);
    CHECK_EQ(got.name.size(), src.name.size());
    CHECK(got.name == src.name);
    CHECK_EQ(got.verts.size(), src.verts.size());
    CHECK(got.verts == src.verts);
}

TEST(BioCodecPipeline, EmptyNameAndVerts) {
    MeshChunk src;
    src.magic = 0xDEADBEEFu;
    src.flags = 0;
    src.vec[0] = src.vec[1] = src.vec[2] = src.vec[3] = 0;
    // empty name, empty vertex array

    std::vector<u8> buf(256, 0);
    VfsHandle* w = VfsOpenMemoryStream(buf.data(), (u32)buf.size(), "wb");
    long written = 0;
    if (w) { CHECK(WriteChunk(w, src)); written = VfsTell(w); VfsCloseStream(w); }
    // 4 + 1 + 16 + (4+0) + (4+4+0) = 33
    CHECK_EQ(written, (long)33);

    VfsHandle* r = VfsOpenMemoryStream(buf.data(), (u32)buf.size(), "rb");
    if (!r) { CHECK(false); return; }
    MeshChunk got;
    CHECK(ReadChunk(r, got));
    VfsCloseStream(r);
    CHECK_EQ(got.magic, 0xDEADBEEFu);
    CHECK_EQ(got.name.size(), (std::size_t)0);
    CHECK_EQ(got.verts.size(), (std::size_t)0);
}

// Two records back-to-back in one stream: the reader must consume exactly the
// right number of bytes per record so the second reads correctly.
TEST(BioCodecPipeline, TwoChunks_Sequential) {
    MeshChunk a; a.magic = 1; a.flags = 0xA0;
    a.vec[0]=1;a.vec[1]=2;a.vec[2]=3;a.vec[3]=4;
    const char* an = "A"; a.name.assign((const u8*)an,(const u8*)an+1);
    a.verts = {0xAA00AA00u};

    MeshChunk b; b.magic = 2; b.flags = 0xB0;
    b.vec[0]=9;b.vec[1]=8;b.vec[2]=7;b.vec[3]=6;
    const char* bn = "BBBB"; b.name.assign((const u8*)bn,(const u8*)bn+4);
    b.verts = {0xBB00BB00u, 0xBB11BB11u};

    std::vector<u8> buf(1024, 0);
    VfsHandle* w = VfsOpenMemoryStream(buf.data(), (u32)buf.size(), "wb");
    if (w) { CHECK(WriteChunk(w, a)); CHECK(WriteChunk(w, b)); VfsCloseStream(w); }

    VfsHandle* r = VfsOpenMemoryStream(buf.data(), (u32)buf.size(), "rb");
    if (!r) { CHECK(false); return; }
    MeshChunk ga, gb;
    CHECK(ReadChunk(r, ga));
    CHECK(ReadChunk(r, gb));
    VfsCloseStream(r);
    CHECK_EQ(ga.magic, 1u); CHECK(ga.name.size()==1 && ga.name[0]=='A');
    CHECK_EQ(gb.magic, 2u); CHECK(gb.name.size()==4);
    CHECK(gb.verts.size()==2 && gb.verts[1]==0xBB11BB11u);
}
