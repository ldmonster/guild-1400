#include "net/net_session.h"

#include "crt/printf.h"   // guild::crt::Sprintf — the REAL reconstructed VIBE_Crt_Sprintf_0 @0x5cba00

// gilde.exe 0x43af00 — VIBE_Net_ReportWinsockError
// -------------------------------------------------
// The original is a balanced binary search over the WinSock error space. Each
// terminal arm calls
//     VIBE_ErrorLog_ReportModuleLine("..\units\net\net.c", <line>, 1, <message>)
// and returns; the default arm sprintf()s "Unknown winsock error %u!!!" into a
// 256-byte stack buffer and reports it on line 89. The control flow here is a
// flat switch, which compiles to the same terminal arms; the (code -> line,
// message) table is transcribed verbatim from the decompiled branch targets.
//
// Two faithfully-preserved quirks:
//   * WSANOTINITIALISED (10093) is tested at the very BOTTOM of the >0x2746
//     subtree even though 10093 is far above the WinSock error range — the
//     original's `if (a1 == 10093)` sits inside the 0x274C arm. A switch
//     reproduces the identical dispatch result.
//   * The unknown arm in the IDB passes an *uninitialised* `v3` as the severity;
//     every other arm passes the literal 1. We pass 1 (the only meaningful value
//     and the documented "this is an error" severity) — observable message/line
//     are unaffected, and the diagnostic text is byte-identical.

namespace guild::net {

const char kNetSourceFile[] = "..\\units\\net\\net.c";

// --- WinSock diagnostic strings (recovered verbatim from .rdata, see header) ---
namespace {
constexpr const char* kMsgIntr =
    "WSAEINTR: The (blocking) call was canceled through WSACancelBlockingCall.";
constexpr const char* kMsgNetDown =
    "WSAENETDOWN: The network subsystem has failed!";
constexpr const char* kMsgFault =
    "WSAEFAULT: The buf parameter is not completely contained in a valid part of "
    "the user address space";
constexpr const char* kMsgInProgress =
    "WSAEINPROGRESS: A blocking Windows Sockets 1.1 call is in progress, or the "
    "service provider is still processing a callback function.";
constexpr const char* kMsgNetReset =
    "WSAENETRESET: The connection has been broken due to the keep-alive activity "
    "detecting a failure while the operation was in progress.";
constexpr const char* kMsgNotSock =
    "WSAENOTSOCK: The descriptor is not a socket.";
constexpr const char* kMsgOpNotSupp =
    "WSAEOPNOTSUPP: MSG_OOB was specified, but the socket is not stream-style such "
    "as type SOCK_STREAM, OOB data is not supported in the communication domain "
    "associated with this socket, or the socket is unidirectional and supports "
    "only send operations.";
constexpr const char* kMsgShutdown =
    "WSAESHUTDOWN: The socket has been shut down; it is not possible to receive on "
    "a socket after shutdown has been invoked with how set to SD_RECEIVE or SD_BOTH.";
constexpr const char* kMsgWouldBlock =
    "WSAEWOULDBLOCK: The socket is marked as nonblocking and the receive operation "
    "would block.";
constexpr const char* kMsgMsgSize =
    "WSAEMSGSIZE: The message was too large to fit into the specified buffer and "
    "was truncated.";
constexpr const char* kMsgInval =
    "WSAEINVAL: The socket has not been bound with bind, or an unknown flag was "
    "specified, or MSG_OOB was specified for a socket with SO_OOBINLINE enabled or "
    "(for byte stream sockets only) len was zero or negative.";
constexpr const char* kMsgConnAborted =
    "WSAECONNABORTED: The virtual circuit was terminated due to a time-out or other "
    "failure. The application should close the socket as it is no longer usable.";
constexpr const char* kMsgConnReset =
    "WSAECONNRESET: The virtual circuit was reset by the remote side executing a "
    "hard or abortive close. The application should close the socket as it is no "
    "longer usable. On a UPD-datagram socket this error would indicate that a "
    "previous send operation resulted in an ICMP >Port Unreachable< message.";
constexpr const char* kMsgTimedOut =
    "WSAETIMEDOUT: The connection has been dropped because of a network failure or "
    "because the peer system failed to respond.";
constexpr const char* kMsgNotInit = "WSANOTINITIALISED";
constexpr const char* kMsgNotConn = "WSAENOTCONN: The socket is not connected.";
}  // namespace

WinsockDiagnostic ClassifyWinsockError(unsigned int code, char* unknownBuf, std::size_t cap) {
    switch (code) {
        case kWsaEIntr:          return {65, kMsgIntr,        true};
        case kWsaEFault:         return {61, kMsgFault,       true};
        case kWsaEInval:         return {81, kMsgInval,       true};
        case kWsaEWouldBlock:    return {77, kMsgWouldBlock,  true};
        case kWsaEInProgress:    return {67, kMsgInProgress,  true};
        case kWsaENotSock:       return {71, kMsgNotSock,     true};
        case kWsaEMsgSize:       return {79, kMsgMsgSize,     true};
        case kWsaEOpNotSupp:     return {73, kMsgOpNotSupp,   true};
        case kWsaENetDown:       return {59, kMsgNetDown,     true};
        case kWsaENetReset:      return {69, kMsgNetReset,    true};
        case kWsaEConnAborted:   return {83, kMsgConnAborted, true};
        case kWsaEConnReset:     return {87, kMsgConnReset,   true};
        case kWsaENotConn:       return {63, kMsgNotConn,     true};
        case kWsaEShutdown:      return {75, kMsgShutdown,    true};
        case kWsaETimedOut:      return {85, kMsgTimedOut,    true};
        case kWsaNotInitialised: return {57, kMsgNotInit,     true};
        default:
            // LABEL_40 in the original: sprintf into the local buffer, line 89.
            if (unknownBuf && cap > 0) {
                // VIBE_Crt_Sprintf_0((int)v4, "Unknown winsock error %u!!!", a1)
                crt::Snprintf(unknownBuf, cap, "Unknown winsock error %u!!!", code);
            }
            return {89, unknownBuf, false};
    }
}

// --- installable error-log sink with inert default ---
namespace {
void InertReportModuleLine(const char*, int, int, const char*) {}
ReportHooks g_reportHooks = { &InertReportModuleLine };
}  // namespace

ReportHooks& NetReportHooks() { return g_reportHooks; }

void ReportWinsockError(unsigned int code) {
    char unknownBuf[256];  // char v4[256] — the original's stack buffer
    WinsockDiagnostic d = ClassifyWinsockError(code, unknownBuf, sizeof(unknownBuf));
    // VIBE_ErrorLog_ReportModuleLine(aUnitsNetNetC, <line>, 1, <message>)
    if (g_reportHooks.reportModuleLine)
        g_reportHooks.reportModuleLine(kNetSourceFile, d.line, 1, d.message);
}

}  // namespace guild::net
