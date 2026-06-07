#pragma once
#include "guild/common/types.h"
#include "net/discovery.h"
#include "net/net_savegame.h"
#include "sim/command.h"

#include <cstddef>
#include <string>
#include <vector>

// gilde.exe — guild::net  (MODULE: multiplayer LOBBY setup state machine)
//
// The lobby is the bridge between the discovery layer (net/discovery) and the
// in-world session: the host advertises a game on the LAN, clients browse for it,
// and the host->client ready/start handshake drives every peer from "in the
// lobby" to "loading the world". This module recovers, 1:1:
//
//   VIBE_Menu_RunHostNetworkSetup  @0x528dac — host: build the broadcast ADVERT
//        record (magic/kind/tag/name/playerCount) for a NEW single game (kind 1).
//   VIBE_Net_LoadNetworkSaveProfile@0x528f24 — join: read a saved network game's
//        header and build the ADVERT record for it (kind 4, saved game).
//   VIBE_Net_StartNetworkGame      @0x503f78 — host/join lobby SPINE for a new
//        game: wait for the ready bitmask, host ships the savegame, then the
//        ready-count (cmd6) + all-ready barrier + scene sync.
//   VIBE_Net_LoadSavedNetworkGame  @0x50442c — host/join lobby SPINE for a saved
//        game: same shape, shipping the .SRV/.SAV and loading it on every peer.
//
// The ADVERT record built here is the SAME byte image the discovery layer parses
// (net/discovery: kAdMagic/kAdKindOne/kAdKindSav/kAdTag, the 42-byte kind-1 /
// 106-byte kind-4 blob). So this module's BuildHostAdvert() -> SendBroadcast() ->
// DiscoverServers() round-trips through the REAL discovery siblings, and its
// host->client save transfer round-trips through the REAL net/net_savegame +
// sim/command siblings. OS sockets go through shim::INetSocket / INetDatagram.
//
// REUSED (extern, not redefined — ODR): guild::net::BroadcastAdvertiser /
// DiscoverServers / DiscoveredServer (net/discovery); SendSaveGameToClients /
// SaveStreamReassembler / TransferSaveLocally (net/net_savegame); sim::CommandQueue
// / CommandPacket (sim/command); guild::app::session flag bits (app/gamelogic.h).

namespace guild::net {

// ===========================================================================
// The lobby ADVERT record (unk_122F84E.. block built by the two setup funcs)
// ---------------------------------------------------------------------------
// Recovered field offsets (relative to the record base @0x122F84E). The host /
// join setup functions store, in order:
//   +0x00  u16  magic       word_122F84E = 1            (== net::kAdMagic)
//   +0x02  u16  kind        word_122F850 = 1 | 4        (== kAdKindOne/kAdKindSav)
//   +0x04  u16  tag         word_122F852 = 106          (== net::kAdTag)
//   +0x06  char name[..]    unk_122F854  = session/city name (NUL-terminated)
//   +0x26  u8   playerCount byte_122F874 = byte_63CC1D  (live player count)
//   +0x28  u16  extra       word_122F876 = dword_122F490 (map/scenario id word)
// The whole record is broadcast verbatim; the discovery side reads buf[0]==magic,
// buf[2]==kind, buf[4]==tag and copies 42 (kind 1) / 106 (kind 4) bytes of blob.
struct LobbyAdvert {
    u16         magic = kAdMagic;      // +0x00  word_122F84E (1)
    u16         kind = kAdKindOne;     // +0x02  word_122F850 (1 host / 4 sav-join)
    u16         tag = kAdTag;          // +0x04  word_122F852 (106)
    std::string name;                  // +0x06  unk_122F854  (city/session name)
    u8          playerCount = 0;       // +0x26  byte_122F874 (byte_63CC1D)
    u16         extra = 0;             // +0x28  word_122F876 (dword_122F490)

    bool host() const { return kind == kAdKindOne; }   // kind 1 == host new game
};

// Record field byte offsets (record base == 0x122F84E in the original).
enum LobbyAdvertOffset : std::size_t {
    kAdvMagicOff = 0x00,   // word_122F84E
    kAdvKindOff  = 0x02,   // word_122F850
    kAdvTagOff   = 0x04,   // word_122F852
    kAdvNameOff  = 0x06,   // unk_122F854  (name string)
    kAdvCountOff = 0x26,   // byte_122F874 (38)
    kAdvExtraOff = 0x28,   // word_122F876 (40)
};
// Bytes broadcast for each advert kind (matches kAdKindOneBytes/kAdKindSavBytes).
constexpr std::size_t kHostAdvertBytes = kAdKindOneBytes; // 42  (kind 1)
constexpr std::size_t kSaveAdvertBytes = kAdKindSavBytes; // 106 (kind 4)

// Serialize a LobbyAdvert into the exact little-endian advert datagram the host
// broadcasts (and DiscoverServers parses). The buffer is kHost/kSaveAdvertBytes
// long (kind decides), magic/kind/tag at +0/+2/+4, the name at +6 (NUL-clamped
// into the blob), playerCount at +0x26, extra at +0x28; the rest zero. This is
// the byte image qmemcpy'd into the discovery entry blob at +16.
std::vector<u8> EncodeLobbyAdvert(const LobbyAdvert& a);

// Decode an advert datagram (the discovery blob) back into a LobbyAdvert. Returns
// false if the header doesn't validate (magic/kind/tag) — the same accept
// predicate DiscoverServers applies before collecting the entry.
bool DecodeLobbyAdvert(const std::vector<u8>& buf, LobbyAdvert& out);

// ===========================================================================
// Lobby setup (build the advert from the host / saved-game profile)
// ---------------------------------------------------------------------------

// gilde.exe 0x528dac — VIBE_Menu_RunHostNetworkSetup (the advert-build slice).
// After the player-count + city pickers (GUI leaves), the original stamps the
// advert record: magic 1, kind 1 (host new game), tag 106, the chosen city name,
// the live player count (byte_63CC1D), and the scenario word (dword_122F490). It
// also sets word_63C740 |= 0x10 (HOST). Returns the built advert; `outSessionFlags`
// receives the |=0x10 host bit OR'd into the caller's session flags.
LobbyAdvert BuildHostAdvert(const std::string& cityName, u8 playerCount,
                            u16 scenarioWord, u16* outSessionFlags);

// gilde.exe 0x528f24 — VIBE_Net_LoadNetworkSaveProfile (the advert-build slice).
// Reads the saved network game's header (the 231-byte v22 scratch in the original)
// and stamps the advert for it: magic 1, kind 4 (saved game), tag 106, the session
// name, the saved player count (byte_63CC1D <- header[44]), and the scenario word.
// It also sets word_63C740 |= 0x50 (HOST | LoadNetSave). `header` is the decoded
// save header (playerCount at index 44, scenario word from the header); the parse
// itself is the REUSED io/save path. Returns the built advert.
struct SaveProfileHeader {
    std::string name;       // a127001-style session/name string (byte_122EE90 copy)
    u8          playerCount = 0; // v22[44] -> byte_63CC1D
    u16         scenarioWord = 0; // dword_122F490
};
LobbyAdvert BuildSaveJoinAdvert(const SaveProfileHeader& header, u16* outSessionFlags);

// ===========================================================================
// Lobby spine — the host/join ready->start->sync handshake
// ---------------------------------------------------------------------------
// Pump hook: in the binary the lobby loops call VIBE_Window_PumpMessages +
// VIBE_Amt_RefreshGuildState while spinning on the ready bitmask byte_63CC28. We
// expose that pump as a hook so a test can advance the (host|all-ready) bits and
// the queue without the renderer/world.
struct ILobbyHook {
    virtual ~ILobbyHook() = default;
    // VIBE_Window_PumpMessages @0x4bea64 + VIBE_Amt_RefreshGuildState @0x4becdc:
    // one lobby pump. `readyMask` is the live byte_63CC28 the hook may set bits in
    // (bit0 = host designated, bit2 = all peers ready). Return false to abort.
    virtual bool pumpAndRefresh(u8& readyMask) = 0;
};

// byte_63CC28 ready-bitmask bits (the lobby's readiness handshake state).
enum LobbyReadyBit : u8 {
    kReadyHost     = 0x01,  // &1  this peer is the designated host (sends the save)
    kReadyAllPeers = 0x04,  // &4  every peer has acknowledged -> proceed
};

// Result of a lobby run (the observable outcome of the spine).
struct LobbyRunResult {
    bool   ready = false;         // reached the all-ready barrier (byte_63CC28 & 4)
    bool   savedSent = false;     // host shipped the savegame (kReadyHost path)
    int    chunksSent = 0;        // opcode-9 chunks emitted by SendSaveGameToClients
    int    readyCount = 0;        // byte_63CC1D live players counted into the cmd6
    i32    readyCmdId = -1;       // ring id of the QueueRequestFlagBlob32(6,...) cmd
    int    pumps = 0;             // pump iterations spent in the spine
    int    rc = 0;                // 0 success; 1 send failed; 2 stream load failed
};

// gilde.exe 0x503f78 — VIBE_Net_StartNetworkGame (the lobby SPINE slice).
// gilde.exe 0x50442c — VIBE_Net_LoadSavedNetworkGame (the same SPINE shape).
// The recovered control flow (both functions share it):
//   byte_63CC28 = 0;
//   do { PumpMessages; RefreshGuildState; } while (!byte_63CC28);   // wait ready
//   if (byte_63CC28 & 1) {                                          // host
//       ShowProgressForm();
//       if (!Net_SendSaveGameToClients(savePath)) return 1;         // ship save
//       Command_QueueRequestFlagBlob32(6, &byte_63CC1D);            // ready count
//   }
//   while ((byte_63CC28 & 4) == 0) { PumpMessages; RefreshGuildState; } // barrier
//   ... load received stream / Save_LoadGameFile + Scene_Sync* (DEFERRED leaves)
//
// We REUSE: net::SendSaveGameToClients (host ship), sim::CommandQueue
// (EnqueuePacket of the cmd6 ready packet), and route the per-frame pump through
// ILobbyHook. The scene-sync render leaves are DEFERRED. `q` carries the lockstep
// state; `saveBytes` is the savegame the host ships (empty on a client). Returns
// the observable outcome. `maxPumps` bounds each spin so a never-ready peer can't
// hang a test.
LobbyRunResult RunNetworkLobby(const LobbyAdvert& advert, sim::CommandQueue& q,
                               ILobbyHook& hook,
                               const std::vector<u8>& saveBytes,
                               int maxPumps);

// One-shot host-advertise -> client-discover round-trip over the REAL discovery
// siblings: open the host's BroadcastAdvertiser, blast `advert`, then run
// DiscoverServers on the client and decode the host back out. Returns the number
// of servers the client found (the advert it built equals the one discovered).
// `host`/`client` are the two endpoints of an in-process datagram bus; `nowMs`/
// `tickMs` drive the discovery timeout exactly as the discovery recon documents.
int AdvertiseAndDiscover(INetDatagram* host, INetDatagram* client,
                         const LobbyAdvert& advert, u16 port, u32 timeoutMs,
                         std::vector<DiscoveredServer>& found,
                         u32 (*nowMs)(void* user), void* user, u32 tickMs);

} // namespace guild::net
