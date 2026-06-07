#pragma once
#include "guild/common/types.h"

// net_session — the WinSock error-diagnostics leaf of the VIBE_Net_* module.
//
// This stem owns the one remaining UNTRANSLATED deterministic function of the
// VIBE_Net_* / VIBE_Session_* / VIBE_Lockstep_* / VIBE_Sync_* / VIBE_Transport_*
// slice (the other four prefixes are EMPTY in the IDB; 22 of the 23 VIBE_Net_*
// functions were already reconstructed under src/net/{transport,session,discovery,
// lobby,net_savegame}.{h,cpp}). See the final report for the done-set evidence.
//
//   VIBE_Net_ReportWinsockError  @0x43af00  — maps a WSAGetLastError() code to a
//     fixed diagnostic string and a fixed "..\units\net\net.c" source line, then
//     emits it through the engine error log. Pure, branch-table dispatch; the only
//     non-determinism (the actual log sink) is routed through an installable hook
//     with an inert default, and the unknown-error path formats via the REAL
//     reconstructed guild::crt::Sprintf sibling.
//
// The function appears in transport.cpp's translation notes as the
// "any other error -> ReportWinsockError + teardown" branch; this is its body.

namespace guild::net {

// The WinSock error codes the original dispatches on (WSABASEERR + n). Recovered
// 1:1 from the compare constants / `==` literals in VIBE_Net_ReportWinsockError.
enum WinsockError : unsigned int {
    kWsaEIntr             = 10004,  // 0x2714
    kWsaEFault            = 10014,  // 0x271E
    kWsaEInval            = 10022,  // 0x2726
    kWsaEWouldBlock       = 10035,  // 0x2733
    kWsaEInProgress       = 10036,  // 0x2734
    kWsaENotSock          = 10038,  // 0x2736
    kWsaEMsgSize          = 10040,  // 0x2738
    kWsaEOpNotSupp        = 10045,  // 0x273D
    kWsaENetDown          = 10050,  // 0x2742
    kWsaENetReset         = 10052,  // 0x2744
    kWsaEConnAborted      = 10053,  // 0x2745
    kWsaEConnReset        = 10054,  // 0x2746
    kWsaENotConn          = 10057,  // 0x2749
    kWsaEShutdown         = 10058,  // 0x274A
    kWsaETimedOut         = 10060,  // 0x274C
    kWsaNotInitialised    = 10093,  // 0x276D (binary quirk: tested last, far above range)
};

// Result of classifying a WinSock error code (everything the original commits to
// disk through the error log: the source-file line number and the message text).
// `line` is the verbatim "..\units\net\net.c" line the original passes as the 2nd
// argument of VIBE_ErrorLog_ReportModuleLine; `message` is the diagnostic string.
struct WinsockDiagnostic {
    int         line;       // source line argument (57..89)
    const char* message;    // diagnostic message; for the unknown case this points
                            // into `unknownBuf` (formatted "Unknown winsock error %u!!!")
    bool        known;      // false only for the default / unknown-code path
};

// Pure classifier (no I/O, no globals). Mirrors the exact branch table of
// VIBE_Net_ReportWinsockError @0x43af00. For an unrecognised code it formats the
// "Unknown winsock error %u!!!" string into `unknownBuf` (>= 32 bytes) using the
// reconstructed guild::crt::Sprintf and points `message` at it, exactly as the
// original LABEL_40 does into its 256-byte stack buffer.
WinsockDiagnostic ClassifyWinsockError(unsigned int code, char* unknownBuf, std::size_t cap);

// The source-file name string the original passes as arg1 of every
// VIBE_ErrorLog_ReportModuleLine call (aUnitsNetNetC @0x615e48).
extern const char kNetSourceFile[];

// Installable error-log sink. The original calls VIBE_ErrorLog_ReportModuleLine
// @0x438e3c (NOT reconstructed) as
//     ReportModuleLine(file, line, severity=1, message).
// We route it through this hook so callers can capture diagnostics. The default
// implementation is inert (drops the message), so the unified-build library has a
// definition and never performs real I/O.
struct ReportHooks {
    void (*reportModuleLine)(const char* file, int line, int severity, const char* message);
};

// The process-wide hooks (inert defaults defined in net_session.cpp).
ReportHooks& NetReportHooks();

// gilde.exe 0x43af00 — VIBE_Net_ReportWinsockError.
// Classifies `code`, then emits the diagnostic via NetReportHooks().reportModuleLine
// with severity 1, exactly as the original (the severity arg is the literal `1` on
// every known branch; the unknown branch passes an uninitialised `v3` in the IDB —
// we preserve the observable behaviour by passing 1 there too, matching every other
// call site's severity and the documented "report an error" intent).
void ReportWinsockError(unsigned int code);

}  // namespace guild::net
