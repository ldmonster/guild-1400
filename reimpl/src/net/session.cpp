#include "net/session.h"
#include "sim/command.h"

#include <cstring>

// Translation notes
// -----------------
// The original wait loops/handshake are inseparable from the live engine: they
// enqueue a sync command through the REUSED command codec, then spin the frame
// loop (VIBE_GameLogic_RunFrameLoop) and the message pump until the command's ACK
// status flips (VIBE_Command_GetPacketStatusById). We preserve that EXACT control
// flow — sync enqueue, then loop `while (!GetPacketStatusById(id)) pump;` — over a
// real CommandQueue, and route the per-frame pump through ISessionHook so the
// world/render side is mockable. In single-player the queue runs standalone
// (dword_764CE0 == -1), so FlushSendQueue applies the sync locally and the loop
// terminates after one pump — exactly the single-player behavior of the binary.

namespace guild::net {

// LocalSessionHook::drive — flush the pending-send queue (standalone => local
// apply) then exec the received list so enqueued commands are acknowledged. This
// is the testable stand-in for one VIBE_GameLogic_RunFrameLoop tick's command
// service (the flush/receive/exec pump gated by mask bit 17 in the real loop).
void LocalSessionHook::drive() {
    ++frames_;
    if (!q_)
        return;
    q_->FlushSendQueue();   // standalone => StoreReceivedPacket (local apply)
    q_->ExecCommands();     // mark ACKs status=2 so the wait loop terminates
}

// gilde.exe 0x43b51c (ip==0 path) — VIBE_Net_ConnectToServer(0, port).
// host==null returns the disconnected sentinel without any OS call; the command
// queue is left in standalone mode so the single-player game lockstep applies its
// own commands locally.
bool NetSession::ConnectLocalStub() {
    if (queue_) {
        queue_->set_standalone(true);    // dword_764CE0 == -1
        queue_->set_disconnected(false); // dword_764CF0 = 0
    }
    return false;  // mirrors the `return -1` of the local stub
}

// Networked connect: delegate to the REUSED transport, put the queue in networked
// mode on success.
bool NetSession::Connect(NetTransport* transport, const char* host, guild::u16 port) {
    if (!transport)
        return false;
    bool ok = transport->ConnectToServer(host, port);
    if (queue_) {
        queue_->set_standalone(!ok ? true : false);
        queue_->set_disconnected(false);
    }
    return ok;
}

// Shared sync-enqueue + pump-until-acked core of the three wait loops.
// Mirrors: copy status banner -> QueueRequestFlagBlob32(14, blob) ->
//          for (id = ...; !GetPacketStatusById(id); RunFrameLoop(flags)) { ... }
int NetSession::RunSyncCore(guild::u32 pumpFlags, const char* statusText) {
    // VIBE_Light_SetGrayColorThunk(0,124) — loading-banner fade (UI side effect).
    if (statusText) {
        std::strncpy(last_status_, statusText, sizeof(last_status_) - 1);
        last_status_[sizeof(last_status_) - 1] = '\0';
    }

    // Build the opcode-14 sync staging packet exactly as QueueRequestFlagBlob32
    // (@0x494ab4) does: v3[0]=0x20 (type), v3[16]=14 (flag/marker), 124-byte blob.
    sim::CommandPacket sync{};
    sync.bytes[sim::kFOpcode] = kSessionSyncType;   // +0  = 0x20
    sync.bytes[sim::kFSync]   = kSessionSyncMarker;  // +16 = 14
    // blob payload (v4[124] @ +17) is left zeroed (no blob arg for the handshake).

    int id = queue_ ? queue_->EnqueuePacket(sync) : -1;
    last_sync_ring_ = id;

    // for (; !GetPacketStatusById(id); RunFrameLoop(flags)) { re-arm banner }
    // status 0 == pending; 2 == applied/acked. Bounded so a never-acked networked
    // peer can't hang a test (the binary relies on the peer; we cap at a large N).
    int guard = 0;
    const int kMaxPumps = 1 << 20;
    while (queue_ && id >= 0 && queue_->GetPacketStatusById(static_cast<guild::u32>(id)) == 0) {
        if (hook_)
            hook_->runFrameLoop(pumpFlags);
        ++pump_count_;
        if (++guard >= kMaxPumps)
            break;
    }
    return pump_count_;
}

// gilde.exe 0x4beb80 — VIBE_Net_RunWaitLoopWithStatus.
int NetSession::RunWaitLoopWithStatus(guild::u32 mode, const char* statusText) {
    return RunSyncCore(mode | 0x300000u, statusText);   // v7 = a1 | 0x300000
}

// gilde.exe 0x4bec44 — VIBE_Net_RunWaitLoop.
int NetSession::RunWaitLoop(guild::u32 mode) {
    return RunSyncCore(mode | 0x210000u, nullptr);      // dword_631598 | 0x210000
}

// gilde.exe 0x4beac8 — VIBE_Net_RunSyncWaitLoop. Same sync enqueue; the original
// shows a timed message box after 1500ms and pumps mode|0x300000. We record the
// mode into mode_word_ (dword_11BC2D0 = a1) like the original and run the core.
int NetSession::RunSyncWaitLoop(guild::u32 mode) {
    mode_word_ = mode;                                  // dword_11BC2D0 = a1
    return RunSyncCore(mode | 0x300000u, "sv_NetworkSync");
}

// gilde.exe 0x56d930 — VIBE_Net_AllPlayersReady.
// byte_63CC40 == 0 -> 0. Walk the table; any live slot in state 6/7 whose ready
// dword is still -1 -> 0. Else 1.
int NetSession::AllPlayersReady(bool gateEnabled, const ReadySlot* slots, std::size_t count) {
    if (!gateEnabled)                       // !byte_63CC40
        return 0;
    for (std::size_t i = 0; i < count; ++i) {
        const ReadySlot& s = slots[i];
        if (!s.alive)                       // byte_12CE918 zero -> skip
            continue;
        const guild::u8 st = s.state;       // byte_12CE912
        // dword_12CEB18 != -1 -> this player slot still has a pending action (not ready).
        if ((st == 6 || st == 7) && s.ready != -1)
            return 0;
    }
    return 1;
}

// Poll the ready bitmask, pumping until bit 2 (all-ready) is set or the bound hits.
// Mirrors `while ((byte_63CC28 & 4) == 0) { PumpMessages; RefreshGuildState; }`.
void NetSession::WaitAllReady(int maxPumps) {
    int n = 0;
    while ((ready_mask_ & 4) == 0 && n < maxPumps) {
        if (hook_)
            hook_->pumpAndRefresh();
        ++pump_count_;
        ++n;
    }
}

// The minimal single-player local path: connect via the local stub (no socket),
// run the sync handshake to ready, then mark the ready bitmask. In standalone the
// LocalSessionHook applies the enqueued sync locally, so RunWaitLoopWithStatus
// returns after one pump and the session is "ready/enter".
bool NetSession::StartLocalSinglePlayer() {
    ConnectLocalStub();                 // ip==0 stub; queue standalone
    mode_word_ |= 0x20000u;             // BYTE2(dword_11BC2D0) |= 2 (LoadAndSyncSession)
    RunWaitLoopWithStatus(mode_word_, "sv_NetworkSync");
    ready_mask_ |= 4;                   // single player is trivially "all ready"
    return last_sync_ring_ >= 0;
}

} // namespace guild::net
