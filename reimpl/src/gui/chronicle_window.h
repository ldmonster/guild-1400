#pragma once
// guild::gui — the chronicle / history-event "scroll" window family.
//
//   VIBE_History_DisplayCurrentEvent  @0x4fed20  — the per-day dispatcher. When the
//     daily chronicle becomes available it (optionally) queues a "refresh guild
//     state" request, then dispatches to one of two scroll-window builders based on
//     a mode byte (byte_633924):
//       1 -> ShowEventScrollForward (scan forward from the current cursor)
//       2 -> ShowEventScrollReal    (scan the "real"/player-relevant events)
//
//   VIBE_History_ShowEventScrollForward @0x4fe74c
//   VIBE_History_ShowEventScrollReal    @0x4fea88
//     Near-identical builders. Each:
//       1. zeroes a title buffer (256B) + a body buffer (8120B),
//       2. scans the next chronicle event (ScanNextEventForward / ScanNextEventReal)
//          into (title, body); if the scan does not return 1, bails out,
//       3. opens the parchment-scroll form (Scroll_Open) and sets the drag cursor,
//       4. selects window slot 1 (body) + 2 (header) and renders:
//            slot 2: RenderRichString(3347)                      // the "chronicle" header
//                    + Window_CreateScrollButtons(..., 1753)     // the page arrows
//                    + window flag dword[238*win] = 24,
//            slot 1: RenderRichString("$C%s", body)              // centred body text
//            slot 3: RenderRichString("$C$Z$[%s$]", title)       // centred boxed title
//       5. runs the modal frame loop (RunFrameLoop 417927). Each frame, when the
//          "next page" signal fires (dword_75BF38 == 1210) it re-scans the next event
//          and re-renders slots 1/2/3 (slot 2 also clears scroll-state dword[+584]=0,
//          sets byte[+608]=2); a scan miss sets the exit flag (dword_631614 = 1).
//       6. closes the scroll (Scroll_Close) on exit.
//
// The recoverable cores are: the dispatch decision (DisplayCurrentEvent's mode gate),
// the scroll-window BUILD PLAN (form slots + text ids + the markup format strings +
// the scroll-button id + the window flag value), and the page-advance loop rule
// (re-scan on signal, exit on miss). The chronicle scan itself is REUSED from
// world::Chronicle (world/history_chronicle.h) / world::HistoryClassifyEntry
// (world/history.h). The scroll open/close, drag cursor, text render and frame loop
// are engine leaves, routed through a mockable host.

#include "gui/types.h"

#include <string>
#include <vector>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Recovered constants.
// ---------------------------------------------------------------------------
inline constexpr int kChronicleLoopForm   = 417927; // RunFrameLoop selector (scroll modal)
inline constexpr int kChronicleHeaderText = 3347;   // RenderRichString header id (slot 2)
inline constexpr int kChronicleScrollBtns = 1753;   // Window_CreateScrollButtons gfx id
inline constexpr int kChroniclePageSignal = 1210;   // dword_75BF38 == 1210 -> next page
inline constexpr int kChronicleWinFlag    = 24;     // dword_67EDE4[238*win] = 24

// Window slots the builder selects (VIBE_Form_SelectWindow slot args).
inline constexpr int kChronicleBodySlot   = 1; // "$C%s"        (scanned body text)
inline constexpr int kChronicleHeaderSlot = 2; // header(3347)  + scroll arrows
inline constexpr int kChronicleTitleSlot  = 3; // "$C$Z$[%s$]"  (scanned title text)

// Markup format strings (recovered byte-for-byte from the RenderRichString args).
inline constexpr const char* kChronicleBodyFmt  = "$C%s";        // aCS    @0x620b14
inline constexpr const char* kChronicleTitleFmt = "$C$Z$[%s$]";  // aCZS_1 @0x620b1c

// Mode byte (byte_633924) values DisplayCurrentEvent dispatches on.
inline constexpr int kChronicleModeIdle    = 0; // no scroll
inline constexpr int kChronicleModeForward = 1; // -> ShowEventScrollForward
inline constexpr int kChronicleModeReal    = 2; // -> ShowEventScrollReal

// Object-record offsets the page-advance path touches on the header window record.
inline constexpr int kChronicleWinScrollOff = 584; // dword[+584] = 0 (reset scroll pos)
inline constexpr int kChronicleWinModeOff   = 608; // byte[+608]  = 2 (header render mode)

// ---------------------------------------------------------------------------
// A single chronicle event the scroll window shows (the (title, body) pair a scan
// fills). `kind` mirrors the ScanNextEventForward return classification (1 = a
// normal event with body, 6 = an event whose body text came back empty).
// ---------------------------------------------------------------------------
struct ChronicleEvent {
    std::string title; // v22 / v24 — the date / heading text
    std::string body;  // v21 / v23 — the event body text
    int kind = 1;      // scan result (1 = emit, 6 = empty body)
    bool valid = false; // true when a scan returned an event (result 1)
};

// One rendered scroll "page" — the three text draws the builder issues per event.
struct ChroniclePage {
    int bodySlot   = kChronicleBodySlot;
    int headerSlot = kChronicleHeaderSlot;
    int titleSlot  = kChronicleTitleSlot;
    int headerId   = kChronicleHeaderText;  // RenderRichString(3347)
    int scrollBtns = kChronicleScrollBtns;  // CreateScrollButtons(..., 1753)
    int winFlag    = kChronicleWinFlag;      // dword[238*win] = 24
    std::string bodyText;   // the "$C%s" expansion (body)
    std::string titleText;  // the "$C$Z$[%s$]" expansion (title)
};

// The complete plan for a scroll session: the open page plus every page the loop
// advances through on the page-signal (in scan order, until a scan miss).
struct ChronicleScrollPlan {
    int loopForm = kChronicleLoopForm;
    bool opened = false;            // first scan returned 1 -> the scroll opened
    std::vector<ChroniclePage> pages; // page 0 = initial; 1.. = each advance
};

// ===========================================================================
// Mockable engine edges. The scan source is the recovered world chronicle (reused);
// the render/scroll/loop leaves are stubbed.
// ===========================================================================
struct ChronicleHost {
    virtual ~ChronicleHost() = default;
    // Scan the next event into (out). Returns the scan-result code (1 = emit, 2 = stop,
    // 3 = idle, 4 = end, 6 = empty body). `real` selects ScanNextEventReal vs ..Forward.
    virtual int ScanNextEvent(bool real, ChronicleEvent* out) { (void)real; (void)out; return 3; }
    // The page-advance signal (dword_75BF38). True the frame the "next page" arrow fires.
    virtual bool PageSignal() { return false; }
};

// gilde.exe 0x4fed20 (dispatch core) — DisplayCurrentEvent's mode gate. Given the
// chronicle availability and the mode byte, returns which scroll builder to run:
//   kChronicleModeForward / kChronicleModeReal / kChronicleModeIdle.
// `available` mirrors (!dword_764CF0 && byte_633924); `mode` is byte_633924.
int Chronicle_DispatchMode(bool available, int mode);

// gilde.exe 0x4fe74c / 0x4fea88 — build the scroll plan for a chronicle session.
//   `real` selects the Real vs Forward scan. The host supplies events + the page
//   signal. Stops a page run after at most `maxPages` advances (the original loops
//   until the scan misses or the user exits; `maxPages` bounds the test harness).
// Returns a plan whose `opened` is false (and `pages` empty) when the first scan did
// not return 1 (the early bail in the original).
ChronicleScrollPlan Chronicle_BuildScrollPlan(ChronicleHost& host, bool real,
                                              int maxPages = 64);

// Format one event into a ChroniclePage (the three RenderRichString expansions). The
// body uses kChronicleBodyFmt, the title kChronicleTitleFmt (literal "%s" only).
ChroniclePage Chronicle_FormatPage(const ChronicleEvent& ev);

} // namespace guild::gui
