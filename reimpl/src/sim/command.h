#pragma once
#include "guild/common/types.h"

// gilde.exe — Command lockstep CODEC: the deterministic packet
// build/encode/size/enqueue/sequence machinery (namespace guild::sim).
//
// This is the determinism core of the game's networking. All state mutations on
// every peer travel as fixed-size 153-byte command packets applied in identical
// sequence order. This file models the queue machinery: the 153-byte packet
// record, the per-opcode wire-size table (VIBE_Command_ComputePacketSize), the
// 32768-slot send ring (unk_BAFB60), the ACK/status table (byte_B5FB60), the
// intrusive pending/received linked lists, and the enqueue/flush/receive/exec
// glue. The ~120 Command_Ex* apply handlers (the jump-table targets) and the
// delta encoder live elsewhere (delta encoder -> command_codec.{h,cpp};
// apply handlers -> deferred).
//
// Socket I/O is guild::net (already implemented); we forward-declare its two
// entry points and let the host wire them (tests provide a mock transport).

namespace guild::sim {

// --- Packet geometry --------------------------------------------------------

// Fixed wire/staging record stride. qmemcpy(...,0x99) in EnqueuePacket /
// StoreReceivedPacket. 153 bytes.
constexpr u32 kPacketStride = 0x99;        // 153

// Send-ring capacity and sequence mask. Ring index = (seq) & 0x7FFF.
constexpr u32 kSendRingSlots = 0x8000;     // 32768
constexpr u32 kSeqMask       = 0x7FFF;

// ACK/status table: 10-byte entries indexed 10 * (seq & 0x7FFF). 32768 entries.
constexpr u32 kAckEntryBytes = 10;
constexpr u32 kAckTableBytes = kAckEntryBytes * kSendRingSlots; // 327680

// Max delta/raw payload size (cursor guard in the Append* encoders).
constexpr u32 kMaxPayload = 0x77;          // 119

// Number of dispatch opcodes (jump table funcs_4941F4 @0x631298 has 96 entries).
constexpr u32 kNumOpcodes = 96;

// Opcodes referenced explicitly by the codec / recon.
enum Opcode : u8 {
    kOpInitSync   = 3,    // QueueInitAndSync handshake
    kOpGroupBegin = 5,    // ExecCommandGroup: group-begin frame
    kOpGroupEnd   = 6,    // ExecCommandGroup: group-end frame
    kOpGroupSkip  = 7,    // skip marker
    kOpVarFields1 = 0x16, // 22 — variable-length field batch
    kOpVarFields2 = 0x17, // 23 — variable-length field batch
    kOpSync       = 0x20, // 32 — entity sync (short 17B if marker byte +16==14)
};

// Sync packet discriminator (matches guild::net): type 0x20 && byte[+16]==14.
constexpr u8 kSyncType   = 0x20;
constexpr u8 kSyncMarker = 14;

// --- Packet header field offsets (recovered from EnqueuePacket @0x49388c and
// the net transport recon). All multi-byte fields are little-endian. -----------
enum PacketField : u32 {
    kFOpcode = 0x00,  // u8  opcode (0..95) -> handler + wire size
    kFLen    = 0x01,  // u16 computed wire length (ComputePacketSize)
    kFFlag   = 0x03,  // u8  status/flag byte (set 0 by encoder)
    kFCmdId  = 0x04,  // u32 ring slot index (seq & 0x7FFF)  [cmdId/owner]
    kFCount  = 0x08,  // u32 sequence Count (monotonic send counter)
    kFExtra  = 0x0C,  // u32 ack/pending scratch (set 0 by encoder)
    kFSync   = 0x10,  // u8  sync marker byte (14 => short sync variant)
    kFPayload = 0x10, // payload region begins here for most opcodes
};

// Intrusive doubly-linked-list link fields inside the 153-byte record.
enum PacketLink : u32 {
    kLPrev = 145,     // u32 prev link (+0x91)
    kLNext = 149,     // u32 next link (+0x95)
};

// The queued 153-byte command record. The first 16 bytes are the header; the
// remainder is opcode-specific payload plus the two trailing intrusive links.
// Layout is byte-exact so it round-trips on the wire.
struct CommandPacket {
    u8 bytes[kPacketStride];

    u8&  opcode()       { return bytes[kFOpcode]; }
    u8   opcode() const { return bytes[kFOpcode]; }

    // Little-endian header accessors (mirror the raw +0xNN stores in the IDB).
    void  set_len(u16 v)    { put16(kFLen, v); }
    u16   len() const       { return get16(kFLen); }
    void  set_flag(u8 v)    { bytes[kFFlag] = v; }
    u8    flag() const      { return bytes[kFFlag]; }
    void  set_cmd_id(u32 v) { put32(kFCmdId, v); }
    u32   cmd_id() const    { return get32(kFCmdId); }
    void  set_count(u32 v)  { put32(kFCount, v); }
    u32   count() const     { return get32(kFCount); }
    void  set_extra(u32 v)  { put32(kFExtra, v); }
    u32   extra() const     { return get32(kFExtra); }
    u8    sync_marker() const { return bytes[kFSync]; }

    bool is_sync() const { return opcode() == kSyncType && bytes[kFSync] == kSyncMarker; }

    void put16(u32 off, u16 v) {
        bytes[off]     = static_cast<u8>(v);
        bytes[off + 1] = static_cast<u8>(v >> 8);
    }
    void put32(u32 off, u32 v) {
        bytes[off]     = static_cast<u8>(v);
        bytes[off + 1] = static_cast<u8>(v >> 8);
        bytes[off + 2] = static_cast<u8>(v >> 16);
        bytes[off + 3] = static_cast<u8>(v >> 24);
    }
    u16 get16(u32 off) const {
        return static_cast<u16>(bytes[off] | (bytes[off + 1] << 8));
    }
    u32 get32(u32 off) const {
        return static_cast<u32>(bytes[off]) | (static_cast<u32>(bytes[off + 1]) << 8)
             | (static_cast<u32>(bytes[off + 2]) << 16) | (static_cast<u32>(bytes[off + 3]) << 24);
    }
};
static_assert(sizeof(CommandPacket) == kPacketStride, "packet stride must be 153");

// --- Per-slot ACK/status entry (byte_B5FB60, 10-byte stride) ----------------
// Recovered from EnqueuePacket (writes +0 status, +2 ring index), ExecCommands
// (sets +0=2 applied, +1 slot, +6 seq), QueueInitAndSync (init +0=1, +2=-1),
// GetPacketStatusById (reads +0), GetPacketSeqById (reads +6).
GUILD_PACKED_BEGIN
struct AckEntry {
    u8  status;   // +0  0=pending, 1=free/init, 2=applied/acked
    u8  slot;     // +1  exec slot tag
    i32 ring;     // +2  ring slot index assigned at enqueue (init -1)
    i32 seq;      // +6  sequence Count recorded at exec
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(AckEntry) == 10, "ack entry must be 10 bytes");

// VIBE_Command_ComputePacketSize @0x493034 — per-opcode wire size. The big
// switch(opcode) giving each opcode's on-wire length. Opcodes 0x16/0x17 and
// 0x18 are variable (count byte at +20); 0x20 is 17 (short sync) or 141.
// Reads from the packet because variable sizes depend on payload bytes.
u16 ComputePacketSize(const CommandPacket& pkt);
// Convenience overload for the fixed-size opcodes (no payload dependence).
u16 ComputePacketSizeFixed(u8 opcode);

// The CommandQueue owns the send ring, the ACK table, the pending-send list,
// the received list and the free list. In the original these are file globals
// (unk_BAFB60, byte_B5FB60, dword_11AA46C/498/49C/494, dword_764CE0/CE8 …); we
// gather them into one instance so the codec is testable in isolation. One live
// instance per session reproduces the original's single global state.
class CommandQueue {
public:
    CommandQueue();
    ~CommandQueue();
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    // VIBE_Command_QueueInitAndSync @0x4931e0 (init half only — the net
    // handshake is left to the host). Resets the ring/lists/ACK table to the
    // initial state: free list spans the received-packet pool, send counter 0,
    // all ACK entries status=1 ring=-1. The original also enqueues an opcode-3
    // handshake and blocks on the reply; that lives in the net glue.
    void Init();

    // dword_764CE0 == -1 => standalone (single-player/host): FlushSendQueue
    // applies packets locally instead of sending. Set false for networked play.
    void set_standalone(bool v) { standalone_ = v; }
    bool standalone() const { return standalone_; }

    // dword_764CF0 — transport-disconnected latch (EnqueuePacket early-outs).
    void set_disconnected(bool v) { disconnected_ = v; }
    bool disconnected() const { return disconnected_; }

    // VIBE_Command_EnqueuePacket @0x49388c — copy a staged 153-byte packet into
    // the send ring at (sendCount+1)&0x7FFF, stamp the header (len/cmdId/count),
    // seed the ACK entry, and link it onto the pending-send list. Returns the
    // assigned ring slot index, or -1 if disconnected / ring full / duplicate.
    i32 EnqueuePacket(const CommandPacket& staged);

    // VIBE_Command_FlushSendQueue @0x4934cc — drain the pending-send list. In
    // standalone mode each packet is applied locally via StoreReceivedPacket;
    // networked it is handed to the transport. Returns 0 normally, 1 if local
    // apply signalled "received-pool exhausted".
    int FlushSendQueue();

    // VIBE_Command_StoreReceivedPacket @0x493f80 — pull a 153-byte record from
    // the free list, copy `src` into it, recompute its length, and append it to
    // the received list. Returns 1 if the free pool is exhausted, else 0.
    int StoreReceivedPacket(const CommandPacket& src);

    // VIBE_Command_UnlinkReceivedPacket @0x494028 — detach a received-list node
    // and return it to the free list head.
    void UnlinkReceivedPacket(CommandPacket* pkt);

    // VIBE_Command_ExecCommands @0x494088 — walk the received list in sequence
    // (Count) order, classify lost/duplicate/sync gaps against last-requested /
    // last-sync counters, dispatch each opcode to its handler, and recycle the
    // node. Handlers are supplied via a 96-entry dispatch table (see set_handler);
    // unset opcodes are no-ops. Returns 0.
    int ExecCommands();

    using Handler = void (*)(CommandQueue&, CommandPacket& pkt, AckEntry* ack);
    void set_handler(u8 opcode, Handler fn) { if (opcode < kNumOpcodes) handlers_[opcode] = fn; }

    // VIBE_Command_GetPacketStatusById @0x4939d4 — ACK status by ring id (-1 if
    // out of range).
    int GetPacketStatusById(u32 ringId) const;
    // VIBE_Command_GetPacketSeqById @0x4939fc — recorded seq by ring id (0 if
    // out of range).
    i32 GetPacketSeqById(u32 ringId) const;

    // --- inspectable state (the original globals) ---------------------------
    u32 send_count() const { return send_count_; }     // dword_11AA494
    u32 last_req_count() const { return last_req_count_; } // dword_11AA468
    u32 last_sync_count() const { return last_sync_count_; } // dword_11AA470
    CommandPacket* pending_head() const { return pending_head_; }   // dword_11AA46C
    CommandPacket* received_head() const { return received_head_; } // dword_11AA498
    const AckEntry& ack(u32 ringId) const { return ack_[ringId & kSeqMask]; }

    // Direct ring access for tests (the original indexes unk_BAFB60 by slot).
    CommandPacket& ring_slot(u32 idx) { return ring_[idx & kSeqMask]; }

    // Received/free packet pool capacity (byte_1078360 spans 8192 records).
    static constexpr u32 kPoolSlots = 0x2000; // 8192

private:
    // Intrusive list links (the original's +145/+149 record fields), held in a
    // side table so the 153-byte record stays byte-exact on 64-bit hosts.
    struct Links;
    Links* PoolLink(CommandPacket* p);  // links for received/free lists
    Links* RingLink(CommandPacket* p);  // links for the pending-send list

    CommandPacket* FreePop();           // take a node from the free list
    void           FreePush(CommandPacket* p); // return a node to the free list
    void           AppendReceived(CommandPacket* p);

    // Pending-send list is threaded through the *ring* records' link fields.
    CommandPacket* ring_next(CommandPacket* p);
    CommandPacket* ring_prev(CommandPacket* p);
    void           ring_set_next(CommandPacket* p, CommandPacket* n);
    void           ring_set_prev(CommandPacket* p, CommandPacket* pr);

    // unk_BAFB60: the 153-byte send ring (32768 slots).
    CommandPacket* ring_;               // heap-allocated (large)
    // byte_B5FB60: ACK/status table, 10-byte entries, 32768.
    AckEntry*      ack_;
    // byte_1078360 pool: received/free packet records (8192 in the original).
    CommandPacket* pool_;
    Links*         poolLinks_;          // side links for pool_ records
    Links*         ringLinks_;          // side links for ring_ records

    CommandPacket* pending_head_  = nullptr; // dword_11AA46C
    CommandPacket* received_head_ = nullptr; // dword_11AA498
    CommandPacket* free_head_     = nullptr; // dword_11AA49C

    u32 send_count_     = 0;  // dword_11AA494 — monotonic send counter
    u32 last_req_count_ = 0;  // dword_11AA468 — last in-order Count seen
    u32 last_sync_count_= 0;  // dword_11AA470 — last sync Count seen

    bool standalone_   = true;   // dword_764CE0 == -1
    bool disconnected_ = false;  // dword_764CF0

    Handler handlers_[kNumOpcodes] = {};
};

} // namespace guild::sim
