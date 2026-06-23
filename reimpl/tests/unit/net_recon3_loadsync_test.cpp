// Golden tests for net_recon3_loadsync — the sync-handshake core of
// VIBE_Net_LoadAndSyncSession @0x56da74. Headless; no sockets.
#include "tests/framework/test.h"

#include "net/net_recon3_loadsync.h"

using namespace guild;
using namespace guild::net::recon3;

// --- Step 1: dword_13CEC48[even] = -1 (8 slots) -------------------------------
TEST(NetRecon3, SyncAckReset_SetsAllSlotsToMinusOne) {
    SyncAckTable t{};
    for (u32 i = 0; i < kSyncAckSlots; ++i) { t.slot[i] = 5; t.ack[i] = 5; }
    SyncAckReset(t);
    for (u32 i = 0; i < kSyncAckSlots; ++i) {
        CHECK_EQ(t.slot[i], -1);
        CHECK_EQ(t.ack[i], -1);
    }
    CHECK_EQ(kSyncAckSlots, 8u); // loop bound 16 / stride 2
}

// --- Step 3: count remote players (status 6 or 7, live marker != -1) ----------
TEST(NetRecon3, CountRemotePlayers_CountsStatus6And7Only) {
    PlayerRow rows[6] = {};
    rows[0] = {0x0000, 6};   // live host       -> count
    rows[1] = {0x0001, 7};   // live client     -> count
    rows[2] = {0xFFFF, 6};   // empty slot      -> skip (marker == -1)
    rows[3] = {0x0002, 5};   // live, status 5  -> skip
    rows[4] = {0x0003, 7};   // live client     -> count
    rows[5] = {0x0004, 8};   // live, status 8  -> skip
    CHECK_EQ(CountRemotePlayers(rows, 6), 3u);
}

TEST(NetRecon3, CountRemotePlayers_EmptyTableIsZero) {
    PlayerRow rows[3] = { {0xFFFF, 6}, {0xFFFF, 7}, {0xFFFF, 6} };
    CHECK_EQ(CountRemotePlayers(rows, 3), 0u);
}

// status boundary: 5 and 8 excluded, 6 and 7 included
TEST(NetRecon3, CountRemotePlayers_StatusBoundary) {
    PlayerRow rows[4] = { {0,5}, {0,6}, {0,7}, {0,8} };
    CHECK_EQ(CountRemotePlayers(rows, 4), 2u);
}

// --- CRC32 golden: standard reflected CRC-32 ("123456789" -> 0xCBF43926) ------
TEST(NetRecon3, SaveHeaderCrc32_KnownVector) {
    const char* s = "123456789";
    u32 c = SaveHeaderCrc32(reinterpret_cast<const u8*>(s), 9);
    CHECK_EQ(c, 0xCBF43926u);
}

TEST(NetRecon3, SaveHeaderCrc32_Empty) {
    u32 c = SaveHeaderCrc32(reinterpret_cast<const u8*>(""), 0);
    CHECK_EQ(c, 0x00000000u); // CRC32 of empty input
}

// --- Step 4: blob layout v29[0]=id, v29[1]=crc, rest zero ---------------------
TEST(NetRecon3, BuildSyncBlob_PutsIdAndCrcInFirstTwoDwords) {
    SyncBlob b = BuildSyncBlob(0x11223344, 0xCBF43926u);
    CHECK_EQ(b.dword[0], 0x11223344);
    CHECK_EQ(static_cast<u32>(b.dword[1]), 0xCBF43926u);
    for (int i = 2; i < 31; ++i) CHECK_EQ(b.dword[i], 0);
}

TEST(NetRecon3, BuildSyncBlob_IsExactly124Bytes) {
    // 31 dwords == the 124-byte payload region copied by QueueRequestFlagBlob32.
    CHECK_EQ(sizeof(SyncBlob), 124u);
    CHECK_EQ(kSyncOpcode, 16); // byte[0x10] flag of the type-0x20 sync packet
}

// --- Step 5: ack progress -----------------------------------------------------
TEST(NetRecon3, CountAckedSlots_CountsAckLaneNotMinusOne) {
    SyncAckTable t{};
    SyncAckReset(t);                 // all ack = -1
    CHECK_EQ(CountAckedSlots(t), 0u);
    t.ack[0] = 100;                  // one client acked
    t.ack[3] = 0;                    // ack value 0 still counts (!= -1)
    CHECK_EQ(CountAckedSlots(t), 2u);
}

TEST(NetRecon3, AllClientsAcked_ExitCondition) {
    SyncAckTable t{};
    SyncAckReset(t);
    CHECK(!AllClientsAcked(t, 2));   // 0 acked, expect 2
    t.ack[0] = 1;
    CHECK(!AllClientsAcked(t, 2));   // 1 acked
    t.ack[1] = 1;
    CHECK(AllClientsAcked(t, 2));    // 2 acked -> loop exits
    CHECK(AllClientsAcked(t, 0));    // expecting nobody -> immediately satisfied
}
