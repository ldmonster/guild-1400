#pragma once
// guild::gui — the misc "statistics" windows (general economy figures + the per-round
// tax history).
//
//   VIBE_Statistics_ShowGeneralWindow @0x57a61c
//     form = GameTick_Finalize("misc\statistics");  CenterChildWindows; drag cursor.
//     1. SelectWindow(form, 0); then a header pass renders text ids 2096..2123 in a
//        "$A"-separated column (RenderRichString(id); RenderRichString("$A");).
//     2. Each frame, when a refresh is due it repaints four windows:
//          slot 0: "$C" + RenderRichString(byte_641DA0 + 2095)  // mode-specific header
//                  + sprintf("$AM\xf6gliche Einwohnerzahl: %3.1f", possible) "%s // "
//                  + sprintf("Tats\xe4chliche Einwohnerzahl: %3.1f", actual)   "%s // "
//                  + sprintf("Geb Ratio: %3.3f",  residential/industry) "%s$A"
//                  + sprintf("Residential: %3.3f", residential) "%s"
//                  + sprintf(" // Industry: %3.3f", industry)   "%s"
//          slot 2: "$C", then for each of 28 floats (flt_1234758 column, stride 16)
//                  sprintf("%3.3f", v) "%s$A"        // the "$A"-stacked figures column
//          slot 3: "$C", then for each of 28 floats (flt_123475C column, stride 16)
//                  sprintf("%3.3f", v) "%s" + "$A"
//     3. modal frame loop (RunFrameLoop 423879); Form_Destroy on exit.
//
//   VIBE_Statistics_ShowTaxWindow @0x57a900
//     form = GameTick_Finalize("misc\statistics_taxes");  CenterChildWindows; cursor.
//     SelectWindow(form, 0); then a 17-row loop renders one line per round:
//          RenderRichString("Runde %i:   %T$N", round, amount)
//       with round = currentRound - 16 + i and amount = dword_12350E0[i] (the tax ring).
//     modal frame loop (RunFrameLoop 423879); Form_Destroy on exit.
//
// The recoverable cores are: the BUILD PLANs (form name + the slot/text-id/format-string
// schedule for each window) and, for the tax window, the per-row (round, amount) data
// which is REUSED from world::StatisticsTaxRowRound (world/statistics.h). The float
// figures, the inhabitant counts and the tax ring are sim data (injected); the text
// render, form load/center, drag cursor and frame loop are engine leaves (mocked).

#include "gui/types.h"

#include <string>
#include <vector>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Recovered constants.
// ---------------------------------------------------------------------------
inline constexpr int kStatLoopForm = 423879; // RunFrameLoop selector (both windows)

inline constexpr const char* kFormStatGeneral = "misc\\statistics";       // aMiscStatistics
inline constexpr const char* kFormStatTaxes   = "misc\\statistics_taxes"; // aMiscStatistics_0

// General-window window slots (VIBE_Form_SelectWindow slot args).
inline constexpr int kStatGenSlotHeader  = 0; // header + inhabitant/ratio summary
inline constexpr int kStatGenSlotColLeft = 2; // flt_1234758 column ("%s$A" stacked)
inline constexpr int kStatGenSlotColRight = 3; // flt_123475C column ("%s" + "$A")

// The header text-id sweep at window 0. DISASM 0x57a667: edx=2096; render(edx); inc edx;
// while (edx != 2123). The inc precedes the test, so the RENDERED ids are 2096..2122 (27
// ids); 2123 is the loop-exit sentinel and is NEVER rendered.
inline constexpr int kStatGenHeaderFirst = 2096;       // first rendered id
inline constexpr int kStatGenHeaderLast  = 2123;       // exit sentinel (NOT rendered)
inline constexpr int kStatGenHeaderCount = 27;         // 2096..2122 inclusive

inline constexpr int kStatGenModeTextBase = 2095; // RenderRichString(byte_641DA0 + 2095)

// The two stacked float columns: byte-stride 16. DISASM (0x57a7d7 `mov edx,10h`;
// loop at 0x57a7e4: fld flt[edx]; add edx,10h; cmp edx,1C0h; jnz) — v6 STARTS AT 16
// (the byte-0 element is skipped), reads flt[16], flt[32], ..., flt[432], then v6 hits
// 448 and the do/while exits. That is 27 passes, reading stride-elements 1..27.
inline constexpr int kStatGenColStart  = 16;  // v6 = 0x10 (first read at byte 16, NOT 0)
inline constexpr int kStatGenColStride = 16;  // (char*)flt + v6, v6 += 16 up to 448
inline constexpr int kStatGenColEnd    = 448; // while (v6 != 448)
inline constexpr int kStatGenColCount  = 27;  // (448 - 16) / 16 passes (v6 = 16,32,..,432)

// Recovered markup / format strings (byte-for-byte; German extended chars preserved).
inline constexpr const char* kStatCenter      = "$C";   // aC_6
inline constexpr const char* kStatRowSep      = "$A";   // aA_2
inline constexpr const char* kStatFmtPossible = "$AM\xf6" "gliche Einwohnerzahl: %3.1f";  // byte_6257BC
inline constexpr const char* kStatFmtActual   = "Tats\xe4" "chliche Einwohnerzahl: %3.1f"; // byte_6257E4
inline constexpr const char* kStatFmtGebRatio = "Geb Ratio: %3.3f";    // aGebRatio33f
inline constexpr const char* kStatFmtResid    = "Residential: %3.3f";  // aResidential33f
inline constexpr const char* kStatFmtIndustry = " // Industry: %3.3f"; // aIndustry33f
inline constexpr const char* kStatFmtFigure   = "%3.3f";               // a33f
inline constexpr const char* kStatWrapSlashSA = "%s // ";  // aS_16
inline constexpr const char* kStatWrapSA      = "%s$A";    // aSA_0
inline constexpr const char* kStatWrapS       = "%s";      // aS_17

// Tax-window constants.
inline constexpr int kStatTaxSlot    = 0;                  // SelectWindow(form, 0)
inline constexpr int kStatTaxRows    = 17;                 // v5 < 17 (REUSEs world::kStatTaxRounds)
inline constexpr const char* kStatTaxFmt = "Runde %i:   %T$N"; // aRundeITN

// ---------------------------------------------------------------------------
// Inputs.
// ---------------------------------------------------------------------------
// The summary figures the general header line renders (slot 0). All are sim floats.
struct StatGeneralSummary {
    int    modeByte    = 0;   // byte_641DA0 -> header text id (modeByte + 2095)
    float  possible    = 0.f; // flt_641DAC ("...Einwohnerzahl: %3.1f")
    float  actual      = 0.f; // flt_641DA8
    float  residential = 0.f; // flt_641FD4
    float  industry    = 0.f; // flt_641FD8 (Geb Ratio = residential / industry)
};

// One rendered general-window window pass.
struct StatGeneralPlan {
    const char* form = kFormStatGeneral;
    int loopForm = kStatLoopForm;
    std::vector<int> headerIds;       // the 2096..2123 sweep (each followed by "$A")
    int modeHeaderId = 0;             // RenderRichString(modeByte + 2095) for slot 0
    std::vector<std::string> headerLines; // the formatted slot-0 summary lines
    std::vector<std::string> colLeft;  // slot 2: the 28 "%3.3f" figures
    std::vector<std::string> colRight; // slot 3: the 28 "%3.3f" figures
};

// One rendered tax-window row.
struct StatTaxRow {
    int round = 0;   // currentRound - 16 + i
    int amount = 0;  // dword_12350E0[i]
    std::string text; // "Runde %i:   %T$N" expansion (the "%i" round only; %T is engine)
};

struct StatTaxPlan {
    const char* form = kFormStatTaxes;
    int loopForm = kStatLoopForm;
    std::vector<StatTaxRow> rows; // 17 rows
};

// ===========================================================================
// Builders.
// ===========================================================================

// gilde.exe 0x57a61c — build the general-window render plan.
//   `left`/`right` are the two float columns (28 entries each); `summary` the header
//   figures. `colLeft`/`colRight` may be shorter than 28 (the harness can pass any
//   length); the original always walks 28.
StatGeneralPlan Statistics_BuildGeneralPlan(const StatGeneralSummary& summary,
                                            const std::vector<float>& left,
                                            const std::vector<float>& right);

// gilde.exe 0x57a900 — build the tax-window render plan. `currentRound` is the live
// round (qword_13CE852); `amounts` is the 17-entry tax ring (dword_12350E0). Each row
// i gets round = currentRound - 16 + i (== world::StatisticsTaxRowRound).
StatTaxPlan Statistics_BuildTaxPlan(int currentRound, const std::vector<int>& amounts);

} // namespace guild::gui
