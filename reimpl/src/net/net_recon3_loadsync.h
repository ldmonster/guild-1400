#pragma once
// net_recon3_loadsync — the network-PROTOCOL core of the saved-game resync spine.
//
// gilde.exe 0x56da74 — VIBE_Net_LoadAndSyncSession  (__usercall, eax = sessionName)
//
// The original function is a host-side "load a .SAV, then reach lockstep agreement
// with every connected client" spine. Most of its body is engine glue that lives
// outside this networking cluster (VFS open/read of Gamedata\network\%s.SAV,
// VIBE_Save_* serialization, VIBE_Light_*, VIBE_Hud_* banner, wait-loop status box).
// What IS genuine, reconstructable network-protocol logic — and what this file
// reproduces 1:1 — is the lockstep sync handshake the function performs:
//
//   1. Init the 8-slot per-player sync-ack table to "no ack" (-1):
//        for (result = 0; result != 16; result += 2) dword_13CEC48[result] = -1;
//      i.e. dword_13CEC48[0,2,4,...,14] = -1  (eight dwords at stride 2).
//      (0x56da94..0x56daa3)
//
//   2. Gate on session flag (word_63C740 & 4): only the host that owns the save
//      drives the exchange. (0x56daab)  Clients fall through with result*4.
//
//   3. Count the clients to wait for: walk the 768-slot player/building table
//      (byte_12CE910/byte_12CE912 family, stride 268 bytes, 205824 total) and for
//      each live row (word_12CE910[row] != -1) whose status byte is 6 or 7,
//      ++byte_63CC1D.  byte_63CC1D = number of remote players in the session.
//      (0x56dad0..0x56dafb)
//
//   4. CRC32 the just-loaded save header (v30 = VIBE_Util_Crc32(hdr, len)) and emit
//      the SYNC packet:
//        v29[0] = dword_12CE914[134 * word_63CC5C];   // local player's id field
//        v29[1] = v30;                                // header CRC32
//        VIBE_Command_QueueRequestFlagBlob32(16, v29);
//      The blob32 builder (0x494ab4) lays this on the wire as a type-0x20 packet
//      with byte[0x10] = 16 (the sync opcode) and bytes[0x11..0x8C] = the 124-byte
//      payload (v29[0]=id, v29[1]=crc, rest zero).  (0x56dbbe..0x56dbe4)
//
//   5. Spin until every client has acked: while v19 < byte_63CC1D, recount the acked
//      slots (dword_13CEC50[0,2,...,14] != -1 → ++acked) and pump the engine.
//      dword_13CEC50 == &dword_13CEC48[2]: the table is {slotId, ackValue} pairs, so
//      the "ack" lane is the odd dwords.  (0x56dbe9..0x56dc14)
//
// Socket syscalls are NOT here — the packet is handed to the Command queue, which
// is flushed by VIBE_Net_SendPacket through INetSocket (see src/net/transport.cpp).
// CRC32 reuses compress::CrcCompute (0x5dc6e0), the blob32 framing reuses
// sim::QueueRequestFlagBlob32 (0x494ab4); neither is redefined here.
//
// DEFERRED (rule 8): the surrounding VFS/save/scene/banner glue of 0x56da74, and the
// sibling lobby spines VIBE_Net_StartNetworkGame (0x503f78) /
// VIBE_Net_LoadSavedNetworkGame (0x50442c), depend on ~30 unreconstructed engine
// subsystems (Universe slots, Scene_LoadFromStream, Mission_*, INI files, Window
// forms) and are out of this cluster's scope. See lobby.h for their call-graph spine.

#include <cstdint>
#include "guild/common/types.h"

namespace guild::net::recon3 {

using guild::u8;
using guild::u16;
using guild::u32;
using guild::i32;

// Constants recovered verbatim from 0x56da74.
constexpr u32 kSyncAckSlots   = 8;        // 8 dword pairs in dword_13CEC48
constexpr u8  kSyncOpcode     = 16;       // blob32 flag byte (byte[0x10]) for the sync packet
constexpr i32 kPlayerRowStride = 268;     // byte_12CE910 row stride
constexpr i32 kPlayerRowTotal  = 205824;  // loop bound (== 768 * 268)
constexpr u8  kStatusHost      = 6;       // status bytes counted as "remote player"
constexpr u8  kStatusClient    = 7;

// One row of the player/building status table the count loop walks.
// gilde.exe word_12CE910 (+0, the live marker) / byte_12CE912 (+2, the status byte).
struct PlayerRow {
    u16 liveMarker; // word_12CE910[row]; 0xFFFF (-1 as i16) means empty slot
    u8  status;     // byte_12CE912[row*2]; 6 or 7 => remote player in the session
};

// The 8-slot sync-ack table (dword_13CEC48). Modeled as {slotId, ack} pairs so the
// even lane (slotId) and odd lane (ack, dword_13CEC50) match the original strides.
struct SyncAckTable {
    i32 slot[kSyncAckSlots]; // dword_13CEC48[0,2,...,14]
    i32 ack[kSyncAckSlots];  // dword_13CEC50[0,2,...,14]
};

// gilde.exe 0x56da94 — init step: dword_13CEC48[even] = -1 (slots reset to "unset").
// The original only writes the slot lane; ack lane is left as-is by step 1, then
// observed in step 5. We reset both to a clean -1 baseline for a fresh exchange.
void SyncAckReset(SyncAckTable& t);

// gilde.exe 0x56dad0 — count remote players: rows with liveMarker != -1 and status in
// {6,7}. Returns byte_63CC1D. `rows`/`count` describe the live table snapshot.
u8 CountRemotePlayers(const PlayerRow* rows, u32 count);

// The 124-byte payload of the type-0x20 sync packet (matches the v29[31] dword blob
// fed to QueueRequestFlagBlob32; only the first two dwords are populated).
struct SyncBlob {
    i32 dword[31]; // v29[31]
};

// gilde.exe 0x56dbbe..0x56dbe4 — build the blob-16 sync payload.
//   v29[0] = localPlayerId (dword_12CE914[134 * word_63CC5C]);
//   v29[1] = headerCrc32   (VIBE_Util_Crc32 of the save header);
// remaining 29 dwords are zero. Returns the blob; the caller hands it to
// QueueRequestFlagBlob32(16, &blob).
SyncBlob BuildSyncBlob(i32 localPlayerId, u32 headerCrc32);

// gilde.exe 0x5dc6e0 — VIBE_Util_Crc32 (standard reflected CRC-32, init ~0, final ~).
// Thin pass-through to the existing compress::CrcCompute so the header-CRC step is
// reproduced exactly without re-tabulating the polynomial here.
u32 SaveHeaderCrc32(const u8* header, u32 len);

// gilde.exe 0x56dbe9..0x56dc14 — ack progress: count slots whose ack lane != -1.
// Returns the number acked; the spine loops until this reaches byte_63CC1D.
u32 CountAckedSlots(const SyncAckTable& t);

// True once every expected remote player has acked (the spine's exit condition).
bool AllClientsAcked(const SyncAckTable& t, u8 expectedClients);

} // namespace guild::net::recon3
