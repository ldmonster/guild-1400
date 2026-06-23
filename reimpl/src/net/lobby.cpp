#include "net/lobby.h"

#include <cstring>

// Translation notes
// -----------------
// The two SETUP functions (RunHostNetworkSetup / LoadNetworkSaveProfile) are pure
// record builders past their GUI/file leaves: each stamps the broadcast advert
// block @0x122F84E (magic/kind/tag/name/playerCount/scenario) and OR's a bit into
// the session flags word_63C740. We translate exactly those stores; the GUI
// pickers (ChoosePlayerCount/RunChooseCity/RunLoadNetworkGame) and the save-header
// parse (Save_LoadHeaderAndThumbnail) are leaves the caller supplies the result of.
//
// The two SPINE functions (StartNetworkGame / LoadSavedNetworkGame) share one
// control-flow skeleton — the lobby ready->ship->barrier handshake — which we
// translate 1:1 over a real CommandQueue, with the savegame ship delegated to the
// REUSED net::SendSaveGameToClients and the per-frame pump routed through a hook.
// The scene-sync / Save_LoadGameFile render+world leaves are DEFERRED.

namespace guild::net {

// ===========================================================================
// Advert record encode / decode (the 0x122F84E block <-> discovery datagram)
// ===========================================================================
std::vector<u8> EncodeLobbyAdvert(const LobbyAdvert& a) {
    const std::size_t n = (a.kind == kAdKindSav) ? kSaveAdvertBytes : kHostAdvertBytes;
    std::vector<u8> e(n, 0);

    // word_122F84E = magic (+0), word_122F850 = kind (+2), word_122F852 = tag (+4).
    e[kAdvMagicOff + 0] = static_cast<u8>(a.magic);
    e[kAdvMagicOff + 1] = static_cast<u8>(a.magic >> 8);
    e[kAdvKindOff + 0]  = static_cast<u8>(a.kind);
    e[kAdvKindOff + 1]  = static_cast<u8>(a.kind >> 8);
    e[kAdvTagOff + 0]   = static_cast<u8>(a.tag);
    e[kAdvTagOff + 1]   = static_cast<u8>(a.tag >> 8);

    // unk_122F854 = name string (+6), copied NUL-terminated, clamped to the record
    // (the original does the 2-byte-at-a-time strcpy into the advert block).
    std::size_t maxName = (kAdvCountOff > kAdvNameOff) ? (kAdvCountOff - kAdvNameOff - 1) : 0;
    std::size_t nameLen = a.name.size();
    if (nameLen > maxName)
        nameLen = maxName;
    std::memcpy(e.data() + kAdvNameOff, a.name.data(), nameLen);
    // terminator already zero from the memset.

    // byte_122F874 = playerCount (+0x26), word_122F876 = extra/scenario (+0x28).
    e[kAdvCountOff]     = a.playerCount;
    e[kAdvExtraOff + 0] = static_cast<u8>(a.extra);
    e[kAdvExtraOff + 1] = static_cast<u8>(a.extra >> 8);
    return e;
}

bool DecodeLobbyAdvert(const std::vector<u8>& buf, LobbyAdvert& out) {
    // The DiscoverServers accept predicate: >=6 bytes, magic==1, kind in {1,4},
    // tag==106. Reproduced so decode == the exact discovery acceptance.
    if (buf.size() < kAdMinBytes)
        return false;
    const u16 magic = static_cast<u16>(buf[kAdvMagicOff] | (buf[kAdvMagicOff + 1] << 8));
    const u16 kind  = static_cast<u16>(buf[kAdvKindOff]  | (buf[kAdvKindOff + 1] << 8));
    const u16 tag   = static_cast<u16>(buf[kAdvTagOff]   | (buf[kAdvTagOff + 1] << 8));
    if (magic != kAdMagic || (kind != kAdKindOne && kind != kAdKindSav) || tag != kAdTag)
        return false;

    out.magic = magic;
    out.kind  = kind;
    out.tag   = tag;
    // name at +6: read up to the NUL or the count field, whichever comes first.
    out.name.clear();
    for (std::size_t i = kAdvNameOff; i < buf.size() && i < kAdvCountOff; ++i) {
        if (buf[i] == 0)
            break;
        out.name.push_back(static_cast<char>(buf[i]));
    }
    out.playerCount = (buf.size() > kAdvCountOff) ? buf[kAdvCountOff] : 0;
    out.extra = (buf.size() > kAdvExtraOff + 1)
                    ? static_cast<u16>(buf[kAdvExtraOff] | (buf[kAdvExtraOff + 1] << 8))
                    : 0;
    return true;
}

// ===========================================================================
// Lobby setup — build the advert
// ===========================================================================

// gilde.exe 0x528dac — VIBE_Menu_RunHostNetworkSetup (advert-build slice).
LobbyAdvert BuildHostAdvert(const std::string& cityName, u8 playerCount,
                            u16 scenarioWord, u16* outSessionFlags) {
    LobbyAdvert a;
    a.magic = kAdMagic;          // word_122F84E = 1
    a.kind  = kAdKindOne;        // word_122F850 = 1  (host new game)
    a.tag   = kAdTag;            // word_122F852 = 106
    a.name  = cityName;          // unk_122F854  = ReturnedString (the chosen city)
    a.playerCount = playerCount; // byte_122F874 = byte_63CC1D
    a.extra = scenarioWord;      // word_122F876 = dword_122F490
    // LOBYTE(word_63C740) = word_63C740 | 0x10 — set the HOST session bit.
    if (outSessionFlags)
        *outSessionFlags |= 0x10;
    return a;
}

// gilde.exe 0x528f24 — VIBE_Net_LoadNetworkSaveProfile (advert-build slice).
LobbyAdvert BuildSaveJoinAdvert(const SaveProfileHeader& header, u16* outSessionFlags) {
    LobbyAdvert a;
    a.magic = kAdMagic;              // word_122F84E = 1
    a.kind  = kAdKindSav;            // word_122F850 = 4  (saved game)
    a.tag   = kAdTag;                // word_122F852 = 106
    a.name  = header.name;           // unk_122F854  = byte_122F878-style name copy
    a.playerCount = header.playerCount; // byte_122F874 = byte_63CC1D <- header[44]
    a.extra = header.scenarioWord;   // word_122F876 = dword_122F490
    // LOBYTE(word_63C740) = word_63C740 | 0x50 — HOST | LoadNetSave.
    if (outSessionFlags)
        *outSessionFlags |= 0x50;
    return a;
}

// ===========================================================================
// Lobby spine — ready -> ship -> barrier handshake
// ===========================================================================
LobbyRunResult RunNetworkLobby(const LobbyAdvert& advert, sim::CommandQueue& q,
                               ILobbyHook& hook, const std::vector<u8>& saveBytes,
                               int maxPumps) {
    LobbyRunResult r;
    u8 readyMask = 0;   // byte_63CC28 = 0

    // do { PumpMessages; RefreshGuildState; } while (!byte_63CC28);
    // Spin until the server assigns this peer a role (bit0 = host, or non-zero =
    // a client slot). Bounded so a never-assigned peer can't hang.
    int guard = 0;
    do {
        if (!hook.pumpAndRefresh(readyMask))
            break;
        ++r.pumps;
        if (++guard >= maxPumps)
            break;
    } while (readyMask == 0);

    // if (byte_63CC28 & 1) { host ships the save, then enqueues the ready count. }
    if ((readyMask & kReadyHost) != 0) {
        // ShowProgressForm() is a GUI leaf (deferred). Net_SendSaveGameToClients
        // packetizes the savegame to every client; (!ret) => return 1 (read error).
        const int chunks = SendSaveGameToClients(q, saveBytes);
        if (chunks <= 0) {
            r.rc = 1;   // return 1: the original's send-failed early out
            return r;
        }
        r.savedSent  = true;
        r.chunksSent = chunks;

        // v45[0] = byte_63CC1D; Command_QueueRequestFlagBlob32(6, v45) — the ready
        // count (flag 6) carrying the live player count in the blob (0x503f78 @
        // 0x50422e..0x50423a). QueueRequestFlagBlob32 @0x494ab4 stages a type-0x20
        // packet: v3[0]=0x20 (type), v3[16]=flag(6), then the 124-byte blob copied
        // at v3[17] — so the count dword lands at byte offset 17, NOT at +0 or +16.
        sim::CommandPacket ready{};
        ready.bytes[sim::kFOpcode] = sim::kSyncType;       // v3[0]  = 0x20 (type)
        ready.bytes[sim::kFSync]   = 6;                    // v3[16] = flag 6 (ready)
        ready.put32(sim::kFSync + 1, advert.playerCount);  // v4[0] = v45[0] = byte_63CC1D
        r.readyCmdId = q.EnqueuePacket(ready);
        r.readyCount = advert.playerCount;
    }

    // while ((byte_63CC28 & 4) == 0) { PumpMessages; RefreshGuildState; }
    // The all-ready barrier: spin until every peer has acknowledged (bit2). On a
    // host the local apply of the cmd6 lets the hook flip bit2; on a client the
    // host's broadcast does. Bounded.
    guard = 0;
    while ((readyMask & kReadyAllPeers) == 0) {
        if (!hook.pumpAndRefresh(readyMask))
            break;
        ++r.pumps;
        if (++guard >= maxPumps)
            break;
    }
    r.ready = (readyMask & kReadyAllPeers) != 0;

    // Past the barrier the original loads the received stream (client) or
    // Save_LoadGameFile (host saved game) and runs Scene_Sync* / RunSyncWaitLoop —
    // DEFERRED render/world leaves. rc stays 0 (success) here.
    return r;
}

// ===========================================================================
// Host-advertise -> client-discover round-trip (over the REAL discovery siblings)
// ===========================================================================
int AdvertiseAndDiscover(INetDatagram* host, INetDatagram* client,
                         const LobbyAdvert& advert, u16 port, u32 timeoutMs,
                         std::vector<DiscoveredServer>& found,
                         u32 (*nowMs)(void* user), void* user, u32 tickMs) {
    // HOST: open the broadcast socket and blast the encoded advert (the same byte
    // image the discovery side parses). REUSE the real BroadcastAdvertiser.
    BroadcastAdvertiser adv(host);
    if (!adv.OpenBroadcastSocket(port))
        return -1;
    const std::vector<u8> blob = EncodeLobbyAdvert(advert);
    adv.SendBroadcast(blob.data(), blob.size());

    // CLIENT: run the real DiscoverServers; it binds the client datagram socket,
    // drains the bus, validates the advert header, and collects the host entry.
    const int n = DiscoverServers(client, port, timeoutMs, /*maxServers=*/8, found,
                                  nowMs, user, tickMs);
    adv.CloseBroadcastSocket();
    return n;
}

} // namespace guild::net
