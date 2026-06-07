#include "gui/chronicle_window.h"

namespace guild::gui {

namespace {

// Expand a single "%s" format against `arg` (the only specifier the chronicle markup
// strings use). Everything else is copied verbatim, mirroring the engine's
// RenderRichString("$C%s", body) / RenderRichString("$C$Z$[%s$]", title) calls.
std::string ExpandSingleS(const char* fmt, const std::string& arg) {
    std::string out;
    for (const char* p = fmt; *p; ++p) {
        if (p[0] == '%' && p[1] == 's') {
            out += arg;
            ++p;
        } else {
            out += *p;
        }
    }
    return out;
}

} // namespace

// gilde.exe 0x4fed20 (dispatch core).
//   if (!dword_764CF0 && byte_633924) {
//     if (byte_633924 == 1) ShowEventScrollForward();
//     else if (byte_633924 == 2) ShowEventScrollReal();
//   }
int Chronicle_DispatchMode(bool available, int mode) {
    if (!available)
        return kChronicleModeIdle;
    if (mode == kChronicleModeForward)
        return kChronicleModeForward;
    if (mode == kChronicleModeReal)
        return kChronicleModeReal;
    return kChronicleModeIdle;
}

// gilde.exe 0x4fe74c / 0x4fea88 — the per-event render plan (the three RenderRichString
// draws): slot 2 header(3347)+arrows, slot 1 "$C%s" body, slot 3 "$C$Z$[%s$]" title.
ChroniclePage Chronicle_FormatPage(const ChronicleEvent& ev) {
    ChroniclePage page{};
    page.bodyText  = ExpandSingleS(kChronicleBodyFmt, ev.body);   // VIBE_Text_RenderRichString("$C%s", body)
    page.titleText = ExpandSingleS(kChronicleTitleFmt, ev.title); // ..("$C$Z$[%s$]", title)
    return page;
}

// gilde.exe 0x4fe74c / 0x4fea88 (build half).
//   result = ScanNext(...); if (result != 1) return result;   // bail
//   Scroll_Open(); SetSprite(0); render slots 2/1/3 for the first event;
//   do { UpdateAnimation();
//        if (dword_75BF38 == 1210) {                          // next-page signal
//          if (ScanNext(...) == 1) { re-render slots 2/1/3; }
//          else dword_631614 = 1;                              // exit flag
//        }
//   } while (RunFrameLoop(417927, ...));
//   Scroll_Close();
ChronicleScrollPlan Chronicle_BuildScrollPlan(ChronicleHost& host, bool real,
                                              int maxPages) {
    ChronicleScrollPlan plan{};

    // First scan — drives the bail-vs-open decision (result == 1).
    ChronicleEvent ev{};
    int result = host.ScanNextEvent(real, &ev);
    if (result != 1) {
        plan.opened = false; // early `return result;` in the original
        return plan;
    }
    ev.kind = result;
    ev.valid = true;
    plan.opened = true; // Scroll_Open()
    plan.pages.push_back(Chronicle_FormatPage(ev));

    // Modal loop: advance a page whenever the page signal fires.
    int advances = 0;
    while (advances < maxPages) {
        if (host.PageSignal()) {
            ChronicleEvent next{};
            int r = host.ScanNextEvent(real, &next);
            if (r == 1) {
                next.kind = r;
                next.valid = true;
                plan.pages.push_back(Chronicle_FormatPage(next)); // re-render slots 1/2/3
            } else {
                // dword_631614 = 1 — request loop exit; no more pages.
                break;
            }
        }
        ++advances;
        if (!host.PageSignal())
            break; // host stops signalling -> RunFrameLoop returns 0 (modal closed)
    }
    // Scroll_Close() on exit (no further plan state).
    return plan;
}

} // namespace guild::gui
