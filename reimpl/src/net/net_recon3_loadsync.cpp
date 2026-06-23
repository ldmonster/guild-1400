#include "net/net_recon3_loadsync.h"

#include "compress/crc.h"  // CrcCompute — VIBE_Util_Crc32 @0x5dc6e0 (reused, not redefined)

namespace guild::net::recon3 {

// gilde.exe 0x56da94:
//   for ( result = 0; result != 16; dword_13CEC48[result] = -1 ) result += 2;
// Writes indices 0,2,4,...,14 of the dword table — eight entries at stride 2.
void SyncAckReset(SyncAckTable& t) {
    for (u32 i = 0; i < kSyncAckSlots; ++i) {
        t.slot[i] = -1;
        t.ack[i]  = -1;
    }
}

// gilde.exe 0x56dad0:
//   v4 = 0; byte_63CC1D = 0;
//   do {
//     if ( word_12CE910[v4] != -1 ) {
//       v5 = byte_12CE912[v4*2];
//       if ( v5 == 6 || v5 == 7 ) ++byte_63CC1D;
//     }
//     v4 += 268;
//   } while ( v4 != 205824 );
// The original indexes the same table by byte offset (268-byte stride, 768 rows);
// we iterate rows directly. byte_63CC1D is a u8 and wraps at 256 just like the
// original increment of a BYTE.
u8 CountRemotePlayers(const PlayerRow* rows, u32 count) {
    u8 n = 0;
    for (u32 i = 0; i < count; ++i) {
        if (rows[i].liveMarker != 0xFFFF) {
            const u8 s = rows[i].status;
            if (s == kStatusHost || s == kStatusClient)
                ++n; // u8 wraparound matches the BYTE increment
        }
    }
    return n;
}

// gilde.exe 0x56dbbe:
//   v29[0] = dword_12CE914[134 * (unsigned __int16)word_63CC5C];
//   v29[1] = v30;  // = VIBE_Util_Crc32(header)
//   VIBE_Command_QueueRequestFlagBlob32(16, v29);
SyncBlob BuildSyncBlob(i32 localPlayerId, u32 headerCrc32) {
    SyncBlob b{};                 // 31 dwords zero-initialized (matches the v29 stack blob)
    b.dword[0] = localPlayerId;   // v29[0]
    b.dword[1] = static_cast<i32>(headerCrc32); // v29[1] (stored as a raw dword)
    return b;
}

// gilde.exe 0x5dc6e0 — VIBE_Util_Crc32(header, len). compress::CrcCompute applies the
// init/final ~ internally; pass running value 0 for a fresh hash.
u32 SaveHeaderCrc32(const u8* header, u32 len) {
    return guild::compress::CrcCompute(0, header, len);
}

// gilde.exe 0x56dbe9:
//   v19 = 0;
//   while ( v19 < byte_63CC1D ) {
//     v20 = 0;
//     for ( i = 0; i != 16; i += 2 )
//       if ( dword_13CEC50[i] != -1 ) ++v20;
//     VIBE_Amt_RefreshGuildState();   // pump
//   }
// CountAckedSlots reproduces the inner recount (v20); the pump/exit is the caller's.
u32 CountAckedSlots(const SyncAckTable& t) {
    u32 acked = 0;
    for (u32 i = 0; i < kSyncAckSlots; ++i)
        if (t.ack[i] != -1)
            ++acked;
    return acked;
}

bool AllClientsAcked(const SyncAckTable& t, u8 expectedClients) {
    return CountAckedSlots(t) >= expectedClients;
}

} // namespace guild::net::recon3
