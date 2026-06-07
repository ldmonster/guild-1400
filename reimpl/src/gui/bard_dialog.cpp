#include "gui/bard_dialog.h"

namespace guild::gui {

namespace {

BardCommandSink  g_defaultSink;
BardCommandSink* g_sink = &g_defaultSink;

} // namespace

void BardDialog_SetCommandSink(BardCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// gilde.exe 0x5495a8 (layout half).
//   if (!v5 || *(v5+112) != 2) gate=6657;
//   else if (dword_12CEAD8 & 0x20000) gate=6658;
//   else if (!ComputeZoomScale) gate=6659;
//   else { announce 6661; lawByte = *((word*)handler+86); }
//   verdict (phase 2):
//     verdict==0 -> 6713 (verseId + 4810); ==1 -> 6712 (verseId + 446); else 6714.
BardLayout BardDialog_Build(const BardState& s) {
    BardLayout l{};

    if (!s.sceneValid || s.activeScene != 2) {
        l.gate = BardGate::kNoScene;
        l.gateText = kTextBardNoScene;   // 6657
        return l;
    }
    if (s.lawFlagRecited) {
        l.gate = BardGate::kRecent;
        l.gateText = kTextBardRecent;    // 6658
        return l;
    }
    if (s.zoomScale == 0) {
        l.gate = BardGate::kNoZoom;
        l.gateText = kTextBardNoZoom;    // 6659
        return l;
    }

    l.gate = BardGate::kOk;
    l.gateText = kTextBardAnnounce;      // 6661
    l.lawByte = s.poemLawByte;

    // Verdict line that phase 2 would show on completion.
    if (s.verdict == 0) {
        l.verdictText = kTextBardVerdictNeu;          // 6713
        l.verdictArg  = s.poemVerseId + 4810;
    } else if (s.verdict == 1) {
        l.verdictText = kTextBardVerdictPos;          // 6712
        l.verdictArg  = s.poemVerseId + 446;
    } else {
        l.verdictText = kTextBardVerdictNeg;          // 6714
        l.verdictArg  = 0;
    }
    return l;
}

// gilde.exe 0x5495a8 (wiring half).
//   if (dword_75BF38 == 1210) { EnqueueLawAction(self, lawByte); recite=1; done; }
//   else if (dword_75BF38 == 1155) cancel.
bool BardDialog_Dispatch(const BardLayout& l, const BardState& s, int clickedId,
                         bool* recited) {
    if (recited) *recited = false;
    if (l.gate != BardGate::kOk)
        return true; // gate-failed loops end on a click; nothing to recite

    if (clickedId == kBardClickOK) {
        g_sink->Recite(s.self, l.lawByte);
        if (recited) *recited = true;
        return true;
    }
    if (clickedId == kBardClickCancel)
        return true;
    return false;
}

} // namespace guild::gui
