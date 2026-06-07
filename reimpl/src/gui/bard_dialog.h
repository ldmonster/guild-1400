#pragma once
// guild::gui — VIBE_BardDialog_PerformPoem @0x5495a8.  The bard "recite a poem" flow.
//
// Two forms in sequence:
//   PHASE 1 — "special\barde": the gate / announcement.
//     SelectWindow(form,0) SyncWindowColors; CenterChild; SelectWindow(form,1).
//     Gate checks (each renders one text id then runs a wait-loop and aborts):
//       - no/!=2 active scene object   -> 6657
//       - law flag 0x20000 set         -> 6658 (already recited recently)
//       - zoom/scale check failed (0)  -> 6659
//     else: SetGlobalVolume(1000); RenderRichString(6661, poemTitle+1, scale);
//       play "Gedichte_Announcement\<name>.mp3";  modal loop:
//         1210 -> EnqueueLawAction(self, poemLawByte); recite=1; done.
//         1155 -> cancel.
//   PHASE 2 — "special\barde2" (only if recite): the performance + result.
//     SelectWindow(form,1) RenderRichString(poemTitle);
//     SelectWindow(form,3) Window_CreateScrollButtons(..., 1753); window flag byte = 48;
//     SelectWindow(form,2) RenderRichString(poemTitle+2);  play "gedichte\<name>.mp3";
//     wait loop; then the verdict line:
//       record[0]==0 -> 6713 (verse id + 4810);  ==1 -> 6712 (verse id + 446);  else 6714;
//       plus the closing 6711.
//
// We recover both form names, the gate text ids + their order, the announcement/performance
// mp3 path templates, the law-action command, and the verdict-line selection.  The frame
// loop, audio, text engine and command codec are forward-declared / mocked.

#include "gui/types.h"

namespace guild::gui {

inline constexpr int kBardClickOK     = 1210;
inline constexpr int kBardClickCancel = 1155;
inline constexpr int kBardLoopForm    = 423879;

inline constexpr const char* kFormBard  = "special\\barde";
inline constexpr const char* kFormBard2 = "special\\barde2";

inline constexpr const char* kBardAnnounceMp3 = "Gedichte_Announcement\\%s.mp3";
inline constexpr const char* kBardPerformMp3  = "gedichte\\%s.mp3";

// Gate / phase text ids.
inline constexpr int kTextBardNoScene   = 6657; // no valid scene object
inline constexpr int kTextBardRecent    = 6658; // law flag 0x20000 -> already recited
inline constexpr int kTextBardNoZoom    = 6659; // zoom/scale check failed
inline constexpr int kTextBardAnnounce  = 6661; // announcement (verse id+1, scale)
inline constexpr int kTextBardClose     = 6711; // closing line (phase 2)
inline constexpr int kTextBardVerdictPos = 6712; // record[0]==1 (verse id + 446)
inline constexpr int kTextBardVerdictNeu = 6713; // record[0]==0 (verse id + 4810)
inline constexpr int kTextBardVerdictNeg = 6714; // record[0] other

inline constexpr int kBardLawFlagRecited = 0x20000; // dword_12CEAD8 bit -> recently recited
inline constexpr int kBardScrollButtonId = 1753;
inline constexpr int kBardWindowFlagByte = 48; // dword_67EDE4 byte set in phase 2

// Phase-1 gate outcome.
enum class BardGate {
    kOk,         // can recite
    kNoScene,    // 6657
    kRecent,     // 6658
    kNoZoom,     // 6659
};

struct BardState {
    int activeScene = 2;   // *(v5+112): must == 2 to proceed
    bool sceneValid = true; // v5 non-null
    bool lawFlagRecited = false; // dword_12CEAD8 & 0x20000
    int  zoomScale = 1;    // Camera_ComputeZoomScale (0 -> gate fail)
    int  poemTitle = 0;    // *((word*)record+3) — base text id for the verses
    int  poemVerseId = 0;  // *((word*)record+1) — the verse id used in verdict lines
    int  poemLawByte = 0;  // *((word*)handler+86) — the law-action argument
    int  verdict = 0;      // *(byte*)record — 0/1/other selects the verdict line
    int  self = 0;         // active char entity (EnqueueLawAction target)
};

struct BardLayout {
    BardGate gate = BardGate::kOk;
    int gateText = 0;       // the gate's rendered text id (or 6661 announcement)
    int announceText = kTextBardAnnounce;
    int lawByte = 0;        // EnqueueLawAction argument
    // Phase-2 verdict (only meaningful after a successful recite):
    int verdictText = 0;    // 6712 / 6713 / 6714
    int verdictArg  = 0;    // verse id + 446 (pos) or + 4810 (neutral); 0 for negative
};

struct BardCommandSink {
    virtual ~BardCommandSink() = default;
    // Recite: enqueue the law action that performs the poem.
    virtual void Recite(int /*self*/, int /*lawByte*/) {}
};
void BardDialog_SetCommandSink(BardCommandSink* sink);

// gilde.exe 0x5495a8 (layout half) — evaluate the gate + assemble the verdict line.
BardLayout BardDialog_Build(const BardState& s);

// gilde.exe 0x5495a8 (wiring half) — 1210 recites (only when gate==kOk); 1155 cancels.
// Returns true when the click ends the phase-1 loop; sets `*recited` when a recite fired.
bool BardDialog_Dispatch(const BardLayout& l, const BardState& s, int clickedId,
                         bool* recited);

} // namespace guild::gui
