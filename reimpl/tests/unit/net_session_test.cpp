// Unit tests for net_session — VIBE_Net_ReportWinsockError @0x43af00.
//
// Golden vectors: the (code -> source line, message-prefix) table transcribed
// from the decompiled branch arms. We assert the source line number exactly and
// the message prefix (the leading "WSAE..." token) so the full verbatim strings
// are not duplicated here while still proving the right branch fired.
#include "test.h"

#include "net/net_session.h"

#include <cstring>

using namespace guild;

namespace {

bool StartsWith(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

}  // namespace

TEST(NetSessionWinsock, KnownCodesMapToExactLines) {
    char buf[64];
    struct Row { unsigned int code; int line; const char* prefix; };
    const Row rows[] = {
        {10004, 65, "WSAEINTR"},
        {10014, 61, "WSAEFAULT"},
        {10022, 81, "WSAEINVAL"},
        {10035, 77, "WSAEWOULDBLOCK"},
        {10036, 67, "WSAEINPROGRESS"},
        {10038, 71, "WSAENOTSOCK"},
        {10040, 79, "WSAEMSGSIZE"},
        {10045, 73, "WSAEOPNOTSUPP"},
        {10050, 59, "WSAENETDOWN"},
        {10052, 69, "WSAENETRESET"},
        {10053, 83, "WSAECONNABORTED"},
        {10054, 87, "WSAECONNRESET"},
        {10057, 63, "WSAENOTCONN"},
        {10058, 75, "WSAESHUTDOWN"},
        {10060, 85, "WSAETIMEDOUT"},
        {10093, 57, "WSANOTINITIALISED"},
    };
    for (const Row& r : rows) {
        net::WinsockDiagnostic d = net::ClassifyWinsockError(r.code, buf, sizeof(buf));
        CHECK(d.known);
        CHECK_EQ(d.line, r.line);
        CHECK(StartsWith(d.message, r.prefix));
    }
}

TEST(NetSessionWinsock, NotInitialisedIsExactString) {
    char buf[64];
    net::WinsockDiagnostic d = net::ClassifyWinsockError(10093, buf, sizeof(buf));
    CHECK(d.known);
    CHECK_EQ(d.line, 57);
    if (d.message)
        CHECK(std::strcmp(d.message, "WSANOTINITIALISED") == 0);
}

TEST(NetSessionWinsock, UnknownCodeFormatsAndUsesLine89) {
    char buf[64];
    net::WinsockDiagnostic d = net::ClassifyWinsockError(12345, buf, sizeof(buf));
    CHECK(!d.known);
    CHECK_EQ(d.line, 89);
    // Message must have been formatted into the supplied buffer.
    CHECK(d.message == buf);
    CHECK(std::strcmp(buf, "Unknown winsock error 12345!!!") == 0);
}

TEST(NetSessionWinsock, UnknownBoundaryCodesStillUnknown) {
    char buf[64];
    // Codes adjacent to the dispatch range that the original does NOT match.
    const unsigned int codes[] = {0u, 10000u, 10005u, 10037u, 10100u, 0xFFFFFFFFu};
    for (unsigned int c : codes) {
        net::WinsockDiagnostic d = net::ClassifyWinsockError(c, buf, sizeof(buf));
        CHECK(!d.known);
        CHECK_EQ(d.line, 89);
    }
}

TEST(NetSessionWinsock, ClassifyToleratesNullBuffer) {
    // The unknown arm must not crash when no scratch buffer is given.
    net::WinsockDiagnostic d = net::ClassifyWinsockError(99999u, nullptr, 0);
    CHECK(!d.known);
    CHECK_EQ(d.line, 89);
    CHECK(d.message == nullptr);
}

TEST(NetSessionWinsock, SourceFileNameMatchesBinary) {
    CHECK(std::strcmp(net::kNetSourceFile, "..\\units\\net\\net.c") == 0);
}

// ReportWinsockError routes the classified diagnostic through the installable
// hook. With a capturing hook installed we observe the exact (file,line,sev,msg).
namespace {
// The diagnostic `msg` may point into ReportWinsockError's *stack* buffer (the
// unknown-code path formats into a local char[256]). That pointer is only valid
// for the duration of the synchronous hook call, exactly as in the original (the
// error-log sink consumes the string before the function returns). So the sink
// must COPY the message contents here — stashing the raw pointer and reading it
// after ReportWinsockError returns is a use-after-return (ASAN-caught) bug.
struct Capture { const char* file; int line; int sev; char msg[256]; bool hit; };
Capture g_cap;
void CapturingSink(const char* f, int l, int s, const char* m) {
    g_cap.file = f;
    g_cap.line = l;
    g_cap.sev  = s;
    g_cap.msg[0] = '\0';
    if (m) {
        std::strncpy(g_cap.msg, m, sizeof(g_cap.msg) - 1);
        g_cap.msg[sizeof(g_cap.msg) - 1] = '\0';
    }
    g_cap.hit = true;
}
}  // namespace

TEST(NetSessionWinsock, ReportRoutesThroughHook) {
    auto saved = net::NetReportHooks();
    net::NetReportHooks().reportModuleLine = &CapturingSink;

    g_cap = {};
    net::ReportWinsockError(10054);  // WSAECONNRESET -> line 87
    CHECK(g_cap.hit);
    CHECK_EQ(g_cap.line, 87);
    CHECK_EQ(g_cap.sev, 1);
    CHECK(StartsWith(g_cap.msg, "WSAECONNRESET"));
    if (g_cap.file)
        CHECK(std::strcmp(g_cap.file, "..\\units\\net\\net.c") == 0);

    g_cap = {};
    net::ReportWinsockError(424242);  // unknown -> line 89, formatted message
    CHECK(g_cap.hit);
    CHECK_EQ(g_cap.line, 89);
    CHECK(std::strcmp(g_cap.msg, "Unknown winsock error 424242!!!") == 0);

    net::NetReportHooks() = saved;  // restore inert default
}

TEST(NetSessionWinsock, DefaultHookIsInertAndSafe) {
    // The default (inert) sink must accept a report without effect or crash.
    net::ReportWinsockError(10035);
    CHECK(true);
}
