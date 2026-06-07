#include "test.h"
#include "net/lobby.h"
#include "net/discovery.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::net;

// Unit coverage for the lobby advert record builders + encode/decode. The advert
// byte image MUST equal what the discovery layer accepts (magic/kind/tag at +0/+2/
// +4), and the two setup functions must stamp the correct kind + session-flag bit.

TEST(NetLobby, HostAdvertFields) {
    u16 flags = 0x0001;  // a pre-existing kNewGame bit
    LobbyAdvert a = BuildHostAdvert("AUGSBURG", /*players=*/4, /*scenario=*/7, &flags);
    CHECK_EQ(a.magic, static_cast<u16>(kAdMagic));      // 1
    CHECK_EQ(a.kind, static_cast<u16>(kAdKindOne));     // 1 (host new game)
    CHECK_EQ(a.tag, static_cast<u16>(kAdTag));          // 106
    CHECK_EQ(a.name, std::string("AUGSBURG"));
    CHECK_EQ(a.playerCount, static_cast<u8>(4));
    CHECK_EQ(a.extra, static_cast<u16>(7));
    CHECK(a.host());
    // word_63C740 |= 0x10 (host bit), pre-existing bit preserved.
    CHECK_EQ(flags, static_cast<u16>(0x0011));
}

TEST(NetLobby, SaveJoinAdvertFields) {
    u16 flags = 0;
    SaveProfileHeader h;
    h.name = "WICHTEL";
    h.playerCount = 3;
    h.scenarioWord = 42;
    LobbyAdvert a = BuildSaveJoinAdvert(h, &flags);
    CHECK_EQ(a.kind, static_cast<u16>(kAdKindSav));     // 4 (saved game)
    CHECK_EQ(a.name, std::string("WICHTEL"));
    CHECK_EQ(a.playerCount, static_cast<u8>(3));
    CHECK(!a.host());                                   // kind 4 != host-new-game
    // word_63C740 |= 0x50 (HOST | LoadNetSave).
    CHECK_EQ(flags, static_cast<u16>(0x50));
}

TEST(NetLobby, EncodeHostAdvertImage) {
    LobbyAdvert a = BuildHostAdvert("KOELN", 2, 0x1234, nullptr);
    std::vector<u8> e = EncodeLobbyAdvert(a);
    // kind 1 advert is exactly 42 bytes.
    CHECK_EQ(e.size(), static_cast<std::size_t>(kHostAdvertBytes));  // 42
    // header at +0/+2/+4 — the discovery accept predicate fields.
    CHECK_EQ(e[0], static_cast<u8>(1));    // magic lo
    CHECK_EQ(e[1], static_cast<u8>(0));
    CHECK_EQ(e[2], static_cast<u8>(1));    // kind lo
    CHECK_EQ(e[4], static_cast<u8>(106));  // tag lo
    // name string at +6.
    CHECK_EQ(std::string(reinterpret_cast<const char*>(e.data() + 6)),
             std::string("KOELN"));
    // playerCount at +0x26, scenario word at +0x28.
    CHECK_EQ(e[0x26], static_cast<u8>(2));
    CHECK_EQ(e[0x28], static_cast<u8>(0x34));
    CHECK_EQ(e[0x29], static_cast<u8>(0x12));
}

TEST(NetLobby, EncodeSaveAdvertSize) {
    SaveProfileHeader h; h.name = "X"; h.playerCount = 1; h.scenarioWord = 0;
    LobbyAdvert a = BuildSaveJoinAdvert(h, nullptr);
    std::vector<u8> e = EncodeLobbyAdvert(a);
    CHECK_EQ(e.size(), static_cast<std::size_t>(kSaveAdvertBytes));  // 106
    CHECK_EQ(e[2], static_cast<u8>(4));    // kind 4
}

TEST(NetLobby, EncodeDecodeRoundTrip) {
    LobbyAdvert a = BuildHostAdvert("REGENSBURG", 5, 99, nullptr);
    std::vector<u8> e = EncodeLobbyAdvert(a);
    LobbyAdvert b;
    CHECK(DecodeLobbyAdvert(e, b));
    CHECK_EQ(b.magic, a.magic);
    CHECK_EQ(b.kind, a.kind);
    CHECK_EQ(b.tag, a.tag);
    CHECK_EQ(b.name, a.name);
    CHECK_EQ(b.playerCount, a.playerCount);
    CHECK_EQ(b.extra, a.extra);
}

TEST(NetLobby, DecodeRejectsBadHeader) {
    LobbyAdvert b;
    // too short
    std::vector<u8> tiny(4, 0);
    CHECK(!DecodeLobbyAdvert(tiny, b));
    // wrong magic
    std::vector<u8> badMagic(42, 0);
    badMagic[0] = 9; badMagic[2] = 1; badMagic[4] = 106;
    CHECK(!DecodeLobbyAdvert(badMagic, b));
    // wrong tag
    std::vector<u8> badTag(42, 0);
    badTag[0] = 1; badTag[2] = 1; badTag[4] = 7;
    CHECK(!DecodeLobbyAdvert(badTag, b));
    // wrong kind
    std::vector<u8> badKind(42, 0);
    badKind[0] = 1; badKind[2] = 3; badKind[4] = 106;
    CHECK(!DecodeLobbyAdvert(badKind, b));
}

TEST(NetLobby, NameClampedIntoRecord) {
    // A name longer than the record's name region must be clamped and NUL-bounded
    // so it never overruns into the playerCount field at +0x26.
    std::string longName(64, 'Z');
    LobbyAdvert a = BuildHostAdvert(longName, 7, 0, nullptr);
    std::vector<u8> e = EncodeLobbyAdvert(a);
    CHECK_EQ(e.size(), static_cast<std::size_t>(kHostAdvertBytes));
    CHECK_EQ(e[0x26], static_cast<u8>(7));   // playerCount intact (name didn't bleed)
    // the byte right before the count field is the NUL terminator.
    CHECK_EQ(e[0x25], static_cast<u8>(0));
}
