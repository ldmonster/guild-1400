#pragma once
#include "guild/common/types.h"
#include "shim/INetSocket.h"
#include "net/transport.h"
#include "sim/command.h"

// gilde.exe — guild::net  (MODULE: network SESSION orchestration)
//
// The session layer is the glue that drives a multiplayer (or single-player
// loopback) game from "connected" to "in the world": the connect/sync handshake,
// the all-players-ready gate, the host->client savegame transfer, and the wait
// loops that pump frames until a lockstep command is acknowledged.
//
// It sits on top of the already-translated transport (net/transport), the command
// codec (sim/command), the savegame serializer (io/save, io/gamestate) and the
// engine's reflected CRC-32 (compress/crc). Per ODR we REUSE those modules — this
// file forward-declares only the cross-module command-codec helpers it needs and
// otherwise links against them. OS sockets go through shim::INetSocket; world
// mutations (building tables, scene sync, frame loop) are injected as a mock
// "command hook" so the orchestration is testable in isolation.
//
// Recovered entry points (1:1):
//   VIBE_Net_LoadAndSyncSession    @0x56da74  (CRC32 of save header, exchange blob 16)
//   VIBE_Net_StartNetworkGame      @0x503f78  (host: send save -> ready -> enter)
//   VIBE_Net_LoadSavedNetworkGame  @0x50442c  (host: send .SAV -> ready -> load -> enter)
//   VIBE_Net_AllPlayersReady       @0x56d930  (ready gate over the building table)
//   VIBE_Net_RunWaitLoopWithStatus @0x4beb80  (enqueue opcode-14 sync, pump until acked)
//   VIBE_Net_RunSyncWaitLoop       @0x4beac8  (   "   with a timed message box)
//   VIBE_Net_RunWaitLoop           @0x4bec44  (   "   minimal variant)
//   ConnectToServer(0,0) local stub@0x43b51c  (ip==0 => single-player loopback)
//
// The savegame-transfer wire protocol (host->clients) is recovered byte-for-byte
// from VIBE_Net_SendSaveGameToClients @0x5abfe8 / the cmd08/cmd09 apply handlers
// VIBE_Command_HandleLoadBufAlloc @0x4964a4 / HandleLoadBufAppend @0x4964d8 and the
// reassembly consumer VIBE_Net_LoadReceivedSaveStream @0x5ac140. See net_savegame.h.

namespace guild::sim { class CommandQueue; struct CommandPacket; }

namespace guild::net {

// ===========================================================================
// Recovered session-handshake packet layout (the opcode-14 SYNC blob)
// ---------------------------------------------------------------------------
// Every wait loop and LoadAndSyncSession reaches the ready state by enqueueing a
// SYNC command through VIBE_Command_QueueRequestFlagBlob32(flag, blob32) @0x494ab4:
//
//   v3[17]   staging header   v3[0] = 0x20 (type 32, kSyncType)
//                             v3[16]= flag byte  (14 for the sync handshake)
//   v4[124]  blob payload      copied from `blob32` at +17 (124 bytes)
//   -> VIBE_Command_EnqueuePacket(v3)
//
// On the wire the encoder (EnqueuePacket @0x49388c) then stamps the standard
// header: len@+1 (ComputePacketSize: opcode 0x20 with marker 14 => 17 bytes, the
// SHORT sync variant), cmdId@+4 (ring slot), Count@+8. So a handshake sync frame
// is exactly 17 bytes: [0x20][len=17][flag0][cmdId][Count][...][marker=14].
//
// The "ready" command is the same QueueRequestFlagBlob32 path with flag 6
// (opcode 6 => 145-byte default size) carrying the live-player count byte_63CC1D.
enum SessionFlag : guild::u8 {
    kFlagSync  = 14, // sv_NetworkSync handshake: type 0x20 + marker 14 (short, 17B)
    kFlagReady = 6,  // group-end / "all players ready" request (byte_63CC1D count)
    kFlagEnter = 9,  // post-ready enter/mission request
};

// Sync packet discriminator on the wire (matches sim/net): type 0x20 && byte[16]==14.
constexpr guild::u8 kSessionSyncType   = 0x20;
constexpr guild::u8 kSessionSyncMarker = 14;
// The short sync frame size emitted for the handshake (ComputePacketSize op 0x20).
constexpr guild::u16 kSyncFrameBytes = 17;
// Blob payload size copied by QueueRequestFlagBlob32 (the v4[124] region @+17).
constexpr guild::u32 kBlobBytes = 124;

// ===========================================================================
// Command hook (mock seam for world mutations)
// ---------------------------------------------------------------------------
// In the binary the wait loops/handshake call into the live engine: enqueue a
// sync command, then spin VIBE_GameLogic_RunFrameLoop / VIBE_Amt_RefreshGuildState
// until VIBE_Command_GetPacketStatusById(id) flips. We drive a real CommandQueue
// (so the sequence/ack semantics are exact) and expose the per-frame "pump" as a
// hook so a test can advance state without the renderer/world. Returning the
// queue's flush+exec applies the command locally in standalone mode, which is how
// single-player reaches ready.
struct ISessionHook {
    virtual ~ISessionHook() = default;
    // VIBE_GameLogic_RunFrameLoop @0x4c09a0 — one pump of the frame/command loop.
    // `flags` mirrors the (mode | 0x300000 / 0x210000) bitmask the loops pass.
    virtual void runFrameLoop(guild::u32 flags) = 0;
    // VIBE_Window_PumpMessages @0x4bea64 + VIBE_Amt_RefreshGuildState @0x4becdc.
    virtual void pumpAndRefresh() = 0;
};

// A trivial hook that just drives the CommandQueue (flush -> exec) each pump, so a
// standalone session applies its own enqueued commands and the wait loops finish.
class LocalSessionHook : public ISessionHook {
public:
    explicit LocalSessionHook(sim::CommandQueue* q) : q_(q) {}
    void runFrameLoop(guild::u32) override { drive(); }
    void pumpAndRefresh() override { drive(); }
    int frames() const { return frames_; }
private:
    void drive();
    sim::CommandQueue* q_;
    int frames_ = 0;
};

// ===========================================================================
// NetSession — the orchestration object
// ---------------------------------------------------------------------------
// Gathers the original's scattered globals (word_63C740 session flags, byte_63CC1D
// live-player count, byte_63CC28 ready bitmask, dword_11BC2D0 mode word) onto one
// instance so the handshake/ready/transfer flow is testable. The lockstep state
// lives in the supplied CommandQueue (REUSED); socket I/O in the NetTransport
// (REUSED); world mutations go through the ISessionHook.
class NetSession {
public:
    NetSession(sim::CommandQueue* queue, ISessionHook* hook)
        : queue_(queue), hook_(hook) {}

    // ---- session flags (word_63C740) --------------------------------------
    void set_session_flags(guild::u16 f) { session_flags_ = f; }
    guild::u16 session_flags() const { return session_flags_; }
    bool is_network() const { return (session_flags_ & 0x0004) != 0; } // &4 network
    bool is_host()    const { return (session_flags_ & 0x0010) != 0; } // &0x10 host

    // byte_63CC1D — count of live "ready-relevant" building slots (states 6/7).
    void set_live_player_count(guild::u8 n) { live_player_count_ = n; }
    guild::u8 live_player_count() const { return live_player_count_; }

    // byte_63CC28 — ready bitmask the server sets (&1 host, &4 all-ready).
    void set_ready_mask(guild::u8 m) { ready_mask_ = m; }
    guild::u8 ready_mask() const { return ready_mask_; }

    // dword_11BC2D0 — the engine mode word; LoadAndSyncSession ORs in 0x20000.
    guild::u32 mode_word() const { return mode_word_; }
    void set_mode_word(guild::u32 w) { mode_word_ = w; }

    // ===== single-player local-stub connect (ConnectToServer(0,0)) =========
    // gilde.exe 0x43b51c (ip==0 path @0x43b590) — `ConnectToServer(0, port)` resets
    // all transport state then, because host==null, returns the disconnected
    // sentinel (-1) WITHOUT touching the OS. The single-player game runs the
    // CommandQueue in standalone mode (dword_764CE0 == -1) and never opens a socket.
    // Returns false (== the -1 sentinel); the queue is put in standalone mode.
    bool ConnectLocalStub();

    // ===== networked connect (delegates to the transport, REUSED) ==========
    // host != nullptr => real (loopback/TCP) connect; the queue is networked.
    bool Connect(NetTransport* transport, const char* host, guild::u16 port);

    // ===== wait loops ======================================================
    // gilde.exe 0x4beb80 — VIBE_Net_RunWaitLoopWithStatus(mode, statusText).
    // Copies the status string into the loading banner (modeled as a recorded
    // string), enqueues an opcode-14 SYNC via QueueRequestFlagBlob32, then pumps
    // RunFrameLoop(mode|0x300000) until the command is acked. Returns the ack tick.
    int RunWaitLoopWithStatus(guild::u32 mode, const char* statusText);

    // gilde.exe 0x4bec44 — VIBE_Net_RunWaitLoop (minimal: pump mode|0x210000).
    int RunWaitLoop(guild::u32 mode);

    // gilde.exe 0x4beac8 — VIBE_Net_RunSyncWaitLoop(mode). Same sync enqueue with
    // a timed message-box (modeled by a deadline counter); pumps mode|0x300000.
    int RunSyncWaitLoop(guild::u32 mode);

    // ===== all-players-ready gate ==========================================
    // gilde.exe 0x56d930 — VIBE_Net_AllPlayersReady. The original walks the 768-slot
    // building table (word_12CE910 stride 268): if byte_63CC40 is unset returns 0;
    // for every live slot whose state byte (byte_12CE912) is 6 or 7 AND whose
    // pending-action dword (dword_12CEB18) is still NON-(-1), returns 0 (that slot
    // has not finished); a slot is ready once that dword clears to -1. Else 1.
    // We model the table as an
    // injected vector of (alive, state, ready) so the gate logic is exact & testable.
    struct ReadySlot {
        bool alive;        // word_12CE910[i] != -1  (and byte_12CE918 nonzero)
        guild::u8 state;   // byte_12CE912 (6 or 7 == a network player slot)
        guild::i32 ready;  // dword_12CEB18 (-1 == not yet ready)
    };
    static int AllPlayersReady(bool gateEnabled /*byte_63CC40*/,
                               const ReadySlot* slots, std::size_t count);

    // Convenience: poll AllPlayersReady, pumping until the gate opens (bounded).
    // Mirrors the `while ((byte_63CC28 & 4) == 0) pump;` loops in Start/LoadSaved.
    void WaitAllReady(int maxPumps);

    // ===== full single-player local path ===================================
    // The minimal "start a session without a real socket": ConnectLocalStub, run
    // the sync handshake to ready, mark the ready bitmask. Returns true when the
    // session reaches the ready/enter state (the single-player happy path).
    bool StartLocalSinglePlayer();

    // --- inspection (tests) -------------------------------------------------
    const char* last_status() const { return last_status_; }   // recorded banner
    int last_sync_ring() const { return last_sync_ring_; }      // last opcode-14 id
    int pump_count() const { return pump_count_; }

private:
    // The shared sync-enqueue + pump-until-acked core of all three wait loops.
    int RunSyncCore(guild::u32 pumpFlags, const char* statusText);

    sim::CommandQueue* queue_;
    ISessionHook*      hook_;

    guild::u16 session_flags_ = 0;     // word_63C740
    guild::u8  live_player_count_ = 0; // byte_63CC1D
    guild::u8  ready_mask_ = 0;        // byte_63CC28
    guild::u32 mode_word_ = 0;         // dword_11BC2D0

    char last_status_[64] = {0};
    int  last_sync_ring_ = -1;
    int  pump_count_ = 0;
};

} // namespace guild::net
