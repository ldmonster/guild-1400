#include "gui/credits.h"

// guild::gui — credits screens (scroll crawl + boxed block).
//
// gilde.exe 0x56e524 / 0x529c30.  See credits.h for the recovery notes.  The fade
// register / scene render / window create calls live in unowned modules; this module owns
// the scroll arithmetic (speed ramp, per-frame advance, completion test) and the frame
// loop shape.

namespace guild::gui {

// gilde.exe 0x56e681 / 0x56e768 — the scroll-speed ramp.  The original compares the raw
// float bits of dword_631630 against 0x42A00000 (80.0) and 0x41F00000 (30.0):
//   if (metric < 80.0) { if (metric < 30.0) step = 1; else step = 2; } else step = 4;
int Credits_ScrollStep(float frameTimeMetric) {
    if (frameTimeMetric < kCreditsRampHigh) {
        if (frameTimeMetric < kCreditsRampMid)
            return 1;
        return 2;
    }
    return 4;
}

// gilde.exe 0x56e635 — `if (!(frame % step)) ++*(window+584);`
int Credits_AdvanceOffset(int offset, int frame, int step) {
    if (step != 0 && (frame % step) == 0)
        return offset + 1;
    return offset;
}

// gilde.exe 0x56e669 —
//   (double)*(window+580) < (double)(__int16)*(window+10) * dbl_625324 + (double)*(window+584)
// i.e. the text bottom (textBottom) is below textHeight*scale + offset -> crawl finished.
bool Credits_ScrollComplete(int textBottom, int textHeight, double lineScale, int offset) {
    return static_cast<double>(textBottom) <
           static_cast<double>(textHeight) * lineScale + static_cast<double>(offset);
}

// gilde.exe 0x529c30 — boxed credits window frame loop.
int Credits_RunWindow(CreditsHost& host) {
    int frames = 0;
    // The original sets several scene flags, creates the window + credits block, then
    // loops RunFrameLoop, closing on byte_67225C == 1.
    while (host.RunFrame()) {
        ++frames;
        if (host.CloseRequested()) {
            // dword_631614 = 1 -> the frame loop terminates on the next RunFrame.
            // We model this by letting the host's RunFrame return false next time.
            break;
        }
    }
    return frames;
}

// gilde.exe 0x56e524 — the main crawl loop (the first while-loop).  Drives the scroll
// offset; the original advances every `step` frames and exits when the host closes
// (dword_672230 || byte_67225C) or the crawl scrolls past its height.
int Credits_RunScrollLoop(CreditsHost& host, int screenHeight, int textBottom,
                          int textHeight, double lineScale, float frameTimeMetric,
                          int maxFrames, int* frames) {
    int offset = Credits_InitialOffset(screenHeight); // 0x56e5d2
    int frame = 0;
    int step = Credits_ScrollStep(frameTimeMetric);   // 0x56e549 starts at 4
    while (host.RunFrame()) {
        if (host.CloseRequested())
            break;                                    // 0x56e624 dword_631614 = 1
        offset = Credits_AdvanceOffset(offset, frame, step); // 0x56e635
        ++frame;                                      // 0x56e65d
        if (Credits_ScrollComplete(textBottom, textHeight, lineScale, offset))
            break;                                    // 0x56e66b
        step = Credits_ScrollStep(frameTimeMetric);   // 0x56e681 re-evaluated each frame
        if (maxFrames > 0 && frame >= maxFrames)
            break;                                    // test guard (not in original)
    }
    if (frames)
        *frames = frame;
    return offset;
}

} // namespace guild::gui
