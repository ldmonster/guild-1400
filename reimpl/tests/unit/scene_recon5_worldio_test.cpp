#include "io/vfs_recon5_worldio.h"
#include "test.h"
#include <vector>
#include <cstring>
#include <string>

using namespace guild::io;

// Capturing byte sink.
struct VecSink : ByteSink {
    std::vector<guild::u8> bytes;
    void write(const void* d, std::size_t n) override {
        const guild::u8* p = (const guild::u8*)d;
        bytes.insert(bytes.end(), p, p + n);
    }
};

static guild::u32 rd32(const std::vector<guild::u8>& b, std::size_t off) {
    return (guild::u32)b[off] | ((guild::u32)b[off + 1] << 8) |
           ((guild::u32)b[off + 2] << 16) | ((guild::u32)b[off + 3] << 24);
}

// --- SaveSceneObjects: exact framing ----------------------------------------
TEST(SceneRecon5, SaveSceneFraming) {
    VecSink sink;
    SceneSaveData d{};
    d.magic = 980156603;
    d.tag = "GG";              // 3 bytes incl NUL
    d.formatByte = 0x11223344;
    d.vec3A[0] = 1; d.vec3A[1] = 2; d.vec3A[2] = 3;
    d.vec3B[0] = 4; d.vec3B[1] = 5; d.vec3B[2] = 6;
    d.fieldA = 0xAAAA; d.fieldB = 0xBBBB;
    d.fogNear = 0xC; d.fogFar = 0xD;
    d.objectCount = 2;
    d.hasBuilding = 1;

    int objWrites = 0, bldWrites = 0, evtWrites = 0;
    SceneSaveHooks h{};
    struct Ctx { int* o; int* b; int* e; } cx{&objWrites, &bldWrites, &evtWrites};
    h.ctx = &cx;
    h.writeObject = [](ByteSink& s, int, void* c) {
        (*static_cast<Ctx*>(c)->o)++; s.dword(0xDEAD);
    };
    h.writeBuildingData = [](ByteSink& s, void* c) {
        (*static_cast<Ctx*>(c)->b)++; s.byte(0x99);
    };
    h.writeEventNames = [](ByteSink& s, void* c) {
        (*static_cast<Ctx*>(c)->e)++; s.byte(0x00);
    };

    SaveSceneObjects(sink, d, h);

    // Verify the fixed-prefix framing byte for byte.
    std::size_t off = 0;
    CHECK_EQ(rd32(sink.bytes, off), 980156603u); off += 4;     // magic
    CHECK_EQ((int)sink.bytes[off], (int)'G'); off += 1;
    CHECK_EQ((int)sink.bytes[off], (int)'G'); off += 1;
    CHECK_EQ((int)sink.bytes[off], 0); off += 1;               // tag NUL
    CHECK_EQ(rd32(sink.bytes, off), 0x11223344u); off += 4;    // formatByte
    CHECK_EQ(rd32(sink.bytes, off), 1u); off += 4;             // vec3A
    CHECK_EQ(rd32(sink.bytes, off), 2u); off += 4;
    CHECK_EQ(rd32(sink.bytes, off), 3u); off += 4;
    CHECK_EQ(rd32(sink.bytes, off), 4u); off += 4;             // vec3B
    CHECK_EQ(rd32(sink.bytes, off), 5u); off += 4;
    CHECK_EQ(rd32(sink.bytes, off), 6u); off += 4;
    CHECK_EQ(rd32(sink.bytes, off), 0xAAAAu); off += 4;        // fieldA
    CHECK_EQ(rd32(sink.bytes, off), 0xBBBBu); off += 4;        // fieldB
    CHECK_EQ(rd32(sink.bytes, off), 0xCu); off += 4;           // fogNear
    CHECK_EQ(rd32(sink.bytes, off), 0xDu); off += 4;           // fogFar

    // 7 light slots: each = 12 (pos) + 12 (dir) + 24 (two triples) = 48 bytes.
    off += 7 * 48;

    CHECK_EQ(rd32(sink.bytes, off), 2u); off += 4;             // objectCount
    // 2 object bodies, each writes a dword (0xDEAD).
    CHECK_EQ(rd32(sink.bytes, off), 0xDEADu); off += 4;
    CHECK_EQ(rd32(sink.bytes, off), 0xDEADu); off += 4;
    CHECK_EQ((int)sink.bytes[off], 1); off += 1;               // hasBuilding byte
    CHECK_EQ((int)sink.bytes[off], 0x99); off += 1;            // building body
    CHECK_EQ((int)sink.bytes[off], 0x00); off += 1;            // event names body
    CHECK_EQ((int)sink.bytes.size(), (int)off);

    CHECK_EQ(objWrites, 2);
    CHECK_EQ(bldWrites, 1);
    CHECK_EQ(evtWrites, 1);
}

// hasBuilding==0 skips building body but still writes the gate byte + events.
TEST(SceneRecon5, SaveSceneNoBuilding) {
    VecSink sink;
    SceneSaveData d{};
    d.tag = ""; d.objectCount = 0; d.hasBuilding = 0;
    int bld = 0;
    SceneSaveHooks h{};
    h.ctx = &bld;
    h.writeBuildingData = [](ByteSink&, void* c) { (*static_cast<int*>(c))++; };
    h.writeEventNames = [](ByteSink& s, void*) { s.byte(7); };
    SaveSceneObjects(sink, d, h);
    CHECK_EQ(bld, 0);             // building writer never called
    // last byte is the event-names body
    CHECK_EQ((int)sink.bytes.back(), 7);
}

// --- LoadObjectGroup: magic gate + multi-object linking ---------------------
struct LoadState {
    std::vector<guild::u32> reads;   // queued dwords
    std::size_t pos = 0;
    int objCounter = 0;
    std::vector<std::pair<int,int>> links;  // (root,obj)
    int setParentRoot = -1, setParentParent = -1;
};
static guild::u32 LRead(void* s, bool* ok, void*) {
    LoadState* st = static_cast<LoadState*>(s);
    if (st->pos >= st->reads.size()) { if (ok) *ok = false; return 0; }
    return st->reads[st->pos++];
}
static guild::i32 LObj(void* s, guild::i32, guild::u32, guild::i32, void*) {
    LoadState* st = static_cast<LoadState*>(s);
    return ++st->objCounter;     // each object gets a unique nonzero token
}
static void LLink(guild::i32 root, guild::i32 obj, void* s) {
    static_cast<LoadState*>(s)->links.push_back({root, obj});
}
static void LParent(guild::i32 p, guild::i32 r, void* s) {
    LoadState* st = static_cast<LoadState*>(s);
    st->setParentParent = p; st->setParentRoot = r;
}

TEST(SceneRecon5, LoadObjectGroupMulti) {
    LoadState st;
    // magic high word must be 0x3A6C (980156416 = 0x3A6C0000). Use a version
    // >= 980156590 to take the multi-object path. 980156592 = 0x3A6C00B0.
    st.reads = {980156592u, /*count*/3u};
    ObjectGroupHooks h{};
    h.ctx = &st;
    h.readDword = &LRead;
    h.readObject = &LObj;
    h.linkSibling = &LLink;
    h.setParent = &LParent;

    // stream pointer is &st (LRead reads from it); parent = 77 (nonzero).
    guild::i32 root = LoadObjectGroup(&st, /*parent*/77, h);
    CHECK_EQ(root, 1);              // first object is the root
    CHECK_EQ(st.objCounter, 3);     // 3 objects read
    // objects 2 and 3 linked as siblings of root (1)
    CHECK_EQ((int)st.links.size(), 2);
    CHECK_EQ(st.links[0].first, 1); CHECK_EQ(st.links[0].second, 2);
    CHECK_EQ(st.links[1].first, 1); CHECK_EQ(st.links[1].second, 3);
    // SetParent(parent=77, root=1)
    CHECK_EQ(st.setParentParent, 77);
    CHECK_EQ(st.setParentRoot, 1);
}

TEST(SceneRecon5, LoadObjectGroupBadMagic) {
    LoadState st;
    st.reads = {0x12345678u};     // wrong high word
    ObjectGroupHooks h{};
    h.readDword = &LRead; h.readObject = &LObj;
    guild::i32 root = LoadObjectGroup(&st, 0, h);
    CHECK_EQ(root, 0);
    CHECK_EQ(st.objCounter, 0);   // never read an object
}

TEST(SceneRecon5, LoadObjectGroupSingleOldVersion) {
    LoadState st;
    // version < 980156590 -> single ReadObject, no count. High word must be 0x3A6C
    // (== 980156416). 980156544 = 0x3A6C0080 (< 980156590).
    st.reads = {980156544u};
    ObjectGroupHooks h{};
    h.ctx = &st;
    h.readDword = &LRead; h.readObject = &LObj; h.setParent = &LParent;
    guild::i32 root = LoadObjectGroup(&st, /*parent*/5, h);
    CHECK_EQ(root, 1);
    CHECK_EQ(st.objCounter, 1);
    CHECK_EQ(st.setParentParent, 5);
}

// --- VfsAddFileByPath: path split -------------------------------------------
struct VfsState {
    std::vector<std::string> dirs;   // segments passed to GetOrCreateSubDir
    std::string leaf;
    bool addCalled = false;
};
static void* VGet(const char* seg, void* parent, void* c) {
    static_cast<VfsState*>(c)->dirs.push_back(seg);
    return parent;   // pretend descent
}
static void* VAdd(const char* leaf, void*, guild::i32, guild::i32, guild::i32,
                  guild::u32, guild::u32, void* c) {
    VfsState* st = static_cast<VfsState*>(c);
    st->leaf = leaf; st->addCalled = true;
    return st;
}

TEST(SceneRecon5, VfsAddFileByPathSplit) {
    VfsState st;
    VfsAddHooks h{&VGet, &VAdd, &st};
    void* r = VfsAddFileByPath("data/textures/wall.bgf", (void*)0x1, 0, 0, 0, 0, 0, h);
    CHECK(r == &st);
    CHECK(st.addCalled);
    CHECK_EQ((int)st.dirs.size(), 2);
    CHECK(st.dirs[0] == std::string("data"));
    CHECK(st.dirs[1] == std::string("textures"));
    CHECK(st.leaf == std::string("wall.bgf"));
}

TEST(SceneRecon5, VfsAddFileNoDirs) {
    VfsState st;
    VfsAddHooks h{&VGet, &VAdd, &st};
    void* r = VfsAddFileByPath("readme.txt", (void*)0x1, 0, 0, 0, 0, 0, h);
    CHECK(r == &st);
    CHECK_EQ((int)st.dirs.size(), 0);
    CHECK(st.leaf == std::string("readme.txt"));
}

TEST(SceneRecon5, VfsAddFileTrailingSlashRejected) {
    VfsState st;
    VfsAddHooks h{&VGet, &VAdd, &st};
    void* r = VfsAddFileByPath("data/textures/", (void*)0x1, 0, 0, 0, 0, 0, h);
    CHECK(r == nullptr);          // ends in '/' -> not a file
    CHECK(!st.addCalled);
}
