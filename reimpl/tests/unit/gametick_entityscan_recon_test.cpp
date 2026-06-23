// Tests for the VIBE_GameTick entity hit-test scans reconstructed in
// src/sim/gametick_entityscan_recon.{h,cpp} (0x4146d8, 0x414a38).
//
// The scans read entity fields at fixed byte offsets and 16.16 fixed-point box
// edges (value >> 16). We construct synthetic 740-byte-safe records by hand and
// verify the InitEntityTracking hit/miss logic and the MainLoop fallback path.
#include "tests/framework/test.h"
#include "sim/gametick_entityscan_recon.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// Build one entity record big enough for every offset the scans touch (<=448).
struct EntityRec {
    u8 bytes[512];
    EntityRec() { std::memset(bytes, 0, sizeof(bytes)); }
    void put32(std::size_t off, i32 v) { std::memcpy(bytes + off, &v, 4); }
    void put8(std::size_t off, u8 v) { bytes[off] = v; }
    // The scans read each box edge as `*(int*)(rec+off) >> 16`, i.e. the integer
    // coordinate lives in the HIGH word (bytes off+2..off+3). These windows
    // overlap at adjacent offsets in the real records, so we set the high word
    // directly to mirror the engine's exact read.
    void putHi16(std::size_t off, i16 coord) { std::memcpy(bytes + off + 2, &coord, 2); }
    // Store a native pointer (child / sub-node) at byte offset.
    void putptr(std::size_t off, const void* p) { std::memcpy(bytes + off, &p, sizeof(p)); }
};

// Object-type record table: 740 bytes per type; byte +24 must be 64 for a hit.
struct TypeTable {
    std::vector<u8> data;
    explicit TypeTable(int types) : data(static_cast<std::size_t>(types) * 740, 0) {}
    void setByte24(int type, u8 v) { data[static_cast<std::size_t>(type) * 740 + 24] = v; }
    const u8* base() const { return data.data(); }
};

} // namespace

// 0x4146d8 — a fully-qualifying entity is picked; hoverChildId = entity[29].
TEST(GameTimeReconEntity, InitTrackingHit) {
    EntityRec e;
    e.put32(0, 3);          // type index 3
    e.putHi16(14, 0);         // box top = 0
    e.putHi16(18, 100);       // height = 100  (top..top+100)
    e.putHi16(16, 0);         // box left = 0
    e.putHi16(20, 100);       // width = 100
    e.putHi16(30, 0);         // inner-y lower = 0
    e.putHi16(32, 100);       // inner-y upper = 100
    e.put32(60, 0);         // entity[15] child = 0 (no busy proxy)
    e.put32(56, 0);         // entity[14] = 0
    e.put32(52, 0);         // entity[13] = 0
    e.put32(116, 4242);     // entity[29] -> hover id

    TypeTable tt(8);
    tt.setByte24(3, 64);    // type 3's +24 byte == 64 (required)

    const u8* table[1] = { e.bytes };
    GameTickScanState st;
    st.entityTable = table;
    st.entityTableCount = 1;
    st.objectTypeRecords = tt.base();

    i32 r = GameTickInitEntityTracking(st, /*px*/50, /*py*/50);
    CHECK_EQ(r, 4242);
    CHECK_EQ(st.hoverChildId, 4242);
}

// 0x4146d8 — point outside the box: no hit, hoverChildId stays -1.
TEST(GameTimeReconEntity, InitTrackingMissOutsideBox) {
    EntityRec e;
    e.put32(0, 1);
    e.putHi16(14, 0);  e.putHi16(18, 10);
    e.putHi16(16, 0);  e.putHi16(20, 10);
    e.putHi16(30, 0);  e.putHi16(32, 10);
    e.put32(116, 7);
    TypeTable tt(2);
    tt.setByte24(1, 64);

    const u8* table[1] = { e.bytes };
    GameTickScanState st;
    st.entityTable = table;
    st.entityTableCount = 1;
    st.objectTypeRecords = tt.base();

    CHECK_EQ(GameTickInitEntityTracking(st, 500, 500), -1);
    CHECK_EQ(st.hoverChildId, -1);
}

// 0x4146d8 — type byte +24 not 64 disqualifies even an in-box point.
TEST(GameTimeReconEntity, InitTrackingMissWrongType) {
    EntityRec e;
    e.put32(0, 2);
    e.putHi16(14, 0); e.putHi16(18, 100);
    e.putHi16(16, 0); e.putHi16(20, 100);
    e.putHi16(30, 0); e.putHi16(32, 100);
    e.put32(116, 9);
    TypeTable tt(4);
    tt.setByte24(2, 1);   // NOT 64

    const u8* table[1] = { e.bytes };
    GameTickScanState st;
    st.entityTable = table;
    st.entityTableCount = 1;
    st.objectTypeRecords = tt.base();

    CHECK_EQ(GameTickInitEntityTracking(st, 50, 50), -1);
}

// 0x4146d8 — busy child (entity[15]!=0 with +408 set) disqualifies (the !v4 gate).
TEST(GameTimeReconEntity, InitTrackingBusyChildDisqualifies) {
    EntityRec child;
    child.put32(408, 1);   // child-busy flag (v4 = *(child+408)) != 0

    EntityRec e;
    e.put32(0, 0);
    e.putHi16(14, 0); e.putHi16(18, 100);
    e.putHi16(16, 0); e.putHi16(20, 100);
    e.putHi16(30, 0); e.putHi16(32, 100);
    e.putptr(60, child.bytes);  // entity[15] -> child node
    e.put32(116, 11);
    TypeTable tt(1);
    tt.setByte24(0, 64);

    const u8* table[1] = { e.bytes };
    GameTickScanState st;
    st.entityTable = table;
    st.entityTableCount = 1;
    st.objectTypeRecords = tt.base();

    CHECK_EQ(GameTickInitEntityTracking(st, 50, 50), -1);
}

// 0x414a38 — with no qualifying entity, MainLoop falls back to the supplied
// SelectEntity functor and propagates its -1 (clearing selectionId).
TEST(GameTimeReconEntity, MainLoopFallbackMinusOne) {
    GameTickScanState st;
    st.entityTable = nullptr;
    st.entityTableCount = 0;
    auto fb = [](i32, i32) -> i32 { return -1; };
    i32 r = GameTickMainLoop(st, 10, 20, fb);
    CHECK_EQ(r, -1);
    CHECK_EQ(st.selectionId, -1);
}

// 0x414a38 — fallback returns a non-(-1) value: it is returned unchanged and
// selectionId is left as-is (-1 path not taken).
TEST(GameTimeReconEntity, MainLoopFallbackValue) {
    GameTickScanState st;
    auto fb = [](i32 px, i32 py) -> i32 { return px + py; };
    i32 r = GameTickMainLoop(st, 3, 4, fb);
    CHECK_EQ(r, 7);
}

// 0x414a38 — second scan picks an entity (v16==0 branch). With *(v11+56)==0 the
// scan resolves immediately; *(v11+24)!=9 => returns *(v11+8).
TEST(GameTimeReconEntity, MainLoopPicksEntityReturnsActionCode) {
    EntityRec e;
    // First scan must NOT match (so mainGroupId stays -1): make type byte != 64.
    e.put32(0, 0);
    // Second-scan box (else/v16==0 branch reads +14,+18,+16,+20 then +26..+32):
    e.putHi16(14, 0);  e.putHi16(18, 100);   // x outer range 0..100
    e.putHi16(16, 0);  e.putHi16(20, 100);   // y outer range 0..100
    e.putHi16(26, 0);                       // v29 (x lower) = 0
    e.putHi16(28, 100);                     // b0  (x upper) = 100
    e.putHi16(30, 0);                       // v25 (y lower) = 0
    e.putHi16(32, 100);                     // v26 (y upper) = 100
    e.put32(44, 0);                       // *(v11+44) == 0 -> else branch
    e.put32(52, 0);                       // *(v11+52) == 0
    e.put32(56, 0);                       // *(v11+56) == 0 -> pick now
    e.put32(72, 1);                       // *(v11+72)!=0 -> passes the OR gate
    e.put8(24, 5);                        // *(v11+24) != 9
    e.put32(8, 555);                      // *(v11+8) -> action code returned
    e.put32(0, 0);                        // *(v11) selection id

    const u8* table[1] = { e.bytes };
    GameTickScanState st;
    st.entityTable = table;
    st.entityTableCount = 1;
    st.objectTypeRecords = nullptr;       // first scan type-test fails -> no group
    auto fb = [](i32, i32) -> i32 { return -1; };

    i32 r = GameTickMainLoop(st, 50, 50, fb);
    CHECK_EQ(r, 555);
    CHECK_EQ(st.selectionId, 0);          // dword_62D22C = *(v11)
}
