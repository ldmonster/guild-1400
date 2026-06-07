// End-to-end: the transport error-reporting flow as the live net.c drives it.
//
// In the binary, VIBE_Net_SendPacket / VIBE_Net_ReceivePacket inspect the socket
// result and, on any error other than WSAEWOULDBLOCK, call
//     ReportWinsockError(WSAGetLastError())   then tear the connection down.
// This e2e reconstructs that flow across net_session's functions: it walks a
// sequence of WinSock results a non-blocking TCP pump would observe over a
// session's lifetime, routes each through ReportWinsockError into a capturing
// "error console", and asserts the diagnostics the player would have seen — then
// the would-block result that must NOT be reported (the retry path).
#include "test.h"

#include "net/net_session.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {
struct Console {
    std::vector<std::string> lines;
    std::vector<int>         lineNos;
};
Console g_console;

void ConsoleSink(const char* file, int line, int /*sev*/, const char* msg) {
    (void)file;
    g_console.lineNos.push_back(line);
    g_console.lines.emplace_back(msg ? msg : "");
}
}  // namespace

TEST(NetSessionE2E, SessionErrorTimelineProducesExpectedDiagnostics) {
    auto saved = net::NetReportHooks();
    net::NetReportHooks().reportModuleLine = &ConsoleSink;
    g_console = {};

    // A plausible timeline of WinSock results across one networked session:
    //   connect attempt -> peer not yet up (WSAENOTCONN)
    //   transient send failure (WSAENETRESET)
    //   peer drops the circuit (WSAECONNRESET)  -> SendPacket teardown
    //   later recv from a dead peer (WSAETIMEDOUT)
    //   an unrecognised provider code (12321)    -> "Unknown winsock error"
    const unsigned int timeline[] = {
        net::kWsaENotConn,   // 10057 -> line 63
        net::kWsaENetReset,  // 10052 -> line 69
        net::kWsaEConnReset, // 10054 -> line 87
        net::kWsaETimedOut,  // 10060 -> line 85
        12321u,              // unknown -> line 89
    };
    for (unsigned int code : timeline)
        net::ReportWinsockError(code);

    // Every error in the timeline produced exactly one console line, in order.
    CHECK_EQ(static_cast<int>(g_console.lines.size()), 5);
    if (g_console.lineNos.size() == 5) {
        CHECK_EQ(g_console.lineNos[0], 63);
        CHECK_EQ(g_console.lineNos[1], 69);
        CHECK_EQ(g_console.lineNos[2], 87);
        CHECK_EQ(g_console.lineNos[3], 85);
        CHECK_EQ(g_console.lineNos[4], 89);
    }
    if (g_console.lines.size() == 5) {
        CHECK(g_console.lines[0].rfind("WSAENOTCONN", 0) == 0);
        CHECK(g_console.lines[2].rfind("WSAECONNRESET", 0) == 0);
        CHECK(g_console.lines[4] == "Unknown winsock error 12321!!!");
    }

    net::NetReportHooks() = saved;
}

// The WSAEWOULDBLOCK retry contract: in the transport, a would-block result is
// NOT an error — it is the "call again later" path and is never reported. This
// e2e asserts that the diagnostic for it, if ever surfaced, is the benign
// WSAEWOULDBLOCK message on line 77 (and a real transport simply never calls it).
TEST(NetSessionE2E, WouldBlockIsTheRetryDiagnostic) {
    char buf[64];
    net::WinsockDiagnostic d = net::ClassifyWinsockError(net::kWsaEWouldBlock, buf, sizeof(buf));
    CHECK(d.known);
    CHECK_EQ(d.line, 77);
    CHECK(std::strncmp(d.message, "WSAEWOULDBLOCK", 14) == 0);
}
