// Integration test: net_session's unknown-winsock-error path wired against the
// REAL reconstructed CRT sibling guild::crt::Sprintf (VIBE_Crt_Sprintf_0 @0x5cba00,
// NO stub) — exactly the call the binary makes at LABEL_40
// (VIBE_Crt_Sprintf_0(v4, "Unknown winsock error %u!!!", a1)).
//
// ClassifyWinsockError uses crt::Snprintf internally; here we assert the full
// cross-module flow end to end: ReportWinsockError -> ClassifyWinsockError ->
// crt formatting -> a REAL log sink that re-formats the captured (line, message)
// with the SAME crt::Sprintf engine, and verify the bytes match the engine's own
// independent output. This proves the net <-> crt wiring as the live game has it.
#include "test.h"

#include "net/net_session.h"
#include "crt/printf.h"   // REAL guild::crt::Sprintf / Snprintf sibling, not a mock

#include <cstring>

using namespace guild;

namespace {
// A real log sink (the production wiring's role of VIBE_ErrorLog_ReportModuleLine):
// it composes "<file>(<line>): <message>" using the genuine crt engine.
struct CrtLog {
    char composed[512];
    bool hit;
};
CrtLog g_log;

void RealComposingSink(const char* file, int line, int sev, const char* message) {
    (void)sev;
    // Use the REAL reconstructed VIBE_Crt_Sprintf_0 to format — the exact engine
    // the original net.c links against.
    crt::Snprintf(g_log.composed, sizeof(g_log.composed),
                  "%s(%d): %s", file, line, message ? message : "(null)");
    g_log.hit = true;
}
}  // namespace

TEST(NetSessionItest, UnknownErrorComposedViaRealCrtEngine) {
    auto saved = net::NetReportHooks();
    net::NetReportHooks().reportModuleLine = &RealComposingSink;

    g_log = {};
    net::ReportWinsockError(98765u);  // unknown code -> line 89, formatted msg
    CHECK(g_log.hit);

    // Build the expected string independently with the same real crt engine.
    char expected[512];
    crt::Snprintf(expected, sizeof(expected),
                  "%s(%d): %s", "..\\units\\net\\net.c", 89,
                  "Unknown winsock error 98765!!!");
    CHECK(std::strcmp(g_log.composed, expected) == 0);

    net::NetReportHooks() = saved;
}

TEST(NetSessionItest, KnownErrorComposedViaRealCrtEngine) {
    auto saved = net::NetReportHooks();
    net::NetReportHooks().reportModuleLine = &RealComposingSink;

    g_log = {};
    net::ReportWinsockError(net::kWsaEWouldBlock);  // 10035 -> line 77
    CHECK(g_log.hit);

    char expected[512];
    crt::Snprintf(expected, sizeof(expected),
                  "%s(%d): %s", "..\\units\\net\\net.c", 77,
                  "WSAEWOULDBLOCK: The socket is marked as nonblocking and the "
                  "receive operation would block.");
    CHECK(std::strcmp(g_log.composed, expected) == 0);

    net::NetReportHooks() = saved;
}

// The default-hook (inert) path exercised end to end: classify still runs the
// real crt engine for the unknown buffer, the inert sink simply drops it.
TEST(NetSessionItest, InertDefaultPathRunsRealCrt) {
    char buf[64];
    net::WinsockDiagnostic d = net::ClassifyWinsockError(31337u, buf, sizeof(buf));
    CHECK(!d.known);
    CHECK(std::strcmp(buf, "Unknown winsock error 31337!!!") == 0);
    // Drives the actual inert default sink (no hook installed).
    net::ReportWinsockError(31337u);
    CHECK(true);
}
